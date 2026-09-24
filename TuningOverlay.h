#pragma once
#include <windows.h>
#include <d3d9.h>
#include <algorithm>
#include <array>
#include <string>
#include <vector>

namespace tuningoverlay {
enum ItemKind { KIND_HEADER, KIND_TOGGLE, KIND_SLIDER };
struct Item { ItemKind kind; const wchar_t* section; const wchar_t* key; const char* label; int lo,hi,step,value; };
inline std::wstring ini;
inline bool visible=false,f7Down=false,mouseDown=false;
inline int hover=-1,dragItem=-1;
inline std::vector<Item> items={
 // === ATMOSPHERE & GOD RAYS ===
 {KIND_HEADER,nullptr,nullptr,"--- ATMOSPHERE & GOD RAYS ---",0,0,0,0},
 {KIND_TOGGLE,L"Atmosphere",L"ShaftsEnabled","SUN RAYS",0,1,1,1},
 {KIND_TOGGLE,L"Atmosphere",L"DirectionalVolumetricEnabled","VOLUMETRIC (HEAVY)",0,1,1,0},
 {KIND_SLIDER,L"Atmosphere",L"ShaftPercent","RAY STRENGTH",0,400,5,160},
 {KIND_SLIDER,L"Atmosphere",L"SunGlowPercent","SUN GLOW",0,200,5,80},
 {KIND_SLIDER,L"Atmosphere",L"SunVerticalProjectionPercent","SUN VERT SCALE",10,100,2,40},
 {KIND_SLIDER,L"Atmosphere",L"SunOffsetYPercent","SUN OFFSET Y",-50,50,1,0},
 {KIND_TOGGLE,L"Atmosphere",L"FogEnabled","FOG",0,1,1,1},
 {KIND_TOGGLE,L"DistanceFog",L"Enabled","DISTANCE HAZE",0,1,1,1},
 {KIND_SLIDER,L"Atmosphere",L"DensityPermille","FOG DENSITY",0,20,1,5},
 {KIND_SLIDER,L"Atmosphere",L"BaseHeight","FOG BASE HEIGHT",-50,200,2,28},
 {KIND_SLIDER,L"Atmosphere",L"FogWashPercent","FOG WASH",0,100,2,35},

 // === WATER & REFLECTIONS ===
 {KIND_HEADER,nullptr,nullptr,"--- WATER & REFLECTIONS ---",0,0,0,0},
 {KIND_TOGGLE,L"Water",L"Enabled","WATER",0,1,1,1},
 {KIND_TOGGLE,L"Water",L"WavesEnabled","WAVES",0,1,1,1},
 {KIND_TOGGLE,L"Water",L"ReflectionsEnabled","REFLECTIONS",0,1,1,1},
 {KIND_SLIDER,L"Water",L"ReflectionPercent","SSR STRENGTH",0,150,2,80},
 {KIND_SLIDER,L"Water",L"EnvironmentPercent","SKY REFLECTION",0,150,2,45},
 {KIND_TOGGLE,L"Water",L"SunGlintEnabled","SUN MOON GLINT",0,1,1,1},
 {KIND_SLIDER,L"Water",L"SunGlintPercent","GLINT STRENGTH",0,500,5,160},
 {KIND_TOGGLE,L"Water",L"RefractionEnabled","REFRACTION",0,1,1,1},
 {KIND_SLIDER,L"Water",L"RefractionPercent","REFRACT AMOUNT",0,50,1,14},
 {KIND_SLIDER,L"Water",L"DepthAbsorptionPercent","WATER DEPTH",0,100,1,38},
 {KIND_TOGGLE,L"Water",L"FoamEnabled","FOAM",0,1,1,1},
 {KIND_SLIDER,L"Water",L"ShoreFoamPercent","FOAM AMOUNT",0,60,1,28},
 {KIND_SLIDER,L"Water",L"SpecularPercent","WATER SHINE",0,400,5,115},

 // === DYNAMIC SHADOWS ===
 {KIND_HEADER,nullptr,nullptr,"--- DYNAMIC SHADOWS ---",0,0,0,0},
 {KIND_TOGGLE,L"Atmosphere",L"ShadowsEnabled","CONTACT SHADOW",0,1,1,1},
 {KIND_SLIDER,L"Atmosphere",L"DirectionalShadowPercent","SHADOW STRENGTH",0,100,5,45},
 {KIND_SLIDER,L"Atmosphere",L"ShadowMapDistance","SHADOW DIST",50,300,10,180},
 {KIND_SLIDER,L"Atmosphere",L"ContactShadowPercent","CONTACT SHADOW",0,60,2,25},

 // === IMAGE & COLOR ===
 {KIND_HEADER,nullptr,nullptr,"--- IMAGE & COLOR ---",0,0,0,0},
 {KIND_TOGGLE,L"PostProcess",L"Enabled","POST PROCESS",0,1,1,0},
 {KIND_SLIDER,L"PostProcess",L"BrightnessPercent","BRIGHTNESS",-50,50,1,0},
 {KIND_SLIDER,L"PostProcess",L"ContrastPercent","CONTRAST",50,180,2,100},
 {KIND_SLIDER,L"PostProcess",L"GammaPercent","GAMMA",50,180,2,100},
 {KIND_SLIDER,L"PostProcess",L"SharpnessPercent","SHARPNESS",0,100,2,35}
};
inline void Configure(const std::wstring& base){ini=base+L"GraphicsEffects.ini";for(auto& i:items){if(i.kind==KIND_HEADER)continue;i.value=std::clamp(static_cast<int>(GetPrivateProfileIntW(i.section,i.key,i.value,ini.c_str())),i.lo,i.hi);}}
inline void Save(Item& i){if(i.kind==KIND_HEADER)return;wchar_t b[32]{};_itow_s(i.value,b,10);WritePrivateProfileStringW(i.section,i.key,b,ini.c_str());}
inline const std::array<unsigned char,7>& Glyph(char c){
 static const std::array<unsigned char,7> blank{};
 static const std::array<unsigned char,7> dash={0,0,0,31,0,0,0};
 static const std::array<unsigned char,7> slash={1,2,4,8,16,0,0};
 static const std::array<unsigned char,7> amp={12,18,12,10,18,19,13};
 static const std::array<unsigned char,7> glyphs[]={
  {14,17,17,31,17,17,17},{30,17,17,30,17,17,30},{14,17,16,16,16,17,14},{30,17,17,17,17,17,30},
  {31,16,16,30,16,16,31},{31,16,16,30,16,16,16},{14,17,16,23,17,17,15},{17,17,17,31,17,17,17},
  {14,4,4,4,4,4,14},{7,2,2,2,2,18,12},{17,18,20,24,20,18,17},{16,16,16,16,16,16,31},
  {17,27,21,21,17,17,17},{17,25,21,19,17,17,17},{14,17,17,17,17,17,14},{30,17,17,30,16,16,16},
  {14,17,17,17,21,18,13},{30,17,17,30,20,18,17},{15,16,16,14,1,1,30},{31,4,4,4,4,4,4},
  {17,17,17,17,17,17,14},{17,17,17,17,17,10,4},{17,17,17,21,21,21,10},{17,17,10,4,10,17,17},
  {17,17,10,4,4,4,4},{31,1,2,4,8,16,31},
  {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},{30,1,1,14,1,1,30},
  {2,6,10,18,31,2,2},{31,16,16,30,1,1,30},{14,16,16,30,17,17,14},{31,1,2,4,8,8,8},
  {14,17,17,14,17,17,14},{14,17,17,15,1,1,14}
 };
 if(c>='A'&&c<='Z')return glyphs[c-'A'];if(c>='0'&&c<='9')return glyphs[26+c-'0'];
 if(c=='-')return dash;if(c=='/')return slash;if(c=='&')return amp;return blank;
}
struct V {float x,y,z,w;DWORD color;};
inline void Rect(std::vector<V>& out,float x,float y,float w,float h,DWORD color){V a{x,y,0,1,color},b{x+w,y,0,1,color},c{x,y+h,0,1,color},d{x+w,y+h,0,1,color};out.insert(out.end(),{a,b,c,c,b,d});}
inline void Text(std::vector<V>& out,float x,float y,const char* s,DWORD color,float scale=2.f){for(;*s;++s,x+=6*scale){if(*s==' '){continue;}auto& g=Glyph(*s);for(int yy=0;yy<7;++yy)for(int xx=0;xx<5;++xx)if(g[yy]&(16>>xx))Rect(out,x+xx*scale,y+yy*scale,scale,scale,color);}}
inline bool Update(){
 DWORD pid=0;HWND window=GetForegroundWindow();GetWindowThreadProcessId(window,&pid);bool focused=pid==GetCurrentProcessId();
 bool f7=(GetAsyncKeyState(VK_F7)&0x8000)!=0;if(focused&&f7&&!f7Down)visible=!visible;f7Down=f7;if(!visible||!focused){hover=-1;dragItem=-1;return false;}
 POINT p{};GetCursorPos(&p);ScreenToClient(window,&p);constexpr int x=160,y=42,w=550,row=21;
 bool down=(GetAsyncKeyState(VK_LBUTTON)&0x8000)!=0;bool changed=false;
 if(!down)dragItem=-1;
 int curHover=(p.x>=x&&p.x<x+w&&p.y>=y+34)?(p.y-(y+34))/row:-1;
 if(curHover<0||curHover>=static_cast<int>(items.size())||items[curHover].kind==KIND_HEADER)curHover=-1;
 hover=dragItem>=0?dragItem:curHover;
 if(down){
  if(!mouseDown&&curHover>=0){
   auto& i=items[curHover];
   if(i.kind==KIND_TOGGLE){i.value=!i.value;Save(i);changed=true;}
   else if(i.kind==KIND_SLIDER){dragItem=curHover;}
  }
  int activeIndex=dragItem>=0?dragItem:((!mouseDown&&curHover>=0&&items[curHover].kind==KIND_SLIDER)?curHover:-1);
  if(activeIndex>=0){
   auto& i=items[activeIndex];
   float t=std::clamp((p.x-(x+265))/245.f,0.f,1.f);
   int newVal=i.lo+int((i.hi-i.lo)*t/i.step+.5f)*i.step;
   newVal=std::clamp(newVal,i.lo,i.hi);
   if(newVal!=i.value){i.value=newVal;Save(i);changed=true;}
  }
 }
 mouseDown=down;return changed;
}
inline void Draw(IDirect3DDevice9* d){
 if(!visible||!d)return;IDirect3DStateBlock9* raw=nullptr;if(FAILED(d->CreateStateBlock(D3DSBT_ALL,&raw)))return;if(FAILED(raw->Capture())){raw->Release();return;}
 std::vector<V> v;constexpr float x=160,y=42,w=550,row=21;
 Rect(v,x,y,w,34+row*items.size()+10,0xe8121820);
 Rect(v,x,y,w,32,0xff1b3340);
 Text(v,x+14,y+8,"MODERN WOW RENDERER   F7 CLOSE",0xff8effbb,1.8f);
 for(size_t n=0;n<items.size();++n){
  auto& i=items[n];float yy=y+34+float(n)*row;
  if(i.kind==KIND_HEADER){
   Rect(v,x,yy,w,row-1,0xff162836);
   Text(v,x+14,yy+4,i.label,0xffffdc82,1.5f);
  }else{
   DWORD fg=int(n)==hover?0xffffffff:0xffc8d4da;
   Text(v,x+14,yy+4,i.label,fg,1.4f);
   if(i.kind==KIND_TOGGLE){
    Rect(v,x+445,yy+3,78,15,i.value?0xff246b47:0xff4a3232);
    Text(v,x+468,yy+4,i.value?"ON":"OFF",i.value?0xff9dffc0:0xffffaaaa,1.4f);
   }else if(i.kind==KIND_SLIDER){
    Rect(v,x+265,yy+6,245,9,0xff26343b);
    float t=float(i.value-i.lo)/float(std::max(1,i.hi-i.lo));
    Rect(v,x+265,yy+6,245*t,9,0xff45c985);
    char b[16]{};_itoa_s(i.value,b,10);
    Text(v,x+210,yy+4,b,0xffffdc82,1.4f);
   }
  }
 }
 d->SetVertexShader(nullptr);d->SetPixelShader(nullptr);d->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE);d->SetTexture(0,nullptr);
 d->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1);d->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_DIFFUSE);
 d->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1);d->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_DIFFUSE);
 d->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE);d->SetRenderState(D3DRS_ZENABLE,FALSE);d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE);
 d->SetRenderState(D3DRS_LIGHTING,FALSE);d->SetRenderState(D3DRS_FOGENABLE,FALSE);d->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE);
 d->SetRenderState(D3DRS_STENCILENABLE,FALSE);d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);
 d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE);d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA);d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);
 d->SetRenderState(D3DRS_COLORWRITEENABLE,15);d->DrawPrimitiveUP(D3DPT_TRIANGLELIST,UINT(v.size()/3),v.data(),sizeof(V));
 raw->Apply();raw->Release();
}
}
