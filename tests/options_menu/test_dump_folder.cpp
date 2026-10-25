// Numbered dump folders (NNN-dump-YYYYMMDD-HHMMSS), sequence file and BMP header.
#include "dump_folder.hpp"
#include <cassert>
#include <cstdio>
#include <string>
#include <unistd.h>
static void touch(const std::string& path){FILE* f=std::fopen(path.c_str(),"wb");assert(f);std::fputs("x",f);std::fclose(f);}
int main(){
    uint32_t n=0;
    assert(dump_folder::numbered("000-dump-20261024-101500",n)&&n==0);
    assert(dump_folder::numbered("1234-dump-20261024-101500",n)&&n==1234);
    assert(!dump_folder::numbered("00-dump-20261024-101500",n));
    assert(!dump_folder::numbered("000-dump-20261024_101500",n));
    assert(!dump_folder::numbered("000-dump-20261024-101500x",n));
    assert(!dump_folder::numbered("dump-sequence.txt",n));
    char temporary[]="/tmp/sm64ds-dumps-XXXXXX";assert(mkdtemp(temporary));
    const std::string root=std::string(temporary)+"/dumps";
    char out[dump_folder::max_path];
    assert(!dump_folder::create_at(root.c_str(),"2026102-101500",out,sizeof out)&&!out[0]);
    assert(dump_folder::create_at(root.c_str(),"20261024-101500",out,sizeof out));
    assert(std::string(out)==root+"/000-dump-20261024-101500");
    touch(std::string(out)+"/info.txt");
    assert(dump_folder::create_at(root.c_str(),"20261024-101500",out,sizeof out));
    assert(std::string(out)==root+"/001-dump-20261024-101500");
    // The counter survives a deleted folder and an earlier clock.
    assert(rmdir(out)==0);
    assert(dump_folder::create_at(root.c_str(),"20200101-000000",out,sizeof out));
    assert(std::string(out)==root+"/002-dump-20200101-000000");
    auto deleted=dump_folder::delete_all(root.c_str());
    assert(deleted.failed==0&&deleted.removed==2); // info.txt + dump-sequence.txt
    // An emptied dumps folder starts again at 000.
    assert(dump_folder::create_at(root.c_str(),"20261024-101501",out,sizeof out));
    assert(std::string(out)==root+"/000-dump-20261024-101501");
    deleted=dump_folder::delete_all(root.c_str());
    assert(deleted.failed==0);
    assert(rmdir(root.c_str())==0&&rmdir(temporary)==0);
    uint8_t header[54];dump_folder::bmp_header(header,400,240);
    assert(header[0]=='B'&&header[1]=='M'&&header[18]==144&&header[19]==1&&header[22]==240&&header[28]==24);
    assert(dump_folder::bmp_row_bytes(400)==1200&&dump_folder::bmp_row_bytes(321)==964);
    const uint32_t file=header[2]|header[3]<<8|header[4]<<16|header[5]<<24;assert(file==54+1200*240);
    std::puts("PASS dump folders: numbering, sequence, restart at 000, deletion and BMP header");
}
