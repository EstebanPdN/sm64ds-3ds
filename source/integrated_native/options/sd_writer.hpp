#pragma once
// Large sequential SD writes through FS:USER for diagnostic dumps. libctru's
// sdmc devoptab copies every write through an 8 KiB bounce buffer, one FS IPC
// per 8 KiB: a 120 MB process dump took minutes on hardware. This writer keeps
// one archive/file handle and issues 1 MiB writes at explicit offsets.
// Source bytes must be ordinary process memory (FS IPC cannot map VRAM).
#ifdef __3DS__
#include <3ds.h>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace native_diagnostics {
class SdWriter {
public:
    // Where the staging buffer came from (reported in the dump log).
    enum class Buffer : uint8_t { none, heap, linear, fallback, caller };
    SdWriter(void* fallback, size_t fallback_bytes) : fallback_(static_cast<uint8_t*>(fallback)), fallback_bytes_(fallback_bytes) {}
    SdWriter(const SdWriter&) = delete;
    SdWriter& operator=(const SdWriter&) = delete;
    ~SdWriter() { close(); }

    // path: "sdmc:/..." or "/...". size_hint pre-sizes the file (optional).
    bool open(const char* path, uint64_t size_hint = 0) {
        close();
        // BUILD139 dumps 001-007: the libc heap was full in levels, so this
        // fell back to the 64 KiB static buffer (1,385 writes for memory.vm
        // instead of 87). The linear heap still had 10+ MiB free.
        if ((buffer_ = static_cast<uint8_t*>(std::malloc(kChunk)))) buffer_kind_ = Buffer::heap;
        else if ((buffer_ = static_cast<uint8_t*>(linearAlloc(kChunk)))) buffer_kind_ = Buffer::linear;
        else { buffer_ = fallback_; buffer_kind_ = Buffer::fallback; }
        capacity_ = buffer_kind_ == Buffer::fallback ? fallback_bytes_ : kChunk;
        return open_file(path, size_hint);
    }
    // Caller-owned staging (the packed-dump workspace); never freed here.
    bool open_with(const char* path, uint8_t* buffer, size_t capacity, uint64_t size_hint = 0) {
        close();
        buffer_ = buffer; capacity_ = capacity; buffer_kind_ = Buffer::caller;
        return open_file(path, size_hint);
    }
    bool write(const void* data, size_t bytes) {
        const auto* p = static_cast<const uint8_t*>(data);
        while (ok_ && bytes) {
            const size_t n = bytes < capacity_ - used_ ? bytes : capacity_ - used_;
            std::memcpy(buffer_ + used_, p, n);
            used_ += n; p += n; bytes -= n;
            if (used_ == capacity_) flush_prefix(used_ < kChunk ? used_ : kChunk);
        }
        return ok_;
    }
    // Space for direct fills (e.g. word copies from VRAM) without a memcpy.
    uint8_t* reserve(size_t& bytes) {
        if (ok_ && used_ == capacity_) flush();
        const size_t free = capacity_ - used_;
        if (bytes > free) bytes = free;
        return ok_ ? buffer_ + used_ : nullptr;
    }
    // Exactly `bytes` of contiguous space (flushing first when needed), or
    // nullptr when the buffer can never hold that many. Finish with commit().
    uint8_t* reserve_contiguous(size_t bytes) {
        if (ok_ && capacity_ - used_ < bytes) {
            // Keep sector-aligned bulk writes even when compressed records vary in size.
            if (used_ >= kChunk) {
                if (!flush_prefix(kChunk)) return nullptr;
            }
            if (capacity_ - used_ < bytes && !flush()) return nullptr;
        }
        return ok_ && capacity_ - used_ >= bytes ? buffer_ + used_ : nullptr;
    }
    bool commit(size_t bytes) { used_ += bytes; if (used_ == capacity_) flush_prefix(used_ < kChunk ? used_ : kChunk); return ok_; }
    // Rewrites bytes already written (headers patched at the end).
    bool write_at(uint64_t offset, const void* data, size_t bytes) {
        if (!ok_ || !flush()) return false;
        const uint64_t start = svcGetSystemTick();
        uint32_t written = 0;
        ok_ = R_SUCCEEDED(FSFILE_Write(file_, &written, offset, data, uint32_t(bytes), 0)) && written == bytes;
        fs_ticks_ += svcGetSystemTick() - start; ++writes_;
        return ok_;
    }
    uint64_t size() const { return offset_ + used_; }
    bool close() {
        bool result = ok_;
        if (file_) {
            if (!flush()) result = false;
            const uint64_t start = svcGetSystemTick();
            if (R_FAILED(FSFILE_SetSize(file_, offset_))) result = false;
            if (R_FAILED(FSFILE_Flush(file_))) result = false;
            if (R_FAILED(FSFILE_Close(file_))) result = false;
            fs_ticks_ += svcGetSystemTick() - start;
            file_ = 0;
        }
        if (archive_open_) { FSUSER_CloseArchive(archive_); archive_open_ = false; }
        if (buffer_kind_ == Buffer::heap) std::free(buffer_);
        else if (buffer_kind_ == Buffer::linear) linearFree(buffer_);
        buffer_ = nullptr; buffer_kind_ = Buffer::none; ok_ = false;
        return result;
    }
    // FS time of the last file (open, writes, close) and its FSFILE_Write
    // count. Reset by open/open_with, still valid after close().
    uint64_t fs_ticks() const { return fs_ticks_; }
    unsigned writes() const { return writes_; }
    Buffer buffer_kind() const { return buffer_kind_; }
    static const char* buffer_name(Buffer kind) {
        switch (kind) {
        case Buffer::heap: return "heap-1MiB";
        case Buffer::linear: return "linear-1MiB";
        case Buffer::fallback: return "static-64KiB";
        case Buffer::caller: return "workspace";
        default: return "none";
        }
    }
private:
    static constexpr size_t kChunk = 1024 * 1024;
    bool open_file(const char* path, uint64_t size_hint) {
        if (!std::strncmp(path, "sdmc:", 5)) path += 5;
        offset_ = used_ = 0; fs_ticks_ = 0; writes_ = 0;
        const uint64_t start = svcGetSystemTick();
        ok_ = buffer_ && capacity_ &&
              R_SUCCEEDED(FSUSER_OpenArchive(&archive_, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, ""))) &&
              (archive_open_ = true) &&
              R_SUCCEEDED(FSUSER_OpenFile(&file_, archive_, fsMakePath(PATH_ASCII, path), FS_OPEN_WRITE | FS_OPEN_CREATE, 0)) &&
              R_SUCCEEDED(FSFILE_SetSize(file_, size_hint));
        if (!ok_ && file_) { FSFILE_Close(file_); file_ = 0; }
        fs_ticks_ += svcGetSystemTick() - start;
        return ok_;
    }
    bool flush() { return flush_prefix(used_); }
    bool flush_prefix(size_t count) {
        if (!ok_ || !used_) return ok_;
        const uint64_t start = svcGetSystemTick();
        uint32_t written = 0;
        ok_ = R_SUCCEEDED(FSFILE_Write(file_, &written, offset_, buffer_, uint32_t(count), 0)) && written == count;
        fs_ticks_ += svcGetSystemTick() - start; ++writes_;
        if (ok_) {
            offset_ += count;
            used_ -= count;
            if (used_) std::memmove(buffer_, buffer_ + count, used_);
        }
        return ok_;
    }
    uint8_t* fallback_;
    size_t fallback_bytes_;
    uint8_t* buffer_ = nullptr;
    size_t capacity_ = 0, used_ = 0;
    uint64_t offset_ = 0, fs_ticks_ = 0;
    FS_Archive archive_ = 0;
    Handle file_ = 0;
    unsigned writes_ = 0;
    Buffer buffer_kind_ = Buffer::none;
    bool ok_ = false, archive_open_ = false;
};
}
#endif
