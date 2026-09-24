#pragma once
#include "VolumeEffects.h"
#include <cstdio>
#include <unordered_set>
#include <unordered_map>
namespace volume {
using Microsoft::WRL::ComPtr;
using SetDepthFn=HRESULT(WINAPI*)(IDirect3DDevice9*,IDirect3DSurface9*);
using GetDepthFn=HRESULT(WINAPI*)(IDirect3DDevice9*,IDirect3DSurface9**);
SetDepthFn setDepth=nullptr; GetDepthFn getDepth=nullptr;
bool enabled=false,active=true,ready=false,composed=false,internal=false;
bool fogEffectEnabled=true,shaftsEffectEnabled=true,shadowsEffectEnabled=true,shadowMapEnabled=false,cloudShadowsEffectEnabled=true,temporalShaftsEnabled=false;
IDirect3DDevice9* owner=nullptr;
// Do not release D3D resources from DLL static destructors under the loader lock
// when a process exits mid-frame. Finish releases them during normal operation;
// the tiny holder itself intentionally lives until process teardown.
struct FrameResources{ComPtr<IDirect3DTexture9> depth,noise,rayA,rayB,localA,localB,scene,rayHistory,shadowDepth,shadowColor;ComPtr<IDirect3DSurface9> surface,originalDepth,target,rayASurface,rayBSurface,localASurface,localBSurface,sceneSurface,rayHistorySurface,shadowDepthSurface,shadowColorSurface;IDirect3DDevice9* noiseOwner=nullptr;IDirect3DDevice9* historyOwner=nullptr;IDirect3DDevice9* shadowOwner=nullptr;UINT historyWidth=0,historyHeight=0,shadowSize=0;bool historyValid=false;};
FrameResources& resources=*new FrameResources;
auto& depth=resources.depth;auto& surface=resources.surface;
auto& originalDepth=resources.originalDepth;auto& target=resources.target;
 ComPtr<ID3DBlob> bytecode,blurBytecode,copyBytecode,temporalBytecode,localLightBytecode,radialBytecode,contactShadowBytecode,postProcessBytecode,rayCompositeBytecode;
float constants[13][4]{};
uint64_t cameraCaptureShaderHash=0;
float capturedViewTranslation[3]{};
bool capturedViewValid=false;
 float baseHeight=60,density=.004f,falloff=.07f,strength=2.6f,moonStrength=.35f,variation=1.35f,lowLayer=.35f,raySoftness=5.f,rayFalloff=2.f,fogWash=.45f,sunVerticalScale=.4f,sunOffsetX=0,sunOffsetY=0,localLightStrength=.8f,localLightThreshold=.25f,sunSourceThreshold=.60f;
 float contactShadowStrength=.28f,contactShadowRadius=10.f,directionalShadowStrength=.16f,shadowMapDistance=140.f,shadowMapBias=.0008f,shadowSoftness=1.6f,cloudShadowStrength=.06f,localLightRadius=140.f;
UINT configuredShadowMapSize=1024;
int brightnessPercent=0,contrastPercent=100,gammaPercent=100,sharpnessPercent=35;
int sunGlowPercent=150;
float shadowMatrix[4][4]{},viewToShadowMatrix[4][4]{},shadowView[4][4]{},shadowProjection[4][4]{};
bool shadowFrameStarted=false,shadowFrameValid=false;
bool stableShadowLightValid=false;
float stableShadowLight[3]{};
bool shadowCacheValid=false,shadowAnchorValid=false;
float shadowAnchor[3]{};
float celestialDaylight=0,celestialMoonlight=0;
float celestialShadowLight=0;
float smoothSunX=-1,smoothSunY=-1;
bool shaftDebug=false;
bool localLightDebug=false;
unsigned frames=0,applied=0,depthFrames=0,cameraFrames=0,shadowDraws=0,shadowFrameDraws=0;
std::wstring logPath;
std::wstring mainIni,tuningIni;
void Log(const char* s){std::ofstream(std::filesystem::path(logPath),std::ios::app)<<s<<'\n';}
int ReadTuning(const wchar_t* key,int fallback,const wchar_t* section=L"Atmosphere"){wchar_t value[64]{};GetPrivateProfileStringW(section,key,L"",value,std::size(value),tuningIni.c_str());return value[0]?int(wcstol(value,nullptr,10)):GetPrivateProfileIntW(section,key,fallback,mainIni.c_str());}
void ReloadTuning(){
 fogEffectEnabled=ReadTuning(L"FogEnabled",1)!=0;shaftsEffectEnabled=ReadTuning(L"ShaftsEnabled",1)!=0;
 shadowsEffectEnabled=ReadTuning(L"ShadowsEnabled",1)!=0;shadowMapEnabled=ReadTuning(L"ShadowMapEnabled",1)!=0;cloudShadowsEffectEnabled=ReadTuning(L"CloudShadowsEnabled",1)!=0;temporalShaftsEnabled=ReadTuning(L"TemporalShaftsEnabled",0)!=0;
 baseHeight=float(ReadTuning(L"BaseHeight",60));density=std::clamp(ReadTuning(L"DensityPermille",4),0,20)*.001f;
 strength=std::clamp(ReadTuning(L"ShaftPercent",160),0,500)*.01f;variation=std::clamp(ReadTuning(L"VariationPercent",135),0,250)*.01f;
 moonStrength=std::clamp(ReadTuning(L"MoonShaftPercent",35),0,100)*.01f;
 lowLayer=std::clamp(ReadTuning(L"LowLayerPercent",35),0,150)*.01f;raySoftness=float(std::clamp(ReadTuning(L"ShaftSoftnessPixels",5),0,16));
 rayFalloff=std::clamp(ReadTuning(L"ShaftFalloffPercent",200),50,500)*.01f;
 fogWash=std::clamp(ReadTuning(L"FogWashPercent",35),0,100)*.01f;
 sunVerticalScale=std::clamp(ReadTuning(L"SunVerticalProjectionPercent",100),10,100)*.01f;
 sunOffsetX=std::clamp(ReadTuning(L"SunOffsetXPercent",0),-50,50)*.01f;sunOffsetY=std::clamp(ReadTuning(L"SunOffsetYPercent",0),-50,50)*.01f;
  localLightStrength=std::clamp(ReadTuning(L"LocalLightPercent",80),0,300)*.01f;localLightThreshold=std::clamp(ReadTuning(L"LocalLightThresholdPercent",25),5,95)*.01f;
  localLightRadius=float(std::clamp(ReadTuning(L"LocalLightRadiusPixels",140),40,260));
  sunSourceThreshold=std::clamp(ReadTuning(L"SunSourceThresholdPercent",60),10,95)*.01f;
  contactShadowStrength=std::clamp(ReadTuning(L"ContactShadowPercent",25),0,70)*.01f;
  contactShadowRadius=float(std::clamp(ReadTuning(L"ContactShadowRadiusPixels",10),2,24));
  directionalShadowStrength=std::clamp(ReadTuning(L"DirectionalShadowPercent",45),0,100)*.01f;
  shadowMapDistance=float(std::clamp(ReadTuning(L"ShadowMapDistance",ReadTuning(L"ShadowReach",180)),50,300));
  configuredShadowMapSize=UINT(std::clamp(ReadTuning(L"ShadowMapSize",1024),512,4096));
  brightnessPercent=std::clamp(ReadTuning(L"BrightnessPercent",0,L"PostProcess"),-50,50);
  contrastPercent=std::clamp(ReadTuning(L"ContrastPercent",100,L"PostProcess"),50,180);
  gammaPercent=std::clamp(ReadTuning(L"GammaPercent",100,L"PostProcess"),50,180);
  sharpnessPercent=std::clamp(ReadTuning(L"SharpnessPercent",35,L"PostProcess"),0,100);
  sunGlowPercent=std::clamp(ReadTuning(L"SunGlowPercent",80),0,300);
 shaftDebug=ReadTuning(L"ShaftDebugMask",0)!=0;
 localLightDebug=ReadTuning(L"LocalLightDebugMask",0)!=0;
  if(!logPath.empty()){std::ofstream out(std::filesystem::path(logPath),std::ios::app);out<<"tuning shaft="<<strength<<" moon="<<moonStrength<<" softness="<<raySoftness<<" falloff="<<rayFalloff<<" sunThreshold="<<sunSourceThreshold<<" local="<<localLightStrength<<" threshold="<<localLightThreshold<<" localRadius="<<localLightRadius<<" density="<<density<<" fogWash="<<fogWash<<" baseHeight="<<baseHeight<<" variation="<<variation<<" lowLayer="<<lowLayer<<" contact="<<contactShadowStrength<<','<<contactShadowRadius<<" shadowmap="<<shadowMapEnabled<<','<<directionalShadowStrength<<','<<configuredShadowMapSize<<','<<shadowMapDistance<<','<<shadowMapBias<<" cloud="<<cloudShadowStrength<<" sunYScale="<<sunVerticalScale<<" offset="<<sunOffsetX<<','<<sunOffsetY<<" debug="<<shaftDebug<<" localDebug="<<localLightDebug<<" bright="<<brightnessPercent<<" contrast="<<contrastPercent<<" gamma="<<gammaPercent<<" sharp="<<sharpnessPercent<<'\n';}
}
void Configure(const std::wstring& base){
 mainIni=base+L"ModernWoWRenderer.ini";tuningIni=base+L"GraphicsEffects.ini";logPath=base+L"VolumeEffects.log";
 enabled=GetPrivateProfileIntW(L"Volume",L"Enabled",0,mainIni.c_str())!=0;ReloadTuning();
 if(!enabled)return;
 ComPtr<ID3DBlob> errors;
 HRESULT hr=D3DCompile(volumePixelSource,strlen(volumePixelSource),nullptr,nullptr,nullptr,"main","ps_3_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,bytecode.GetAddressOf(),errors.ReleaseAndGetAddressOf());
 if(SUCCEEDED(hr))hr=D3DCompile(volumeBlurPixelSource,strlen(volumeBlurPixelSource),nullptr,nullptr,nullptr,"main","ps_3_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,blurBytecode.GetAddressOf(),errors.ReleaseAndGetAddressOf());
 if(SUCCEEDED(hr))hr=D3DCompile(volumeCopyPixelSource,strlen(volumeCopyPixelSource),nullptr,nullptr,nullptr,"main","ps_3_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,copyBytecode.GetAddressOf(),errors.ReleaseAndGetAddressOf());
 if(SUCCEEDED(hr))hr=D3DCompile(volumeTemporalPixelSource,strlen(volumeTemporalPixelSource),nullptr,nullptr,nullptr,"main","ps_3_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,temporalBytecode.GetAddressOf(),errors.ReleaseAndGetAddressOf());
 if(SUCCEEDED(hr))hr=D3DCompile(localLightPixelSource,strlen(localLightPixelSource),nullptr,nullptr,nullptr,"main","ps_3_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,localLightBytecode.GetAddressOf(),errors.ReleaseAndGetAddressOf());
 if(SUCCEEDED(hr))hr=D3DCompile(solarRadialPixelSource,strlen(solarRadialPixelSource),nullptr,nullptr,nullptr,"main","ps_3_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,radialBytecode.GetAddressOf(),errors.ReleaseAndGetAddressOf());
 if(SUCCEEDED(hr))hr=D3DCompile(contactShadowPixelSource,strlen(contactShadowPixelSource),nullptr,nullptr,nullptr,"main","ps_3_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,contactShadowBytecode.GetAddressOf(),errors.ReleaseAndGetAddressOf());
 if(SUCCEEDED(hr))hr=D3DCompile(postProcessPixelSource,strlen(postProcessPixelSource),nullptr,nullptr,nullptr,"main","ps_3_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,postProcessBytecode.GetAddressOf(),errors.ReleaseAndGetAddressOf());
 if(SUCCEEDED(hr))hr=D3DCompile(rayCompositePixelSource,strlen(rayCompositePixelSource),nullptr,nullptr,nullptr,"main","ps_3_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,rayCompositeBytecode.GetAddressOf(),errors.ReleaseAndGetAddressOf());
 if(FAILED(hr)){enabled=false;Log("compile failed; volume disabled");}
}
// Frame-scoped DEFAULT resources: never keep a depth reference across Present/Reset.
void Finish(IDirect3DDevice9* d){
 if(owner&&owner!=d)return;
 if(owner){ComPtr<IDirect3DSurface9> current;if(SUCCEEDED(getDepth(d,current.GetAddressOf()))&&current.Get()==surface.Get())setDepth(d,originalDepth.Get());}
 resources.sceneSurface.Reset();resources.scene.Reset();resources.localASurface.Reset();resources.localBSurface.Reset();resources.localA.Reset();resources.localB.Reset();resources.rayASurface.Reset();resources.rayBSurface.Reset();resources.rayA.Reset();resources.rayB.Reset();
 surface.Reset();depth.Reset();originalDepth.Reset();target.Reset();owner=nullptr;ready=false;composed=false;
}
void Reset(IDirect3DDevice9* d){
 Finish(d);resources.rayHistorySurface.Reset();resources.rayHistory.Reset();resources.historyOwner=nullptr;resources.historyWidth=resources.historyHeight=0;resources.historyValid=false;resources.shadowDepthSurface.Reset();resources.shadowColorSurface.Reset();resources.shadowDepth.Reset();resources.shadowColor.Reset();resources.shadowOwner=nullptr;resources.shadowSize=0;shadowFrameStarted=shadowFrameValid=false;stableShadowLightValid=false;shadowCacheValid=shadowAnchorValid=false;
}
HRESULT WINAPI SetDepth(IDirect3DDevice9* d,IDirect3DSurface9* s){return setDepth(d,owner==d&&s&&s==originalDepth.Get()?surface.Get():s);}
HRESULT WINAPI GetDepth(IDirect3DDevice9* d,IDirect3DSurface9** out){
 HRESULT hr=getDepth(d,out);
 if(SUCCEEDED(hr)&&out&&owner==d&&*out==surface.Get()&&originalDepth){(*out)->Release();*out=originalDepth.Get();(*out)->AddRef();}
 return hr;
}
void BeforeClear(IDirect3DDevice9* d,DWORD count,DWORD flags,float z){
 if(!enabled||!active||internal||!(flags&D3DCLEAR_ZBUFFER))return;
 if(owner==d){ComPtr<IDirect3DSurface9> current;if(SUCCEEDED(getDepth(d,current.GetAddressOf()))&&current.Get()==surface.Get())ready=false;return;}
 if(count||z!=1||owner)return;
 ComPtr<IDirect3DSurface9> rt,back,ds;
 D3DSURFACE_DESC desc{},rd{};D3DVIEWPORT9 vp{};
 if(FAILED(d->GetRenderTarget(0,rt.GetAddressOf()))||FAILED(d->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,back.GetAddressOf()))||rt.Get()!=back.Get())return;
 if(FAILED(getDepth(d,ds.GetAddressOf()))||!ds||FAILED(ds->GetDesc(&desc))||FAILED(rt->GetDesc(&rd))||FAILED(d->GetViewport(&vp)))return;
 if(desc.MultiSampleType!=D3DMULTISAMPLE_NONE||desc.Width!=rd.Width||desc.Height!=rd.Height||vp.X||vp.Y||vp.Width!=rd.Width||vp.Height!=rd.Height)return;
 if(desc.Format!=D3DFMT_D24X8&&desc.Format!=D3DFMT_D24S8)return;
 ComPtr<IDirect3DTexture9> tex;ComPtr<IDirect3DSurface9> replacement;
 if(FAILED(d->CreateTexture(desc.Width,desc.Height,1,D3DUSAGE_DEPTHSTENCIL,static_cast<D3DFORMAT>(MAKEFOURCC('I','N','T','Z')),D3DPOOL_DEFAULT,tex.GetAddressOf(),nullptr))||FAILED(tex->GetSurfaceLevel(0,replacement.GetAddressOf()))||FAILED(setDepth(d,replacement.Get())))return;
 depth=tex;surface=replacement;originalDepth=ds;target=rt;owner=d;++depthFrames;
 shadowFrameStarted=false;shadowFrameValid=false;shadowFrameDraws=0;
}
template<class T> uint64_t Hash(T* shader){
 if(!shader)return 0;
 // The game reuses the same handful of compiled shader objects for essentially
 // every draw call of a given material; re-fetching and re-hashing the full
 // bytecode (a heap alloc + byte-by-byte FNV pass) on every single opaque draw
 // call, every frame, was the dominant per-frame cost once the shadow pass
 // stopped capping how many casters it inspects. Cache by the raw COM pointer:
 // the game keeps these shader objects alive for its own lifetime, so the
 // pointer is a stable key for as long as it is ever passed back to us.
 static std::unordered_map<void*,uint64_t> cache;
 auto it=cache.find(static_cast<void*>(shader));if(it!=cache.end())return it->second;
 UINT size=0;if(FAILED(shader->GetFunction(nullptr,&size))||!size||size>65536){cache.emplace(static_cast<void*>(shader),0);return 0;}
 std::vector<BYTE> bytes(size);if(FAILED(shader->GetFunction(bytes.data(),&size))){cache.emplace(static_cast<void*>(shader),0);return 0;}
 uint64_t h=14695981039346656037ull;for(BYTE b:bytes){h^=b;h*=1099511628211ull;}
 cache.emplace(static_cast<void*>(shader),h);return h;
}
uint32_t NoiseHash(int x,int y){uint32_t h=uint32_t(x)*374761393u+uint32_t(y)*668265263u;h=(h^(h>>13))*1274126177u;return h^(h>>16);}
float SmoothNoise(float x,float y,int cells){
 int x0=int(floor(x)),y0=int(floor(y));float fx=x-x0,fy=y-y0;fx=fx*fx*(3-2*fx);fy=fy*fy*(3-2*fy);
 auto sample=[&](int sx,int sy){return float(NoiseHash(sx&(cells-1),sy&(cells-1))&65535u)/65535.f;};
 float a=sample(x0,y0),b=sample(x0+1,y0),c=sample(x0,y0+1),e=sample(x0+1,y0+1);
 return (a+(b-a)*fx)*(1-fy)+(c+(e-c)*fx)*fy;
}
bool EnsureNoise(IDirect3DDevice9* d){
 if(resources.noise&&resources.noiseOwner==d)return true;
 resources.noise.Reset();resources.noiseOwner=nullptr;
 ComPtr<IDirect3DTexture9> texture;if(FAILED(d->CreateTexture(128,128,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,texture.GetAddressOf(),nullptr)))return false;
 D3DLOCKED_RECT locked{};if(FAILED(texture->LockRect(0,&locked,nullptr,0)))return false;
 for(int y=0;y<128;++y)for(int x=0;x<128;++x){float sum=0,weight=0,amplitude=1;
  for(int octave=0;octave<4;++octave){int cells=4<<octave;sum+=SmoothNoise(x*cells/128.f,y*cells/128.f,cells)*amplitude;weight+=amplitude;amplitude*=.52f;}
  BYTE n=BYTE(std::clamp(sum/weight,0.f,1.f)*255+.5f);*reinterpret_cast<DWORD*>(static_cast<BYTE*>(locked.pBits)+y*locked.Pitch+x*4)=D3DCOLOR_ARGB(255,n,n,n);
 }
 texture->UnlockRect(0);resources.noise=texture;resources.noiseOwner=d;return true;
}
bool CaptureCamera(IDirect3DDevice9* d){
 float v[27][4]{};D3DVIEWPORT9 vp{};
 if(FAILED(d->GetVertexShaderConstantF(0,v[0],27))||FAILED(d->GetViewport(&vp))||vp.MinZ!=0||vp.MaxZ<=0)return false;
 for(auto& row:v)for(float f:row)if(!std::isfinite(f))return false;
 // Only the captured standard perspective projection and rigid world-view.
 if(v[4][0]<=0||v[5][1]<=0||v[6][2]<=1||v[7][2]>=0||fabs(v[6][3]-1)>.001f||fabs(v[7][3])>.001f)return false;
 for(int i=0;i<3;++i)for(int j=0;j<3;++j){float dot=0;for(int k=0;k<3;++k)dot+=v[i][k]*v[j][k];if(fabs(dot-(i==j?1.f:0.f))>.002f)return false;}
 memset(constants,0,sizeof(constants));
 constants[0][0]=v[6][2];constants[0][1]=v[7][2];constants[0][2]=v[4][0];constants[0][3]=v[5][1];
 constants[1][0]=baseHeight;constants[1][1]=falloff;constants[1][2]=density;constants[1][3]=350;
  float directLuminance=v[26][0]*.2126f+v[26][1]*.7152f+v[26][2]*.0722f;
  // Moonlight in this client is distinctly blue. A brightness-only source
  // test mistakes the white moon disc for the sun and projects rays toward the
  // (hidden) solar direction when the camera pitches. Gate sky shafts by the
  // actual directional-light colour; neutral/warm daylight remains accepted.
  float daylightBrightness=std::clamp((directLuminance-.18f)*4.f,0.f,1.f);
  float daylightTint=std::clamp((v[26][0]-v[26][2])*4.f+.35f,0.f,1.f);
  float daylight=daylightBrightness*daylightTint;celestialDaylight=daylight;
  float moonBrightness=std::clamp((directLuminance-.12f)*4.f,0.f,1.f);
  float moonTint=std::clamp((v[26][2]-v[26][0])*6.f,0.f,1.f);
  float moonlight=moonBrightness*moonTint*(1-daylight);celestialMoonlight=moonlight;
  // Zone palettes can be strongly blue even in broad daylight. Shadows depend
  // on directional-light energy, not on that colour-temperature heuristic.
  celestialShadowLight=std::clamp((directLuminance-.08f)*3.8f,.18f,1.f);
  constants[2][0]=-1;constants[2][1]=-1;constants[2][2]=0;constants[2][3]=vp.MaxZ;
  float lightLen=std::sqrt(v[24][0]*v[24][0]+v[24][1]*v[24][1]+v[24][2]*v[24][2]);
  if(lightLen>1e-4f){
   // In WoW, v[24] represents directional light propagation in view space.
   // The vector pointing TOWARDS the celestial sun/moon in view space is:
   // +X (right), +Y (up towards sky), +Z (forward in front of camera).
   float lx = -v[24][0] / lightLen;
   float ly = -v[24][1] / lightLen;
   float lz =  v[24][2] / lightLen;
   constants[10][0]=lx; constants[10][1]=ly; constants[10][2]=lz; constants[10][3]=0;
   // In view space, +Z is in front of the camera. The celestial body is in front when lz > 0.02.
   if(lz > 0.02f){
    float projX = (lx * v[4][0]) / lz;
    float projY = (ly * v[5][1]) / lz;
    float targetX = 0.5f + 0.5f * projX + sunOffsetX;
    float targetY = 0.5f - 0.5f * projY * sunVerticalScale + sunOffsetY;
    constants[2][0] = targetX;
    constants[2][1] = targetY;
    smoothSunX = targetX;
    smoothSunY = targetY;
    float facing = std::clamp((lz + 0.05f) * 2.5f, 0.0f, 1.0f);
    if(targetX < -0.50f || targetX > 1.50f || targetY < -0.50f || targetY > 1.50f) facing = 0.0f;
    constants[2][2] = strength * (daylight + moonStrength * moonlight) * facing;
   }else{
    smoothSunX = smoothSunY = -1;
   }
  }else{
   smoothSunX = smoothSunY = -1;
   constants[10][0] = 0; constants[10][1] = 0; constants[10][2] = 1; constants[10][3] = 0;
  }
 for(int i=0;i<3;++i){
  float horizon=v[25][i]*1.5f+v[26][i]*0.30f;
  constants[3][i]=std::clamp(horizon,0.12f,0.95f);
 }
 for(int i=0;i<3;++i)for(int j=0;j<3;++j)constants[4+i][j]=v[j][i];
 for(int i=0;i<3;++i)for(int j=0;j<3;++j)constants[7][i]-=v[3][j]*v[i][j];
 constants[7][3]=1;constants[8][0]=3;constants[8][1]=fogWash;constants[8][2]=shaftDebug?1.f:0.f;
 capturedViewTranslation[0]=v[3][0];capturedViewTranslation[1]=v[3][1];capturedViewTranslation[2]=v[3][2];capturedViewValid=true;
 constants[9][0]=float(GetTickCount64()%1200000)*.001f;constants[9][1]=.018f;constants[9][2]=variation;constants[9][3]=lowLayer;
  for(int i=0;i<3;++i) constants[11][i]=v[26][i];
 constants[12][0]=1.f/vp.Width;constants[12][1]=1.f/vp.Height;constants[12][2]=raySoftness;constants[12][3]=rayFalloff;
  static unsigned logged=0;if(logged++<32||frames%180==0){char hashHex[24];sprintf_s(hashHex,"0x%016llX",static_cast<unsigned long long>(cameraCaptureShaderHash));std::ofstream out(std::filesystem::path(logPath),std::ios::app);out<<"light="<<v[24][0]<<','<<v[24][1]<<','<<v[24][2]<<" viewSun="<<constants[10][0]<<','<<constants[10][1]<<','<<constants[10][2]<<" direct="<<v[26][0]<<','<<v[26][1]<<','<<v[26][2]<<" daylight="<<daylight<<" moonlight="<<moonlight<<" sourceUV="<<constants[2][0]<<','<<constants[2][1]<<" strength="<<constants[2][2]<<" capturedBy="<<hashHex<<'\n';}
 return true;
}
struct Vec3{float x,y,z;};
Vec3 Add(Vec3 a,Vec3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
Vec3 Mul(Vec3 a,float s){return {a.x*s,a.y*s,a.z*s};}
float Dot(Vec3 a,Vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
Vec3 Cross(Vec3 a,Vec3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
Vec3 Normalize(Vec3 a){float n=sqrtf(std::max(Dot(a,a),1e-8f));return Mul(a,1.f/n);}
bool EnsureShadowResources(IDirect3DDevice9* d){
 if(resources.shadowOwner==d&&resources.shadowSize==configuredShadowMapSize&&resources.shadowDepth&&resources.shadowColor&&resources.shadowDepthSurface&&resources.shadowColorSurface)return true;
 resources.shadowDepthSurface.Reset();resources.shadowColorSurface.Reset();resources.shadowDepth.Reset();resources.shadowColor.Reset();resources.shadowOwner=nullptr;resources.shadowSize=0;shadowCacheValid=false;
 if(FAILED(d->CreateTexture(configuredShadowMapSize,configuredShadowMapSize,1,D3DUSAGE_DEPTHSTENCIL,static_cast<D3DFORMAT>(MAKEFOURCC('I','N','T','Z')),D3DPOOL_DEFAULT,resources.shadowDepth.GetAddressOf(),nullptr))||
    FAILED(resources.shadowDepth->GetSurfaceLevel(0,resources.shadowDepthSurface.GetAddressOf()))||
    FAILED(d->CreateTexture(configuredShadowMapSize,configuredShadowMapSize,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,resources.shadowColor.GetAddressOf(),nullptr))||
    FAILED(resources.shadowColor->GetSurfaceLevel(0,resources.shadowColorSurface.GetAddressOf())))return false;
 resources.shadowOwner=d;resources.shadowSize=configuredShadowMapSize;return true;
}
void BuildShadowCamera(){
 Vec3 camera{constants[7][0],constants[7][1],constants[7][2]};
 Vec3 invX{constants[4][0],constants[4][1],constants[4][2]},invY{constants[5][0],constants[5][1],constants[5][2]},invZ{constants[6][0],constants[6][1],constants[6][2]};
 Vec3 lightView{constants[10][0],constants[10][1],constants[10][2]};
 Vec3 measuredLight=Normalize(Add(Add(Mul(invX,lightView.x),Mul(invY,lightView.y)),Mul(invZ,lightView.z)));
 if(measuredLight.z < 0.04f) measuredLight.z = 0.04f;
 measuredLight = Normalize(measuredLight);
 Vec3 towardLight=measuredLight;
 if(!stableShadowLightValid){
  stableShadowLight[0]=measuredLight.x;stableShadowLight[1]=measuredLight.y;stableShadowLight[2]=measuredLight.z;stableShadowLightValid=true;
 } else {
  Vec3 stable{stableShadowLight[0],stableShadowLight[1],stableShadowLight[2]};
  if(Dot(stable,measuredLight)<0.999f){
   stableShadowLight[0]=measuredLight.x;stableShadowLight[1]=measuredLight.y;stableShadowLight[2]=measuredLight.z;
  } else towardLight=stable;
 }
 Vec3 forward=Mul(towardLight,-1.f),reference=fabsf(forward.z)>.94f?Vec3{0,1,0}:Vec3{0,0,1};
 Vec3 right=Normalize(Cross(reference,forward)),up=Normalize(Cross(forward,right));
 float halfExtent=shadowMapDistance*0.95f;
 float texelWorld=(halfExtent*2.f)/float(std::max<UINT>(configuredShadowMapSize,1));
 Vec3 center = camera;
 float alongRight=Dot(center,right),alongUp=Dot(center,up);
 center=Add(center,Mul(right,roundf(alongRight/texelWorld)*texelWorld-alongRight));
 center=Add(center,Mul(up,roundf(alongUp/texelWorld)*texelWorld-alongUp));
 Vec3 eye=Add(center,Mul(towardLight,shadowMapDistance*2.0f));
 memset(shadowView,0,sizeof(shadowView));
 shadowView[0][0]=right.x;shadowView[0][1]=up.x;shadowView[0][2]=forward.x;
 shadowView[1][0]=right.y;shadowView[1][1]=up.y;shadowView[1][2]=forward.y;
 shadowView[2][0]=right.z;shadowView[2][1]=up.z;shadowView[2][2]=forward.z;
 shadowView[3][0]=-Dot(right,eye);shadowView[3][1]=-Dot(up,eye);shadowView[3][2]=-Dot(forward,eye);shadowView[3][3]=1;
 float nearPlane=1.f,farPlane=shadowMapDistance*4.5f;
 memset(shadowProjection,0,sizeof(shadowProjection));
 shadowProjection[0][0]=1.f/halfExtent;shadowProjection[1][1]=1.f/halfExtent;shadowProjection[2][2]=1.f/(farPlane-nearPlane);shadowProjection[3][2]=-nearPlane/(farPlane-nearPlane);shadowProjection[3][3]=1;
 for(int column=0;column<4;++column)for(int row=0;row<4;++row){shadowMatrix[column][row]=0;for(int k=0;k<4;++k)shadowMatrix[column][row]+=shadowProjection[k][row]*shadowView[column][k];}
 float inverseView[4][4]={{constants[4][0],constants[4][1],constants[4][2],0},
                          {constants[5][0],constants[5][1],constants[5][2],0},
                          {constants[6][0],constants[6][1],constants[6][2],0},
                          {constants[7][0],constants[7][1],constants[7][2],1}};
 for(int column=0;column<4;++column)for(int row=0;row<4;++row){viewToShadowMatrix[column][row]=0;for(int k=0;k<4;++k)viewToShadowMatrix[column][row]+=shadowMatrix[k][row]*inverseView[column][k];}
}
template<class DrawCall> void ShadowDraw(IDirect3DDevice9* d,DrawCall&& draw){
 if(!enabled||!active||internal||!ready||!shadowsEffectEnabled||!shadowMapEnabled||directionalShadowStrength<=0||owner!=d)return;
 ComPtr<IDirect3DSurface9> rt,ds;if(FAILED(d->GetRenderTarget(0,rt.GetAddressOf()))||rt.Get()!=target.Get()||FAILED(getDepth(d,ds.GetAddressOf()))||ds.Get()!=surface.Get())return;
 DWORD zWrite=FALSE;if(FAILED(d->GetRenderState(D3DRS_ZWRITEENABLE,&zWrite))||!zWrite)return;
 DWORD alphaBlend=FALSE;if(FAILED(d->GetRenderState(D3DRS_ALPHABLENDENABLE,&alphaBlend))||alphaBlend)return;

 float original[9][4]{};if(FAILED(d->GetVertexShaderConstantF(0,original[0],9)))return;

 // M2 models (characters, trees, doodads, creatures, objects): row-major projection in c2..c5
 bool isM2 = (fabsf(original[5][2] - 1.0f) < 0.02f &&
              fabsf(original[5][0]) < 0.01f &&
              fabsf(original[5][1]) < 0.01f &&
              fabsf(original[5][3]) < 0.01f &&
              original[2][0] > 0.1f &&
              original[3][1] > 0.1f);

 // WMO objects and Terrain both use column-major projection in c4..c7
 bool hasProj4 = (original[4][0] > 0.1f &&
                  original[5][1] > 0.1f &&
                  fabsf(original[6][3] - 1.0f) < 0.02f &&
                  original[7][2] < -0.01f);

 ComPtr<IDirect3DVertexShader9> curVs;
 uint64_t casterHash = 0;
 if(hasProj4 && SUCCEEDED(d->GetVertexShader(curVs.GetAddressOf())) && curVs){
  casterHash = Hash(curVs.Get());
 }

 // Terrain chunk shaders have the camera's view translation in c3 identically matching capturedViewTranslation,
 // or match cameraCaptureShaderHash. WMO shaders have local object-to-view translation in c3.
 bool isTerrain = hasProj4 &&
                  ((cameraCaptureShaderHash != 0 && casterHash == cameraCaptureShaderHash) ||
                   (capturedViewValid &&
                    fabsf(original[3][0] - capturedViewTranslation[0]) < 0.01f &&
                    fabsf(original[3][1] - capturedViewTranslation[1]) < 0.01f &&
                    fabsf(original[3][2] - capturedViewTranslation[2]) < 0.01f));

 bool isWmo = hasProj4 && !isTerrain;

 if(!isM2 && !isWmo)return;

 if(!EnsureShadowResources(d))return;
 if(!shadowFrameStarted){
  BuildShadowCamera();shadowFrameStarted=true;
  ComPtr<IDirect3DStateBlock9> state;if(FAILED(d->CreateStateBlock(D3DSBT_ALL,state.GetAddressOf()))||FAILED(state->Capture()))return;
  internal=true;setDepth(d,nullptr);d->SetRenderTarget(0,resources.shadowColorSurface.Get());setDepth(d,resources.shadowDepthSurface.Get());
  D3DVIEWPORT9 vp{0,0,configuredShadowMapSize,configuredShadowMapSize,0,1};d->SetViewport(&vp);
  d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xffffffff,1,0);
  state->Apply();internal=false;shadowFrameValid=true;
 }
 if(!shadowFrameValid)return;
 D3DVIEWPORT9 oldVp{};d->GetViewport(&oldVp);DWORD colorWrite=0,zEnable=0,zFunc=0,fog=0,alphaTest=0,oldDepthBias=0,oldSlopeBias=0,oldCull=0;d->GetRenderState(D3DRS_COLORWRITEENABLE,&colorWrite);d->GetRenderState(D3DRS_ZENABLE,&zEnable);d->GetRenderState(D3DRS_ZFUNC,&zFunc);d->GetRenderState(D3DRS_FOGENABLE,&fog);d->GetRenderState(D3DRS_ALPHATESTENABLE,&alphaTest);d->GetRenderState(D3DRS_DEPTHBIAS,&oldDepthBias);d->GetRenderState(D3DRS_SLOPESCALEDEPTHBIAS,&oldSlopeBias);d->GetRenderState(D3DRS_CULLMODE,&oldCull);
 ComPtr<IDirect3DPixelShader9> originalPixelShader;d->GetPixelShader(originalPixelShader.GetAddressOf());
 internal=true;setDepth(d,nullptr);d->SetRenderTarget(0,resources.shadowColorSurface.Get());setDepth(d,resources.shadowDepthSurface.Get());D3DVIEWPORT9 shadowVp{0,0,configuredShadowMapSize,configuredShadowMapSize,0,1};d->SetViewport(&shadowVp);
 if(isWmo)d->SetVertexShaderConstantF(4,viewToShadowMatrix[0],4);
 else {float shadowRows[4][4]{};for(int row=0;row<4;++row)for(int column=0;column<4;++column)shadowRows[row][column]=viewToShadowMatrix[column][row];d->SetVertexShaderConstantF(2,shadowRows[0],4);}
 float rasterBias=.00045f,slopeBias=2.25f;DWORD rasterBiasBits=0,slopeBiasBits=0;memcpy(&rasterBiasBits,&rasterBias,sizeof(DWORD));memcpy(&slopeBiasBits,&slopeBias,sizeof(DWORD));
 d->SetRenderState(D3DRS_DEPTHBIAS,rasterBiasBits);d->SetRenderState(D3DRS_SLOPESCALEDEPTHBIAS,slopeBiasBits);
 d->SetRenderState(D3DRS_COLORWRITEENABLE,0);d->SetRenderState(D3DRS_ZENABLE,TRUE);d->SetRenderState(D3DRS_ZWRITEENABLE,TRUE);d->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL);d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);d->SetRenderState(D3DRS_FOGENABLE,FALSE);
 d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);
 if(!alphaTest)d->SetPixelShader(nullptr);
 HRESULT shadowHr=draw();if(SUCCEEDED(shadowHr)){++shadowFrameDraws;if(++shadowDraws==1)Log("uncapped depth-only light-space shadows active");}
 d->SetPixelShader(originalPixelShader.Get());d->SetVertexShaderConstantF(0,original[0],8);setDepth(d,nullptr);d->SetRenderTarget(0,rt.Get());setDepth(d,ds.Get());d->SetViewport(&oldVp);d->SetRenderState(D3DRS_COLORWRITEENABLE,colorWrite);d->SetRenderState(D3DRS_ZENABLE,zEnable);d->SetRenderState(D3DRS_ZWRITEENABLE,zWrite);d->SetRenderState(D3DRS_ZFUNC,zFunc);d->SetRenderState(D3DRS_ALPHABLENDENABLE,alphaBlend);d->SetRenderState(D3DRS_ALPHATESTENABLE,alphaTest);d->SetRenderState(D3DRS_DEPTHBIAS,oldDepthBias);d->SetRenderState(D3DRS_SLOPESCALEDEPTHBIAS,oldSlopeBias);d->SetRenderState(D3DRS_CULLMODE,oldCull);d->SetRenderState(D3DRS_FOGENABLE,fog);internal=false;
}
bool Composite(IDirect3DDevice9* d){
 static bool firstCompositeFinished=false;
 auto trace=[&](const char* message){if(!firstCompositeFinished)Log(message);};
 trace("composite:first begin");
 ComPtr<IDirect3DStateBlock9> state;ComPtr<IDirect3DSurface9> ds;
 ComPtr<IDirect3DPixelShader9> shader,blurShader,copyShader,temporalShader,radialShader,contactShadowShader,postShader,rayCompositeShader;
 if(FAILED(d->CreateStateBlock(D3DSBT_ALL,state.GetAddressOf()))||FAILED(state->Capture())||FAILED(getDepth(d,ds.GetAddressOf()))||
    FAILED(d->CreatePixelShader(static_cast<DWORD*>(bytecode->GetBufferPointer()),shader.GetAddressOf()))||
    FAILED(d->CreatePixelShader(static_cast<DWORD*>(blurBytecode->GetBufferPointer()),blurShader.GetAddressOf()))||
    FAILED(d->CreatePixelShader(static_cast<DWORD*>(copyBytecode->GetBufferPointer()),copyShader.GetAddressOf()))||
    FAILED(d->CreatePixelShader(static_cast<DWORD*>(temporalBytecode->GetBufferPointer()),temporalShader.GetAddressOf()))||!EnsureNoise(d))return false;
 if(FAILED(d->CreatePixelShader(static_cast<DWORD*>(radialBytecode->GetBufferPointer()),radialShader.GetAddressOf()))||
    FAILED(d->CreatePixelShader(static_cast<DWORD*>(contactShadowBytecode->GetBufferPointer()),contactShadowShader.GetAddressOf())))return false;
 if(postProcessBytecode)d->CreatePixelShader(static_cast<DWORD*>(postProcessBytecode->GetBufferPointer()),postShader.GetAddressOf());
 if(rayCompositeBytecode)d->CreatePixelShader(static_cast<DWORD*>(rayCompositeBytecode->GetBufferPointer()),rayCompositeShader.GetAddressOf());
 trace("composite:first shaders ready");
 D3DSURFACE_DESC desc{};if(FAILED(target->GetDesc(&desc)))return false;
 UINT rayWidth=std::max<UINT>(1,desc.Width/2),rayHeight=std::max<UINT>(1,desc.Height/2);
 resources.localASurface.Reset();resources.localBSurface.Reset();resources.localA.Reset();resources.localB.Reset();resources.rayASurface.Reset();resources.rayBSurface.Reset();resources.rayA.Reset();resources.rayB.Reset();
 if(FAILED(d->CreateTexture(rayWidth,rayHeight,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,resources.rayA.GetAddressOf(),nullptr))||
    FAILED(d->CreateTexture(rayWidth,rayHeight,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,resources.rayB.GetAddressOf(),nullptr))||
    FAILED(d->CreateTexture(rayWidth,rayHeight,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,resources.localA.GetAddressOf(),nullptr))||
    FAILED(d->CreateTexture(rayWidth,rayHeight,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,resources.localB.GetAddressOf(),nullptr))||
    FAILED(resources.rayA->GetSurfaceLevel(0,resources.rayASurface.GetAddressOf()))||FAILED(resources.rayB->GetSurfaceLevel(0,resources.rayBSurface.GetAddressOf()))||
    FAILED(resources.localA->GetSurfaceLevel(0,resources.localASurface.GetAddressOf()))||FAILED(resources.localB->GetSurfaceLevel(0,resources.localBSurface.GetAddressOf())))return false;
 trace("composite:first ray targets ready");
 bool newHistory=resources.historyOwner!=d||resources.historyWidth!=rayWidth||resources.historyHeight!=rayHeight||!resources.rayHistory||!resources.rayHistorySurface;
 if(newHistory){resources.rayHistorySurface.Reset();resources.rayHistory.Reset();resources.historyValid=false;
  if(FAILED(d->CreateTexture(rayWidth,rayHeight,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,resources.rayHistory.GetAddressOf(),nullptr))||FAILED(resources.rayHistory->GetSurfaceLevel(0,resources.rayHistorySurface.GetAddressOf())))return false;
  resources.historyOwner=d;resources.historyWidth=rayWidth;resources.historyHeight=rayHeight;
 }
 if(FAILED(d->CreateTexture(desc.Width,desc.Height,1,D3DUSAGE_RENDERTARGET,desc.Format,D3DPOOL_DEFAULT,resources.scene.GetAddressOf(),nullptr))||FAILED(resources.scene->GetSurfaceLevel(0,resources.sceneSurface.GetAddressOf()))||FAILED(d->StretchRect(target.Get(),nullptr,resources.sceneSurface.Get(),nullptr,D3DTEXF_NONE)))return false;
 trace("composite:first scene copied");
 internal=true;
 bool ok=true;
 // Every draw below is gated on `ok`, but the trace() lines above/below are not:
 // a single failed state call here used to silently blank out rays/water while
 // the log kept reporting "complete". Log the first failure, once, with its
 // source line, so a bad call is visible instead of just an all-white frame.
 auto check=[&](HRESULT hr,int line=__LINE__){if(FAILED(hr)){if(ok){ok=false;if(!firstCompositeFinished){char msg[96];sprintf_s(msg,"composite:FAILED line=%d hr=0x%08lX",line,static_cast<unsigned long>(hr));Log(msg);}}}};
 check(setDepth(d,nullptr));check(d->SetVertexShader(nullptr));check(d->SetPixelShader(shader.Get()));
 check(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));check(d->SetTexture(0,nullptr));check(d->SetTexture(1,depth.Get()));check(d->SetTexture(2,resources.noise.Get()));
 for(DWORD s=0;s<3;++s){check(d->SetSamplerState(s,D3DSAMP_MINFILTER,s==2?D3DTEXF_LINEAR:D3DTEXF_POINT));check(d->SetSamplerState(s,D3DSAMP_MAGFILTER,s==2?D3DTEXF_LINEAR:D3DTEXF_POINT));check(d->SetSamplerState(s,D3DSAMP_MIPFILTER,D3DTEXF_NONE));check(d->SetSamplerState(s,D3DSAMP_SRGBTEXTURE,FALSE));check(d->SetSamplerState(s,D3DSAMP_ADDRESSU,s==2?D3DTADDRESS_WRAP:D3DTADDRESS_CLAMP));check(d->SetSamplerState(s,D3DSAMP_ADDRESSV,s==2?D3DTADDRESS_WRAP:D3DTADDRESS_CLAMP));}
 for(D3DRENDERSTATETYPE s:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ALPHATESTENABLE,D3DRS_STENCILENABLE,D3DRS_SCISSORTESTENABLE,D3DRS_FOGENABLE,D3DRS_LIGHTING,D3DRS_SRGBWRITEENABLE})check(d->SetRenderState(s,FALSE));
 check(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));check(d->SetRenderState(D3DRS_COLORWRITEENABLE,7));
 check(d->SetRenderState(D3DRS_FILLMODE,D3DFILL_SOLID));check(d->SetRenderState(D3DRS_CLIPPLANEENABLE,0));
 check(d->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD));check(d->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE,FALSE));
 struct V{float x,y,z,w,u,v;};
 auto drawQuad=[&](UINT width,UINT height){float w=float(width)-.5f,h=float(height)-.5f;V quad[]={{-.5f,-.5f,0,1,0,0},{w,-.5f,0,1,1,0},{-.5f,h,0,1,0,1},{w,h,0,1,1,1}};check(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(V)));};
 D3DVIEWPORT9 fullVp{0,0,desc.Width,desc.Height,0,1};check(d->SetViewport(&fullVp));check(d->SetPixelShaderConstantF(0,constants[0],13));
  // Screen-space contact shadow is rendered before atmospheric fog so distant
  // shadow contrast naturally disappears into the medium.
  bool shadowsDrawn=false;
  if(!shaftDebug&&shadowsEffectEnabled&&contactShadowStrength>0){
   check(d->SetRenderTarget(0,resources.rayASurface.Get()));D3DVIEWPORT9 shadowVp{0,0,rayWidth,rayHeight,0,1};check(d->SetViewport(&shadowVp));
   check(d->SetPixelShader(contactShadowShader.Get()));check(d->SetTexture(0,depth.Get()));check(d->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_POINT));check(d->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_POINT));
   float shadowTuning[4]={1.f/desc.Width,1.f/desc.Height,contactShadowStrength,contactShadowRadius};
   float shadowProjection[4]={constants[0][0],constants[0][1],constants[2][3],0};
   check(d->SetPixelShaderConstantF(0,shadowTuning,1));check(d->SetPixelShaderConstantF(1,shadowProjection,1));check(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));if(ok)drawQuad(rayWidth,rayHeight);
   check(d->SetTexture(0,nullptr));check(d->SetPixelShader(blurShader.Get()));check(d->SetRenderTarget(0,resources.rayBSurface.Get()));check(d->SetTexture(0,resources.rayA.Get()));check(d->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_LINEAR));check(d->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR));
   float shadowBlur[4]={.8f/rayWidth,.8f/rayHeight,0,0};check(d->SetPixelShaderConstantF(0,shadowBlur,1));if(ok)drawQuad(rayWidth,rayHeight);
   check(d->SetTexture(0,nullptr));check(d->SetRenderTarget(0,target.Get()));check(d->SetViewport(&fullVp));check(d->SetPixelShader(copyShader.Get()));check(d->SetTexture(0,resources.rayB.Get()));
   check(d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE));check(d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_ZERO));check(d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_SRCCOLOR));if(ok)drawQuad(desc.Width,desc.Height);
   check(d->SetTexture(0,nullptr));check(d->SetPixelShader(shader.Get()));check(d->SetTexture(1,depth.Get()));check(d->SetTexture(2,resources.noise.Get()));check(d->SetPixelShaderConstantF(0,constants[0],13));
   shadowsDrawn=true;
  }
  if(!shaftDebug&&fogEffectEnabled){
   float fogMode[4]={3,constants[8][1],0,0};check(d->SetPixelShaderConstantF(8,fogMode,1));check(d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE));check(d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA));check(d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA));
   if(ok)drawQuad(desc.Width,desc.Height);
  }
  trace("composite:first fog drawn");
  if(shaftsEffectEnabled&&(constants[2][2]>0.001f||shaftDebug)){
 // First generate only a continuous sky/occlusion source mask. INTZ is point
 // sampled here once; all beam construction below uses a linearly filtered
 // colour target, avoiding the repeated dotted depth pattern.
 check(d->SetRenderTarget(0,resources.rayASurface.Get()));D3DVIEWPORT9 rayVp{0,0,rayWidth,rayHeight,0,1};check(d->SetViewport(&rayVp));
 check(d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0));check(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));check(d->SetPixelShader(shader.Get()));check(d->SetTexture(0,resources.scene.Get()));check(d->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_LINEAR));check(d->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR));check(d->SetPixelShaderConstantF(0,constants[0],13));
 float shaftMode[4]={shaftDebug?7.f:6.f,float(sunGlowPercent)*0.01f,shaftDebug?1.f:0.f,sunSourceThreshold};check(d->SetPixelShaderConstantF(8,shaftMode,1));if(ok)drawQuad(rayWidth,rayHeight);
 trace("composite:first solar source drawn");
 // True radial blur: transport the source mask continuously toward the sun.
 check(d->SetTexture(1,nullptr));check(d->SetTexture(2,nullptr));check(d->SetPixelShader(radialShader.Get()));check(d->SetTexture(0,resources.rayA.Get()));
 check(d->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_LINEAR));check(d->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR));check(d->SetRenderTarget(0,resources.rayBSurface.Get()));check(d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0));
 float radial[4]={constants[2][0],constants[2][1],.78f,.958f};check(d->SetPixelShaderConstantF(0,radial,1));if(ok)drawQuad(rayWidth,rayHeight);
 trace("composite:first radial rays drawn");
 // Local lamps are intentionally not processed here. WoW submits lantern and
 // flame particles after this pre-UI atmosphere hook, so an early scene copy
 // cannot contain them. Their isolated late pass runs immediately before
 // Present, once every world-light sprite has reached the backbuffer.
 // Symmetric separable Gaussian blur. ShaftSoftnessPixels now controls a
 // visible screen-space radius while the world and interface remain sharp.
 check(d->SetTexture(0,nullptr));check(d->SetPixelShader(blurShader.Get()));check(d->SetRenderTarget(0,resources.rayASurface.Get()));check(d->SetTexture(0,resources.rayB.Get()));
 check(d->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_LINEAR));check(d->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR));
 float aaRadius=.65f+raySoftness*.12f;
 check(d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0));float blur[4]={aaRadius/rayWidth,0,0,0};check(d->SetPixelShaderConstantF(0,blur,1));check(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));if(ok)drawQuad(rayWidth,rayHeight);
 check(d->SetTexture(0,nullptr));check(d->SetRenderTarget(0,resources.rayBSurface.Get()));check(d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0));check(d->SetTexture(0,resources.rayA.Get()));blur[0]=0;blur[1]=aaRadius/rayHeight;check(d->SetPixelShaderConstantF(0,blur,1));if(ok)drawQuad(rayWidth,rayHeight);
 trace("composite:first blur drawn");
 // Forever separates shaft propagation from temporal accumulation. Clip old
 // radiance to the current neighbourhood before blending so a moving camera
 // keeps stable beams without dragging bright silhouettes across the screen.
 IDirect3DTexture9* finalRays=resources.rayB.Get();
 if(!shaftDebug&&temporalShaftsEnabled&&resources.rayHistory&&resources.rayHistorySurface){
  if(newHistory){check(d->SetRenderTarget(0,resources.rayHistorySurface.Get()));check(d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0));}
  check(d->SetRenderTarget(0,resources.rayASurface.Get()));check(d->SetPixelShader(temporalShader.Get()));check(d->SetTexture(0,resources.rayB.Get()));check(d->SetTexture(1,resources.rayHistory.Get()));
  check(d->SetSamplerState(1,D3DSAMP_MINFILTER,D3DTEXF_LINEAR));check(d->SetSamplerState(1,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR));check(d->SetSamplerState(1,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP));check(d->SetSamplerState(1,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP));
  float temporal[4]={1.f/rayWidth,1.f/rayHeight,.72f,resources.historyValid?1.f:0.f};check(d->SetPixelShaderConstantF(0,temporal,1));if(ok)drawQuad(rayWidth,rayHeight);
  check(d->SetTexture(0,nullptr));check(d->SetTexture(1,nullptr));check(d->StretchRect(resources.rayASurface.Get(),nullptr,resources.rayHistorySurface.Get(),nullptr,D3DTEXF_NONE));resources.historyValid=ok;finalRays=resources.rayA.Get();
 }
 // Upsample and composite the smoothed rays back over the full-size scene.
 check(d->SetTexture(0,nullptr));check(d->SetTexture(1,nullptr));check(d->SetTexture(2,nullptr));
 check(d->SetRenderTarget(0,target.Get()));check(d->SetViewport(&fullVp));
 check(d->SetPixelShader(copyShader.Get()));check(d->SetTexture(0,finalRays));
 check(d->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_LINEAR));check(d->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR));
 check(d->SetRenderState(D3DRS_ALPHABLENDENABLE,shaftDebug?FALSE:TRUE));if(!shaftDebug){check(d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_ONE));check(d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_ONE));}if(ok)drawQuad(desc.Width,desc.Height);
 check(d->SetTexture(0,nullptr));check(d->SetTexture(1,nullptr));check(d->SetTexture(2,nullptr));
 }
 if(postShader&&(brightnessPercent!=0||contrastPercent!=100||gammaPercent!=100||sharpnessPercent>0)){
  if(SUCCEEDED(d->StretchRect(target.Get(),nullptr,resources.sceneSurface.Get(),nullptr,D3DTEXF_NONE))){
   check(d->SetRenderTarget(0,target.Get()));check(d->SetViewport(&fullVp));
   check(d->SetPixelShader(postShader.Get()));check(d->SetTexture(0,resources.scene.Get()));
   check(d->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_LINEAR));check(d->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR));
   check(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));
   float postParams[4]={float(brightnessPercent)*0.01f,float(contrastPercent)*0.01f,float(gammaPercent)*0.01f,float(sharpnessPercent)*0.01f};
   float rsize[4]={1.f/desc.Width,1.f/desc.Height,0,0};
   check(d->SetPixelShaderConstantF(0,postParams,1));check(d->SetPixelShaderConstantF(1,rsize,1));
   if(ok)drawQuad(desc.Width,desc.Height);
  }
 }
 check(d->SetTexture(0,nullptr));check(d->SetTexture(1,nullptr));check(d->SetTexture(2,nullptr));check(setDepth(d,ds.Get()));check(state->Apply());internal=false;
 trace("composite:first complete");firstCompositeFinished=true;
 return ok;
}
bool CompositeLateLocalLights(IDirect3DDevice9* d){
 if(!enabled||!active||internal||owner!=d||!composed||localLightStrength<=0||
    !target||!depth||!resources.sceneSurface||!resources.localASurface||!resources.localBSurface||
    !localLightBytecode||!blurBytecode||!copyBytecode)return false;
 ComPtr<IDirect3DStateBlock9> state;ComPtr<IDirect3DSurface9> ds;
 ComPtr<IDirect3DPixelShader9> localLightShader,blurShader,copyShader;
 if(FAILED(d->CreateStateBlock(D3DSBT_ALL,state.GetAddressOf()))||FAILED(state->Capture())||FAILED(getDepth(d,ds.GetAddressOf()))||
    FAILED(d->CreatePixelShader(static_cast<DWORD*>(localLightBytecode->GetBufferPointer()),localLightShader.GetAddressOf()))||
    FAILED(d->CreatePixelShader(static_cast<DWORD*>(blurBytecode->GetBufferPointer()),blurShader.GetAddressOf()))||
    FAILED(d->CreatePixelShader(static_cast<DWORD*>(copyBytecode->GetBufferPointer()),copyShader.GetAddressOf())))return false;
 D3DSURFACE_DESC desc{},localDesc{};
 if(FAILED(target->GetDesc(&desc))||FAILED(resources.localASurface->GetDesc(&localDesc))||
    FAILED(d->StretchRect(target.Get(),nullptr,resources.sceneSurface.Get(),nullptr,D3DTEXF_NONE)))return false;
 internal=true;bool ok=true;static bool loggedLocalFail=false;
 auto check=[&](HRESULT hr,int line=__LINE__){if(FAILED(hr)){if(ok){ok=false;if(!loggedLocalFail){loggedLocalFail=true;char msg[96];sprintf_s(msg,"localLights:FAILED line=%d hr=0x%08lX",line,static_cast<unsigned long>(hr));Log(msg);}}}};
 check(setDepth(d,nullptr));check(d->SetVertexShader(nullptr));check(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));
 for(D3DRENDERSTATETYPE s:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ALPHATESTENABLE,D3DRS_STENCILENABLE,D3DRS_SCISSORTESTENABLE,D3DRS_FOGENABLE,D3DRS_LIGHTING,D3DRS_SRGBWRITEENABLE})check(d->SetRenderState(s,FALSE));
 check(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));check(d->SetRenderState(D3DRS_COLORWRITEENABLE,7));check(d->SetRenderState(D3DRS_FILLMODE,D3DFILL_SOLID));check(d->SetRenderState(D3DRS_CLIPPLANEENABLE,0));check(d->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD));check(d->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE,FALSE));
 for(DWORD s=0;s<2;++s){check(d->SetSamplerState(s,D3DSAMP_MINFILTER,s?D3DTEXF_POINT:D3DTEXF_LINEAR));check(d->SetSamplerState(s,D3DSAMP_MAGFILTER,s?D3DTEXF_POINT:D3DTEXF_LINEAR));check(d->SetSamplerState(s,D3DSAMP_MIPFILTER,D3DTEXF_NONE));check(d->SetSamplerState(s,D3DSAMP_SRGBTEXTURE,FALSE));check(d->SetSamplerState(s,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP));check(d->SetSamplerState(s,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP));}
 struct V{float x,y,z,w,u,v;};auto drawQuad=[&](UINT width,UINT height){float w=float(width)-.5f,h=float(height)-.5f;V q[]={{-.5f,-.5f,0,1,0,0},{w,-.5f,0,1,1,0},{-.5f,h,0,1,0,1},{w,h,0,1,1,1}};check(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,q,sizeof(V)));};
 D3DVIEWPORT9 localVp{0,0,localDesc.Width,localDesc.Height,0,1};check(d->SetViewport(&localVp));check(d->SetRenderTarget(0,resources.localASurface.Get()));check(d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0));
 check(d->SetPixelShader(localLightShader.Get()));check(d->SetTexture(0,resources.scene.Get()));check(d->SetTexture(1,depth.Get()));check(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));
 float local[4]={1.f/desc.Width,1.f/desc.Height,localLightStrength,localLightThreshold};float localMode[4]={localLightDebug?1.f:0.f,localLightRadius,0,0};float localDepth[4]={constants[2][3],0,0,0};
 check(d->SetPixelShaderConstantF(0,local,1));check(d->SetPixelShaderConstantF(1,localMode,1));check(d->SetPixelShaderConstantF(2,localDepth,1));if(ok)drawQuad(localDesc.Width,localDesc.Height);
 check(d->SetTexture(0,nullptr));check(d->SetTexture(1,nullptr));check(d->SetPixelShader(blurShader.Get()));float localBlur[4]={};
 for(int pass=0;pass<3;++pass){
  check(d->SetTexture(0,resources.localA.Get()));check(d->SetRenderTarget(0,resources.localBSurface.Get()));check(d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0));localBlur[0]=localLightRadius/(16.f*desc.Width);localBlur[1]=0;check(d->SetPixelShaderConstantF(0,localBlur,1));if(ok)drawQuad(localDesc.Width,localDesc.Height);
  check(d->SetTexture(0,nullptr));check(d->SetTexture(0,resources.localB.Get()));check(d->SetRenderTarget(0,resources.localASurface.Get()));check(d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0));localBlur[0]=0;localBlur[1]=localLightRadius/(16.f*desc.Height);check(d->SetPixelShaderConstantF(0,localBlur,1));if(ok)drawQuad(localDesc.Width,localDesc.Height);check(d->SetTexture(0,nullptr));
 }
 D3DVIEWPORT9 fullVp{0,0,desc.Width,desc.Height,0,1};check(d->SetViewport(&fullVp));check(d->SetRenderTarget(0,target.Get()));check(d->SetPixelShader(copyShader.Get()));check(d->SetTexture(0,resources.localA.Get()));check(d->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_LINEAR));check(d->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR));check(d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE));check(d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_ONE));check(d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_ONE));if(ok)drawQuad(desc.Width,desc.Height);
 check(d->SetTexture(0,nullptr));check(d->SetTexture(1,nullptr));check(setDepth(d,ds.Get()));check(state->Apply());internal=false;
 static bool logged=false;if(ok&&!logged){Log("late local-light composite active before Present");logged=true;}return ok;
}
void BeforeDraw(IDirect3DDevice9* d){
 if(!enabled||!active||internal||owner!=d||composed)return;
 try{
 ComPtr<IDirect3DSurface9> rt,ds;if(FAILED(d->GetRenderTarget(0,rt.GetAddressOf()))||rt.Get()!=target.Get()||FAILED(getDepth(d,ds.GetAddressOf())))return;
 ComPtr<IDirect3DVertexShader9> vs;if(FAILED(d->GetVertexShader(vs.GetAddressOf())))return;auto hash=Hash(vs.Get());
 // The three-hash allow-list restored here from the forever-style snapshot
 // turned out to be dead: a live session logged 20+ distinct shader hashes at
 // this capture point and NONE of them were those three (confirmed via the
 // "untrusted shader hash" telemetry below, camera=0/applied=0 all session).
 // Shader bytecode hashes are not a stable identity across a client/server
 // patch -- they were never going to still match four days later. Reverting
 // to the ungated capture: it is what actually produced a running pipeline
 // (camera captured on ~96% of frames). The remaining problem is a data
 // question (does c24 hold something meaningful for whichever shader gets
 // captured), not a "which exact bytecode" question, and hash-matching can't
 // answer that regardless of which hashes are in the list.
 cameraCaptureShaderHash=hash;
 if(!ready&&ds.Get()==surface.Get()){ready=CaptureCamera(d);if(ready)++cameraFrames;}
 bool isUi=(hash==0xd9e7756460af6296ull);
 if(!isUi){ComPtr<IDirect3DPixelShader9> curPs;if(SUCCEEDED(d->GetPixelShader(curPs.GetAddressOf()))&&Hash(curPs.Get())==0xc29c7060b723c0c6ull)isUi=true;}
 if(!ready||!isUi)return;
 composed=true;if(Composite(d)){++applied;if(applied==1)Log("composited before captured UI shader; height fog + screen-space shafts");}else{ready=false;composed=false;Log("composite failed; frame skipped");}
 }catch(...){Log("draw preparation failed; frame skipped");}
}
void Present(IDirect3DDevice9* d){
 if(enabled&&active&&!composed&&ready){
  composed=true;Composite(d);
 }
 if(enabled&&active&&++frames%600==120){std::ofstream(std::filesystem::path(logPath),std::ios::app)<<"frames="<<frames<<" depth="<<depthFrames<<" camera="<<cameraFrames<<" applied="<<applied<<" shadowDraws="<<shadowDraws<<'\n';}
 Finish(d);
}
}
