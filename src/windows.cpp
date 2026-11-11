// SPDX-License-Identifier: Apache-2.0
// Native Win32 desktop UI. No browser, webview, engine, or network dependency.
#define UNICODE
#define _UNICODE
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include "progressive.hpp"
#include <cstdio>
#include <cwchar>
#include <fstream>
#include <functional>
#include <sstream>

using namespace shading;
namespace {
constexpr COLORREF bg=RGB(20,24,31),panel=RGB(29,34,43),ink=RGB(228,231,236),muted=RGB(155,166,181);
constexpr COLORREF gold=RGB(244,188,96),cyan=RGB(112,206,218);
enum {IdModel=101,IdView,IdQuality,IdSave,IdLoad,IdExport,IdReset,IdTabs,
      IdLightType=201,IdAim,IdObject,IdSurface,IdFloorColor,IdLightColor,IdFillColor,
      IdGround,IdGray,IdBounces,IdImport,IdHome,IdPoseJoint,IdPoseReset,IdHandles,IdPreset,
      IdColorFirst=300,IdTabFirst=400,IdSliderFirst=1000,IdEditFirst=2000};
struct Binding {HWND slider=nullptr,edit=nullptr;float* value=nullptr;float lo=0,hi=1;};
struct ColorBinding {HWND button;Vec* color;std::wstring name;};
struct App {
    HWND window=nullptr,modelCombo=nullptr,viewCombo=nullptr,qualityCombo=nullptr;
    std::array<HWND,5> tabButtons{};
    HFONT font=nullptr,titleFont=nullptr,smallFont=nullptr;
    HBRUSH panelBrush=nullptr,bgBrush=nullptr;
    Settings settings;
    Scene picking;
    Progressive renderer;
    std::shared_ptr<const std::vector<Triangle>> imported=std::make_shared<const std::vector<Triangle>>();
    Frame frame;
    int revision=0,quality=1,page=0,selected=0,joint=0,width=1320,height=880;
    float dpi=1;
    RECT viewport{};
    std::vector<HWND> pageControls,headerControls;
    std::vector<Binding> bindings;
    std::vector<ColorBinding> colors;
    bool building=false,handles=true,smoke=false;
    int smokeStage=0;
    ULONGLONG smokeStart=0;
    enum class Drag {None,Orbit,Pan,Model,RotateModel,Light,Direction} drag=Drag::None;
    POINT last{};
    Vec dragAnchor,dragPlane,dragOffset;
    std::wstring message=L"Matte materials  /  linear-light transport  /  no display contrast curve";
    std::filesystem::path smokePath;

