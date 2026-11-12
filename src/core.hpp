#pragma once
// SPDX-License-Identifier: Apache-2.0
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

namespace shading {
constexpr float pi = 3.14159265358979323846f;
constexpr float epsilon = 0.0001f;
constexpr float infinity = std::numeric_limits<float>::infinity();
struct Vec {
    float x=0, y=0, z=0;
    Vec() = default;
    explicit Vec(float s):x(s),y(s),z(s){}
    Vec(float a,float b,float c):x(a),y(b),z(c){}
    float operator[](int i) const { return i==0?x:i==1?y:z; }
    Vec operator-() const { return {-x,-y,-z}; }
    Vec operator+(Vec b) const { return {x+b.x,y+b.y,z+b.z}; }
    Vec operator-(Vec b) const { return {x-b.x,y-b.y,z-b.z}; }
    Vec operator*(Vec b) const { return {x*b.x,y*b.y,z*b.z}; }
    Vec operator*(float s) const { return {x*s,y*s,z*s}; }
    Vec operator/(float s) const { return *this*(1/s); }
    Vec& operator+=(Vec b) { *this=*this+b; return *this; }
};
inline Vec operator*(float s,Vec a) { return a*s; }
inline float dot(Vec a,Vec b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
inline Vec cross(Vec a,Vec b) { return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; }
inline float length(Vec a) { return std::sqrt(dot(a,a)); }
inline Vec unit(Vec a) { float l=length(a); return l>1e-10f?a/l:Vec(0,1,0); }
inline Vec clamp(Vec a,float lo=0,float hi=1) { return {std::clamp(a.x,lo,hi),std::clamp(a.y,lo,hi),std::clamp(a.z,lo,hi)}; }
inline bool finite(Vec a) { return std::isfinite(a.x)&&std::isfinite(a.y)&&std::isfinite(a.z); }
inline float radians(float d) { return d*pi/180; }
inline float luminance(Vec v) { return .2126f*v.x+.7152f*v.y+.0722f*v.z; }
Vec rotate(Vec v,Vec degrees);
Vec fromSrgb(Vec rgb);
Vec toSrgb(Vec linear);
struct Ray { Vec origin,direction; Vec at(float t) const { return origin+direction*t; } };
struct Rng {
    uint32_t state;
    explicit Rng(uint32_t seed):state(seed?seed:1){}
    float next();
};
Vec cosineHemisphere(Vec normal,Rng& rng);
struct Bounds {
    Vec lo{infinity},hi{-infinity};
    void add(Vec p);
    void add(const Bounds& b) { add(b.lo); add(b.hi); }
    bool intersects(const Ray& ray,float maxT) const;
};
struct Triangle { Vec a,b,c; int material=0,object=0; };
struct Sphere { Vec center; float radius=1; int material=0,object=0; };
struct Hit { float t=infinity; Vec position,normal; int material=-1,object=-1; };
bool hitTriangle(const Triangle& t,const Ray& r,Hit& h);
bool hitSphere(const Sphere& s,const Ray& r,Hit& h);
struct BvhNode { Bounds bounds; int start=0,count=0,left=-1,right=-1; };
enum class Model { Head, Ball, Both, Mannequin, Imported };
enum class LightType { Spot, Point, Directional };
enum class View { Beauty, Direct, Indirect, Albedo, Normals };
struct Settings {
    Model model=Model::Head;
    LightType lightType=LightType::Spot;
    View view=View::Beauty;
    Vec surface[2]={{.72f,.48f,.34f},{.72f,.48f,.34f}}; // UI sRGB, decoded for transport
    Vec position[2]={{0,0,0},{2.25f,0,0}};
    Vec rotation[2]={{0,0,0},{0,0,0}};
    Vec floorColor{.47f,.49f,.52f};
    Vec lightColor{1,.91f,.78f}, fillColor{.57f,.69f,1};
    Vec lightPosition{-3,4.5f,3.5f},lightDirection{.55f,-.55f,-.63f};
    float intensity=75,lightRadius=.30f,coneDegrees=55,fill=.10f,exposure=0;
    int bounces=2;
    bool ground=true,grayscale=false;
    Vec cameraTarget{0,1.4f,0};
    float cameraYaw=18,cameraPitch=10,cameraRoll=0,cameraDistance=7.4f,fov=38;
    std::array<float,8> jointPitch{};
    std::array<float,8> jointSpread{};
    std::string objPath;
    void validate();
};
struct Camera {
    Vec eye,right,up,forward;
    float tangent=1,aspect=1;
    Camera(const Settings& s,float a);
    Ray ray(float u,float v) const;
    bool project(Vec world,float& u,float& v) const;
};
struct Scene {
    std::vector<Triangle> triangles;
    std::vector<Sphere> spheres;
    std::vector<BvhNode> nodes;
    std::array<Vec,3> albedo;
    bool ground=true;
    Scene() { albedo.fill(Vec(.5f)); }
    void build();
    bool intersect(const Ray& r,Hit& hit) const;
    bool occluded(Vec origin,Vec direction,float distance) const;
private:
    int buildNode(int begin,int end);
};
Scene makeScene(const Settings& settings,const std::vector<Triangle>& imported={});
bool sameGeometry(const Settings& a,const Settings& b);
std::vector<Triangle> loadObj(const std::filesystem::path& path);
std::vector<Triangle> loadMesh(const std::filesystem::path& path);
void normalizeMesh(std::vector<Triangle>& triangles);
struct Contributions { Vec direct,indirect; };
Vec directLighting(const Scene& scene,const Settings& s,const Hit& h,Rng& rng);
Contributions trace(const Scene& scene,const Settings& s,Ray ray,Rng& rng);
Vec samplePixel(const Scene& scene,const Settings& settings,const Camera& camera,
                int x,int y,int width,int height,uint32_t sample);
uint32_t displayPixel(Vec radiance,const Settings& s);
std::vector<uint32_t> render(const Settings& s,int w,int h,int samples,
                             const std::vector<Triangle>& imported={});
void saveBmp(const std::filesystem::path& file,const std::vector<uint32_t>& pixels,int w,int h);
void saveSettings(const std::filesystem::path& file,const Settings& s);
Settings loadSettings(const std::filesystem::path& file);
} // namespace shading
