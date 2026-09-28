#pragma once
#include "src/D3D9/TrackedRenderState.h"
namespace waterreflection {
using Microsoft::WRL::ComPtr;
struct Resources{
    ComPtr<IDirect3DTexture9> scene;
    ComPtr<IDirect3DSurface9> surface;
    UINT width=0, height=0;
    IDirect3DDevice9* owner=nullptr;
};
inline Resources& resources=*new Resources;
inline bool ready=false;
inline unsigned copiedFrames=0,failedFrames=0;
inline std::wstring logPath;

inline void Configure(const std::wstring& base){logPath=base+L"WaterReflection.log";}
inline void ClearInput(){watereffect::reflectionScene=nullptr;watereffect::reflectionDepth=nullptr;memset(watereffect::reflectionData,0,sizeof(watereffect::reflectionData));}

inline bool IsWater(IDirect3DDevice9* d){
 // This must use exactly the same material identity as watereffect::Scope.
 // A shader whitelist here caused the pre-water scene copy to happen only
 // after an unlisted liquid tile had already rendered. The copy consequently
 // contained part of the water surface itself and later tiles sampled a
 // different SSR/refraction input, producing large geometric seams whose
 // position changed with draw order/camera angle.
 if(!d)return false;
 ComPtr<IDirect3DBaseTexture9> tex0,tex1;
 if(FAILED(d->GetTexture(0,tex0.GetAddressOf()))||!tex0||tex0->GetType()!=D3DRTYPE_TEXTURE||
    FAILED(d->GetTexture(1,tex1.GetAddressOf()))||!tex1||tex1->GetType()!=D3DRTYPE_TEXTURE)return false;
 D3DSURFACE_DESC d0{},d1{};
 if(FAILED(static_cast<IDirect3DTexture9*>(tex0.Get())->GetLevelDesc(0,&d0))||
    FAILED(static_cast<IDirect3DTexture9*>(tex1.Get())->GetLevelDesc(0,&d1)))return false;
 return d0.Width==8&&d0.Height==64&&d1.Width==512&&d1.Height==512;
}

inline void Expose(){
 if(!ready||!resources.scene)return;
 watereffect::reflectionScene=resources.scene.Get();
 watereffect::reflectionDepth=volume::depth.Get();
 memcpy(watereffect::reflectionData,volume::constants[0],sizeof(float)*4);
 watereffect::reflectionData[5]=1.f/float(std::max<UINT>(resources.width,1));
 watereffect::reflectionData[6]=1.f/float(std::max<UINT>(resources.height,1));
 watereffect::reflectionData[7]=volume::constants[2][3];
}

inline void Prepare(IDirect3DDevice9* d){
 if(!watereffect::enabled||!watereffect::active||!watereffect::effectEnabled||
    !watereffect::reflectionsEnabled||volume::internal||!IsWater(d))return;
 if(ready){Expose();return;}
 if(volume::owner!=d||!volume::ready||!volume::target||!volume::depth)return;
 D3DSURFACE_DESC desc{};
 if(FAILED(volume::target->GetDesc(&desc))||desc.MultiSampleType!=D3DMULTISAMPLE_NONE){++failedFrames;return;}
 if(resources.owner!=d||resources.width!=desc.Width||resources.height!=desc.Height||!resources.scene||!resources.surface){
     resources.scene.Reset();
     resources.surface.Reset();
     resources.owner=d;
     resources.width=desc.Width;
     resources.height=desc.Height;
     if(FAILED(d->CreateTexture(desc.Width,desc.Height,1,D3DUSAGE_RENDERTARGET,desc.Format,D3DPOOL_DEFAULT,resources.scene.GetAddressOf(),nullptr))||
        FAILED(resources.scene->GetSurfaceLevel(0,resources.surface.GetAddressOf()))){
         ++failedFrames;return;
     }
 }
 if(FAILED(d->StretchRect(volume::target.Get(),nullptr,resources.surface.Get(),nullptr,D3DTEXF_NONE))){
     ++failedFrames;return;
 }
 ready=true;++copiedFrames;Expose();
 if(copiedFrames==1)std::ofstream(std::filesystem::path(logPath),std::ios::app)<<"SSR source captured before first confirmed water draw "<<desc.Width<<'x'<<desc.Height<<'\n';
}

inline void Finish(){ClearInput();ready=false;}
inline void Reset(IDirect3DDevice9* /*d*/){Finish();resources.surface.Reset();resources.scene.Reset();resources.owner=nullptr;resources.width=resources.height=0;}
inline void Present(){if((copiedFrames+failedFrames)%600==120)std::ofstream(std::filesystem::path(logPath),std::ios::app)<<"copied="<<copiedFrames<<" failed="<<failedFrames<<'\n';Finish();}
}
