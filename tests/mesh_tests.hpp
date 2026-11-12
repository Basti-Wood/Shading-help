// SPDX-License-Identifier: Apache-2.0
#include <cstring>
static void meshTests(const std::filesystem::path& temp) {
    auto write=[&](const char* name,const std::string& bytes){auto p=temp/name;std::ofstream o(p,std::ios::binary);o.write(bytes.data(),std::streamsize(bytes.size()));return p;};
    auto append=[](std::string& bytes,uint32_t value,int n,bool big=false){for(int i=0;i<n;++i)bytes.push_back(char(value>>(8*(big?n-1-i:i))));};
    auto floating=[&](std::string& bytes,float value,bool big=false){uint32_t word;std::memcpy(&word,&value,4);append(bytes,word,4,big);};
    auto rejects=[&](const std::filesystem::path& p){try{loadMesh(p);return false;}catch(const std::exception&){return true;}};
    const Vec vertices[]={{-1,0,-1},{1,0,-1},{0,0,1}};
    auto ascii=write("triangle.stl","solid test\nfacet normal 0 1 0\nouter loop\nvertex -1 0 -1\nvertex 1 0 -1\nvertex 0 0 1\nendloop\nendfacet\nendsolid test\n");
    auto a=loadMesh(ascii);
    require(a.size()==1&&near(a[0].a.y,0)&&near(a[0].b.x-a[0].a.x,2.8f),"ASCII STL and horizontal meshes fit by largest dimension");
    std::string binary(80,' ');binary.replace(0,5,"solid");append(binary,1,4);
    for(float f:{0.f,1.f,0.f})floating(binary,f);
    for(Vec v:vertices)for(float f:{v.x,v.y,v.z})floating(binary,f);
    append(binary,0,2);auto b=loadMesh(write("binary.STL",binary));
    require(b.size()==1&&near(a[0].a,b[0].a)&&near(a[0].c,b[0].c),"binary STL recognized even when header begins with solid");
    require(rejects(write("truncated.stl",binary.substr(0,binary.size()-2))),"truncated binary STL rejected");
    std::string bad=binary;size_t nanOffset=96;uint32_t nanBits=0x7fc00000u;
    for(int i=0;i<4;++i)bad[nanOffset+size_t(i)]=char(nanBits>>(8*i));
    require(rejects(write("nan.stl",bad)),"non-finite binary STL coordinates rejected");
    std::string header="ply\nformat ascii 1.0\ncomment reordered properties with color\nelement vertex 3\nproperty float z\nproperty uchar red\nproperty float x\nproperty float y\nelement face 1\nproperty list uchar int vertex_indices\nend_header\n";
    auto c=loadMesh(write("triangle.ply",header+"-1 255 -1 0\n-1 128 1 0\n1 0 0 0\n3 0 1 2\n"));
    require(c.size()==1&&near(c[0].a,a[0].a)&&near(c[0].c,a[0].c),"ASCII PLY supports reordered coordinates and ignored color properties");
    for(bool big:{false,true}) {
        std::string bytes=header;bytes.replace(bytes.find("ascii"),5,big?"binary_big_endian":"binary_little_endian");
        for(Vec v:vertices){floating(bytes,v.z,big);append(bytes,200,1,big);floating(bytes,v.x,big);floating(bytes,v.y,big);}
        append(bytes,3,1,big);for(uint32_t i=0;i<3;++i)append(bytes,i,4,big);
        auto d=loadMesh(write(big?"big.ply":"little.ply",bytes));
        require(d.size()==1&&near(d[0].a,a[0].a)&&near(d[0].c,a[0].c),big?"big-endian binary PLY matches ASCII geometry":"little-endian binary PLY matches ASCII geometry");
        require(rejects(write("short.ply",bytes.substr(0,bytes.size()-1))),"truncated binary PLY rejected");
    }
    require(rejects(write("index.ply",header+"-1 255 -1 0\n-1 128 1 0\n1 0 0 0\n3 0 1 3\n")),"PLY face references are bounds checked");
    require(rejects(write("fraction.ply",header+"-1 255 -1 0\n-1 128 1 0\n1 0 0 0\n3 0 1.5 2\n")),"fractional PLY indices rejected");
    require(rejects(write("cloud.ply","ply\nformat ascii 1.0\nelement vertex 1\nproperty float x\nproperty float y\nproperty float z\nend_header\n0 0 0\n")),"point clouds without faces rejected with an error");
    require(rejects(temp/"native.blend"),"native Blender projects request an export instead of guessing the format");
    require(rejects(write("model.fbx","not a supported model")),"unsupported formats fail explicitly");
    auto obj=write("flat.obj","v -1 0 -1\nv 1 0 -1\nv 0 0 1\nf 1 2 3\n");
    require(loadMesh(obj).size()==1,"OBJ import accepts planar meshes");
    auto oversized=temp/"oversized.obj";{std::ofstream out(oversized,std::ios::binary);out.seekp(64*1024*1024);out.put('x');}
    require(rejects(oversized),"model file size is checked before parsing");
    Settings before,after=before;after.lightPosition={2,2,2};after.surface[0]={.1f,.2f,.3f};
    require(sameGeometry(before,after),"lighting and color changes reuse mesh acceleration data");
    after.rotation[0].y=20;require(!sameGeometry(before,after),"model rotation invalidates acceleration data");
}
