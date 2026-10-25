#pragma once
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#include "dump_folder.hpp"
namespace native_diagnostics {
// Files of the pre-folder layout (sdmc:/3ds/sm64ds/native-<tick>.*).
inline bool dump_name(const char* name) {
    if (std::strncmp(name,"native-",7)) return false;
    if (std::strlen(name)<26) return false;
    for (unsigned i=7;i<23;++i)
        if (!((name[i]>='0'&&name[i]<='9')||(name[i]>='a'&&name[i]<='f'))) return false;
    return !std::strcmp(name+23,".vm") || !std::strcmp(name+23,".trace") ||
           !std::strcmp(name+23,".anchors.json") || !std::strcmp(name+23,".render") ||
           !std::strcmp(name+23,".upper") || !std::strcmp(name+23,".lower") ||
           !std::strcmp(name+23,".lcd");
}
using DeleteResult=dump_folder::DeleteResult;
// Every numbered dump folder under <directory>/dumps plus legacy files.
inline DeleteResult delete_dumps(const char* directory=dump_folder::game_directory) {
    char root[dump_folder::max_path];
    std::snprintf(root,sizeof root,"%s/dumps",directory);
    DeleteResult result=dump_folder::delete_all(root);
    DIR* dir=::opendir(directory);
    if (!dir) {++result.failed;return result;}
    while (auto* entry=::readdir(dir)) {
        if (!dump_name(entry->d_name)) continue;
        char path[512];
        if (std::snprintf(path,sizeof path,"%s/%s",directory,entry->d_name)>=int(sizeof path)) {
            ++result.failed;continue;
        }
        struct stat info{};
        if (::lstat(path,&info)!=0 || !S_ISREG(info.st_mode)) continue;
        if (std::remove(path)==0) ++result.removed; else ++result.failed;
    }
    ::closedir(dir);
    return result;
}
}
