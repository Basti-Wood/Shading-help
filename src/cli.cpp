// SPDX-License-Identifier: Apache-2.0
#include "core.hpp"
#include <iostream>
#include <chrono>
int main(int argc,char** argv) {
    try {
        shading::Settings s;
        std::string model=argc>2?argv[2]:"head";
        std::vector<shading::Triangle> imported;
        if(std::filesystem::u8path(model).has_extension()) {
            imported=shading::loadMesh(std::filesystem::u8path(model));s.model=shading::Model::Imported;
        }
        if(model=="ball")s.model=shading::Model::Ball;
        if(model=="both"){s.model=shading::Model::Both;s.position[0].x=-1.2f;s.position[1].x=1.2f;s.cameraDistance=10;}
        if(model=="mannequin"){s.model=shading::Model::Mannequin;s.jointSpread[0]=55;s.jointPitch[1]=65;}
        if(model=="bounce"){s.model=shading::Model::Ball;s.floorColor={.7f,.07f,.035f};s.fill=0;s.bounces=3;}
        if(model=="direct"){s.model=shading::Model::Ball;s.floorColor={.7f,.07f,.035f};s.fill=0;s.bounces=0;}
        if(model=="rim"){s.lightPosition={2,3,-2};s.lightDirection=shading::unit(shading::Vec(0,1.4f,0)-s.lightPosition);s.lightColor={.6f,.75f,1};}
        int samples=argc>3?std::stoi(argv[3]):64;
        int width=argc>4?std::stoi(argv[4]):800,height=argc>5?std::stoi(argv[5]):800;
        auto begin=std::chrono::steady_clock::now();
        auto pixels=shading::render(s,width,height,samples,imported);
        const char* path=argc>1?argv[1]:"render.bmp";shading::saveBmp(path,pixels,width,height);
        std::cout<<"Rendered "<<path<<" ("<<samples<<" spp) in "
                 <<std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count()<<" s\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
