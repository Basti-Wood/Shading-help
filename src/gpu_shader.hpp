#pragma once
// SPDX-License-Identifier: Apache-2.0
// Embedded HLSL keeps the executable portable; Windows compiles it locally.
namespace shading {
inline constexpr char pathShader[]=R"HLSL(
struct Triangle { float3 a; int material; float3 b; int objectId; float3 c; float pad; };
struct Sphere { float3 center; float radius; int material; int objectId; int2 pad; };
struct Node { float3 lo; int left; float3 hi; int right; int start; int count; int2 pad; };
StructuredBuffer<Triangle> Triangles : register(t0);
StructuredBuffer<Sphere> Spheres : register(t1);
StructuredBuffer<Node> Nodes : register(t2);
RWStructuredBuffer<float4> Accumulation : register(u0);
RWStructuredBuffer<uint> Pixels : register(u1);
cbuffer Parameters : register(b0) {
    float4 Eye, Right, Up, Forward;
    float4 LightPosition, LightDirection, LightEnergy, Environment;
    float4 Albedo[3];
    uint4 Dimensions; // width, height, sample index, inspection view
    uint4 Geometry;   // sphere count, BVH node count, ground, surface bounces
    uint4 Tile;       // first row, light type, unused, unused
    float4 Display;  // exposure multiplier, grayscale, unused, unused
};
static const float PI=3.14159265358979323846;
static const float EPS=0.0001;
struct Hit { float t; float3 position; float3 normal; int material; };
float3 safeUnit(float3 v) { float len=length(v); return len>1e-10?v/len:float3(0,1,0); }
float random(inout uint state) {
    state=state*747796405u+2891336453u;
    uint word=((state>>((state>>28u)+4u))^state)*277803737u;
    return float(((word>>22u)^word)>>8u)*(1.0/16777216.0);
}
void basis(float3 n,out float3 u,out float3 v) {
    u=safeUnit(cross(abs(n.y)<.99?float3(0,1,0):float3(1,0,0),n));v=cross(n,u);
}
float3 hemisphere(float3 n,inout uint rng) {
    float radius=sqrt(random(rng));float a=2*PI*random(rng);float3 u,v;basis(n,u,v);
    return safeUnit(u*(radius*cos(a))+v*(radius*sin(a))+n*sqrt(max(0,1-radius*radius)));
}
bool boundsHit(Node node,float3 origin,float3 direction,float farT) {
    float nearT=EPS;
    [unroll] for(int axis=0;axis<3;++axis) {
        if(abs(direction[axis])<1e-12) {
            if(origin[axis]<node.lo[axis]-EPS||origin[axis]>node.hi[axis]+EPS)return false;
        } else {
            float a=(node.lo[axis]-EPS-origin[axis])/direction[axis];
            float b=(node.hi[axis]+EPS-origin[axis])/direction[axis];
            nearT=max(nearT,min(a,b));farT=min(farT,max(a,b));
            if(farT<nearT)return false;
        }
    }
    return true;
}
bool triangleHit(Triangle t,float3 origin,float3 direction,inout Hit hit) {
    float3 e1=t.b-t.a,e2=t.c-t.a,p=cross(direction,e2);
    float det=dot(e1,p);if(abs(det)<1e-8)return false;
    float inv=1/det;float3 q=origin-t.a;
    float u=dot(q,p)*inv;if(u<0||u>1)return false;
    float3 k=cross(q,e1);float v=dot(direction,k)*inv;if(v<0||u+v>1)return false;
    float distance=dot(e2,k)*inv;if(distance<=EPS||distance>=hit.t)return false;
    hit.t=distance;hit.position=origin+direction*distance;hit.normal=safeUnit(cross(e1,e2));
    if(dot(hit.normal,direction)>0)hit.normal=-hit.normal;
    hit.material=t.material;return true;
}
bool sphereHit(Sphere s,float3 origin,float3 direction,inout Hit hit) {
    float3 oc=origin-s.center;float a=dot(direction,direction),b=dot(oc,direction);
    float c=dot(oc,oc)-s.radius*s.radius,discriminant=b*b-a*c;
    if(discriminant<0||a<1e-15)return false;
    float distance=(-b-sqrt(discriminant))/a;
    if(distance<=EPS)distance=(-b+sqrt(discriminant))/a;
    if(distance<=EPS||distance>=hit.t)return false;
    hit.t=distance;hit.position=origin+direction*distance;hit.normal=safeUnit(hit.position-s.center);
    if(dot(hit.normal,direction)>0)hit.normal=-hit.normal;
    hit.material=s.material;return true;
}
Hit emptyHit(float distance) { Hit h;h.t=distance;h.position=0;h.normal=0;h.material=-1;return h; }
bool intersectScene(float3 origin,float3 direction,inout Hit hit) {
    bool found=false;
    if(Geometry.z!=0&&abs(direction.y)>1e-8) {
        float distance=-origin.y/direction.y;
        if(distance>EPS&&distance<hit.t) {
            hit.t=distance;hit.position=origin+direction*distance;
            hit.normal=direction.y<0?float3(0,1,0):float3(0,-1,0);hit.material=2;found=true;
        }
    }
    [loop] for(uint s=0;s<Geometry.x;++s)if(sphereHit(Spheres[s],origin,direction,hit))found=true;
    if(Geometry.y>0) {
        int stack[64];int count=1;stack[0]=0;
        [loop] while(count>0) {
            Node n=Nodes[stack[--count]];if(!boundsHit(n,origin,direction,hit.t))continue;
            if(n.count>0) {
                [loop] for(int i=n.start;i<n.start+n.count;++i)if(triangleHit(Triangles[i],origin,direction,hit))found=true;
            } else {stack[count++]=n.left;stack[count++]=n.right;}
        }
    }
    return found;
}
float3 directLight(Hit h,inout uint rng) {
    float3 direction;float distance=1e30,weight=1;
    if(Tile.y==2)direction=-LightDirection.xyz;
    else {
        float3 position=LightPosition.xyz;
        if(Tile.y==0&&LightPosition.w>0) {
            float3 u,v;basis(LightDirection.xyz,u,v);
            float radius=LightPosition.w*sqrt(random(rng));float a=2*PI*random(rng);
            position+=u*(radius*cos(a))+v*(radius*sin(a));
        }
        float3 delta=position-h.position;distance=length(delta);if(distance<=EPS*4)return 0;
        direction=delta/distance;weight=1/(distance*distance);
        if(Tile.y==0) {
            float cosine=dot(-direction,LightDirection.xyz);
            float edge=saturate((cosine-LightDirection.w)/max(1e-6,LightEnergy.w-LightDirection.w));
            weight*=edge*edge*(3-2*edge);
            if(LightPosition.w>0)weight*=max(0,cosine);
        }
    }
    float cosine=max(0,dot(h.normal,direction));if(cosine==0||weight==0)return 0;
    Hit blocker=emptyHit(distance-EPS*6);
    if(intersectScene(h.position+h.normal*EPS*3,direction,blocker))return 0;
    return Albedo[h.material].xyz*LightEnergy.xyz*(cosine*weight/PI);
}
float3 trace(float3 origin,float3 direction,inout uint rng) {
    Hit h=emptyHit(1e30);
    if(!intersectScene(origin,direction,h)) {
        if(Dimensions.w>=3)return .04;
        return Dimensions.w==2?float3(0,0,0):Environment.xyz;
    }
    if(Dimensions.w==3)return Albedo[h.material].xyz;
    if(Dimensions.w==4)return (h.normal+1)*.5;
    float3 direct=0,indirect=0,throughput=1;
    [loop] for(uint depth=0;depth<=Geometry.w;++depth) {
        float3 d=throughput*directLight(h,rng);
        if(depth==0)direct+=d;else indirect+=d;
        throughput*=Albedo[h.material].xyz;
        origin=h.position+h.normal*EPS*3;direction=hemisphere(h.normal,rng);
        Hit next=emptyHit(1e30);
        if(!intersectScene(origin,direction,next)) {
            float3 ambient=throughput*Environment.xyz;
            if(depth==0)direct+=ambient;else indirect+=ambient;
            break;
        }
        h=next;
    }
    if(Dimensions.w==1)return direct;
    if(Dimensions.w==2)return indirect;
    return direct+indirect;
}
float srgb(float v) { return v<=.0031308?12.92*v:1.055*pow(v,1/2.4)-.055; }
[numthreads(8,8,1)]
void main(uint3 dispatch : SV_DispatchThreadID) {
    uint x=dispatch.x,y=dispatch.y+Tile.x;
    if(x>=Dimensions.x||y>=Dimensions.y)return;
    uint index=y*Dimensions.x+x;
    uint rng=(index+1)*9781u+(Dimensions.z+1)*6271u;if(rng==0)rng=1;
    float jx=random(rng),jy=random(rng);
    float u=(x+jx)/Dimensions.x,v=(y+jy)/Dimensions.y;
    float3 direction=safeUnit(Forward.xyz+Right.xyz*((2*u-1)*Eye.w*Right.w)+Up.xyz*((1-2*v)*Eye.w));
    float3 value=trace(Eye.xyz,direction,rng);
    float3 sum=value;if(Dimensions.z>0)sum+=Accumulation[index].xyz;
    Accumulation[index]=float4(sum,Dimensions.z+1);
    float3 average=sum/(Dimensions.z+1);
    if(any(isnan(average))||any(isinf(average)))average=0;
    if(Dimensions.w<3)average*=Display.x;
    if(Display.y!=0)average=dot(average,float3(.2126,.7152,.0722));
    average=saturate(average);
    uint3 rgb=(uint3)(saturate(float3(srgb(average.x),srgb(average.y),srgb(average.z)))*255+.5);
    Pixels[index]=(rgb.x<<16)|(rgb.y<<8)|rgb.z;
}
)HLSL";
}
