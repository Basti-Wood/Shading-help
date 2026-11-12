#pragma once
// SPDX-License-Identifier: Apache-2.0
#include "core.hpp"
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
namespace shading {
struct Frame {
    int width=0,height=0,samples=0,revision=0;
    std::vector<uint32_t> pixels;
    std::vector<Vec> linear;
    bool usingGpu=false;
    std::string backend="CPU",device;
};
class Progressive {
public:
    Progressive();
    ~Progressive();
    Progressive(const Progressive&)=delete;
    Progressive& operator=(const Progressive&)=delete;
    int request(Settings s,int width,int height,int limit,
                std::shared_ptr<const std::vector<Triangle>> imported,bool preferGpu=true);
    bool frame(Frame& out,int afterSamples,int revision);
private:
    struct Job {Settings settings;int w=1,h=1,limit=256,revision=0;std::shared_ptr<const std::vector<Triangle>> imported;bool preferGpu=true;};
    std::mutex mutex;
    std::condition_variable ready;
    std::atomic<bool> stop{false};
    std::atomic<int> revision{0};
    Job pending;
    Frame output;
    std::thread worker;
    void run();
};
}
