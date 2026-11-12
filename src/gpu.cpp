// SPDX-License-Identifier: Apache-2.0
#include "gpu.hpp"
#include <stdexcept>
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <sstream>
#include "gpu_shader.hpp"
namespace shading {
using Microsoft::WRL::ComPtr;
namespace {
void check(HRESULT hr,const char* operation) {
    if(FAILED(hr)){std::ostringstream error;error<<operation<<" failed (0x"<<std::hex<<uint32_t(hr)<<").";throw std::runtime_error(error.str());}
}
struct Float4 {float x=0,y=0,z=0,w=0;Float4()=default;Float4(Vec v,float a=0):x(v.x),y(v.y),z(v.z),w(a){}};
struct Uint4 {uint32_t x=0,y=0,z=0,w=0;};
struct Parameters {
    Float4 eye,right,up,forward,lightPosition,lightDirection,lightEnergy,environment;
    Float4 albedo[3];Uint4 dimensions,geometry,tile;Float4 display;
};
struct GpuTriangle {Vec a;int material;Vec b;int object;Vec c;float pad=0;};
struct GpuSphere {Vec center;float radius;int material,object;int pad[2]{};};
struct GpuNode {Vec lo;int left;Vec hi;int right;int start,count;int pad[2]{};};
static_assert(sizeof(Parameters)==240&&sizeof(GpuTriangle)==48&&sizeof(GpuSphere)==32&&sizeof(GpuNode)==48,"HLSL layout mismatch");
std::string utf8(const wchar_t* text) {
    int n=WideCharToMultiByte(CP_UTF8,0,text,-1,nullptr,0,nullptr,nullptr);
    if(n<=1)return "Direct3D adapter";
    std::string result(size_t(n),'\0');WideCharToMultiByte(CP_UTF8,0,text,-1,result.data(),n,nullptr,nullptr);result.pop_back();return result;
}
}
struct GpuRenderer::Impl {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11ComputeShader> shader;
    ComPtr<ID3D11Buffer> constants,sum,pixels,readSum,readPixels;
    ComPtr<ID3D11ShaderResourceView> geometry[3];
    ComPtr<ID3D11UnorderedAccessView> sumView,pixelView;
    Parameters parameters{};
    int width=0,height=0;
    std::string deviceName,problem;
    template<typename T> void upload(std::vector<T> data,ComPtr<ID3D11ShaderResourceView>& view) {
        if(data.empty())data.resize(1);
        D3D11_BUFFER_DESC desc{};desc.ByteWidth=UINT(data.size()*sizeof(T));desc.Usage=D3D11_USAGE_IMMUTABLE;
        desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;desc.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;desc.StructureByteStride=sizeof(T);
        D3D11_SUBRESOURCE_DATA source{};source.pSysMem=data.data();ComPtr<ID3D11Buffer> buffer;
        check(device->CreateBuffer(&desc,&source,buffer.GetAddressOf()),"Uploading geometry");
        view.Reset();check(device->CreateShaderResourceView(buffer.Get(),nullptr,view.GetAddressOf()),"Creating geometry view");
    }
    void output(UINT stride,ComPtr<ID3D11Buffer>& buffer,ComPtr<ID3D11UnorderedAccessView>& view,ComPtr<ID3D11Buffer>& staging) {
        buffer.Reset();view.Reset();staging.Reset();
        D3D11_BUFFER_DESC desc{};desc.ByteWidth=UINT(width*height)*stride;desc.Usage=D3D11_USAGE_DEFAULT;
        desc.BindFlags=D3D11_BIND_UNORDERED_ACCESS;desc.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;desc.StructureByteStride=stride;
        check(device->CreateBuffer(&desc,nullptr,buffer.GetAddressOf()),"Allocating GPU image");
        check(device->CreateUnorderedAccessView(buffer.Get(),nullptr,view.GetAddressOf()),"Creating GPU image view");
        desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;desc.MiscFlags=0;desc.StructureByteStride=0;
        check(device->CreateBuffer(&desc,nullptr,staging.GetAddressOf()),"Allocating image readback");
    }
};
GpuRenderer::GpuRenderer():impl(std::make_unique<Impl>()){}
GpuRenderer::~GpuRenderer()=default;
const std::string& GpuRenderer::name()const{return impl->deviceName;}
const std::string& GpuRenderer::error()const{return impl->problem;}
bool GpuRenderer::initialize(bool softwareForTests) {
    try {
        auto& d=*impl;D3D_FEATURE_LEVEL level=D3D_FEATURE_LEVEL_11_0;
        if(softwareForTests) {
            check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,&level,1,D3D11_SDK_VERSION,
                d.device.GetAddressOf(),nullptr,d.context.GetAddressOf()),"Creating software shader test device");
            d.deviceName="WARP software shader validation (not GPU hardware)";
        } else {
            ComPtr<IDXGIFactory1> factory;check(CreateDXGIFactory1(IID_PPV_ARGS(factory.GetAddressOf())),"Enumerating graphics hardware");
            struct Adapter {ComPtr<IDXGIAdapter1> value;DXGI_ADAPTER_DESC1 desc;};std::vector<Adapter> candidates;
            for(UINT i=0;;++i) {
                ComPtr<IDXGIAdapter1> adapter;HRESULT hr=factory->EnumAdapters1(i,adapter.GetAddressOf());if(hr==DXGI_ERROR_NOT_FOUND)break;
                check(hr,"Enumerating adapter");DXGI_ADAPTER_DESC1 info{};check(adapter->GetDesc1(&info),"Reading adapter");
                if(!(info.Flags&DXGI_ADAPTER_FLAG_SOFTWARE))candidates.push_back({adapter,info});
            }
            std::stable_sort(candidates.begin(),candidates.end(),[](const Adapter& a,const Adapter& b){return a.desc.DedicatedVideoMemory>b.desc.DedicatedVideoMemory;});
            for(auto& a:candidates) {
                d.device.Reset();d.context.Reset();
                HRESULT hr=D3D11CreateDevice(a.value.Get(),D3D_DRIVER_TYPE_UNKNOWN,nullptr,0,&level,1,D3D11_SDK_VERSION,
                    d.device.GetAddressOf(),nullptr,d.context.GetAddressOf());
                if(SUCCEEDED(hr)){d.deviceName=utf8(a.desc.Description);break;}
            }
            if(!d.device)throw std::runtime_error("No compatible Direct3D 11 hardware adapter. Using CPU.");
        }
        ComPtr<ID3DBlob> code,errors;
        HRESULT compiled=D3DCompile(pathShader,sizeof(pathShader)-1,"ShadingHelpPathTracer",nullptr,nullptr,"main","cs_5_0",
            D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_IEEE_STRICTNESS|D3DCOMPILE_OPTIMIZATION_LEVEL3,0,code.GetAddressOf(),errors.GetAddressOf());
        if(FAILED(compiled)) {
            std::string detail=errors?std::string(static_cast<const char*>(errors->GetBufferPointer()),errors->GetBufferSize()):"Unknown HLSL error";
            throw std::runtime_error("GPU shader compilation: "+detail);
        }
        check(d.device->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,d.shader.GetAddressOf()),"Creating path tracing shader");
        D3D11_BUFFER_DESC desc{};desc.ByteWidth=sizeof(Parameters);desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        check(d.device->CreateBuffer(&desc,nullptr,d.constants.GetAddressOf()),"Creating GPU parameters");
        return true;
    }catch(const std::exception& e){impl->problem=e.what();return false;}
}
void GpuRenderer::reset(const Scene& scene,const Settings& original,int w,int h,bool geometryChanged) {
    auto& d=*impl;if(!d.shader)throw std::runtime_error("GPU renderer is not initialized.");
    if(w<1||h<1||w>2048||h>2048)throw std::runtime_error("GPU image dimensions out of range.");
    Settings s=original;s.validate();
    if(geometryChanged||!d.geometry[0]) {
        std::vector<GpuTriangle> triangles;triangles.reserve(scene.triangles.size());
        for(const auto& t:scene.triangles)triangles.push_back({t.a,t.material,t.b,t.object,t.c,0});
        std::vector<GpuSphere> spheres;for(const auto& b:scene.spheres)spheres.push_back({b.center,b.radius,b.material,b.object,{0,0}});
        std::vector<GpuNode> nodes;nodes.reserve(scene.nodes.size());for(const auto& n:scene.nodes)nodes.push_back({n.bounds.lo,n.left,n.bounds.hi,n.right,n.start,n.count,{0,0}});
        d.upload(std::move(triangles),d.geometry[0]);d.upload(std::move(spheres),d.geometry[1]);d.upload(std::move(nodes),d.geometry[2]);
    }
    if(w!=d.width||h!=d.height) {
        d.width=w;d.height=h;d.output(16,d.sum,d.sumView,d.readSum);d.output(4,d.pixels,d.pixelView,d.readPixels);
    }
    Camera camera(s,float(w)/h);Parameters& p=d.parameters;
    p.eye={camera.eye,camera.tangent};p.right={camera.right,camera.aspect};p.up={camera.up};p.forward={camera.forward};
    p.lightPosition={s.lightPosition,s.lightRadius};p.lightDirection={unit(s.lightDirection),std::cos(radians(s.coneDegrees))};
    p.lightEnergy={fromSrgb(s.lightColor)*s.intensity,std::cos(radians(s.coneDegrees*.8f))};p.environment={fromSrgb(s.fillColor)*s.fill};
    for(int i=0;i<3;++i)p.albedo[i]={scene.albedo[i]};
    p.dimensions={uint32_t(w),uint32_t(h),0,uint32_t(s.view)};
    p.geometry={uint32_t(scene.spheres.size()),uint32_t(scene.nodes.size()),scene.ground?1u:0u,uint32_t(s.bounces)};
    p.tile={0,uint32_t(s.lightType),0,0};p.display={Vec(std::exp2(s.exposure),s.grayscale?1.f:0.f,0)};
}
bool GpuRenderer::sample(uint32_t index,const std::function<bool()>& cancelled) {
    auto& d=*impl;d.parameters.dimensions.z=index;
    ID3D11ShaderResourceView* geometry[]={d.geometry[0].Get(),d.geometry[1].Get(),d.geometry[2].Get()};
    ID3D11UnorderedAccessView* outputs[]={d.sumView.Get(),d.pixelView.Get()};
    d.context->CSSetShader(d.shader.Get(),nullptr,0);d.context->CSSetShaderResources(0,3,geometry);
    d.context->CSSetUnorderedAccessViews(0,2,outputs,nullptr);ID3D11Buffer* constants=d.constants.Get();d.context->CSSetConstantBuffers(0,1,&constants);
    bool completed=true;
    for(int y=0;y<d.height;y+=64) {
        if(cancelled&&cancelled()){completed=false;break;}
        d.parameters.tile.x=uint32_t(y);d.context->UpdateSubresource(d.constants.Get(),0,nullptr,&d.parameters,0,0);
        d.context->Dispatch(UINT(d.width+7)/8,UINT(std::min(64,d.height-y)+7)/8,1);
    }
    ID3D11UnorderedAccessView* emptyOutputs[2]{};ID3D11ShaderResourceView* emptyViews[3]{};
    d.context->CSSetUnorderedAccessViews(0,2,emptyOutputs,nullptr);d.context->CSSetShaderResources(0,3,emptyViews);
    // Finish a pass before queuing the next one, bounding latency and the GPU queue.
    if(completed) {
        d.context->CopyResource(d.readPixels.Get(),d.pixels.Get());D3D11_MAPPED_SUBRESOURCE mapped{};
        check(d.context->Map(d.readPixels.Get(),0,D3D11_MAP_READ,0,&mapped),"Waiting for GPU pass");d.context->Unmap(d.readPixels.Get(),0);
    }
    check(d.device->GetDeviceRemovedReason(),"GPU device");return completed;
}
void GpuRenderer::readback(std::vector<Vec>& linear,std::vector<uint32_t>& pixels,int samples) {
    auto& d=*impl;if(samples<1)throw std::runtime_error("Invalid GPU sample count.");
    size_t count=size_t(d.width)*d.height;linear.resize(count);pixels.resize(count);
    d.context->CopyResource(d.readSum.Get(),d.sum.Get());D3D11_MAPPED_SUBRESOURCE map{};
    check(d.context->Map(d.readSum.Get(),0,D3D11_MAP_READ,0,&map),"Reading GPU radiance");
    const Float4* sum=static_cast<const Float4*>(map.pData);
    for(size_t i=0;i<count;++i)linear[i]=Vec(sum[i].x,sum[i].y,sum[i].z)/float(samples);
    d.context->Unmap(d.readSum.Get(),0);
    check(d.context->Map(d.readPixels.Get(),0,D3D11_MAP_READ,0,&map),"Reading GPU display image");
    std::copy_n(static_cast<const uint32_t*>(map.pData),count,pixels.begin());d.context->Unmap(d.readPixels.Get(),0);
}
}
#else
namespace shading {
struct GpuRenderer::Impl {std::string name,error="Direct3D GPU rendering is available on Windows.";};
GpuRenderer::GpuRenderer():impl(std::make_unique<Impl>()){}
GpuRenderer::~GpuRenderer()=default;
bool GpuRenderer::initialize(bool){return false;}
const std::string& GpuRenderer::name()const{return impl->name;}
const std::string& GpuRenderer::error()const{return impl->error;}
void GpuRenderer::reset(const Scene&,const Settings&,int,int,bool){throw std::runtime_error(impl->error);}
bool GpuRenderer::sample(uint32_t,const std::function<bool()>&){throw std::runtime_error(impl->error);}
void GpuRenderer::readback(std::vector<Vec>&,std::vector<uint32_t>&,int){throw std::runtime_error(impl->error);}
}
#endif
