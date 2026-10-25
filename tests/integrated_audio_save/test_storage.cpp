#include "atomic_file.hpp"
#include <cassert>
#include <cstring>
#include <string>

namespace {
struct FaultOps : integrated_storage::FileOps {
    inline static const char* fault = "";
    inline static bool fat = false, fail_rollback = false;
    inline static bool backup_first = false, delete_then_fail = false;
    inline static unsigned destructive_deletes = 0;
    inline static unsigned renames = 0, fail_rename = 0;
    static bool is(const char* value) { return std::strcmp(fault, value) == 0; }
    static FILE* open(const char* p, const char* mode) {
        if (is("open") && mode[0] == 'w') { errno = EACCES; return nullptr; }
        return FileOps::open(p, mode);
    }
    static size_t write(const void* p, size_t n, FILE* f) {
        return FileOps::write(p, is("write") ? n-1 : n, f);
    }
    static int flush(FILE* f) { const int r = FileOps::flush(f); return is("flush") ? -1 : r; }
    static int sync(FILE* f) { return is("sync") ? -1 : FileOps::sync(f); }
    static int close(FILE* f) { const int r = FileOps::close(f); return is("close") ? -1 : r; }
    static int rename(const char* a, const char* b) {
        ++renames;
        if (delete_then_fail && FileOps::exists(b) == 0) {
            assert(FileOps::remove(b) == 0); ++destructive_deletes;
            errno = EIO; return -1;
        }
        if (renames == fail_rename || (fail_rollback && renames == (backup_first ? 3u : 4u))) {
            errno = EIO; return -1;
        }
        if (fat && FileOps::exists(b) == 0) { errno = EEXIST; return -1; }
        return FileOps::rename(a, b);
    }
    static int exists(const char* p) {
        if (is("stat")) { errno = EIO; return -1; }
        return FileOps::exists(p);
    }
    static void reset() {
        fault = ""; fat = fail_rollback = backup_first = delete_then_fail = false;
        renames = fail_rename = destructive_deletes = 0;
    }
};
void put(const std::string& p, const char* text) {
    FILE* f = std::fopen(p.c_str(), "wb"); assert(f);
    assert(std::fwrite(text, 1, std::strlen(text), f) == std::strlen(text));
    assert(std::fclose(f) == 0);
}
std::string get(const std::string& p) {
    FILE* f = integrated_storage::open_read(p.c_str()); assert(f);
    char bytes[64]; const auto n = std::fread(bytes, 1, sizeof bytes, f);
    assert(!std::ferror(f)); assert(std::fclose(f) == 0);
    return std::string(bytes, n);
}
bool store(const std::string& p) { return integrated_storage::replace_file<FaultOps>(p.c_str(), "new", 3); }
}
int main() {
#ifdef __3DS__
    static_assert(integrated_storage::FileOps::backup_first, "3DS must protect the old primary before rename");
#endif
    char directory[] = "/tmp/sm64ds-storage-XXXXXX"; assert(mkdtemp(directory));
    const std::string p = std::string(directory) + "/save.sav", bak = p + ".bak";
    for (const char* failure : {"open", "write", "flush", "sync", "close", "stat"}) {
        put(p, "old"); FaultOps::reset(); FaultOps::fault = failure;
        assert(!store(p)); assert(get(p) == "old");
    }
    // Failed replacing rename used to lose settings after remove(path).
    FaultOps::reset(); FaultOps::fail_rename = 1;
    assert(!store(p)); assert(get(p) == "old");
    FaultOps::reset(); assert(store(p)); assert(get(p) == "new");
    put(p, "old"); FaultOps::reset(); FaultOps::fat = true;
    assert(store(p)); assert(FaultOps::renames == 3); assert(get(p) == "new");
    assert(access(bak.c_str(), F_OK) != 0);
    for (unsigned failing_rename : {2u, 3u}) {
        put(p, "old"); FaultOps::reset(); FaultOps::fat = true;
        FaultOps::fail_rename = failing_rename;
        assert(!store(p)); assert(get(p) == "old");
    }
    // Both commit and rollback failing still preserve the prior image.
    put(p, "old"); FaultOps::reset(); FaultOps::fat = true;
    FaultOps::fail_rename = 3; FaultOps::fail_rollback = true;
    assert(!store(p)); assert(access(p.c_str(), F_OK) != 0);
    assert(get(p) == "old" && get(bak) == "old");
    FaultOps::reset(); assert(store(p)); assert(get(p) == "new");
    // Refuse to overwrite another recoverable backup on a FAT-like volume.
    put(p, "old"); put(bak, "backup"); FaultOps::reset(); FaultOps::fat = true;
    assert(!store(p)); assert(get(p) == "old" && get(bak) == "backup");
    assert(std::remove(bak.c_str()) == 0);
    // libctru's rename can remove its destination before the retry fails.
    // Reproduce that destructive adapter behavior, then prove backup-first
    // never exposes an existing primary to it, even on a failed commit.
    const auto tmp = p + ".tmp"; put(tmp, "new");
    FaultOps::reset(); FaultOps::delete_then_fail = true;
    assert(FaultOps::rename(tmp.c_str(), p.c_str()) != 0);
    assert(FaultOps::destructive_deletes == 1 && access(p.c_str(), F_OK) != 0);
    assert(std::remove(tmp.c_str()) == 0);
    put(p, "old"); FaultOps::reset(); FaultOps::backup_first = true;
    FaultOps::delete_then_fail = true; FaultOps::fail_rename = 2;
    assert(!store(p)); assert(get(p) == "old" && FaultOps::destructive_deletes == 0);
    FaultOps::reset(); FaultOps::backup_first = FaultOps::delete_then_fail = true;
    FaultOps::fail_rename = 2; FaultOps::fail_rollback = true;
    assert(!store(p)); assert(access(p.c_str(), F_OK) != 0 && get(p) == "old");
    FaultOps::reset(); FaultOps::backup_first = FaultOps::delete_then_fail = true;
    assert(store(p)); assert(get(p) == "new" && FaultOps::renames == 3 && FaultOps::destructive_deletes == 0);
    put(bak, "other-backup");
    FaultOps::reset(); FaultOps::backup_first = FaultOps::delete_then_fail = true;
    assert(!store(p)); assert(get(p) == "new" && get(bak) == "other-backup");
    assert(!integrated_storage::replace_file(nullptr, "x", 1));
    assert(!integrated_storage::replace_file(p.c_str(), nullptr, 1));
    assert(std::remove(bak.c_str()) == 0); assert(std::remove(p.c_str()) == 0);
    assert(rmdir(directory) == 0);
    std::puts("PASS short write/flush/fsync/close/stat/open/rename failures; FAT rollback/recovery; libctru delete-then-EIO reproduced and prevented by 3DS backup-first");
}
