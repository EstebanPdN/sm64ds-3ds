#include "sm64ds/game_files.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>

static void put32(std::vector<uint8_t>& bytes,size_t at,uint32_t n) {
    for(unsigned i=0;i<4;++i) bytes[at+i]=uint8_t(n>>(8*i));
}
int main(int argc,char** argv) {
    // Two members, with padding between them. IDs are offsets within GMIF,
    // not offsets within the outer file or the FAT chunk.
    std::vector<uint8_t> valid(76);
    std::memcpy(valid.data(),"NARC\xfe\xff\x00\x01",8);
    put32(valid,8,76);valid[12]=16;valid[14]=3;
    std::memcpy(valid.data()+16,"BTAF",4);put32(valid,20,28);valid[24]=2;
    put32(valid,28,0);put32(valid,32,3);put32(valid,36,4);put32(valid,40,8);
    std::memcpy(valid.data()+44,"BTNF",4);put32(valid,48,16);
    std::memcpy(valid.data()+60,"GMIF",4);put32(valid,64,16);
    std::memcpy(valid.data()+68,"ABC\0DEFG",8);
    std::vector<sm64ds::NarcMember> members;
    std::string error;
    assert(sm64ds::parse_narc(valid,members,error));
    assert(members.size()==2 && members[0].offset==68 && members[0].size==3 &&
           members[1].offset==72 && members[1].size==4);
    unsigned rejected=0;
    const auto reject=[&](const std::vector<uint8_t>& broken) {
        auto output=members;
        assert(!sm64ds::parse_narc(broken,output,error) && !error.empty());
        assert(output.size()==2 && output[0].offset==68 && output[1].size==4);
        ++rejected;
    };
    for(size_t size : {size_t(0),size_t(15),size_t(27),size_t(75)}) {
        auto bad=valid;bad.resize(size);reject(bad);
    }
    for(size_t byte : {size_t(0),size_t(4),size_t(6),size_t(8),size_t(12),size_t(14),
                      size_t(16),size_t(20),size_t(24),size_t(26),size_t(44),size_t(60)}) {
        auto bad=valid;bad[byte]^=0x80;reject(bad);
    }
    auto bad=valid;put32(bad,40,0xffffffff);reject(bad);
    bad=valid;put32(bad,36,2);reject(bad);
    bad=valid;put32(bad,32,0xffffffff);reject(bad);
    bad=valid;std::memcpy(bad.data()+44,"BTAF",4);reject(bad);
    bad=valid;bad.push_back(0);put32(bad,8,uint32_t(bad.size()));reject(bad);
    printf("NARC parser: valid member ranges and %u invalid cases passed\n",rejected);
    if(argc>=2) {
        sm64ds::AssetPack pack;assert(pack.open(argv[1],error));
        sm64ds::GameFiles files(pack);
        std::vector<uint8_t> output{1,2,3};
        assert(!files.read_archive(0x8000,output,error) && output==std::vector<uint8_t>({1,2,3}));
        assert(files.initialize_common_archive(error));
        for(unsigned i=0;i<198;++i) {
            assert(files.read(uint16_t(0x8000+i),sm64ds::FileIdDomain::ExplicitSourceBindings,output,error));
            assert(!output.empty());
            const auto reference=output;
            assert(files.read(uint16_t(0x8000+i),sm64ds::FileIdDomain::CurrentRomFat,output,error));
            assert(output==reference);
            printf("member %04x %zu %08x\n",0x8000+i,output.size(),sm64ds::crc32(output.data(),output.size()));
        }
        const auto before=output;
        for(uint16_t id : {uint16_t(0x7fff),uint16_t(0x80c6),uint16_t(0x8400),uint16_t(0xffff)})
            assert(!files.read_archive(id,output,error) && output==before);
        assert(files.initialize_common_archive(error));
        assert(files.read_source(0x8083,output,error)); // original Mario body BMD
        printf("Original archive: all 198 members decoded and regional domains agree\n");
        if(argc==3) {
            std::ifstream ranges(argv[2]);assert(ranges);
            unsigned base,end;std::string path;
            while(ranges>>std::hex>>base>>end>>path) {
                assert(files.bind_archive_range(uint16_t(base),uint16_t(end),path,error));
                for(unsigned id=base;id<end;++id) {
                    assert(files.read_archive(uint16_t(id),output,error));
                    printf("archive %04x %zu %08x\n",id,output.size(),sm64ds::crc32(output.data(),output.size()));
                }
            }
            assert(ranges.eof());
            assert(!files.bind_archive_range(0x8001,0x8003,"ARCHIVE/arc0.narc",error));
            assert(files.read_archive(0x8083,output,error));
            assert(!files.read_archive(0xffff,output,error));
        }
    }
}
