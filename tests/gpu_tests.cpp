// SPDX-License-Identifier: Apache-2.0
#include "../src/gpu.hpp"
#include <iostream>
#include <stdexcept>
using namespace shading;
static void require(bool value,const std::string& message){if(!value)throw std::runtime_error(message);std::cout<<"PASS "<<message<<'\n';}
static void compare(GpuRenderer& gpu,Settings s,const char* name,int width=31,int height=73,int samples=32) {
    s.validate();auto scene=makeScene(s);gpu.reset(scene,s,width,height);
    for(int i=0;i<samples;++i)gpu.sample(uint32_t(i));
    std::vector<Vec> linear;std::vector<uint32_t> pixels;gpu.readback(linear,pixels,samples);
    Camera camera(s,float(width)/height);double square=0,energy=0;double displayError=0;
    for(int y=0;y<height;++y)for(int x=0;x<width;++x) {
        Vec cpu;for(int n=0;n<samples;++n)cpu+=samplePixel(scene,s,camera,x,y,width,height,uint32_t(n));cpu=cpu/float(samples);
        size_t index=size_t(y)*width+x;if(!finite(linear[index]))throw std::runtime_error("Non-finite GPU radiance");
        Vec delta=linear[index]-cpu;square+=dot(delta,delta);energy+=luminance(cpu);
        uint32_t display=displayPixel(linear[index],s);
        for(int channel=0;channel<3;++channel)displayError+=std::abs(int((display>>(8*channel))&255)-int((pixels[index]>>(8*channel))&255));
    }
    double rms=std::sqrt(square/(width*height*3)),mean=energy/(width*height),displayMean=displayError/(width*height*3);
    std::cout<<name<<": linear RMSE "<<rms<<", mean luminance "<<mean<<", display error "<<displayMean<<'\n';
    require(rms<.008+.06*mean,std::string(name)+" GPU light transport agrees with CPU reference");
    require(displayMean<.15,std::string(name)+" GPU exposure/grayscale/sRGB agree with CPU display");
}
int main() {
    try {
        GpuRenderer gpu;if(!gpu.initialize(true))throw std::runtime_error(gpu.error());
        std::cout<<"Validating HLSL using "<<gpu.name()<<'\n';
        Settings s;s.model=Model::Ball;s.ground=false;s.surface[0]=toSrgb(Vec(.5f));s.intensity=0;s.fillColor=Vec(1);s.fill=.4f;
        s.cameraTarget={0,1,0};s.cameraDistance=3;s.fov=10;
        gpu.reset(makeScene(s),s,4,4);for(int i=0;i<8;++i)gpu.sample(uint32_t(i));
        std::vector<Vec> values;std::vector<uint32_t> pixels;gpu.readback(values,pixels,8);
        bool normalized=true;for(Vec v:values)if(length(v-Vec(.2f))>1e-5f)normalized=false;
        require(normalized,"GPU constant environment gives rho times radiance");
        s.fill=0;gpu.reset(makeScene(s),s,4,4,false);gpu.sample(0);gpu.readback(values,pixels,1);
        bool black=true;for(size_t i=0;i<values.size();++i)if(length(values[i])!=0||pixels[i]!=0)black=false;
        require(black,"changing illumination clears old GPU accumulation and produces exact black");
        s=Settings();s.model=Model::Head;compare(gpu,s,"Planar head / soft spot / two bounces");
        s=Settings();s.model=Model::Ball;s.lightType=LightType::Point;compare(gpu,s,"Sphere / point light");
        s.lightType=LightType::Directional;s.intensity=2;compare(gpu,s,"Directional light");
        s=Settings();s.model=Model::Both;s.position[0].x=-1.2f;s.position[1].x=1.2f;s.cameraDistance=10;compare(gpu,s,"Triangles plus analytic sphere");
        s=Settings();s.model=Model::Mannequin;s.jointSpread[0]=45;s.jointPitch[1]=70;compare(gpu,s,"Posed mannequin");
        s=Settings();s.view=View::Normals;compare(gpu,s,"Geometric normals",23,19,8);
        s.view=View::Albedo;compare(gpu,s,"Albedo",23,19,8);
        s=Settings();s.model=Model::Ball;s.floorColor={.8f,.02f,.01f};s.view=View::Indirect;s.fill=0;s.bounces=3;
        compare(gpu,s,"Red floor bounce isolation",31,39,128);
        s.view=View::Direct;s.exposure=-1;s.grayscale=true;compare(gpu,s,"Direct grayscale / negative exposure",23,19,32);
        GpuRenderer hardware;
        if(hardware.initialize()) {std::cout<<"Hardware found: "<<hardware.name()<<'\n';compare(hardware,Settings(),"Physical hardware");}
        else std::cout<<"No hardware benchmark on this runner. "<<hardware.error()<<'\n';
        std::cout<<"All GPU shader checks passed.\n";return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
