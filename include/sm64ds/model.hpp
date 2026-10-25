#pragma once
#include "sm64ds/assets.hpp"
#include "ntr/bmd.h"
#include "ntr/gx.h"
#include <vector>
namespace sm64ds {
struct ModelPreview {
    ntr::BmdModel model;
    std::vector<ntr::GxTriangle> triangles;
    float center[3]{},radius=1;
    bool load(AssetPack& pack,const char* path,std::string& error);
};
}
