#pragma once
// SPDX-License-Identifier: Apache-2.0
#include "core.hpp"
#include <functional>
#include <memory>
namespace shading {
// All methods are called on one render thread. WARP is exposed only for tests.
class GpuRenderer {
public:
    GpuRenderer();
    ~GpuRenderer();
    GpuRenderer(const GpuRenderer&)=delete;
    GpuRenderer& operator=(const GpuRenderer&)=delete;
    bool initialize(bool softwareForTests=false);
    const std::string& name() const;
    const std::string& error() const;
    void reset(const Scene& scene,const Settings& settings,int width,int height,bool geometryChanged=true);
    bool sample(uint32_t index,const std::function<bool()>& cancelled={});
    void readback(std::vector<Vec>& linear,std::vector<uint32_t>& pixels,int samples);
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
