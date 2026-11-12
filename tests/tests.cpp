// SPDX-License-Identifier: Apache-2.0
#include "../src/core.hpp"
#include "../src/progressive.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace shading;
static int checks=0;
static void require(bool condition,const char* name) {
    ++checks;if(!condition)throw std::runtime_error(name);
    std::cout<<"PASS "<<name<<'\n';
}
static bool near(float a,float b,float tolerance=1e-4f){return std::abs(a-b)<tolerance;}
static bool near(Vec a,Vec b,float tolerance=1e-4f){return length(a-b)<tolerance;}
#include "mesh_tests.hpp"
int main() {
    auto temp=std::filesystem::current_path()/"build"/"test-data";
    std::filesystem::create_directories(temp);
    try {
        require(near(fromSrgb(Vec(.5f)).x,.21404114f),"sRGB 0.5 decodes to linear 0.214041");
        require(near(toSrgb(fromSrgb({.02f,.5f,.95f})),{.02f,.5f,.95f}),"piecewise sRGB round trip");
        Settings s;Scene scene;scene.ground=false;scene.albedo[0]=Vec(.5f);
        s.lightType=LightType::Point;s.lightColor=Vec(1);s.intensity=100;s.lightPosition={0,2,0};s.fill=0;
        Hit h;h.position={0,0,0};h.normal={0,1,0};h.material=0;Rng rng(1);
        float first=directLighting(scene,s,h,rng).x;
        require(near(first,.5f*100/(4*pi)),"Lambertian rho/pi and inverse-square normalization");
        s.lightPosition.y=4;
        require(near(directLighting(scene,s,h,rng).x,first/4),"doubling point-light distance quarters irradiance");
        s.lightType=LightType::Directional;s.intensity=pi;s.lightDirection={0,-1,0};
        require(near(directLighting(scene,s,h,rng),Vec(.5f)),"directional normal incidence");
        s.lightDirection=unit({1,-1,0});
        require(near(directLighting(scene,s,h,rng).x,.5f/std::sqrt(2.f)),"45-degree surface cosine law");
        s.lightDirection={0,1,0};
        require(near(directLighting(scene,s,h,rng),Vec()),"back-facing surface receives no direct light");
        s.lightType=LightType::Point;s.intensity=100;s.lightPosition={0,2,0};
        scene.spheres.push_back({{0,1,0},.25f,0,0});
        require(near(directLighting(scene,s,h,rng),Vec()),"sphere blocks shadow ray");
        scene.spheres[0].center={0,3,0};
        require(directLighting(scene,s,h,rng).x>0,"occluder beyond point light does not cast shadow");
        scene.spheres.clear();s.lightType=LightType::Spot;s.lightDirection={0,1,0};s.lightRadius=0;
        require(near(directLighting(scene,s,h,rng),Vec()),"spotlight direction excludes outside cone");
        s.lightDirection={0,-1,0};
        require(near(directLighting(scene,s,h,rng).x,first),"zero-radius spotlight agrees with on-axis point light");
        s.lightRadius=.8f;scene.spheres.push_back({{.24f,1,0},.2f,0,0});
        int dark=0,lit=0;
        for(int i=0;i<4096;++i) {Vec v=directLighting(scene,s,h,rng);if(v.x==0)++dark;else ++lit;}
        require(dark>30&&lit>30,"disk area light creates geometric partial visibility / penumbra");
        Sphere sphere{{0,0,0},1,0,0};Hit hit;
        require(hitSphere(sphere,{{0,0,3},{0,0,-1}},hit)&&near(hit.t,2),"analytic sphere intersection");
        hit=Hit();require(hitSphere(sphere,{{0,0,0},{1,0,0}},hit)&&near(hit.t,1),"ray starting inside sphere finds exit");
        Triangle triangle{{-1,0,-1},{1,0,-1},{0,0,1},0,0};hit=Hit();
        require(hitTriangle(triangle,{{0,1,0},{0,-1,0}},hit)&&near(hit.t,1),"triangle intersection and flat normal");
        Bounds box;box.add({0,0,0});box.add({1,1,1});
        require(box.intersects({{0,.5f,-1},{0,0,1}},infinity),"parallel slab ray on box boundary");
        require(!box.intersects({{2,.5f,-1},{0,0,1}},infinity),"parallel slab ray outside box");
        Settings head;Scene mesh=makeScene(head);mesh.ground=false;bool agrees=true;
        for(int i=0;i<3000;++i) {
            Vec o{(rng.next()-.5f)*8,rng.next()*5,(rng.next()-.5f)*8};
            Ray r{o,unit(Vec(0,1.5f,0)-o+Vec(rng.next()-.5f,rng.next()-.5f,rng.next()-.5f))};
            Hit a,b;bool fast=mesh.intersect(r,a),slow=false;
            for(const auto& t:mesh.triangles)slow=hitTriangle(t,r,b)||slow;
            if(fast!=slow||(fast&&!near(a.t,b.t,.001f)))agrees=false;
        }
        require(agrees,"BVH agrees with brute-force geometry on 3000 rays");
        s=Settings();s.model=Model::Ball;s.ground=false;s.surface[0]=toSrgb(Vec(.5f));s.intensity=0;s.fillColor=Vec(1);s.fill=.4f;s.bounces=0;
        scene=makeScene(s);Vec env;
        for(int i=0;i<1000;++i){auto c=trace(scene,s,{{0,1,3},{0,0,-1}},rng);env+=c.direct+c.indirect;}
        require(near(env/1000,Vec(.2f),.0001f),"constant environment integrates to albedo times radiance");
        s.ground=true;s.fill=0;s.intensity=70;s.lightType=LightType::Point;s.lightColor=Vec(1);
        s.lightPosition={-3,4,2};s.floorColor={.8f,.02f,.01f};s.surface[0]=Vec(.7f);s.bounces=1;scene=makeScene(s);
        Ray underside{{0,.35f,3},unit(Vec(0,.5f,0)-Vec(0,.35f,3))};Vec bounce;
        for(int i=0;i<50000;++i)bounce+=trace(scene,s,underside,rng).indirect;
        bounce=bounce/50000;
        require(bounce.x>.002f&&bounce.x>bounce.y*30,"red floor produces actual red indirect transport onto sphere");
        s.bounces=0;require(near(trace(scene,s,underside,rng).indirect,Vec()),"zero bounces removes surface interreflection");
        s.intensity=0;s.bounces=4;
        auto black=trace(scene,s,underside,rng);require(near(black.direct+black.indirect,Vec()),"no lights and no environment means exact black");
        s=Settings();s.cameraRoll=53;s.cameraPitch=67;Camera camera(s,1.5f);float u,v;
        Ray r=camera.ray(.31f,.72f);require(camera.project(r.at(3),u,v)&&near(u,.31f)&&near(v,.72f),"camera ray/project round trip including roll");
        auto sceneFile=temp/"scene.shade";s.jointPitch[3]=71;s.position[1]={1,2,3};s.objPath="C:/a folder/head.obj";
        saveSettings(sceneFile,s);auto loaded=loadSettings(sceneFile);
        require(near(loaded.position[1],s.position[1])&&near(loaded.cameraRoll,53)&&loaded.objPath==s.objPath&&loaded.jointPitch[3]==71,"scene save/load retains transforms, pose, camera and spaced paths");
        {std::ofstream out(sceneFile);out<<"SHADING_HELP 1\n0 0";}
        bool rejected=false;try{loadSettings(sceneFile);}catch(...){rejected=true;}
        require(rejected,"truncated scene rejected atomically");
        auto obj=temp/"mesh.obj";{std::ofstream out(obj);out<<"v -1 0 0\nv 1 0 0\nv 0 2 0\nf -3/-1 -2/-1 -1/-1\n";}
        auto imported=loadObj(obj);require(imported.size()==1&&near(imported[0].c.y,2.8f),"OBJ negative indices and height normalization");
        {std::ofstream out(obj);out<<"v 0 0 0\nf 1 99 1\n";}
        rejected=false;try{loadObj(obj);}catch(...){rejected=true;}
        require(rejected,"out-of-range OBJ indices rejected");
        s=Settings();s.intensity=infinity;s.lightDirection=Vec();s.cameraDistance=-1;s.bounces=500;s.validate();
        require(std::isfinite(s.intensity)&&near(length(s.lightDirection),1)&&s.cameraDistance>0&&s.bounces==8,"input bounds and degenerate vectors sanitized");
        s=Settings();auto image=render(s,32,32,4);auto repeat=render(s,32,32,4);
        require(image==repeat,"render is deterministic across worker scheduling");
        saveBmp(temp/"render.bmp",image,32,32);
        require(std::filesystem::file_size(temp/"render.bmp")==54+32*32*4,"BMP output includes complete header and pixel data");
        {
            Progressive progressive;auto importedPtr=std::make_shared<const std::vector<Triangle>>();
            progressive.request(s,256,256,512,importedPtr,false);s.model=Model::Ball;
            int id=progressive.request(s,32,24,8,importedPtr,false);Frame frame;
            auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
            while(std::chrono::steady_clock::now()<deadline) {
                if(progressive.frame(frame,7,id))break;
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            require(frame.revision==id&&frame.samples==8&&frame.width==32&&frame.height==24,"progressive renderer cancels stale work and publishes latest scene");
            require(frame.pixels==render(s,32,24,8),"progressive accumulation matches reference renderer");
        }
        meshTests(temp);
        std::filesystem::remove_all(temp);
        std::cout<<"All "<<checks<<" checks passed.\n";return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
