// Exercises the shipped atmosphere/local-light classes on the real D3D9 device.
// Synthetic depth is encoded with WoW's viewport range, not just 0..1.
#include <windows.h>
#include <d3d9.h>
#include <wrl/client.h>
#include <cstdio>
#include <cmath>
#include <stdexcept>
#include <fstream>
#include <filesystem>
#include "../src/D3D9/TrackedRenderState.h"
#include "../src/Effects/DirectionalVolumetricLighting.h"
#include "../src/Effects/LocalLightingRenderer.h"
#include "../src/Effects/GroundSurfaceCapture.h"
#include "../src/Lighting/LocalLightManager.h"
#include "../src/D3D9/DepthCapture.h"
using Microsoft::WRL::ComPtr;
using namespace renderer;
void Check(HRESULT h) { if (FAILED(h)) { printf("HRESULT=%08lx\n", h); throw std::runtime_error("D3D call"); } }
void Require(bool value, const char* name) { if (!value) throw std::runtime_error(name); printf("PASS %s\n", name); }
HRESULT WINAPI SetDepth(IDirect3DDevice9* d, IDirect3DSurface9* s) { return d->SetDepthStencilSurface(s); }
HRESULT WINAPI GetDepth(IDirect3DDevice9* d, IDirect3DSurface9** s) { return d->GetDepthStencilSurface(s); }
int main() try {
 wchar_t path[MAX_PATH]{}; GetSystemDirectoryW(path, MAX_PATH); wcscat_s(path,L"\\d3d9.dll");
 auto dll=LoadLibraryW(path);
 auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(GetProcAddress(dll,"Direct3DCreate9"));
 ComPtr<IDirect3D9> api; api.Attach(create(D3D_SDK_VERSION));
 HWND window=CreateWindowW(L"STATIC",L"Atmosphere regression",WS_OVERLAPPEDWINDOW,0,0,128,128,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
 D3DPRESENT_PARAMETERS pp{}; pp.Windowed=TRUE; pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
 pp.hDeviceWindow=window; pp.BackBufferWidth=128; pp.BackBufferHeight=128; pp.BackBufferFormat=D3DFMT_A8R8G8B8;
 ComPtr<IDirect3DDevice9> d; Check(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,d.GetAddressOf()));
 DepthCapture::Instance().SetFunctions(SetDepth,GetDepth);
 ComPtr<IDirect3DSurface9> back,read; Check(d->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,back.GetAddressOf()));
 Check(d->CreateOffscreenPlainSurface(128,128,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,read.GetAddressOf(),nullptr));
 auto texture=[&](D3DFORMAT fmt){ComPtr<IDirect3DTexture9> t; Check(d->CreateTexture(128,128,1,0,fmt,D3DPOOL_MANAGED,t.GetAddressOf(),nullptr)); return t;};
 auto depth=texture(D3DFMT_R32F), waterDepth=texture(D3DFMT_R32F);
 auto scene=texture(D3DFMT_A8R8G8B8),noise=texture(D3DFMT_A8R8G8B8),mask=texture(D3DFMT_A8R8G8B8);
 auto fill=[&](IDirect3DTexture9* t,DWORD value){D3DLOCKED_RECT r{};Check(t->LockRect(0,&r,nullptr,0));for(int y=0;y<128;++y)for(int x=0;x<128;++x)reinterpret_cast<DWORD*>(static_cast<BYTE*>(r.pBits)+y*r.Pitch)[x]=value;Check(t->UnlockRect(0));};
 auto fillFloat=[&](IDirect3DTexture9* t,float v){DWORD bits;memcpy(&bits,&v,4);fill(t,bits);};
 fill(scene.Get(),0xff202020);fill(noise.Get(),0xffb0b0b0);fill(mask.Get(),0);fillFloat(waterDepth.Get(),20.f);
 ComPtr<IDirect3DSurface9> sceneSurface;Check(scene->GetSurfaceLevel(0,sceneSurface.GetAddressOf()));
 FrameContext f; f.cameraValid=f.depthAvailable=true;f.sceneColor=scene.Get();f.sceneSurface=sceneSurface.Get();
 f.depthTexture=depth.Get();f.atmosphereNoise=noise.Get();f.waterMaskTexture=mask.Get();f.waterDepthTexture=waterDepth.Get();
 f.projUnpack[0]=1000.f/999.9f;f.projUnpack[1]=-100.f/999.9f;f.projUnpack[2]=f.projUnpack[3]=1;
 f.inverseView.SetZero();f.inverseView.m[0][0]=f.inverseView.m[1][2]=f.inverseView.m[2][1]=f.inverseView.m[3][3]=1;
 f.sunDirectionView={0,0,1};f.viewport={0,0,128,128,0,1};f.daylightFactor=1;
 auto& fog=DirectionalVolumetricLighting::Instance();auto& settings=fog.Settings();
 settings.temporalEnabled=false;settings.localFogEnabled=false;settings.useEnvironmentBaseline=false;
 auto encode=[&](float distance,float maxZ,float minZ=0){
  f.viewport.MinZ=minZ;f.viewport.MaxZ=maxZ;f.depthMaxZ=maxZ;
  D3DLOCKED_RECT r{};Check(depth->LockRect(0,&r,nullptr,0));
  for(int y=0;y<128;++y)for(int x=0;x<128;++x){
   // A real ground plane five units below the camera plus an end wall.
   float vertical=1-2*(y+.5f)/128;
   float z=vertical<0?std::min(distance,-5/vertical):distance;
   reinterpret_cast<float*>(static_cast<BYTE*>(r.pBits)+y*r.Pitch)[x]=minZ+(maxZ-minZ)*(f.projUnpack[0]+f.projUnpack[1]/z);
  }Check(depth->UnlockRect(0));
 };
 float sentinels[64][4];for(int c=0;c<64;++c)for(int j=0;j<4;++j)sentinels[c][j]=float(c*4+j)*.003f;
 auto pixel=[&](int px=64,int py=64){Check(d->GetRenderTargetData(back.Get(),read.Get()));D3DLOCKED_RECT r{};Check(read->LockRect(&r,nullptr,D3DLOCK_READONLY));DWORD p=reinterpret_cast<DWORD*>(static_cast<BYTE*>(r.pBits)+py*r.Pitch)[px];Check(read->UnlockRect());return p;};
 auto render=[&](bool global){Check(d->SetRenderTarget(0,back.Get()));Check(d->SetViewport(&f.viewport));Check(d->SetPixelShaderConstantF(0,sentinels[0],64));Check(d->BeginScene());bool ok=fog.Render(d.Get(),f,back.Get(),global);Check(d->EndScene());Require(ok,"production atmosphere render");float restored[64][4];Check(d->GetPixelShaderConstantF(0,restored[0],64));Require(!memcmp(sentinels,restored,sizeof(restored)),"c0..c63 restored");return pixel();};
 auto diff=[](DWORD a,DWORD b){int m=0;for(int s=0;s<24;s+=8)m=std::max(m,abs(int((a>>s)&255)-int((b>>s)&255)));return m;};
 encode(120,1);DWORD standard=render(true);Require(diff(standard,0xff202020)>8,"fog visible on first resource creation");
 encode(120,.94f);DWORD compressed=render(true);printf("fog z=120 standard=%08lx compressed=%08lx\n",standard,compressed);Require(diff(standard,compressed)<=2,"viewport MaxZ=.94 matches MaxZ=1");
 encode(120,.94f,.1f);Require(diff(standard,render(true))<=2,"nonzero viewport MinZ");
 Require(render(false)==0xff202020,"both fog layers off preserve scene");
 settings.densityScale=0;Require(render(true)==0xff202020,"zero density removes global fog");settings.densityScale=1;
 settings.localFogEnabled=true;settings.localFogHeightFalloff=.125f;Require(diff(render(false),0xff202020)>5,"local fog has volume above ground, independent of global switch");
 settings.localFogHeightFalloff=1.f;DWORD thin=render(false);
 settings.localFogHeightFalloff=.125f;DWORD thick=render(false);
 Require(diff(thick,0xff202020)>diff(thin,0xff202020)+5,"increasing local height thickens the volume");
 settings.localFogHeightFalloff=1.f/3;Require(diff(render(false),0xff202020)<=2,"view above compact bank remains clear within Gaussian tail");
 settings.localFogHeightFalloff=.125f;fill(noise.Get(),0xff000000);
 Require(render(false)==0xff202020,"gaps between banks have zero local haze");fill(noise.Get(),0xffb0b0b0);
 // Reproduce the user's low-wash night scene, with no selected lamps.
 settings.localFogHeightFalloff=1.f/3;settings.localFogDensity=.3f;
 float savedWash=settings.fogWash;settings.fogWash=.18f;f.daylightFactor=0;
 render(false);DWORD nightBank=pixel(64,100);
 Require(diff(nightBank,0xff202020)>25,"height-three ground bank visible at night without lights");
 Require(diff(nightBank,((nightBank&255)*0x010101)|0xff000000)<12,"unlit bank is neutral whitish, not dark environment tint");
 settings.fogWash=0;render(false);
 Require(diff(nightBank,pixel(64,100))<=1,"local bank independent of global fog wash");
 fill(noise.Get(),0xff888888);render(false);DWORD sparseBank=pixel(64,100);
 Require(diff(nightBank,0xff202020)>diff(sparseBank,0xff202020)+10,"noise produces different ground bank densities");
 fill(noise.Get(),0);render(false);Require(pixel(64,100)==0xff202020,"clear gaps remain clear at maximum local density");
 fill(noise.Get(),0xffb0b0b0);settings.fogWash=savedWash;f.daylightFactor=1;
 settings.localFogEnabled=false;
 settings.resolutionScale=.5f;DWORD resized=render(true);DWORD resizedAgain=render(true);
 printf("resized first=%08lx second=%08lx\n",resized,resizedAgain);
 Require(diff(resized,0xff202020)>8 && diff(resized,resizedAgain)<=1,"quality resize preserves first-frame fog");
 settings.debugMode=VolumetricDebugMode::BoundaryDepth;encode(120,.94f);DWORD boundary=render(true);
 fill(mask.Get(),0xffffffff);DWORD water=render(true);Require((boundary&255)>(water&255)+30,"water stops atmosphere at surface, not seabed");
 fill(mask.Get(),0);settings.debugMode=VolumetricDebugMode::None;
 settings.edgeFogEnabled=true;settings.edgeFogDistance=30;settings.edgeFogPower=2;
 encode(120,.94f);DWORD edgeFog=render(false);Require(diff(edgeFog,0xff202020)>25,"world edge fog works with global and local fog disabled");
 encode(20,.94f);Require(render(false)==0xff202020,"world edge fog preserves near surfaces");// Sky well above the horizon must retain its original colour.
 fillFloat(depth.Get(),1.f);render(false);Require(diff(pixel(64,0),0xff202020)<=1,"world edge fog leaves high sky clear");
 settings.edgeFogEnabled=false;
 // Fog above nearby water must survive the final composite, not just integration.
 settings.localFogEnabled=true;settings.localFogDensity=.3f;settings.localFogHeightFalloff=1.f/3;
 encode(120,.94f);render(false);DWORD dryBank=pixel(64,100);
 D3DLOCKED_RECT wr{};Check(waterDepth->LockRect(0,&wr,nullptr,0));
 for(int y=0;y<128;++y)for(int x=0;x<128;++x){float v=1-2*(y+.5f)/128;reinterpret_cast<float*>(static_cast<BYTE*>(wr.pBits)+y*wr.Pitch)[x]=v<0?std::min(120.f,-5/v):120.f;}
 Check(waterDepth->UnlockRect(0));fill(mask.Get(),0xffffffff);render(false);
 Require(diff(dryBank,pixel(64,100))<=2&&diff(pixel(64,100),0xff202020)>20,"nearby water retains air fog above its surface");
 fill(mask.Get(),0);settings.localFogEnabled=false;
 // Read back only in this regression executable; production state stays on GPU.
 auto interaction=[&](){std::array<float,8> result{};ComPtr<IDirect3DSurface9> src,staging;
  Check(fog.GetInteractionState()->GetSurfaceLevel(0,src.GetAddressOf()));
  Check(d->CreateOffscreenPlainSurface(2,1,D3DFMT_A32B32G32R32F,D3DPOOL_SYSTEMMEM,staging.GetAddressOf(),nullptr));
  Check(d->GetRenderTargetData(src.Get(),staging.Get()));D3DLOCKED_RECT r{};Check(staging->LockRect(&r,nullptr,D3DLOCK_READONLY));memcpy(result.data(),r.pBits,32);Check(staging->UnlockRect());return result;};
 auto actorDepth=[&](){encode(120,.94f);D3DLOCKED_RECT r{};Check(depth->LockRect(0,&r,nullptr,0));
  for(int y=54;y<82;++y)for(int x=61;x<68;++x)reinterpret_cast<float*>(static_cast<BYTE*>(r.pBits)+y*r.Pitch)[x]=.94f*(f.projUnpack[0]+f.projUnpack[1]/12.f);
  Check(depth->UnlockRect(0));};
 fog.Reset(d.Get());actorDepth();render(false);auto actorStart=interaction();
 Require(fabsf(actorStart[1]-12)<.2f,"wake anchors to central character silhouette, not camera");
 for(int i=0;i<20;++i){Sleep(16);f.cameraPosition.x+=.08f;actorDepth();render(false);}
 auto actorMoved=interaction();
 Require(actorMoved[0]>.2f&&actorMoved[3]>.03f&&actorMoved[4]>.3f,"moving actor generates persistent directional wake");
 Require(fabsf(actorMoved[2]-actorStart[2])<.02f,"flat world ground stays anchored during movement");
 f.frameIndex+=100000;render(false);auto unchanged=interaction();
 Require(fabsf(unchanged[2]-actorMoved[2])<.02f,"frame counter wrap cannot displace ground state");
 f.cameraPosition={0,0,0};fog.Reset(d.Get());encode(120,.94f);
 auto& manager=LocalLightManager::Instance();
 auto lamp=[&](float y){manager.Reset();D3DLIGHT9 l{};l.Type=D3DLIGHT_POINT;l.Position={0,y,0};l.Range=12;l.Diffuse={1,.65f,.25f,1};for(int n=0;n<8;++n)manager.OnSetLight(0,&l);manager.OnLightEnable(0,TRUE);manager.SelectForFrame({0,0,0},{0,1,0});Require(!manager.Selected().empty(),"test world light selected");};
 encode(120,.94f);DWORD noLight=render(true);lamp(40);DWORD glow=render(true);Require(diff(noLight,glow)>2,"small lamp between world march samples produces halo");
 encode(20,.94f);DWORD blocked=render(true);manager.Reset();Require(diff(blocked,render(true))==0,"foreground wall occludes lamp volume");

 // User's low Light Rays=15 must still show a visible halo, even with global
 // Fog Wash zero. The zero endpoint must remain a real off switch.
 std::filesystem::create_directories("build/halo-fixture");auto savedFog=settings;
 settings.localFogEnabled=true;settings.localFogDensity=.132f;settings.localFogHeightFalloff=1.f/7;settings.fogWash=0;f.daylightFactor=0;
 auto haloAt=[&](int rays){std::ofstream cfg("build/halo-fixture/GraphicsEffects.ini");cfg<<"[DynamicLighting]\nEnabled=1\nIntensityPercent=55\nRayPercent="<<rays<<"\nQuality=2\n";cfg.close();
  manager.Configure(L"build/halo-fixture/");lamp(40);encode(120,.94f);return render(false);};
 auto haloZero=haloAt(0),haloLow=haloAt(15);
 printf("lamp halo rays0=%08lx rays15=%08lx\n",haloZero,haloLow);
 Require(diff(haloZero,haloLow)>8,"Light Rays 15 produces visible lamp scattering at zero global wash");
 manager.Reset();Require(diff(haloZero,render(false))<=1,"Light Rays zero removes lamp scattering completely");
 settings=savedFog;f.daylightFactor=1;manager.Configure(L".\\");
 auto& lighting=LocalLightingRenderer::Instance();lamp(52);
 auto surface=[&](float maxZ){lamp(52);encode(60,maxZ);f.sceneColor=scene.Get();f.sceneSurface=sceneSurface.Get();Check(d->BeginScene());Require(lighting.Render(d.Get(),f),"production surface light render");Check(d->EndScene());Check(d->StretchRect(lighting.GetLitSurface(),nullptr,back.Get(),nullptr,D3DTEXF_NONE));return pixel();};
 DWORD light1=surface(1),light94=surface(.94f);printf("surface standard=%08lx compressed=%08lx\n",light1,light94);
 Require(diff(light1,0xff202020)>2,"lamp illuminates world surface");Require(diff(light1,light94)<=2,"surface light viewport depth invariant");
 auto unrelatedOccluder=[&](bool blocked){
  encode(60,.94f);manager.Reset();D3DLIGHT9 l{};l.Type=D3DLIGHT_POINT;l.Position={8,52,0};l.Range=24;l.Diffuse={2,1.3f,.5f,1};
  for(int n=0;n<8;++n)manager.OnSetLight(0,&l);manager.OnLightEnable(0,TRUE);
  if(blocked){D3DLOCKED_RECT r{};Check(depth->LockRect(0,&r,nullptr,0));
   for(int y=62;y<=66;++y)for(int x=69;x<=72;++x)
    reinterpret_cast<float*>(static_cast<BYTE*>(r.pBits)+y*r.Pitch)[x]=.94f*(f.projUnpack[0]+f.projUnpack[1]/10.f);
   Check(depth->UnlockRect(0));}
  f.sceneColor=scene.Get();f.sceneSurface=sceneSurface.Get();Check(d->BeginScene());
  Require(lighting.Render(d.Get(),f),"surface occlusion fixture render");Check(d->EndScene());
  Check(d->StretchRect(lighting.GetLitSurface(),nullptr,back.Get(),nullptr,D3DTEXF_NONE));return pixel();
 };
 DWORD unobstructed=unrelatedOccluder(false),unrelated=unrelatedOccluder(true);
 Require(diff(unobstructed,unrelated)<=2,"foreground silhouette does not extrude a shadow onto distant receiver");
 // Compare reduced-resolution light against an exact-resolution reference
 // on a grazing ground plane. There must be no rejected dark scanlines.
 std::filesystem::create_directories("build/light-slope-fixture");
 auto slopeLighting=[&](int quality){
  {std::ofstream cfg("build/light-slope-fixture/GraphicsEffects.ini");cfg<<"[DynamicLighting]\nEnabled=1\nIntensityPercent=100\nQuality="<<quality<<"\n";}
  manager.Configure(L"build/light-slope-fixture/");manager.Reset();encode(120,.94f);
  D3DLIGHT9 l{};l.Type=D3DLIGHT_POINT;l.Position={0,18,-3};l.Range=40;l.Diffuse={8,5,2,1};
  for(int i=0;i<8;++i)manager.OnSetLight(0,&l);manager.OnLightEnable(0,TRUE);
  f.sceneColor=scene.Get();f.sceneSurface=sceneSurface.Get();Check(d->BeginScene());Require(lighting.Render(d.Get(),f),"grazing plane lighting render");Check(d->EndScene());
  Check(d->StretchRect(lighting.GetLitSurface(),nullptr,back.Get(),nullptr,D3DTEXF_NONE));
  std::array<DWORD,24> line{};for(int y=76;y<100;++y)line[y-76]=pixel(64,y);return line;};
 auto exactSlope=slopeLighting(2),reducedSlope=slopeLighting(1);
 int maxDarkGap=0;
 // A lower-resolution sample may interpolate anywhere between neighbouring
 // reference pixels, especially around a saturated lamp core. A black stripe
 // falls below both neighbours and is not legitimate interpolation.
 for(int i=0;i<24;++i)for(int shift=0;shift<24;shift+=8){
  int low=255;for(int j=std::max(0,i-1);j<=std::min(23,i+1);++j)low=std::min(low,int((exactSlope[j]>>shift)&255));
  maxDarkGap=std::max(maxDarkGap,low-int((reducedSlope[i]>>shift)&255));}
 printf("grazing plane maximum dark gap=%d\n",maxDarkGap);
 Require(maxDarkGap<=6,"sloping light footprint has no black upsampling stripes");
 // Feed a real shipped flame mip and verified rigid M2 draw layout through
 // the exact attachment observer, including animated bone translation.
 ComPtr<IDirect3DTexture9> flame;Check(d->CreateTexture(128,128,1,0,D3DFMT_DXT5,D3DPOOL_MANAGED,flame.GetAddressOf(),nullptr));
 std::ifstream payload("tools/fixtures/flamelicksmall-mip.dxt5",std::ios::binary);Require(bool(payload),"real torch texture fixture available");
 D3DLOCKED_RECT flameLock{};Check(flame->LockRect(0,&flameLock,nullptr,0));
 for(int row=0;row<32;++row)payload.read(static_cast<char*>(flameLock.pBits)+row*flameLock.Pitch,512);
 Check(flame->UnlockRect(0));Require(bool(payload),"torch mip complete");
 struct TorchVertex{float x,y,z;BYTE bone[4];float u,v;};
 TorchVertex tv[]={{-.1f,0,0,{0},0,0},{.1f,0,0,{0},1,0},{0,.2f,0,{0},.5f,1}};
 ComPtr<IDirect3DVertexBuffer9> vb;ComPtr<IDirect3DIndexBuffer9> ib;void* memory=nullptr;
 Check(d->CreateVertexBuffer(sizeof(tv),0,0,D3DPOOL_MANAGED,vb.GetAddressOf(),nullptr));Check(vb->Lock(0,sizeof(tv),&memory,0));memcpy(memory,tv,sizeof(tv));Check(vb->Unlock());
 WORD ti[]={0,1,2};Check(d->CreateIndexBuffer(sizeof(ti),0,D3DFMT_INDEX16,D3DPOOL_MANAGED,ib.GetAddressOf(),nullptr));Check(ib->Lock(0,sizeof(ti),&memory,0));memcpy(memory,ti,sizeof(ti));Check(ib->Unlock());
 D3DVERTEXELEMENT9 elements[]={{0,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},{0,12,D3DDECLTYPE_UBYTE4,0,D3DDECLUSAGE_BLENDINDICES,0},{0,16,D3DDECLTYPE_FLOAT2,0,D3DDECLUSAGE_TEXCOORD,0},D3DDECL_END()};
 ComPtr<IDirect3DVertexDeclaration9> decl;Check(d->CreateVertexDeclaration(elements,decl.GetAddressOf()));
 Check(d->SetVertexDeclaration(decl.Get()));Check(d->SetStreamSource(0,vb.Get(),0,sizeof(TorchVertex)));Check(d->SetIndices(ib.Get()));Check(d->SetTexture(0,flame.Get()));
 float boneRows[3][4]={{1,0,0,0},{0,1,0,0},{0,0,1,40}};Check(d->SetVertexShaderConstantF(31,boneRows[0],3));
 FrameContext::Current()=f;g_trackedState.vsHash=0xff32338728131932ull;g_trackedState.zEnable=g_trackedState.alphaBlend=true;
 manager.Reset();manager.ObserveTorchDraw(d.Get(),D3DPT_TRIANGLELIST,0,0,1);manager.SelectForFrame({0,0,0},{0,1,0});
 Require(!manager.Selected().empty()&&(manager.Selected()[0].flags&LocalLightAttached),"held torch mesh creates a dynamic world light");
 Vec3 torchPosition=manager.Selected()[0].position;boneRows[0][3]=1;Check(d->SetVertexShaderConstantF(31,boneRows[0],3));
 manager.ObserveTorchDraw(d.Get(),D3DPT_TRIANGLELIST,0,0,1);manager.SelectForFrame({0,0,0},{0,1,0});
 Require(manager.Selected().size()==1&&fabsf(manager.Selected()[0].position.x-torchPosition.x-1)<.01f,"torch light follows the animated hand without duplicates");
 // Classic torch flames use the unskinned c31..c33 particle path too,
 // and may be submitted as UP draws with no readable GPU vertex buffer.
 g_trackedState.vsHash=0x5f2c6b3af8c6e543ull;manager.Reset();
 manager.ObserveTorchDraw(d.Get(),D3DPT_TRIANGLELIST,0,0,1,false,tv,sizeof(TorchVertex));manager.SelectForFrame({0,0,0},{0,1,0});
 Require(manager.Selected().size()==1,"unskinned flame particle UP draw creates light");
 manager.Reset();manager.ObserveTorchDraw(d.Get(),D3DPT_TRIANGLELIST,0,0,1,true,tv,sizeof(TorchVertex),ti,D3DFMT_INDEX16,3);manager.SelectForFrame({0,0,0},{0,1,0});
 Require(manager.Selected().size()==1,"indexed UP flame particle draw creates light");
 manager.Reset();manager.ObserveTorchDraw(d.Get(),D3DPT_TRIANGLELIST,0,0,1,false);manager.SelectForFrame({0,0,0},{0,1,0});
 Require(manager.Selected().size()==1,"unskinned nonindexed buffer draw creates light");
 TorchVertex separated[6];memcpy(separated,tv,sizeof(tv));memcpy(separated+3,tv,sizeof(tv));for(int i=3;i<6;++i)separated[i].x+=8;
 manager.Reset();manager.ObserveTorchDraw(d.Get(),D3DPT_TRIANGLELIST,0,0,2,false,separated,sizeof(TorchVertex));manager.SelectForFrame({0,0,0},{0,1,0});
 Require(manager.Selected().size()==2,"batched flames remain separate sources, without a midpoint light");
 Check(d->SetTexture(0,scene.Get()));manager.Reset();manager.ObserveTorchDraw(d.Get(),D3DPT_TRIANGLELIST,0,0,1);manager.SelectForFrame({0,0,0},{0,1,0});Require(manager.Selected().empty(),"ordinary textures do not create attached lights");

 // Replay real geometry into the production height atlas: slopes, water,
 // camera rotation, and draw-state restoration are checked independently.
 auto& ground=GroundSurfaceCapture::Instance();ground.Reset();manager.Reset();
 f.cameraPosition={0,0,0};f.inverseView.SetZero();f.inverseView.m[0][0]=f.inverseView.m[1][2]=f.inverseView.m[2][1]=f.inverseView.m[3][3]=1;
 struct GroundVertex{float x,y,z;};
 GroundVertex hill[]={{-110,-110,-24.8f},{110,-110,-2.8f},{-110,110,-7.2f},{110,110,14.8f}};
 float groundView[4][4]={{1,0,0,0},{0,0,1,0},{0,1,0,0},{0,0,0,1}};
 auto captureGround=[&](GroundVertex* q){Check(d->SetFVF(D3DFVF_XYZ));Check(d->SetVertexShaderConstantF(0,groundView[0],4));
  Check(d->SetRenderTarget(0,back.Get()));Check(d->SetViewport(&f.viewport));Check(d->SetPixelShaderConstantF(0,sentinels[0],64));
  float before[9][4]{};Check(d->GetVertexShaderConstantF(0,before[0],9));Check(d->BeginScene());
  // Native shadowed terrain uses a PS absent from the old four-hash list.
  g_trackedState.vsHash=0x938d1ef758aa6085ull;g_trackedState.psHash=0x634793193e26059dull;g_trackedState.zEnable=true;
  ground.Observe(d.Get(),f,[&]{Check(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,q,sizeof(GroundVertex)));});Check(d->EndScene());
  float after[9][4]{};Check(d->GetVertexShaderConstantF(0,after[0],9));Require(!memcmp(before,after,sizeof(before)),"height capture restores vertex constants");
  float ps[64][4]{};Check(d->GetPixelShaderConstantF(0,ps[0],64));Require(!memcmp(ps,sentinels,sizeof(ps)),"height capture restores pixel constants");
  D3DVIEWPORT9 vp{};Check(d->GetViewport(&vp));Require(!memcmp(&vp,&f.viewport,sizeof(vp)),"height capture restores viewport including depth range");};
 auto groundPixel=[&](float x,float y){
  Require(ground.Texture()!=nullptr,"height atlas is available");ComPtr<IDirect3DSurface9> a,b;
  Check(ground.Texture()->GetSurfaceLevel(0,a.GetAddressOf()));Check(d->CreateOffscreenPlainSurface(1024,1024,D3DFMT_A16B16G16R16F,D3DPOOL_SYSTEMMEM,b.GetAddressOf(),nullptr));
  Check(d->GetRenderTargetData(a.Get(),b.Get()));D3DLOCKED_RECT r{};Check(b->LockRect(&r,nullptr,D3DLOCK_READONLY));
  int ix=int((x-ground.Origin()[0]+256)*2),iy=int((y-ground.Origin()[1]+256)*2);
  auto h=reinterpret_cast<unsigned short*>(static_cast<BYTE*>(r.pBits)+iy*r.Pitch)+ix*4;
  auto half=[](unsigned short v){return std::ldexp(float((v&1023)+((v&0x7c00)?1024:0)),((v>>10)&31)?int((v>>10)&31)-25:-24)*((v&0x8000)?-1.f:1.f);};
  std::array<float,2> value{half(h[0])+ground.Origin()[2],half(h[1])};Check(b->UnlockRect());return value;};
 // Reproduce water arriving before supported terrain: it must not turn an
 // empty land atlas into authoritative coverage and erase all ground fog.
 Check(d->SetFVF(D3DFVF_XYZ));Check(d->SetVertexShaderConstantF(5,groundView[0],4));Check(d->BeginScene());
 ground.Capture(d.Get(),f,5,[&]{Check(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,hill,sizeof(GroundVertex)));});Check(d->EndScene());
 Require(!ground.Texture(),"water-only atlas cannot disable land fog fallback");
 for(auto h:{0x2437f34e978bb37full,0x26c1716cad7cb0abull,0x79a7faa602ba6b68ull,0x91f8b826abc842a1ull,0x938d1ef758aa6085ull,0xde5fb134242c2b4eull,0xf6d02568ca25d7caull})Require(GroundSurfaceCapture::IsTerrainVertexShader(h),"native shadow terrain vertex variant recognized");
 Require(!GroundSurfaceCapture::IsTerrainVertexShader(0x28d133339d7839aeull),"WMO vertices cannot become terrain height");
 captureGround(hill);auto gp=groundPixel(32,32);printf("atlas hill h=%f coverage=%f\n",gp[0],gp[1]);
 Require(fabsf(gp[0]-(-5+.1f*32.25f+.08f*32.25f))<.025f&&gp[1]>.99f,"captured height follows sloping terrain at world XY");

 f.sceneColor=scene.Get();f.sceneSurface=sceneSurface.Get();
 settings.localFogEnabled=true;settings.localFogDensity=.1f;settings.localFogHeightFalloff=1.f/3;
 settings.localFogWakeStrength=0;settings.edgeFogEnabled=false;settings.resolutionScale=1;settings.sampleCount=24;
 fill(noise.Get(),0xffb0b0b0);fill(mask.Get(),0);f.daylightFactor=1;
 auto slopingDepth=[&](){D3DLOCKED_RECT r{};Check(depth->LockRect(0,&r,nullptr,0));
  for(int y=0;y<128;++y)for(int x=0;x<128;++x){float vx=2*(x+.5f)/128-1,vy=1-2*(y+.5f)/128;
   float denominator=.1f*vx+.08f-vy;float z=denominator>0?std::min(120.f,5/denominator):120.f;
   reinterpret_cast<float*>(static_cast<BYTE*>(r.pBits)+y*r.Pitch)[x]=.94f*(f.projUnpack[0]+f.projUnpack[1]/z);}
  Check(depth->UnlockRect(0));};
 auto smooth=[](float a,float b,float v){float q=std::clamp((v-a)/(b-a),0.f,1.f);return q*q*(3-2*q);};
 auto referenceBank=[&](int x,int y){
  double vx=2*(x+.5)/128-1,vy=1-2*(y+.5)/128,scale=sqrt(vx*vx+vy*vy+1);
  double denominator=.1*vx+.08-vy,z=denominator>0?std::min(120.,5/denominator):120.;
  double length=std::min(z*scale,97.5),tau=0,step=length/8192;
  double bank=smooth(.30f,.78f,176.f/255),height=3*(.48+.32*bank);
  for(int i=0;i<8192;++i){double t=(i+.5)*step,px=vx/scale*t,py=t/scale,pz=vy/scale*t;
   double above=std::max(0.,pz-(-5+.1*px+.08*py));
   double density=.1*exp(-2*above*above/(height*height))*bank*bank*2.2;
   density*=1-smooth(65*.65f,65,float(sqrt(px*px+py*py)));density*=smooth(1,6,float(t));tau+=density*step;}
  double alpha=1-exp(-tau);return int((32./255*(1-alpha)+.72*alpha)*255+.5);};
 slopingDepth();render(false);int slopeError=0;
 for(int y=70;y<=110;y+=5){int actual=(pixel(64,y)>>16)&255;printf("slope y=%d actual=%d ref=%d\n",y,actual,referenceBank(64,y));slopeError=std::max(slopeError,abs(actual-referenceBank(64,y)));}
 printf("sloped fog error versus 8192-step reference=%d\n",slopeError);
 Require(slopeError<=5,"ground fog integration follows slope instead of camera-height slab");
 render(false);auto stationary=pixel(64,90);render(false);
 Require(diff(stationary,pixel(64,90))<=1,"stationary fog has no frame-random sampling flicker");
 // Change camera basis while keeping the same world geometry.
 f.inverseView.SetZero();f.inverseView.m[0][1]=-1;f.inverseView.m[1][2]=f.inverseView.m[2][0]=f.inverseView.m[3][3]=1;
 float rotated[4][4]={{0,0,1,0},{-1,0,0,0},{0,1,0,0},{0,0,0,1}};memcpy(groundView,rotated,sizeof(rotated));
 captureGround(hill);auto turned=groundPixel(32,32);Require(fabsf(turned[0]-gp[0])<.025f,"terrain height invariant under camera rotation");
 GroundVertex pond[]={{-16,-16,3},{16,-16,3},{-16,16,3},{16,16,3}};
 captureGround(pond);captureGround(hill);auto wet=groundPixel(0,0);
 Require(fabsf(wet[0]-3)<.01f,"water surface wins over seabed independent of draw order");
 // A hole in captured coverage must stay unknown, never invent a floating floor.
 auto missing=groundPixel(150,150);Require(missing[1]==0,"uncaptured ground has explicit missing coverage");
 ground.Reset();Require(!ground.Texture(),"height atlas releases default-pool resources on reset");

 // Compile the complete shipped water shader and exercise its exact local
 // reflection block on this device, with no copied approximation of the math.
 std::ifstream waterFile("WaterEffect.h");std::string waterText((std::istreambuf_iterator<char>(waterFile)),{});
 auto startSource=waterText.find("const char* source=R\"HLSL(")+strlen("const char* source=R\"HLSL(");
 auto endSource=waterText.find(")HLSL\";",startSource);std::string waterShader=waterText.substr(startSource,endSource-startSource);
 ComPtr<ID3DBlob> waterCode,error;ComPtr<IDirect3DPixelShader9> waterPS;
 Check(D3DCompile(waterShader.data(),waterShader.size(),nullptr,nullptr,nullptr,"main","ps_3_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,waterCode.GetAddressOf(),error.GetAddressOf()));
 Check(d->CreatePixelShader((DWORD*)waterCode->GetBufferPointer(),waterPS.GetAddressOf()));Require(bool(waterPS),"complete production water shader accepted by D3D9");
 auto beginLocal=waterShader.find("    float3 localReflection=0;"),endLocal=waterShader.find("    // 4. Celestial",beginLocal);
 std::string reflectionShader="float4 localPosition[8]:register(c180);float4 localColor[8]:register(c188);float4 localControl:register(c196);float3 safeNormalize(float3 v){return normalize(v);}float4 main():COLOR0{float3 rgb=0,viewPos=float3(0,0,5),wave=float3(0,0,-1),v=wave;"+waterShader.substr(beginLocal,endLocal-beginLocal)+"return float4(rgb,1);}";
 {std::ofstream out("build/water-reflection-test.hlsl");out<<reflectionShader;}
 waterCode.Reset();waterPS.Reset();Check(D3DCompile(reflectionShader.data(),reflectionShader.size(),nullptr,nullptr,nullptr,"main","ps_3_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,waterCode.GetAddressOf(),nullptr));Check(d->CreatePixelShader((DWORD*)waterCode->GetBufferPointer(),waterPS.GetAddressOf()));
 auto reflectedLight=[&](float radius,float enabled){float lights[17][4]{};lights[0][3]=radius;lights[8][0]=10;lights[8][1]=5;lights[8][2]=1;lights[8][3]=1;lights[16][0]=1;lights[16][1]=enabled;
  struct Q{float x,y,z,w;};Q q[]={{-.5f,-.5f,0,1},{127.5f,-.5f,0,1},{-.5f,127.5f,0,1},{127.5f,127.5f,0,1}};
  Check(d->SetRenderTarget(0,back.Get()));Check(d->SetViewport(&f.viewport));Check(d->SetVertexShader(nullptr));Check(d->SetFVF(D3DFVF_XYZRHW));Check(d->SetPixelShader(waterPS.Get()));Check(d->SetPixelShaderConstantF(180,lights[0],17));
  for(auto rs:{D3DRS_ZENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_ALPHATESTENABLE,D3DRS_SCISSORTESTENABLE,D3DRS_STENCILENABLE,D3DRS_FOGENABLE,D3DRS_SRGBWRITEENABLE,D3DRS_CLIPPLANEENABLE})Check(d->SetRenderState(rs,FALSE));Check(d->SetRenderState(D3DRS_COLORWRITEENABLE,15));Check(d->SetRenderState(D3DRS_FILLMODE,D3DFILL_SOLID));Check(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));
  Check(d->BeginScene());Check(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,q,sizeof(Q)));Check(d->EndScene());return pixel();};
 auto reflectionOn=reflectedLight(10,1);printf("water reflection=%08lx\n",reflectionOn);Require(((reflectionOn>>16)&255)>10,"local emitter reflects a warm specular highlight in water");
 Require(reflectedLight(10,0)==0xff000000,"reflection toggle suppresses local water highlight");Require(reflectedLight(2,1)==0xff000000,"water reflection cannot exceed emitter radius");
 manager.Configure(L".\\");manager.SelectForFrame({-9492.77f,71.4f,60.45f},{0,1,0});
 bool outdoorLamp=false;
 for(const auto& l:manager.Selected())
  if(Length(l.position-Vec3{-9499.8376f,60.7080f,59.3262f})<.1f)outdoorLamp=true;
 Require(outdoorLamp,"actual screenshot-area street lamp is selected from shipped manifest");
 lighting.Reset(d.Get());fog.Reset(d.Get());DestroyWindow(window);puts("PASS atmosphere regression suite");return 0;
} catch(const std::exception& e) {printf("FAIL %s\n",e.what());return 1;}
