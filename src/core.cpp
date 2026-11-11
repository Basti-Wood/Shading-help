// SPDX-License-Identifier: Apache-2.0
#include "core.hpp"
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace shading {
Vec rotate(Vec v,Vec d) {
    float x=radians(d.x),y=radians(d.y),z=radians(d.z);
    v={v.x,std::cos(x)*v.y-std::sin(x)*v.z,std::sin(x)*v.y+std::cos(x)*v.z};
    v={std::cos(y)*v.x+std::sin(y)*v.z,v.y,-std::sin(y)*v.x+std::cos(y)*v.z};
    return {std::cos(z)*v.x-std::sin(z)*v.y,std::sin(z)*v.x+std::cos(z)*v.y,v.z};
}
static float decode(float x) { return x<=.04045f?x/12.92f:std::pow((x+.055f)/1.055f,2.4f); }
static float encode(float x) { return x<=.0031308f?12.92f*x:1.055f*std::pow(x,1/2.4f)-.055f; }
Vec fromSrgb(Vec v) { return {decode(v.x),decode(v.y),decode(v.z)}; }
Vec toSrgb(Vec v) { return {encode(v.x),encode(v.y),encode(v.z)}; }
float Rng::next() {
    state=state*747796405u+2891336453u;
    uint32_t word=((state>>((state>>28u)+4u))^state)*277803737u;
    return float(((word>>22u)^word)>>8u)*(1.0f/16777216.0f);
}
static void basis(Vec n,Vec& u,Vec& v) {
    u=unit(cross(std::abs(n.y)<.99f?Vec(0,1,0):Vec(1,0,0),n)); v=cross(n,u);
}
Vec cosineHemisphere(Vec n,Rng& rng) {
    float r=std::sqrt(rng.next()),a=2*pi*rng.next(); Vec u,v; basis(n,u,v);
    return unit(u*(r*std::cos(a))+v*(r*std::sin(a))+n*std::sqrt(std::max(0.f,1-r*r)));
}
void Bounds::add(Vec p) {
    lo={std::min(lo.x,p.x),std::min(lo.y,p.y),std::min(lo.z,p.z)};
    hi={std::max(hi.x,p.x),std::max(hi.y,p.y),std::max(hi.z,p.z)};
}
bool Bounds::intersects(const Ray& r,float far) const {
    float near=epsilon;
    for(int a=0;a<3;++a) {
        if(std::abs(r.direction[a])<1e-12f) {
            if(r.origin[a]<lo[a]-epsilon||r.origin[a]>hi[a]+epsilon) return false;
            continue;
        }
        float t0=(lo[a]-epsilon-r.origin[a])/r.direction[a],t1=(hi[a]+epsilon-r.origin[a])/r.direction[a];
        if(t0>t1) std::swap(t0,t1);
        near=std::max(near,t0); far=std::min(far,t1);
        if(far<near) return false;
    }
    return true;
}
bool hitTriangle(const Triangle& t,const Ray& r,Hit& h) {
    Vec e1=t.b-t.a,e2=t.c-t.a,p=cross(r.direction,e2);
    float det=dot(e1,p); if(std::abs(det)<1e-8f) return false;
    float inv=1/det; Vec q=r.origin-t.a;
    float u=dot(q,p)*inv; if(u<0||u>1) return false;
    Vec k=cross(q,e1); float v=dot(r.direction,k)*inv;
    if(v<0||u+v>1) return false;
    float d=dot(e2,k)*inv; if(d<=epsilon||d>=h.t) return false;
    h.t=d; h.position=r.at(d); h.normal=unit(cross(e1,e2));
    if(dot(h.normal,r.direction)>0) h.normal=-h.normal;
    h.material=t.material; h.object=t.object; return true;
}
bool hitSphere(const Sphere& s,const Ray& r,Hit& h) {
    Vec oc=r.origin-s.center; float a=dot(r.direction,r.direction),halfB=dot(oc,r.direction);
    float c=dot(oc,oc)-s.radius*s.radius,disc=halfB*halfB-a*c;
    if(disc<0||a<1e-15f) return false;
    float d=(-halfB-std::sqrt(disc))/a;
    if(d<=epsilon) d=(-halfB+std::sqrt(disc))/a;
    if(d<=epsilon||d>=h.t) return false;
    h.t=d;h.position=r.at(d);h.normal=unit(h.position-s.center);
    if(dot(h.normal,r.direction)>0) h.normal=-h.normal;
    h.material=s.material;h.object=s.object;return true;
}
int Scene::buildNode(int begin,int end) {
    int index=int(nodes.size()); nodes.emplace_back(); Bounds b,centers;
    for(int i=begin;i<end;++i) {
        auto& t=triangles[i]; b.add(t.a);b.add(t.b);b.add(t.c);centers.add((t.a+t.b+t.c)/3);
    }
    nodes[index].bounds=b;
    if(end-begin<=6) {nodes[index].start=begin;nodes[index].count=end-begin;return index;}
    Vec e=centers.hi-centers.lo;int axis=e.x>e.y?(e.x>e.z?0:2):(e.y>e.z?1:2);
    int mid=begin+(end-begin)/2;
    std::nth_element(triangles.begin()+begin,triangles.begin()+mid,triangles.begin()+end,
        [axis](const Triangle& a,const Triangle& c){return (a.a+a.b+a.c)[axis]<(c.a+c.b+c.c)[axis];});
    int left=buildNode(begin,mid),right=buildNode(mid,end);
    nodes[index].left=left;nodes[index].right=right;return index;
}
void Scene::build() {nodes.clear();if(!triangles.empty())buildNode(0,int(triangles.size()));}
bool Scene::intersect(const Ray& r,Hit& h) const {
    bool found=false;
    if(ground&&std::abs(r.direction.y)>1e-8f) {
        float t=-r.origin.y/r.direction.y;
        if(t>epsilon&&t<h.t) {h={t,r.at(t),r.direction.y<0?Vec(0,1,0):Vec(0,-1,0),2,-1};found=true;}
    }
    for(const auto& s:spheres) found=hitSphere(s,r,h)||found;
    if(!nodes.empty()) {
        int stack[64],count=0;stack[count++]=0;
        while(count) {
            const auto& n=nodes[stack[--count]];
            if(!n.bounds.intersects(r,h.t)) continue;
            if(n.count) {for(int i=n.start;i<n.start+n.count;++i)found=hitTriangle(triangles[i],r,h)||found;}
            else {stack[count++]=n.left;stack[count++]=n.right;}
        }
    }
    return found;
}
bool Scene::occluded(Vec o,Vec d,float distance) const {Hit h;h.t=distance;return intersect({o,d},h);}

void Settings::validate() {
    auto val=[](float& v,float lo,float hi,float fallback) {v=std::isfinite(v)?std::clamp(v,lo,hi):fallback;};
    auto vec=[](Vec& v,float lo,float hi,Vec fallback) {v=finite(v)?clamp(v,lo,hi):fallback;};
    if(int(model)<0||int(model)>4)model=Model::Head;
    if(int(lightType)<0||int(lightType)>2)lightType=LightType::Spot;
    if(int(view)<0||int(view)>4)view=View::Beauty;
    for(int i=0;i<2;++i){vec(surface[i],0,1,Vec(.7f));vec(position[i],-50,50,Vec());vec(rotation[i],-360,360,Vec());}
    vec(floorColor,0,1,Vec(.5f));vec(lightColor,0,1,Vec(1));vec(fillColor,0,1,Vec(.6f));
    vec(lightPosition,-50,50,Vec(-3,4,3));
    if(!finite(lightDirection)||length(lightDirection)<1e-5f)lightDirection={0,-1,0};
    lightDirection=unit(lightDirection);
    val(intensity,0,1000,75);val(lightRadius,0,3,.3f);val(coneDegrees,1,89,55);
    val(fill,0,3,.1f);val(exposure,-8,8,0);bounces=std::clamp(bounces,0,8);
    vec(cameraTarget,-100,100,Vec(0,1.4f,0));
    val(cameraYaw,-100000,100000,18);val(cameraPitch,-89,89,10);val(cameraRoll,-360,360,0);
    val(cameraDistance,.5f,100,7.4f);val(fov,10,100,38);
    for(float& p:jointPitch)val(p,-160,160,0);
    for(float& p:jointSpread)val(p,-100,100,0);
}
Camera::Camera(const Settings& s,float a) {
    aspect=a;tangent=std::tan(radians(s.fov)*.5f);
    float y=radians(s.cameraYaw),p=radians(s.cameraPitch);
    eye=s.cameraTarget+Vec(std::sin(y)*std::cos(p),std::sin(p),std::cos(y)*std::cos(p))*s.cameraDistance;
    forward=unit(s.cameraTarget-eye);right=unit(cross(forward,Vec(0,1,0)));up=cross(right,forward);
    float c=std::cos(radians(s.cameraRoll)),n=std::sin(radians(s.cameraRoll));Vec old=right;
    right=right*c+up*n;up=up*c-old*n;
}
Ray Camera::ray(float u,float v) const {return {eye,unit(forward+right*((2*u-1)*tangent*aspect)+up*((1-2*v)*tangent))};}
bool Camera::project(Vec p,float& u,float& v) const {
    Vec d=p-eye;float z=dot(d,forward);if(z<=epsilon)return false;
    u=.5f+.5f*dot(d,right)/(z*tangent*aspect);v=.5f-.5f*dot(d,up)/(z*tangent);return true;
}

static void tri(Scene& s,Vec a,Vec b,Vec c,int mat=0,int obj=0) {
    if(length(cross(b-a,c-a))>1e-7f)s.triangles.push_back({a,b,c,mat,obj});
}
static void quad(Scene& s,Vec a,Vec b,Vec c,Vec d,int mat=0,int obj=0) {tri(s,a,b,c,mat,obj);tri(s,a,c,d,mat,obj);}
static void ellipsoid(Scene& s,Vec center,Vec radius,int segments=10,int rings=6) {
    for(int j=0;j<rings;++j) for(int i=0;i<segments;++i) {
        auto p=[&](int x,int y){float a=2*pi*x/segments,b=pi*y/rings;return center+radius*Vec(std::sin(b)*std::cos(a),std::cos(b),std::sin(b)*std::sin(a));};
        quad(s,p(i,j),p(i+1,j),p(i+1,j+1),p(i,j+1));
    }
}
static void limb(Scene& s,Vec a,Vec b,float ra,float rb,int segments=10) {
    Vec u,v;basis(unit(b-a),u,v);
    for(int i=0;i<segments;++i) {
        float x=2*pi*i/segments,y=2*pi*(i+1)/segments;
        Vec da=u*std::cos(x)+v*std::sin(x),db=u*std::cos(y)+v*std::sin(y);
        quad(s,a+da*ra,b+da*rb,b+db*rb,a+db*ra);tri(s,a,a+da*ra,a+db*ra);tri(s,b,b+db*rb,b+da*rb);
    }
}
// Original, symmetric planar study bust. Facial planes are modeled geometry,
// including recessed eye sockets, brow ridge, nose wings, lips and jaw.
// This is NOT a scan or reproduction of John Asaro's proprietary sculpture.
static void planarHead(Scene& s) {
    struct Row {float y,width;std::array<float,6> z;};
    const Row rows[]={
        {2.94f,.22f,{.06f,.05f,.02f,-.02f,-.10f,-.18f}},
        {2.73f,.60f,{.43f,.43f,.40f,.31f,.14f,-.08f}},
        {2.38f,.73f,{.58f,.58f,.55f,.47f,.23f,-.08f}},
        {2.26f,.74f,{.66f,.58f,.50f,.47f,.25f,-.10f}},
        {2.12f,.72f,{.78f,.52f,.30f,.29f,.23f,-.10f}},
        {1.98f,.73f,{.93f,.65f,.44f,.48f,.30f,-.08f}},
        {1.86f,.68f,{1.00f,.79f,.50f,.43f,.24f,-.12f}},
        {1.78f,.64f,{.66f,.60f,.49f,.38f,.19f,-.17f}},
        {1.65f,.58f,{.72f,.69f,.56f,.35f,.16f,-.22f}},
        {1.59f,.56f,{.63f,.63f,.53f,.32f,.12f,-.24f}},
        {1.52f,.54f,{.70f,.66f,.50f,.28f,.08f,-.25f}},
        {1.35f,.47f,{.67f,.64f,.53f,.31f,.07f,-.27f}},
        {1.20f,.32f,{.43f,.43f,.35f,.20f,-.02f,-.26f}}
    };
    constexpr float fractions[]={0,.22f,.43f,.68f,.88f,1};
    constexpr int count=int(sizeof(rows)/sizeof(rows[0]));
    Vec front[count][11];
    for(int j=0;j<count;++j)for(int i=0;i<11;++i) {
        int k=std::abs(i-5);float sign=i<5?-1.f:1.f;
        float y=rows[j].y;
        if(j==2&&k>=2&&k<=4)y+=.04f;
        if(j==4&&k>=2&&k<=3)y+=.015f;
        front[j][i]={sign*fractions[k]*rows[j].width,y,rows[j].z[k]};
    }
    for(int j=0;j<count-1;++j)for(int i=0;i<10;++i)
        quad(s,front[j][i],front[j+1][i],front[j+1][i+1],front[j][i+1]);
    // Seven-sided rear half, connected to the face at the temples / jaw.
    Vec rear[count][9];
    for(int j=0;j<count;++j)for(int k=0;k<=8;++k) {
        float a=pi*k/8;float depth=j<3?.76f:j<9?.70f:.46f;
        rear[j][k]={rows[j].width*std::cos(a),rows[j].y,rows[j].z[5]-depth*std::sin(a)};
    }
    for(int j=0;j<count-1;++j)for(int k=0;k<8;++k)
        quad(s,rear[j][k],rear[j+1][k],rear[j+1][k+1],rear[j][k+1]);
    // Closed top and lower face; the neck overlaps the sealed bottom.
    for(int cap:{0,count-1}) {
        Vec c{0,rows[cap].y,-.06f};
        for(int i=0;i<10;++i)tri(s,c,front[cap][i],front[cap][i+1]);
        for(int k=0;k<8;++k)tri(s,c,rear[cap][k],rear[cap][k+1]);
    }
    ellipsoid(s,{-.76f,2.02f,-.12f},{.13f,.26f,.16f},6,4);
    ellipsoid(s,{ .76f,2.02f,-.12f},{.13f,.26f,.16f},6,4);
    limb(s,{0,.33f,-.14f},{0,1.38f,-.18f},.40f,.26f,10);
    ellipsoid(s,{0,.25f,-.14f},{.87f,.27f,.52f},10,4);
}
static void mannequin(Scene& s,const Settings& p) {
    ellipsoid(s,{0,2.07f,0},{.43f,.55f,.23f},10,6);
    ellipsoid(s,{0,1.48f,0},{.33f,.29f,.23f},8,4);
    limb(s,{0,2.43f,0},{0,2.72f,0},.13f,.13f,8);
    ellipsoid(s,{0,2.95f,0},{.24f,.33f,.24f},10,6);
    for(int side=0;side<2;++side) {
        float sign=side?-1.f:1.f;int arm=side*2,leg=4+side*2;
        Vec shoulder{sign*.43f,2.35f,0};
        Vec ar{p.jointPitch[arm],0,sign*(15+p.jointSpread[arm])};
        Vec elbow=shoulder+rotate({0,-.52f,0},ar);
        Vec wrist=elbow+rotate(rotate({0,-.49f,0},{p.jointPitch[arm+1],0,p.jointSpread[arm+1]}),ar);
        Vec hip{sign*.22f,1.43f,0};Vec lr{p.jointPitch[leg],0,sign*(3+p.jointSpread[leg])};
        Vec knee=hip+rotate({0,-.64f,0},lr);
        Vec ankle=knee+rotate(rotate({0,-.64f,0},{p.jointPitch[leg+1],0,p.jointSpread[leg+1]}),lr);
        limb(s,shoulder,elbow,.13f,.095f);limb(s,elbow,wrist,.10f,.065f);
        limb(s,hip,knee,.18f,.12f);limb(s,knee,ankle,.13f,.07f);
        for(Vec j:{shoulder,elbow,hip,knee})s.spheres.push_back({j,.105f,0,0});
        ellipsoid(s,wrist+unit(wrist-elbow)*.10f,{.09f,.15f,.055f},8,4);
        ellipsoid(s,ankle+Vec(0,-.025f,.10f),{.11f,.08f,.22f},8,4);
    }
}
Scene makeScene(const Settings& s,const std::vector<Triangle>& imported) {
    Scene scene;scene.ground=s.ground;
    scene.albedo={fromSrgb(s.surface[0]),fromSrgb(s.surface[1]),fromSrgb(s.floorColor)};
    if(s.model==Model::Head||s.model==Model::Both)planarHead(scene);
    if(s.model==Model::Mannequin)mannequin(scene,s);
    if(s.model==Model::Imported) {scene.triangles=imported;for(auto& t:scene.triangles){t.material=0;t.object=0;}}
    Vec pivot{0,1.4f,0};
    for(auto& t:scene.triangles)for(Vec* p:{&t.a,&t.b,&t.c})*p=rotate(*p-pivot,s.rotation[0])+pivot+s.position[0];
    for(auto& b:scene.spheres)b.center=rotate(b.center-pivot,s.rotation[0])+pivot+s.position[0];
    if(s.model==Model::Ball||s.model==Model::Both) {
        int id=s.model==Model::Both?1:0;scene.spheres.push_back({Vec(0,1,0)+s.position[id],1,id,id});
    }
    scene.build();return scene;
}

std::vector<Triangle> loadObj(const std::filesystem::path& path) {
    std::ifstream in(path);if(!in)throw std::runtime_error("Cannot open OBJ file.");
    std::vector<Vec> vertices;std::vector<Triangle> triangles;std::string line;Bounds bounds;
    size_t bytes=0;
    while(std::getline(in,line)) {
        bytes+=line.size();if(bytes>64*1024*1024)throw std::runtime_error("OBJ exceeds the 64 MB limit.");
        std::istringstream stream(line);std::string type;stream>>type;
        if(type=="v") {
            Vec v;if(!(stream>>v.x>>v.y>>v.z)||!finite(v))throw std::runtime_error("Invalid OBJ vertex.");
            if(length(v)>1e10f)throw std::runtime_error("OBJ coordinates are too large.");
            vertices.push_back(v);bounds.add(v);
            if(vertices.size()>300000)throw std::runtime_error("OBJ exceeds 300,000 vertices.");
        } else if(type=="f") {
            std::vector<int> indices;std::string part;
            while(stream>>part) {
                if(part[0]=='#')break;
                std::string number=part.substr(0,part.find('/'));size_t used=0;int i;
                try{i=std::stoi(number,&used);}catch(...){throw std::runtime_error("Invalid OBJ face index.");}
                if(used!=number.size()||i==0)throw std::runtime_error("Invalid OBJ face index.");
                i=i>0?i-1:int(vertices.size())+i;
                if(i<0||size_t(i)>=vertices.size())throw std::runtime_error("OBJ face index is out of bounds.");
                indices.push_back(i);if(indices.size()>512)throw std::runtime_error("OBJ face is too large; triangulate it first.");
            }
            if(indices.size()<3)throw std::runtime_error("OBJ face has fewer than three vertices.");
            for(size_t i=1;i+1<indices.size();++i) {
                Triangle t{vertices[indices[0]],vertices[indices[i]],vertices[indices[i+1]],0,0};
                if(length(cross(t.b-t.a,t.c-t.a))>1e-10f)triangles.push_back(t);
                if(triangles.size()>200000)throw std::runtime_error("OBJ exceeds 200,000 triangles.");
            }
        }
    }
    if(triangles.empty()||bounds.hi.y-bounds.lo.y<1e-7f)throw std::runtime_error("OBJ contains no usable upright geometry.");
    float scale=2.8f/(bounds.hi.y-bounds.lo.y);Vec center=(bounds.lo+bounds.hi)*.5f;center.y=bounds.lo.y;
    for(auto& t:triangles)for(Vec* v:{&t.a,&t.b,&t.c})*v=(*v-center)*scale;
    return triangles;
}

Vec directLighting(const Scene& scene,const Settings& s,const Hit& h,Rng& rng) {
    Vec direction,light=fromSrgb(s.lightColor)*s.intensity;float distance=infinity,weight=1;
    if(s.lightType==LightType::Directional) {direction=-unit(s.lightDirection);}
    else {
        Vec position=s.lightPosition;
        if(s.lightType==LightType::Spot&&s.lightRadius>0) {
            Vec u,v;basis(unit(s.lightDirection),u,v);
            float r=s.lightRadius*std::sqrt(rng.next()),a=2*pi*rng.next();
            position+=u*(r*std::cos(a))+v*(r*std::sin(a));
        }
        Vec delta=position-h.position;distance=length(delta);
        if(distance<=epsilon*4)return Vec();
        direction=delta/distance;weight=1/(distance*distance);
        if(s.lightType==LightType::Spot) {
            float c=dot(-direction,unit(s.lightDirection));
            float outer=std::cos(radians(s.coneDegrees)),inner=std::cos(radians(s.coneDegrees*.8f));
            float edge=std::clamp((c-outer)/std::max(1e-6f,inner-outer),0.f,1.f);
            weight*=edge*edge*(3-2*edge);
            // Uniform disk sampling: Le*A = intensity. Include emitter cosine.
            if(s.lightRadius>0)weight*=std::max(0.f,c);
        }
    }
    float cosine=std::max(0.f,dot(h.normal,direction));
    if(cosine==0||weight==0)return Vec();
    Vec origin=h.position+h.normal*epsilon*3;
    if(scene.occluded(origin,direction,distance-epsilon*6))return Vec();
    return scene.albedo[h.material]*light*(cosine*weight/pi);
}
Contributions trace(const Scene& scene,const Settings& s,Ray ray,Rng& rng) {
    Contributions out;Vec throughput(1);Hit h;
    if(!scene.intersect(ray,h)) {out.direct=fromSrgb(s.fillColor)*s.fill;return out;}
    for(int depth=0;depth<=s.bounces;++depth) {
        Vec d=throughput*directLighting(scene,s,h,rng);
        if(depth==0)out.direct+=d;else out.indirect+=d;
        // Lambert BRDF rho/pi with cosine PDF cos(theta)/pi => throughput *= rho.
        throughput=throughput*scene.albedo[h.material];
        ray={h.position+h.normal*epsilon*3,cosineHemisphere(h.normal,rng)};
        Hit next;
        if(!scene.intersect(ray,next)) {
            Vec environment=throughput*fromSrgb(s.fillColor)*s.fill;
            if(depth==0)out.direct+=environment;else out.indirect+=environment;
            break;
        }
        h=next;
    }
    return out;
}
Vec samplePixel(const Scene& scene,const Settings& s,const Camera& camera,int x,int y,int w,int h,uint32_t sample) {
    uint32_t hash=(uint32_t(y*w+x)+1)*9781u+(sample+1)*6271u;
    Rng rng(hash);Ray ray=camera.ray((x+rng.next())/w,(y+rng.next())/h);
    if(s.view==View::Albedo||s.view==View::Normals) {
        Hit hit;if(!scene.intersect(ray,hit))return Vec(.04f);
        return s.view==View::Albedo?scene.albedo[hit.material]:(hit.normal+Vec(1))*.5f;
    }
    auto c=trace(scene,s,ray,rng);
    if(s.view==View::Direct)return c.direct;
    if(s.view==View::Indirect)return c.indirect;
    return c.direct+c.indirect;
}
uint32_t displayPixel(Vec value,const Settings& s) {
    if(!finite(value))value=Vec();
    if(s.view!=View::Albedo&&s.view!=View::Normals)value=value*std::exp2(s.exposure);
    if(s.grayscale)value=Vec(luminance(value));
    // No hidden contrast curve. Linear exposure -> sRGB transfer -> display clamp.
    Vec rgb=clamp(toSrgb(clamp(value,0,1)));
    return (uint32_t(rgb.x*255+.5f)<<16)|(uint32_t(rgb.y*255+.5f)<<8)|uint32_t(rgb.z*255+.5f);
}
std::vector<uint32_t> render(const Settings& original,int w,int h,int samples,const std::vector<Triangle>& imported) {
    if(w<1||h<1||w>8192||h>8192||samples<1||samples>65536)throw std::runtime_error("Invalid render dimensions or sample count.");
    Settings s=original;s.validate();Scene scene=makeScene(s,imported);Camera camera(s,float(w)/h);
    std::vector<uint32_t> pixels(size_t(w)*h);std::atomic<int> row{0};
    unsigned count=std::min(16u,std::max(1u,std::thread::hardware_concurrency()));std::vector<std::thread> workers;
    for(unsigned i=0;i<count;++i)workers.emplace_back([&]{
        for(int y;(y=row.fetch_add(1))<h;)for(int x=0;x<w;++x) {
            Vec v;for(int n=0;n<samples;++n)v+=samplePixel(scene,s,camera,x,y,w,h,uint32_t(n));
            pixels[size_t(y)*w+x]=displayPixel(v/float(samples),s);
        }
    });
    for(auto& t:workers)t.join();
    return pixels;
}
void saveBmp(const std::filesystem::path& file,const std::vector<uint32_t>& p,int w,int h) {
    if(w<1||h<1||p.size()!=size_t(w)*h)throw std::runtime_error("Invalid bitmap size.");
    std::ofstream out(file,std::ios::binary);if(!out)throw std::runtime_error("Cannot write image.");
    auto u16=[&](uint16_t v){out.put(char(v));out.put(char(v>>8));};
    auto u32=[&](uint32_t v){for(int i=0;i<4;++i)out.put(char(v>>(8*i)));};
    out.put('B');out.put('M');u32(54+uint32_t(w*h)*4);u32(0);u32(54);u32(40);
    u32(uint32_t(w));u32(uint32_t(-h));u16(1);u16(32);u32(0);u32(uint32_t(w*h)*4);u32(2835);u32(2835);u32(0);u32(0);
    for(uint32_t v:p)u32(v);
    if(!out)throw std::runtime_error("Image write failed.");
}
static void writeVec(std::ostream& out,Vec v) {out<<v.x<<' '<<v.y<<' '<<v.z<<'\n';}
static void readVec(std::istream& in,Vec& v) {in>>v.x>>v.y>>v.z;}
void saveSettings(const std::filesystem::path& file,const Settings& s) {
    std::ofstream out(file);if(!out)throw std::runtime_error("Cannot save scene.");
    out<<std::setprecision(9)<<"SHADING_HELP 1\n"<<int(s.model)<<' '<<int(s.lightType)<<' '<<int(s.view)<<'\n';
    for(int i=0;i<2;++i){writeVec(out,s.surface[i]);writeVec(out,s.position[i]);writeVec(out,s.rotation[i]);}
    for(Vec v:{s.floorColor,s.lightColor,s.fillColor,s.lightPosition,s.lightDirection,s.cameraTarget})writeVec(out,v);
    out<<s.intensity<<' '<<s.lightRadius<<' '<<s.coneDegrees<<' '<<s.fill<<' '<<s.exposure<<' '<<s.bounces<<' '<<s.ground<<' '<<s.grayscale<<'\n';
    out<<s.cameraYaw<<' '<<s.cameraPitch<<' '<<s.cameraRoll<<' '<<s.cameraDistance<<' '<<s.fov<<'\n';
    for(float a:s.jointPitch)out<<a<<' ';
    out<<'\n';
    for(float a:s.jointSpread)out<<a<<' ';
    out<<'\n'<<std::quoted(s.objPath)<<'\n';
    if(!out)throw std::runtime_error("Scene write failed.");
}
Settings loadSettings(const std::filesystem::path& file) {
    std::ifstream in(file);std::string magic;int version=0,m=0,l=0,v=0;Settings s;
    if(!(in>>magic>>version)||magic!="SHADING_HELP"||version!=1)throw std::runtime_error("Not a supported Shading Help scene.");
    in>>m>>l>>v;s.model=Model(m);s.lightType=LightType(l);s.view=View(v);
    for(int i=0;i<2;++i){readVec(in,s.surface[i]);readVec(in,s.position[i]);readVec(in,s.rotation[i]);}
    for(Vec* p:{&s.floorColor,&s.lightColor,&s.fillColor,&s.lightPosition,&s.lightDirection,&s.cameraTarget})readVec(in,*p);
    in>>s.intensity>>s.lightRadius>>s.coneDegrees>>s.fill>>s.exposure>>s.bounces>>s.ground>>s.grayscale;
    in>>s.cameraYaw>>s.cameraPitch>>s.cameraRoll>>s.cameraDistance>>s.fov;
    for(float& a:s.jointPitch)in>>a;
    for(float& a:s.jointSpread)in>>a;
    in>>std::quoted(s.objPath);
    if(!in)throw std::runtime_error("Scene file is incomplete or contains invalid values.");
    s.validate();return s;
}
} // namespace shading