    int px(float n)const{return int(n*dpi+.5f);}
    int px(int n)const{return px(float(n));}
    void text(HDC dc,std::wstring value,RECT r,COLORREF color=ink,HFONT use=nullptr,UINT flags=DT_LEFT|DT_VCENTER|DT_SINGLELINE) {
        SetBkMode(dc,TRANSPARENT);SetTextColor(dc,color);SelectObject(dc,use?use:font);
        DrawTextW(dc,value.c_str(),int(value.size()),&r,flags);
    }
    HWND control(const wchar_t* cls,const std::wstring& label,int id,int x,int y,int w,int h,DWORD style=0,bool pageControl=true) {
        HWND c=CreateWindowExW(0,cls,label.c_str(),WS_CHILD|WS_VISIBLE|WS_TABSTOP|style,px(x),px(y),px(w),px(h),window,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);
        SendMessageW(c,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
        (pageControl?pageControls:headerControls).push_back(c);return c;
    }
    void label(const std::wstring& value,int x,int y,int w=290,int h=20) {
        HWND c=control(L"STATIC",value,0,x,y,w,h,SS_LEFT);SetWindowLongPtrW(c,GWL_STYLE,GetWindowLongPtrW(c,GWL_STYLE)&~WS_TABSTOP);
    }
    HWND combo(int id,const std::vector<std::wstring>& items,int index,int x,int y,int w=290,bool pg=true) {
        HWND c=control(WC_COMBOBOXW,L"",id,x,y,w,220,CBS_DROPDOWNLIST|WS_VSCROLL,pg);
        for(const auto& item:items)SendMessageW(c,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(item.c_str()));
        SendMessageW(c,CB_SETCURSEL,index,0);return c;
    }
    void button(int id,const std::wstring& value,int x,int y,int w=290,int h=30) {control(L"BUTTON",value,id,x,y,w,h,BS_PUSHBUTTON);}
    void checkbox(int id,const std::wstring& value,bool checked,int x,int y) {
        HWND c=control(L"BUTTON",value,id,x,y,290,25,BS_AUTOCHECKBOX);SendMessageW(c,BM_SETCHECK,checked?BST_CHECKED:BST_UNCHECKED,0);
    }
    static std::wstring number(float f) {wchar_t s[40];swprintf(s,40,L"%.3g",double(f));return s;}
    void slider(const std::wstring& name,float& value,float lo,float hi,int x,int& y) {
        label(name,x,y);y+=21;int i=int(bindings.size());
        HWND slide=control(TRACKBAR_CLASSW,L"",IdSliderFirst+i,x,y,211,24,TBS_HORZ|TBS_NOTICKS);
        SendMessageW(slide,TBM_SETRANGE,TRUE,MAKELPARAM(0,1000));
        SendMessageW(slide,TBM_SETPOS,TRUE,int(1000*std::clamp((value-lo)/(hi-lo),0.f,1.f)));
        HWND edit=control(L"EDIT",number(value),IdEditFirst+i,x+221,y,69,24,WS_BORDER|ES_AUTOHSCROLL);
        bindings.push_back({slide,edit,&value,lo,hi});y+=28;
    }
    void vectorFields(const std::wstring& name,Vec& value,int x,int& y) {
        label(name,x,y);y+=22;
        for(int axis=0;axis<3;++axis) {
            int left=x+axis*99;label(axis==0?L"X":axis==1?L"Y":L"Z",left,y,15,24);
            int i=int(bindings.size());float* p=axis==0?&value.x:axis==1?&value.y:&value.z;
            HWND edit=control(L"EDIT",number(*p),IdEditFirst+i,left+17,y,73,24,WS_BORDER|ES_AUTOHSCROLL);
            bindings.push_back({nullptr,edit,p,-50,50});
        }
        y+=30;
    }
    void colorButton(const std::wstring& name,Vec& color,int x,int& y) {
        HWND c=control(L"BUTTON",name,IdColorFirst+int(colors.size()),x,y,290,34,BS_OWNERDRAW);
        colors.push_back({c,&color,name});y+=40;
    }
    void setupFonts() {
        for(HFONT f:{font,titleFont,smallFont})if(f)DeleteObject(f);
        font=CreateFontW(-px(14),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        titleFont=CreateFontW(-px(28),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        smallFont=CreateFontW(-px(12),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    }
    void panelPage() {
        building=true;for(HWND c:pageControls)DestroyWindow(c);pageControls.clear();bindings.clear();colors.clear();
        int x=width-px(325);x=int(x/dpi);int y=151;
        if(page==0) {
            combo(IdLightType,{L"Spot / disk area light",L"Point light (omnidirectional)",L"Directional light (sun)"},int(settings.lightType),x,y);y+=36;
            colorButton(L"Light color",settings.lightColor,x,y);
            slider(settings.lightType==LightType::Directional?L"Irradiance (relative)":L"Intensity (relative)",settings.intensity,0,180,x,y);
            slider(L"Emitter radius (spot only)",settings.lightRadius,0,1.5f,x,y);
            slider(L"Cone half-angle (spot only)",settings.coneDegrees,5,89,x,y);
            vectorFields(L"Light position",settings.lightPosition,x,y);
            vectorFields(L"Direction of emitted light",settings.lightDirection,x,y);
            button(IdAim,L"Aim at selected model",x,y);y+=39;
            colorButton(L"Environment / shadow fill",settings.fillColor,x,y);
            slider(L"Environment strength",settings.fill,0,1,x,y);
            combo(IdBounces,{L"0 bounces - direct + environment",L"1 surface bounce",L"2 surface bounces",L"3 surface bounces",L"4 surface bounces",L"5 surface bounces",L"6 surface bounces",L"7 surface bounces",L"8 surface bounces"},settings.bounces,x,y);y+=38;
            label(L"Shadow hue comes from the fill and reflected light.",x,y,290,40);
        } else if(page==1) {
            combo(IdObject,settings.model==Model::Both?std::vector<std::wstring>{L"Head material",L"Sphere material"}:std::vector<std::wstring>{L"Selected model material"},selected,x,y);y+=40;
            colorButton(L"Surface / skin tone",settings.surface[selected],x,y);
            colorButton(L"Floor reflectance / bounce color",settings.floorColor,x,y);
            checkbox(IdGround,L"Ground plane + cast shadows",settings.ground,x,y);y+=34;
            checkbox(IdGray,L"Grayscale value study",settings.grayscale,x,y);y+=40;
            label(L"Surface color is diffuse reflectance (albedo).",x,y,290,42);y+=48;
            label(L"Change the floor color, then choose Bounce only to see reflected color reaching the model.",x,y,290,72);y+=84;
            button(IdPreset,L"Try a red-floor bounce study",x,y);y+=44;
            label(L"Double-click the render to inspect its display RGB and linear luminance. The sample uses the current render pass.",x,y,290,88);
        } else if(page==2) {
            combo(IdObject,settings.model==Model::Both?std::vector<std::wstring>{L"Head",L"Sphere"}:std::vector<std::wstring>{L"Selected model"},selected,x,y);y+=40;
            vectorFields(L"Position",settings.position[selected],x,y);
            slider(L"Pitch",settings.rotation[selected].x,-180,180,x,y);
            slider(L"Yaw",settings.rotation[selected].y,-180,180,x,y);
            slider(L"Roll",settings.rotation[selected].z,-180,180,x,y);
            button(IdImport,L"Import a flat-shaded OBJ...",x,y);y+=42;
            label(L"Built-in head: original planar study bust, not an Asaro scan. Import your own licensed Asaro mesh if you prefer.",x,y,290,76);y+=87;
            label(L"Drag model: move on floor\nShift + drag: raise / lower\nAlt + drag: rotate\nSphere rotation does not change its appearance.",x,y,290,95);
        } else if(page==3) {
            slider(L"Exposure (stops)",settings.exposure,-5,5,x,y);
            slider(L"Field of view (degrees)",settings.fov,15,85,x,y);
            slider(L"Camera roll",settings.cameraRoll,-180,180,x,y);
            checkbox(IdHandles,L"Show light and direction handles",handles,x,y);y+=37;
            button(IdHome,L"Frame the scene",x,y);y+=44;
            label(L"Right drag / empty-space drag: orbit\nMiddle drag: pan\nWheel: zoom\nQ / E: roll camera\nH: show / hide handles\nF: frame scene",x,y,290,135);y+=145;
            label(L"Exposure scales linear radiance by 2^stops. The display uses sRGB encoding and clips values above 1. Lower exposure to recover clipped highlights.",x,y,290,100);
        } else {
            combo(IdPoseJoint,{L"Right shoulder",L"Right elbow",L"Left shoulder",L"Left elbow",L"Right hip",L"Right knee",L"Left hip",L"Left knee"},joint,x,y);y+=42;
            slider(L"Bend / swing",settings.jointPitch[joint],-150,150,x,y);
            slider(L"Sideways angle",settings.jointSpread[joint],-90,90,x,y);
            button(IdPoseReset,L"Reset mannequin pose",x,y);y+=44;
            label(L"Select Mannequin in the study menu. Joint rotations use forward kinematics: upper limbs carry their lower limbs with them.",x,y,290,92);y+=104;
            label(L"This is a simple block-and-joint mannequin for light and pose studies. Hands and fingers are not articulated.",x,y,290,85);
        }
        building=false;InvalidateRect(window,nullptr,FALSE);
    }
    void layout() {
        RECT r;GetClientRect(window,&r);width=r.right;height=r.bottom;
        building=true;
        for(HWND c:headerControls)DestroyWindow(c);headerControls.clear();
        int w=int(width/dpi);
        modelCombo=combo(IdModel,{L"Study: planar head",L"Study: sphere",L"Study: head + sphere",L"Study: mannequin",L"Study: imported OBJ"},int(settings.model),20,78,210,false);
        viewCombo=combo(IdView,{L"Combined lighting",L"Direct + environment",L"Bounce only",L"Surface color",L"Face normals"},int(settings.view),240,78,190,false);
        qualityCombo=combo(IdQuality,{L"Quick - 64 samples",L"Study - 512 samples",L"Fine - 2048 samples"},quality,440,78,190,false);
        control(L"BUTTON",L"Save scene",IdSave,w-445,24,100,30,BS_PUSHBUTTON,false);
        control(L"BUTTON",L"Open scene",IdLoad,w-335,24,100,30,BS_PUSHBUTTON,false);
        control(L"BUTTON",L"Export BMP",IdExport,w-225,24,100,30,BS_PUSHBUTTON,false);
        control(L"BUTTON",L"Reset all",IdReset,w-115,24,95,30,BS_PUSHBUTTON,false);
        const wchar_t* names[]={L"Light",L"Surface",L"Object",L"View",L"Pose"};
        for(int i=0;i<5;++i) {
            tabButtons[i]=control(L"BUTTON",names[i],IdTabFirst+i,w-340+i*64,114,63,30,
                BS_AUTORADIOBUTTON|BS_PUSHLIKE|(i==0?WS_GROUP:0),false);
            SendMessageW(tabButtons[i],BM_SETCHECK,i==page?BST_CHECKED:BST_UNCHECKED,0);
        }
        viewport={px(20),px(119),width-px(365),height-px(115)};
        building=false;panelPage();changed(false);
    }
    void syncBindings() {
        building=true;
        for(auto& b:bindings) {
            SetWindowTextW(b.edit,number(*b.value).c_str());
            if(b.slider)SendMessageW(b.slider,TBM_SETPOS,TRUE,int(1000*std::clamp((*b.value-b.lo)/(b.hi-b.lo),0.f,1.f)));
        }
        building=false;
    }
    void changed(bool moving) {
        settings.validate();picking=makeScene(settings,*imported);
        int vw=std::max(1L,viewport.right-viewport.left),vh=std::max(1L,viewport.bottom-viewport.top);
        float scale=std::min(1.f,float(moving?300:quality==0?600:quality==1?950:1400)/std::max(vw,vh));
        int limit=moving?8:quality==0?64:quality==1?512:2048;
        revision=renderer.request(settings,std::max(1,int(vw*scale)),std::max(1,int(vh*scale)),limit,imported);
        InvalidateRect(window,nullptr,FALSE);
    }
    Camera camera()const{return Camera(settings,float(viewport.right-viewport.left)/std::max(1L,viewport.bottom-viewport.top));}
    Ray mouseRay(POINT p)const{return camera().ray(float(p.x-viewport.left)/(viewport.right-viewport.left),float(p.y-viewport.top)/(viewport.bottom-viewport.top));}
    bool screen(Vec p,POINT& out)const {
        float u,v;if(!camera().project(p,u,v))return false;
        out={LONG(viewport.left+u*(viewport.right-viewport.left)),LONG(viewport.top+v*(viewport.bottom-viewport.top))};return true;
    }
    static float distance(POINT a,POINT b){return std::hypot(float(a.x-b.x),float(a.y-b.y));}
    void line(HDC dc,POINT a,POINT b,COLORREF color,int thickness=2) {
        HPEN pen=CreatePen(PS_SOLID,px(thickness),color);HGDIOBJ old=SelectObject(dc,pen);
        MoveToEx(dc,a.x,a.y,nullptr);LineTo(dc,b.x,b.y);SelectObject(dc,old);DeleteObject(pen);
    }
    void dotHandle(HDC dc,POINT p,int radius,COLORREF color) {
        HBRUSH brush=CreateSolidBrush(color);HGDIOBJ b=SelectObject(dc,brush),pen=SelectObject(dc,GetStockObject(NULL_PEN));
        Ellipse(dc,p.x-px(radius),p.y-px(radius),p.x+px(radius),p.y+px(radius));SelectObject(dc,b);SelectObject(dc,pen);DeleteObject(brush);
    }
    void draw(HDC dc) {
        RECT whole{0,0,width,height};FillRect(dc,&whole,bgBrush);
        RECT pane{width-px(345),px(114),width-px(10),height-px(16)};FillRect(dc,&pane,panelBrush);
        text(dc,L"SHADING HELP",{px(20),px(14),px(430),px(48)},ink,titleFont);
        text(dc,L"A small studio for studying light, form & color",{px(21),px(48),px(580),px(70)},muted,smallFont);
        FillRect(dc,&viewport,panelBrush);
        if(!frame.pixels.empty()) {
            BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=frame.width;info.bmiHeader.biHeight=-frame.height;
            info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
            SetStretchBltMode(dc,COLORONCOLOR);
            StretchDIBits(dc,viewport.left,viewport.top,viewport.right-viewport.left,viewport.bottom-viewport.top,0,0,frame.width,frame.height,frame.pixels.data(),&info,DIB_RGB_COLORS,SRCCOPY);
        }
        int saved=SaveDC(dc);IntersectClipRect(dc,viewport.left,viewport.top,viewport.right,viewport.bottom);
        if(handles) {
            POINT source{},tip{};
            bool a=screen(settings.lightPosition,source),b=screen(settings.lightPosition+unit(settings.lightDirection)*1.25f,tip);
            if(a&&b&&settings.lightType!=LightType::Point) {
                line(dc,source,tip,cyan);dotHandle(dc,tip,7,cyan);
                Vec v{float(tip.x-source.x),float(tip.y-source.y),0};v=unit(v);
                POINT l{LONG(tip.x-px(17)*v.x+px(7)*v.y),LONG(tip.y-px(17)*v.y-px(7)*v.x)};
                POINT r{LONG(tip.x-px(17)*v.x-px(7)*v.y),LONG(tip.y-px(17)*v.y+px(7)*v.x)};
                line(dc,l,tip,cyan);line(dc,r,tip,cyan);
                text(dc,L"AIM",{tip.x+px(13),tip.y-px(10),tip.x+px(75),tip.y+px(12)},cyan,smallFont);
            }
            if(a) {
                if(PtInRect(&viewport,source)) {
                    dotHandle(dc,source,12,gold);dotHandle(dc,{source.x-px(3),source.y-px(3)},4,RGB(255,226,178));
                    text(dc,L"LIGHT",{source.x+px(17),source.y-px(10),source.x+px(100),source.y+px(12)},gold,smallFont);
                } else text(dc,L"Light handle outside view - zoom out to reach it",{viewport.left+px(14),viewport.top+px(14),viewport.right-px(14),viewport.top+px(38)},gold,smallFont);
            }
        }
        RestoreDC(dc,saved);
        bool current=frame.revision==revision;
        std::wstring status=current?std::to_wstring(frame.samples)+L" samples / pixel":L"Updating lighting...";
        status+=L"   |   "+std::to_wstring(settings.bounces)+L" surface bounces   |   CPU path tracing";
        text(dc,status,{px(20),height-px(106),viewport.right,height-px(82)},gold,smallFont);
        text(dc,L"Drag: move   /   Shift: height   /   Alt: rotate   /   Right drag: orbit   /   Wheel: zoom",{px(20),height-px(79),viewport.right,height-px(55)},muted,smallFont);
        text(dc,message,{px(20),height-px(49),viewport.right,height-px(12)},ink,smallFont,DT_LEFT|DT_WORDBREAK);
    }
    void paint() {
        PAINTSTRUCT ps;HDC dc=BeginPaint(window,&ps);HDC memory=CreateCompatibleDC(dc);
        HBITMAP bitmap=CreateCompatibleBitmap(dc,width,height);HGDIOBJ old=SelectObject(memory,bitmap);
        draw(memory);BitBlt(dc,0,0,width,height,memory,0,0,SRCCOPY);
        SelectObject(memory,old);DeleteObject(bitmap);DeleteDC(memory);EndPaint(window,&ps);
    }
    void drawColor(const DRAWITEMSTRUCT& item) {
        int i=int(item.CtlID)-IdColorFirst;if(i<0||size_t(i)>=colors.size())return;
        auto& c=colors[size_t(i)];Vec v=clamp(*c.color);
        FillRect(item.hDC,&item.rcItem,panelBrush);RECT swatch=item.rcItem;swatch.right=swatch.left+px(33);InflateRect(&swatch,-px(4),-px(4));
        HBRUSH brush=CreateSolidBrush(RGB(int(v.x*255),int(v.y*255),int(v.z*255)));FillRect(item.hDC,&swatch,brush);DeleteObject(brush);
        wchar_t hex[16];swprintf(hex,16,L"#%02X%02X%02X",int(v.x*255+.5f),int(v.y*255+.5f),int(v.z*255+.5f));
        RECT r=item.rcItem;r.left+=px(40);r.right-=px(70);text(item.hDC,c.name,r,ink,smallFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
        r.right=item.rcItem.right;
        r.left=r.right-px(64);text(item.hDC,hex,r,muted,smallFont);
        if(item.itemState&ODS_FOCUS)DrawFocusRect(item.hDC,&item.rcItem);
    }
    void pickColor(int i) {
        if(i<0||size_t(i)>=colors.size())return;
        static COLORREF custom[16]={RGB(184,122,87),RGB(224,177,142),RGB(97,62,43),RGB(181,42,23)};
        Vec& c=*colors[size_t(i)].color;CHOOSECOLORW dialog{};dialog.lStructSize=sizeof(dialog);dialog.hwndOwner=window;
        dialog.rgbResult=RGB(int(c.x*255),int(c.y*255),int(c.z*255));dialog.lpCustColors=custom;dialog.Flags=CC_FULLOPEN|CC_RGBINIT;
        if(ChooseColorW(&dialog)){c={GetRValue(dialog.rgbResult)/255.f,GetGValue(dialog.rgbResult)/255.f,GetBValue(dialog.rgbResult)/255.f};InvalidateRect(colors[size_t(i)].button,nullptr,TRUE);changed(false);}
    }
    std::filesystem::path fileDialog(bool save,const wchar_t* filter,const wchar_t* ext,const wchar_t* defaultName=L"") {
        wchar_t name[32768]{};wcsncpy_s(name,defaultName,_TRUNCATE);
        OPENFILENAMEW dialog{};dialog.lStructSize=sizeof(dialog);dialog.hwndOwner=window;dialog.lpstrFilter=filter;dialog.lpstrFile=name;dialog.nMaxFile=32768;dialog.lpstrDefExt=ext;
        dialog.Flags=OFN_EXPLORER|OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|(save?OFN_OVERWRITEPROMPT:OFN_FILEMUSTEXIST);
        if(save?GetSaveFileNameW(&dialog):GetOpenFileNameW(&dialog))return std::filesystem::path(name);return {};
    }
    void frameScene() {
        settings.cameraTarget=settings.model==Model::Both?(settings.position[0]+settings.position[1])*.5f+Vec(0,1.4f,0):settings.position[0]+Vec(0,1.4f,0);
        settings.cameraDistance=settings.model==Model::Both?10.f:7.4f;settings.cameraYaw=18;settings.cameraPitch=10;settings.cameraRoll=0;settings.fov=38;
    }
    void chooseModel(int index) {
        if(index==int(Model::Imported)&&imported->empty()){SendMessageW(modelCombo,CB_SETCURSEL,int(settings.model),0);command(IdImport,BN_CLICKED,nullptr);return;}
        settings.model=Model(index);selected=0;
        if(settings.model==Model::Both){settings.position[0]={-1.2f,0,0};settings.position[1]={1.2f,0,0};}
        else settings.position[0]=Vec();
        frameScene();panelPage();changed(false);
    }
    void command(int id,int notification,HWND sender) {
        if(building)return;
        try {
            if(id>=IdTabFirst&&id<IdTabFirst+5&&notification==BN_CLICKED) {
                page=id-IdTabFirst;
                for(int i=0;i<5;++i)SendMessageW(tabButtons[i],BM_SETCHECK,i==page?BST_CHECKED:BST_UNCHECKED,0);
                panelPage();return;
            }
            if(id>=IdColorFirst&&id<IdColorFirst+int(colors.size())){if(notification==BN_CLICKED)pickColor(id-IdColorFirst);return;}
            if(id>=IdEditFirst&&id<IdEditFirst+int(bindings.size())) {
                if(notification==EN_KILLFOCUS) {
                    auto& b=bindings[size_t(id-IdEditFirst)];wchar_t value[128]{};GetWindowTextW(b.edit,value,128);wchar_t* end=nullptr;float f=std::wcstof(value,&end);
                    if(end!=value&&*end==0&&std::isfinite(f)){*b.value=std::clamp(f,b.lo,b.hi);changed(false);}
                    syncBindings();
                }
                return;
            }
            if(notification==CBN_SELCHANGE) {
                int i=int(SendMessageW(sender,CB_GETCURSEL,0,0));
                switch(id) {
                    case IdModel:chooseModel(i);return;
                    case IdView:settings.view=View(i);break;
                    case IdQuality:quality=i;break;
                    case IdLightType:settings.lightType=LightType(i);panelPage();break;
                    case IdObject:selected=std::clamp(i,0,1);panelPage();return;
                    case IdPoseJoint:joint=std::clamp(i,0,7);panelPage();return;
                    case IdBounces:settings.bounces=std::clamp(i,0,8);break;
                }
                changed(false);return;
            }
            if(notification!=BN_CLICKED)return;
            switch(id) {
                case IdSave:{auto p=fileDialog(true,L"Shading Help scene\0*.shade\0\0",L"shade",L"study.shade");if(!p.empty()){saveSettings(p,settings);message=L"Scene saved.";}break;}
                case IdLoad:{auto p=fileDialog(false,L"Shading Help scene\0*.shade\0\0",L"shade");if(p.empty())break;
                    Settings next=loadSettings(p);auto geometry=std::make_shared<const std::vector<Triangle>>();
                    if(next.model==Model::Imported&&!next.objPath.empty())geometry=std::make_shared<const std::vector<Triangle>>(loadObj(std::filesystem::u8path(next.objPath)));
                    if(next.model==Model::Imported&&geometry->empty())throw std::runtime_error("Scene needs an OBJ mesh.");
                    settings=next;imported=geometry;selected=0;layout();message=L"Scene restored.";break;}
                case IdExport:{if(frame.pixels.empty()||frame.revision!=revision){message=L"Wait for the current render before exporting.";break;}
                    auto p=fileDialog(true,L"Bitmap image\0*.bmp\0\0",L"bmp",L"lighting-study.bmp");if(!p.empty()){saveBmp(p,frame.pixels,frame.width,frame.height);message=L"Exported render without UI handles ("+std::to_wstring(frame.samples)+L" samples).";}break;}
                case IdReset:settings=Settings();settings.intensity=35;settings.lightPosition={-1.8f,3.3f,1.5f};settings.lightDirection=unit(Vec(0,1.5f,0)-settings.lightPosition);selected=0;layout();break;
                case IdAim:settings.lightDirection=unit(settings.position[selected]+Vec(0,1.4f,0)-settings.lightPosition);syncBindings();changed(false);break;
                case IdGround:settings.ground=SendMessageW(sender,BM_GETCHECK,0,0)==BST_CHECKED;changed(false);break;
                case IdGray:settings.grayscale=SendMessageW(sender,BM_GETCHECK,0,0)==BST_CHECKED;changed(false);break;
                case IdHandles:handles=SendMessageW(sender,BM_GETCHECK,0,0)==BST_CHECKED;break;
                case IdHome:frameScene();syncBindings();changed(false);break;
                case IdPoseReset:settings.jointPitch.fill(0);settings.jointSpread.fill(0);syncBindings();changed(false);break;
                case IdPreset:settings.model=Model::Ball;selected=0;settings.position[0]=Vec();settings.surface[0]=Vec(.75f);settings.floorColor={.8f,.08f,.04f};settings.fill=.015f;settings.bounces=3;settings.view=View::Beauty;settings.ground=true;frameScene();layout();break;
                case IdImport:{auto p=fileDialog(false,L"Wavefront OBJ mesh\0*.obj\0\0",L"obj");if(p.empty())break;
                    auto geometry=std::make_shared<const std::vector<Triangle>>(loadObj(p));imported=geometry;settings.objPath=p.u8string();settings.model=Model::Imported;settings.position[0]=Vec();selected=0;frameScene();layout();message=L"Imported flat-shaded OBJ. Materials use the surface color picker.";break;}
            }
            InvalidateRect(window,nullptr,FALSE);
        } catch(const std::exception& e) {MessageBoxA(window,e.what(),"Shading Help",MB_OK|MB_ICONERROR);}
    }
    void onSlider(HWND source,int code) {
        for(auto& b:bindings)if(b.slider==source) {
            *b.value=b.lo+(b.hi-b.lo)*float(SendMessageW(source,TBM_GETPOS,0,0))/1000;
            SetWindowTextW(b.edit,number(*b.value).c_str());changed(code==TB_THUMBTRACK);break;
        }
    }
    Vec onPlane(Ray ray,Vec anchor,Vec normal)const {
        float d=dot(ray.direction,normal);if(std::abs(d)<.001f)return anchor;
        float t=dot(anchor-ray.origin,normal)/d;if(t<=0||t>200)return anchor;return ray.at(t);
    }
    void down(POINT p,UINT button) {
        if(!PtInRect(&viewport,p))return;SetFocus(window);last=p;
        if(button==WM_RBUTTONDOWN)drag=Drag::Orbit;
        else if(button==WM_MBUTTONDOWN)drag=Drag::Pan;
        else {
            POINT source{},tip{};bool sourceVisible=screen(settings.lightPosition,source);
            bool tipVisible=screen(settings.lightPosition+unit(settings.lightDirection)*1.25f,tip);
            if(handles&&tipVisible&&settings.lightType!=LightType::Point&&distance(p,tip)<px(16))drag=Drag::Direction;
            else if(handles&&sourceVisible&&distance(p,source)<px(19)) {
                drag=Drag::Light;dragAnchor=settings.lightPosition;dragPlane=camera().forward;
                dragOffset=onPlane(mouseRay(p),dragAnchor,dragPlane)-settings.lightPosition;
            } else {
                Hit hit;if(picking.intersect(mouseRay(p),hit)&&hit.object>=0) {
                    selected=hit.object;drag=(GetKeyState(VK_MENU)&0x8000)?Drag::RotateModel:Drag::Model;
                    dragAnchor=hit.position;dragPlane={0,1,0};dragOffset=hit.position-settings.position[selected];
                    if(page==1||page==2)panelPage();
                } else drag=Drag::Orbit;
            }
        }
        SetCapture(window);
    }
    void move(POINT p) {
        if(drag==Drag::None)return;int dx=p.x-last.x,dy=p.y-last.y;if(dx==0&&dy==0)return;
        auto cam=camera();float worldPerPixel=2*settings.cameraDistance*cam.tangent/std::max(1L,viewport.bottom-viewport.top);
        switch(drag) {
            case Drag::Orbit:settings.cameraYaw-=dx*.35f/dpi;settings.cameraPitch=std::clamp(settings.cameraPitch+dy*.35f/dpi,-89.f,89.f);break;
            case Drag::Pan:settings.cameraTarget+=cam.right*(-dx*worldPerPixel)+cam.up*(dy*worldPerPixel);break;
            case Drag::Model:
                if(GetKeyState(VK_SHIFT)&0x8000){settings.position[selected].y-=dy*worldPerPixel;dragAnchor.y-=dy*worldPerPixel;}
                else {Vec next=onPlane(mouseRay(p),dragAnchor,dragPlane)-dragOffset;settings.position[selected].x=next.x;settings.position[selected].z=next.z;}
                break;
            case Drag::RotateModel:settings.rotation[selected].y+=dx*.6f/dpi;settings.rotation[selected].x+=dy*.6f/dpi;break;
            case Drag::Light:
                if(GetKeyState(VK_SHIFT)&0x8000){settings.lightPosition.y-=dy*worldPerPixel;dragAnchor.y-=dy*worldPerPixel;}
                else settings.lightPosition=onPlane(mouseRay(p),dragAnchor,dragPlane)-dragOffset;
                break;
            case Drag::Direction:{Ray r=mouseRay(p);Hit h;Sphere sphere{settings.lightPosition,1.25f,0,0};Vec q;
                if(hitSphere(sphere,r,h))q=h.position;
                else q=r.at(std::max(.01f,dot(settings.lightPosition-r.origin,r.direction)));
                settings.lightDirection=unit(q-settings.lightPosition);break;}
            default:break;
        }
        last=p;syncBindings();changed(true);
    }
    void up() {if(drag!=Drag::None){drag=Drag::None;ReleaseCapture();changed(false);}}
    void inspect(POINT p) {
        if(!PtInRect(&viewport,p)||frame.pixels.empty()||frame.revision!=revision)return;
        int x=std::clamp(int(float(p.x-viewport.left)/(viewport.right-viewport.left)*frame.width),0,frame.width-1);
        int y=std::clamp(int(float(p.y-viewport.top)/(viewport.bottom-viewport.top)*frame.height),0,frame.height-1);
        size_t i=size_t(y)*frame.width+x;uint32_t rgb=frame.pixels[i];Vec v=frame.linear[i];
        wchar_t value[220];swprintf(value,220,L"Display #%06X   RGB %u, %u, %u   |   Linear luminance %.4f   |   %d samples",rgb,(rgb>>16)&255,(rgb>>8)&255,rgb&255,double(luminance(v)),frame.samples);
        message=value;InvalidateRect(window,nullptr,FALSE);
    }
    void capture(const std::filesystem::path& path) {
        UpdateWindow(window);HDC screenDC=GetDC(window),memory=CreateCompatibleDC(screenDC);
        BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;
        info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;void* bits=nullptr;
        HBITMAP bitmap=CreateDIBSection(screenDC,&info,DIB_RGB_COLORS,&bits,nullptr,0);HGDIOBJ old=SelectObject(memory,bitmap);
        // Render our actual client and native controls into the bitmap. This also
        // captures any part outside a small CI runner's physical screen.
        draw(memory);
        for(HWND child=GetWindow(window,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT)) {
            if(!IsWindowVisible(child))continue;
            RECT r;GetWindowRect(child,&r);MapWindowPoints(HWND_DESKTOP,window,reinterpret_cast<POINT*>(&r),2);
            int saved=SaveDC(memory);IntersectClipRect(memory,r.left,r.top,r.right,r.bottom);
            SetViewportOrgEx(memory,r.left,r.top,nullptr);
            SendMessageW(child,WM_PRINT,reinterpret_cast<WPARAM>(memory),PRF_CLIENT|PRF_CHILDREN|PRF_ERASEBKGND);
            RestoreDC(memory,saved);
        }
        GdiFlush();
        std::vector<uint32_t> pixels(size_t(width)*height);std::copy_n(static_cast<uint32_t*>(bits),pixels.size(),pixels.begin());
        SelectObject(memory,old);DeleteObject(bitmap);DeleteDC(memory);ReleaseDC(window,screenDC);saveBmp(path,pixels,width,height);
    }
    void tick() {
        if(renderer.frame(frame,frame.revision==revision?frame.samples:0,revision))InvalidateRect(window,nullptr,FALSE);
        if(smoke) {
            if(GetTickCount64()-smokeStart>90000){PostQuitMessage(2);return;}
            if(frame.revision!=revision||frame.samples<8)return;
            try {
                if(smokeStage==0) {
                    RECT lastTab{};GetWindowRect(tabButtons[4],&lastTab);MapWindowPoints(HWND_DESKTOP,window,reinterpret_cast<POINT*>(&lastTab),2);
                    if(lastTab.right>width-px(10))throw std::runtime_error("Settings tabs exceed the available width.");
                    // Exercise the actual window command and pointer handlers.
                    POINT center;screen({0,1.7f,.5f},center);
                    settings.model=Model::Ball;settings.position[0]=Vec();changed(false);
                    screen({0,1,1},center);down(center,WM_LBUTTONDOWN);move({center.x+px(40),center.y});up();
                    if(length(settings.position[0])<.001f)throw std::runtime_error("Model drag smoke test failed.");
                    POINT light;screen(settings.lightPosition,light);Vec before=settings.lightPosition;
                    down(light,WM_LBUTTONDOWN);move({light.x+px(20),light.y+px(10)});up();
                    if(length(settings.lightPosition-before)<.001f)throw std::runtime_error("Light drag smoke test failed.");
                    POINT arrow;screen(settings.lightPosition+unit(settings.lightDirection)*1.25f,arrow);before=settings.lightDirection;
                    down(arrow,WM_LBUTTONDOWN);move({arrow.x+px(24),arrow.y-px(12)});up();
                    if(length(settings.lightDirection-before)<.001f||std::abs(length(settings.lightDirection)-1)>.001f)
                        throw std::runtime_error("Light direction drag smoke test failed.");
                    // Commit an exact numeric intensity through the actual edit handler.
                    SetWindowTextW(bindings[0].edit,L"35");
                    command(IdEditFirst,EN_KILLFOCUS,bindings[0].edit);
                    if(settings.intensity!=35)throw std::runtime_error("Numeric intensity input smoke test failed.");
                    settings.bounces=3;panelPage();
                    if(SendMessageW(GetDlgItem(window,IdBounces),CB_GETCURSEL,0,0)!=3)
                        throw std::runtime_error("Bounce count label does not match the renderer.");
                    settings.bounces=2;
                    settings.model=Model::Head;settings.position[0]=Vec();settings.lightDirection=unit(Vec(0,1.4f,0)-settings.lightPosition);
                    page=0;layout();smokeStage=1;
                } else if(frame.samples>=64) {
                    capture(smokePath);saveBmp(smokePath.parent_path()/L"smoke-render.bmp",frame.pixels,frame.width,frame.height);
                    for(int tab=1;tab<5;++tab) {
                        command(IdTabFirst+tab,BN_CLICKED,tabButtons[tab]);
                        capture(smokePath.parent_path()/(L"smoke-tab-"+std::to_wstring(tab)+L".bmp"));
                    }
                    PostQuitMessage(0);
                }
            }catch(const std::exception& e){
                std::ofstream log(smokePath.parent_path()/L"smoke-error.txt");log<<e.what()<<'\n';
                capture(smokePath);PostQuitMessage(3);
            }
        }
    }
};
App* app=nullptr;
LRESULT CALLBACK windowProc(HWND window,UINT msg,WPARAM w,LPARAM l) {
    if(!app)return DefWindowProcW(window,msg,w,l);
    switch(msg) {
        case WM_CREATE:app->window=window;app->dpi=GetDpiForWindow(window)/96.f;app->setupFonts();app->bgBrush=CreateSolidBrush(bg);app->panelBrush=CreateSolidBrush(panel);SetTimer(window,1,60,nullptr);return 0;
        case WM_SIZE:if(w!=SIZE_MINIMIZED&&app->font)app->layout();return 0;
        case WM_DPICHANGED:{app->dpi=HIWORD(w)/96.f;app->setupFonts();auto r=reinterpret_cast<RECT*>(l);SetWindowPos(window,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);return 0;}
        case WM_GETMINMAXINFO:{auto m=reinterpret_cast<MINMAXINFO*>(l);m->ptMinTrackSize={app->px(1020),app->px(760)};return 0;}
        case WM_PAINT:app->paint();return 0;
        case WM_ERASEBKGND:return 1;
        case WM_CTLCOLORSTATIC:case WM_CTLCOLORBTN:{HDC dc=reinterpret_cast<HDC>(w);SetTextColor(dc,ink);SetBkColor(dc,panel);return reinterpret_cast<LRESULT>(app->panelBrush);}
        case WM_CTLCOLOREDIT:{HDC dc=reinterpret_cast<HDC>(w);SetTextColor(dc,ink);SetBkColor(dc,bg);return reinterpret_cast<LRESULT>(app->bgBrush);}
        case WM_DRAWITEM:app->drawColor(*reinterpret_cast<DRAWITEMSTRUCT*>(l));return TRUE;
        case WM_COMMAND:app->command(LOWORD(w),HIWORD(w),reinterpret_cast<HWND>(l));return 0;
        case WM_HSCROLL:app->onSlider(reinterpret_cast<HWND>(l),LOWORD(w));return 0;
        case WM_TIMER:app->tick();return 0;
        case WM_LBUTTONDOWN:case WM_RBUTTONDOWN:case WM_MBUTTONDOWN:app->down({GET_X_LPARAM(l),GET_Y_LPARAM(l)},msg);return 0;
        case WM_MOUSEMOVE:app->move({GET_X_LPARAM(l),GET_Y_LPARAM(l)});return 0;
        case WM_LBUTTONUP:case WM_RBUTTONUP:case WM_MBUTTONUP:app->up();return 0;
        case WM_CAPTURECHANGED:if(app->drag!=App::Drag::None){app->drag=App::Drag::None;app->changed(false);}return 0;
        case WM_LBUTTONDBLCLK:app->inspect({GET_X_LPARAM(l),GET_Y_LPARAM(l)});return 0;
        case WM_MOUSEWHEEL:{POINT p{GET_X_LPARAM(l),GET_Y_LPARAM(l)};ScreenToClient(window,&p);if(PtInRect(&app->viewport,p)){app->settings.cameraDistance*=std::exp(-GET_WHEEL_DELTA_WPARAM(w)/120.f*.12f);app->changed(false);}return 0;}
        case WM_KEYDOWN:
            if(w=='H'){app->handles=!app->handles;app->panelPage();}
            if(w=='F'){app->frameScene();app->changed(false);}
            if(w=='Q'||w=='E'){app->settings.cameraRoll+=(w=='Q'?-3.f:3.f);app->syncBindings();app->changed(false);}
            if(w==VK_ESCAPE)app->up();return 0;
        case WM_DESTROY:KillTimer(window,1);PostQuitMessage(0);return 0;
        default:return DefWindowProcW(window,msg,w,l);
    }
}
} // namespace
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int show) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_BAR_CLASSES|ICC_TAB_CLASSES|ICC_STANDARD_CLASSES};InitCommonControlsEx(&controls);
    App application;app=&application;
    int argc=0;LPWSTR* argv=CommandLineToArgvW(GetCommandLineW(),&argc);
    if(argc>=3&&std::wstring(argv[1])==L"--smoke"){app->smoke=true;app->smokePath=argv[2];app->quality=0;app->smokeStart=GetTickCount64();}
    LocalFree(argv);
    app->settings.intensity=35;app->settings.lightPosition={-1.8f,3.3f,1.5f};app->settings.lightDirection=unit(Vec(0,1.5f,0)-app->settings.lightPosition);
    WNDCLASSW wc{};wc.style=CS_DBLCLKS;wc.lpfnWndProc=windowProc;wc.hInstance=instance;wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hIcon=LoadIconW(nullptr,IDI_APPLICATION);wc.lpszClassName=L"ShadingHelpWindow";
    if(!RegisterClassW(&wc))return 1;
    HWND window=CreateWindowExW(0,wc.lpszClassName,L"Shading Help - Light & Form Studio",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,1320,920,nullptr,nullptr,instance,nullptr);
    if(!window)return 1;ShowWindow(window,show);UpdateWindow(window);
    MSG message{};
    while(GetMessageW(&message,nullptr,0,0)>0) {
        if(message.message==WM_KEYDOWN&&message.wParam==VK_RETURN&&message.hwnd!=window){SetFocus(window);continue;}
        if(message.message==WM_KEYDOWN&&message.wParam==VK_TAB){HWND next=GetNextDlgTabItem(window,GetFocus(),GetKeyState(VK_SHIFT)<0);if(next)SetFocus(next);continue;}
        TranslateMessage(&message);DispatchMessageW(&message);
    }
    DestroyWindow(window);
    for(HFONT f:{app->font,app->titleFont,app->smallFont})if(f)DeleteObject(f);
    DeleteObject(app->panelBrush);DeleteObject(app->bgBrush);
    return int(message.wParam);
}
