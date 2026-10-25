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
    SdWriter(void* fallback, size_t fallback_bytes) : fallback_(static_cast<uint8_t*>(fallback)), fallback_bytes_(fallback_bytes) {}
    SdWriter(const SdWriter&) = delete;
    SdWriter& operator=(const SdWriter&) = delete;
    ~SdWriter() { close(); }

    // path: "sdmc:/..." or "/...". size_hint pre-sizes the file (optional).
    bool open(const char* path, uint64_t size_hint = 0) {
        close();
        if (!std::strncmp(path, "sdmc:", 5)) path += 5;
        buffer_ = static_cast<uint8_t*>(std::malloc(kChunk));
        capacity_ = buffer_ ? kChunk : fallback_bytes_;
        if (!buffer_) buffer_ = fallback_;
        owned_ = capacity_ == kChunk;
        ok_ = buffer_ && capacity_ &&
              R_SUCCEEDED(FSUSER_OpenArchive(&archive_, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, ""))) &&
              (archive_open_ = true) &&
              R_SUCCEEDED(FSUSER_OpenFile(&file_, archive_, fsMakePath(PATH_ASCII, path), FS_OPEN_WRITE | FS_OPEN_CREATE, 0)) &&
              R_SUCCEEDED(FSFILE_SetSize(file_, size_hint));
        if (!ok_ && file_) { FSFILE_Close(file_); file_ = 0; }
        offset_ = used_ = 0;
        return ok_;
    }
    bool write(const void* data, size_t bytes) {
        const auto* p = static_cast<const uint8_t*>(data);
        while (ok_ && bytes) {
            const size_t n = bytes < capacity_ - used_ ? bytes : capacity_ - used_;
            std::memcpy(buffer_ + used_, p, n);
            used_ += n; p += n; bytes -= n;
            if (used_ == capacity_) flush();
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
    bool commit(size_t bytes) { used_ += bytes; if (used_ == capacity_) flush(); return ok_; }
    // Rewrites bytes already written (headers patched at the end).
    bool write_at(uint64_t offset, const void* data, size_t bytes) {
        if (!ok_ || !flush()) return false;
        uint32_t written = 0;
        ok_ = R_SUCCEEDED(FSFILE_Write(file_, &written, offset, data, uint32_t(bytes), 0)) && written == bytes;
        return ok_;
    }
    uint64_t size() const { return offset_ + used_; }
    bool close() {
        bool result = ok_;
        if (file_) {
            if (!flush()) result = false;
            if (R_FAILED(FSFILE_SetSize(file_, offset_))) result = false;
            if (R_FAILED(FSFILE_Flush(file_))) result = false;
            if (R_FAILED(FSFILE_Close(file_))) result = false;
            file_ = 0;
        }
        if (archive_open_) { FSUSER_CloseArchive(archive_); archive_open_ = false; }
        if (owned_) std::free(buffer_);
        buffer_ = nullptr; owned_ = false; ok_ = false;
        return result;
    }
private:
    static constexpr size_t kChunk = 1024 * 1024;
    bool flush() {
        if (!ok_ || !used_) return ok_;
        uint32_t written = 0;
        ok_ = R_SUCCEEDED(FSFILE_Write(file_, &written, offset_, buffer_, uint32_t(used_), 0)) && written == used_;
        offset_ += used_;
        used_ = 0;
        return ok_;
    }
    uint8_t* fallback_;
    size_t fallback_bytes_;
    uint8_t* buffer_ = nullptr;
    size_t capacity_ = 0, used_ = 0;
    uint64_t offset_ = 0;
    FS_Archive archive_ = 0;
    Handle file_ = 0;
    bool ok_ = false, owned_ = false, archive_open_ = false;
};
}
#endif
