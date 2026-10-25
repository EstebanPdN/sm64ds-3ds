#include "sm64ds/model.hpp"
#include <cstdio>
int main(int argc,char** argv) {
    if(argc!=3 && argc!=4)return 2;
    sm64ds::AssetPack p;sm64ds::ModelPreview m;std::string error;
    if(!p.open(argv[1],error) || !m.load(p,argv[2],error)) {fprintf(stderr,"%s\n",error.c_str());return 1;}
    printf("path=%s triangles=%zu bones=%u materials=%zu center=%f,%f,%f radius=%f\n",argv[2],m.triangles.size(),m.model.num_bones,m.model.materials.size(),m.center[0],m.center[1],m.center[2],m.radius);
    printf("material_ids=");for(auto id:m.model.material_ids)printf("%u,",id);puts("");
    for(auto& t:m.model.textures)printf("texture=%s %dx%d format=%d decoded=%zu\n",t.name.c_str(),t.width,t.height,t.format,t.rgba.size());
    if(argc==4)for(auto& t:m.model.textures)if(!t.rgba.empty()) {
        std::string path=std::string(argv[3])+"/"+t.name+".ppm";
        FILE* f=fopen(path.c_str(),"wb");if(!f)return 1;
        fprintf(f,"P6\n%d %d\n255\n",t.width,t.height);
        for(auto c:t.rgba) {unsigned char rgb[]={static_cast<unsigned char>(c>>16),static_cast<unsigned char>(c>>8),static_cast<unsigned char>(c)};fwrite(rgb,1,3,f);}fclose(f);
    }
    return 0;
}
