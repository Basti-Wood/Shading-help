// SPDX-License-Identifier: Apache-2.0
#include "progressive.hpp"
#include "gpu.hpp"
namespace shading {
Progressive::Progressive():worker([this]{run();}){}
Progressive::~Progressive(){stop=true;ready.notify_one();worker.join();}
int Progressive::request(Settings s,int w,int h,int limit,std::shared_ptr<const std::vector<Triangle>> imported,bool preferGpu) {
    s.validate();std::lock_guard<std::mutex> guard(mutex);
    int id=revision.fetch_add(1)+1;
    pending={s,std::clamp(w,1,2048),std::clamp(h,1,2048),std::clamp(limit,1,4096),id,std::move(imported),preferGpu};
    ready.notify_one();return id;
}
bool Progressive::frame(Frame& out,int samples,int id) {
    std::lock_guard<std::mutex> guard(mutex);
    if(output.revision!=id||output.samples<=samples)return false;
    out=output;return true;
}
void Progressive::run() {
    int last=0,sceneVersion=0,gpuSceneVersion=-1;Settings geometrySettings;Scene scene;
    std::shared_ptr<const std::vector<Triangle>> previousImport;
    GpuRenderer gpu;bool triedGpu=false,gpuReady=false;std::string gpuProblem;
    auto publishSample=[](int n,int limit){return n<=8||(n&(n-1))==0||n%16==0||n==limit;};
    while(!stop) {
        Job job;
        {std::unique_lock<std::mutex> lock(mutex);ready.wait(lock,[&]{return stop||pending.revision!=last;});if(stop)break;job=pending;}
        last=job.revision;
        const std::vector<Triangle> empty;
        if(sceneVersion==0||!sameGeometry(job.settings,geometrySettings)||job.imported!=previousImport) {
            scene=makeScene(job.settings,job.imported?*job.imported:empty);geometrySettings=job.settings;
            previousImport=job.imported;++sceneVersion;
        }
        scene.ground=job.settings.ground;
        scene.albedo={fromSrgb(job.settings.surface[0]),fromSrgb(job.settings.surface[1]),fromSrgb(job.settings.floorColor)};
        auto cancelled=[&]{return stop||revision!=last;};
        if(job.preferGpu&&!triedGpu){triedGpu=true;gpuReady=gpu.initialize();gpuProblem=gpu.error();}
        if(job.preferGpu&&gpuReady) {
            try {
                gpu.reset(scene,job.settings,job.w,job.h,gpuSceneVersion!=sceneVersion);gpuSceneVersion=sceneVersion;
                for(int sample=0;sample<job.limit&&!cancelled();++sample) {
                    if(!gpu.sample(uint32_t(sample),cancelled))break;
                    int n=sample+1;if(!publishSample(n,job.limit)||cancelled())continue;
                    Frame frame;frame.width=job.w;frame.height=job.h;frame.samples=n;frame.revision=last;
                    frame.usingGpu=true;frame.backend="GPU / Direct3D 11";frame.device=gpu.name();
                    gpu.readback(frame.linear,frame.pixels,n);
                    {std::lock_guard<std::mutex> lock(mutex);if(revision==last)output=std::move(frame);}
                }
                continue;
            }catch(const std::exception& e){gpuReady=false;gpuProblem=e.what();}
        }
        Camera camera(job.settings,float(job.w)/job.h);
        std::vector<Vec> sum(size_t(job.w)*job.h);
        unsigned count=std::min(12u,std::max(1u,std::thread::hardware_concurrency()));
        for(int sample=0;sample<job.limit&&!stop&&revision==last;++sample) {
            std::atomic<int> nextRow{0};std::vector<std::thread> pool;
            for(unsigned i=0;i<count;++i)pool.emplace_back([&]{
                for(int y;(y=nextRow.fetch_add(1))<job.h;) {
                    if(stop||revision!=last)return;
                    for(int x=0;x<job.w;++x)sum[size_t(y)*job.w+x]+=samplePixel(scene,job.settings,camera,x,y,job.w,job.h,uint32_t(sample));
                }
            });
            for(auto& t:pool)t.join();
            if(stop||revision!=last)break;
            // Publish early feedback, then at powers of two / every 16 passes.
            int n=sample+1;if(!publishSample(n,job.limit))continue;
            Frame frame;frame.width=job.w;frame.height=job.h;frame.samples=n;frame.revision=last;
            frame.backend=job.preferGpu?"CPU fallback":"CPU reference";
            frame.device=job.preferGpu?gpuProblem:"CPU selected in View settings.";
            frame.pixels.resize(sum.size());frame.linear.resize(sum.size());
            for(size_t i=0;i<sum.size();++i){frame.linear[i]=sum[i]/float(n);frame.pixels[i]=displayPixel(frame.linear[i],job.settings);}
            {std::lock_guard<std::mutex> lock(mutex);if(revision==last)output=std::move(frame);}
        }
    }
}
}
