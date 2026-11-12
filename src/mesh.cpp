// SPDX-License-Identifier: Apache-2.0
#include "core.hpp"
#include <cctype>
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace shading {
namespace {
constexpr size_t maxTriangles=200000,maxVertices=300000,maxBytes=64*1024*1024;
void vertexCheck(Vec v) {
    if(!finite(v)||std::abs(v.x)>1e10f||std::abs(v.y)>1e10f||std::abs(v.z)>1e10f)
        throw std::runtime_error("Model has invalid or excessively large vertex coordinates.");
}
void addTriangle(std::vector<Triangle>& out,Vec a,Vec b,Vec c) {
    vertexCheck(a);vertexCheck(b);vertexCheck(c);
    if(length(cross(b-a,c-a))>1e-10f)out.push_back({a,b,c,0,0});
    if(out.size()>maxTriangles)throw std::runtime_error("Model exceeds 200,000 triangles. Decimate it before export.");
}
uint64_t bits(const std::string& data,size_t& offset,int count,bool big=false) {
    if(offset>data.size()||data.size()-offset<size_t(count))throw std::runtime_error("Truncated binary model.");
    uint64_t result=0;
    for(int i=0;i<count;++i)result|=uint64_t(uint8_t(data[offset++]))<<(8*(big?count-1-i:i));
    return result;
}
float float32(const std::string& data,size_t& offset) {
    uint32_t word=uint32_t(bits(data,offset,4));float f;std::memcpy(&f,&word,4);return f;
}
std::vector<Triangle> stl(const std::string& data) {
    std::vector<Triangle> out;
    if(data.size()>=84) {
        size_t p=80;uint64_t n=bits(data,p,4);
        if(84+n*50==data.size()) {
            if(n>maxTriangles)throw std::runtime_error("STL exceeds 200,000 triangles.");
            for(uint64_t i=0;i<n;++i) {
                p+=12;Vec v[3];
                for(Vec& a:v){a.x=float32(data,p);a.y=float32(data,p);a.z=float32(data,p);}
                p+=2;addTriangle(out,v[0],v[1],v[2]);
            }
            return out;
        }
    }
    std::istringstream in(data);std::string token;
    auto expect=[&](const char* word){if(!(in>>token)||token!=word)throw std::runtime_error("Invalid or truncated ASCII STL.");};
    expect("solid");std::getline(in,token);
    while(in>>token) {
        if(token=="endsolid")break;
        if(token!="facet")throw std::runtime_error("Invalid STL facet.");
        expect("normal");double nx,ny,nz;if(!(in>>nx>>ny>>nz))throw std::runtime_error("Invalid STL normal.");
        expect("outer");expect("loop");Vec v[3];
        for(Vec& a:v){expect("vertex");if(!(in>>a.x>>a.y>>a.z))throw std::runtime_error("Invalid STL vertex.");}
        expect("endloop");expect("endfacet");addTriangle(out,v[0],v[1],v[2]);
    }
    return out;
}
struct Type {int size=0;bool signedInteger=false,floating=false;};
Type plyType(const std::string& name) {
    if(name=="char"||name=="int8")return {1,true,false};
    if(name=="uchar"||name=="uint8")return {1,false,false};
    if(name=="short"||name=="int16")return {2,true,false};
    if(name=="ushort"||name=="uint16")return {2,false,false};
    if(name=="int"||name=="int32")return {4,true,false};
    if(name=="uint"||name=="uint32")return {4,false,false};
    if(name=="float"||name=="float32")return {4,false,true};
    if(name=="double"||name=="float64")return {8,false,true};
    throw std::runtime_error("Unsupported PLY property type: "+name);
}
struct Property {std::string name;Type type,count;bool list=false;};
struct Element {std::string name;size_t count=0;std::vector<Property> properties;};
std::vector<Triangle> ply(const std::string& data) {
    std::istringstream header(data);std::string line,format;std::vector<Element> elements;
    std::getline(header,line);if(line!="ply"&&line!="ply\r")throw std::runtime_error("Not a PLY file.");
    bool ended=false;size_t offset=0,rows=0;
    while(std::getline(header,line)) {
        if(header.tellg()>std::streampos(1024*1024))throw std::runtime_error("PLY header is too large.");
        std::istringstream in(line);std::string token;in>>token;
        if(token=="comment"||token=="obj_info"||token.empty())continue;
        if(token=="end_header"){offset=size_t(header.tellg());ended=true;break;}
        if(token=="format") {
            std::string version;in>>format>>version;
            if(version!="1.0"||(format!="ascii"&&format!="binary_little_endian"&&format!="binary_big_endian"))throw std::runtime_error("Unsupported PLY format.");
        } else if(token=="element") {
            Element e;long long n=-1;in>>e.name>>n;
            if(!in||n<0||n>1000000||elements.size()>=32)throw std::runtime_error("Invalid PLY element count.");
            e.count=size_t(n);rows+=e.count;
            if(rows>1000000||(e.name=="vertex"&&e.count>maxVertices)||(e.name=="face"&&e.count>maxTriangles))throw std::runtime_error("PLY contains too many elements.");
            for(const auto& old:elements)if(old.name==e.name)throw std::runtime_error("Duplicate PLY element.");
            elements.push_back(e);
        } else if(token=="property") {
            if(elements.empty()||elements.back().properties.size()>=64)throw std::runtime_error("Invalid PLY properties.");
            Property p;std::string type;in>>type;
            if(type=="list") {
                p.list=true;std::string count;in>>count>>type;p.count=plyType(count);
                if(p.count.floating)throw std::runtime_error("PLY list count must be an integer.");
            }
            p.type=plyType(type);in>>p.name;if(!in)throw std::runtime_error("Invalid PLY property.");
            if(p.list&&elements.back().name=="face"&&(p.name=="vertex_indices"||p.name=="vertex_index")&&p.type.floating)
                throw std::runtime_error("PLY vertex indices must be integers.");
            elements.back().properties.push_back(p);
        } else throw std::runtime_error("Unsupported PLY header entry.");
    }
    if(!ended||format.empty()||offset>data.size())throw std::runtime_error("Incomplete PLY header.");
    bool ascii=format=="ascii",big=format=="binary_big_endian";std::istringstream text(ascii?data.substr(offset):std::string());
    auto read=[&](Type t)->double {
        double value=0;
        if(ascii) {
            std::string word;if(!(text>>word))throw std::runtime_error("Truncated ASCII PLY.");
            size_t used=0;try{value=std::stod(word,&used);}catch(...){throw std::runtime_error("Invalid ASCII PLY number.");}
            if(used!=word.size())throw std::runtime_error("Invalid ASCII PLY number.");
            if(!t.floating&&std::floor(value)!=value)throw std::runtime_error("PLY integer property contains a fractional value.");
        } else {
            uint64_t raw=bits(data,offset,t.size,big);
            if(t.floating&&t.size==4){uint32_t v=uint32_t(raw);float f;std::memcpy(&f,&v,4);value=f;}
            else if(t.floating){std::memcpy(&value,&raw,8);}
            else if(t.signedInteger&&(raw&(uint64_t(1)<<(t.size*8-1))))value=double(raw)-std::ldexp(1.0,t.size*8);
            else value=double(raw);
        }
        if(!std::isfinite(value))throw std::runtime_error("PLY contains a non-finite number.");
        return value;
    };
    std::vector<Vec> vertices;std::vector<std::vector<int>> faces;size_t indexTotal=0;
    for(const auto& element:elements)for(size_t row=0;row<element.count;++row) {
        Vec v;bool x=false,y=false,z=false,hasFace=false;std::vector<int> face;
        for(const auto& p:element.properties) {
            if(p.list) {
                double n=read(p.count);if(n<0||n>4096)throw std::runtime_error("PLY list is too large or invalid.");
                bool indices=element.name=="face"&&(p.name=="vertex_indices"||p.name=="vertex_index");
                if(indices){if(hasFace||n<3||n>512)throw std::runtime_error("Invalid PLY polygon.");hasFace=true;}
                for(int k=0;k<int(n);++k) {
                    double value=read(p.type);
                    if(indices){if(value<0||value>=maxVertices)throw std::runtime_error("PLY face index is out of bounds.");face.push_back(int(value));}
                }
            } else {
                double value=read(p.type);
                if(element.name=="vertex") {
                    if(p.name=="x"){v.x=float(value);x=true;}
                    if(p.name=="y"){v.y=float(value);y=true;}
                    if(p.name=="z"){v.z=float(value);z=true;}
                }
            }
        }
        if(element.name=="vertex"){if(!x||!y||!z)throw std::runtime_error("PLY vertices need x, y and z.");vertexCheck(v);vertices.push_back(v);}
        if(element.name=="face") {
            if(!hasFace)throw std::runtime_error("PLY faces need vertex_indices.");
            indexTotal+=face.size();if(indexTotal>maxTriangles*3)throw std::runtime_error("PLY has too many polygon indices.");
            faces.push_back(std::move(face));
        }
    }
    std::vector<Triangle> out;
    for(const auto& face:faces) {
        for(int i:face)if(size_t(i)>=vertices.size())throw std::runtime_error("PLY face references a missing vertex.");
        for(size_t i=1;i+1<face.size();++i)addTriangle(out,vertices[face[0]],vertices[face[i]],vertices[face[i+1]]);
    }
    return out;
}
}
void normalizeMesh(std::vector<Triangle>& triangles) {
    if(triangles.empty())throw std::runtime_error("Model contains no non-degenerate faces.");
    Bounds bounds;for(const auto& t:triangles)for(Vec v:{t.a,t.b,t.c}){vertexCheck(v);bounds.add(v);}
    Vec extent=bounds.hi-bounds.lo;float largest=std::max({extent.x,extent.y,extent.z});
    if(!std::isfinite(largest)||largest<1e-7f)throw std::runtime_error("Model is too small or invalid.");
    float scale=2.8f/largest;Vec center=(bounds.lo+bounds.hi)*.5f;center.y=bounds.lo.y;
    for(auto& t:triangles)for(Vec* v:{&t.a,&t.b,&t.c})*v=(*v-center)*scale;
}
std::vector<Triangle> loadMesh(const std::filesystem::path& path) {
    std::string extension=path.extension().u8string();
    for(char& c:extension)c=char(std::tolower(static_cast<unsigned char>(c)));
    if(extension==".blend")throw std::runtime_error("Open the .blend project in Blender, then use File > Export > Wavefront OBJ, STL or PLY. Import that exported file here.");
    if(extension==".obj")return loadObj(path);
    if(extension!=".stl"&&extension!=".ply")throw std::runtime_error("Supported models: OBJ, STL and PLY. Export other formats to one of these first.");
    std::ifstream in(path,std::ios::binary|std::ios::ate);if(!in)throw std::runtime_error("Cannot open the model file.");
    auto size=in.tellg();if(size<0||size>std::streampos(maxBytes))throw std::runtime_error("Model exceeds the 64 MB limit.");
    std::string data(size_t(size),'\0');in.seekg(0);in.read(data.data(),std::streamsize(data.size()));
    if(!in)throw std::runtime_error("Could not read the complete model file.");
    auto result=extension==".stl"?stl(data):ply(data);normalizeMesh(result);return result;
}
}
