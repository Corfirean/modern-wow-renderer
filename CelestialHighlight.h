#pragma once
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <vector>
#pragma comment(lib,"d3dcompiler.lib")

// Debug-only, opt-in visual confirmation for the celestial-draw hypothesis
// found via src/Diagnostics/CelestialDrawDiagnostics.h (F9 capture): tints
// any draw matching that same classification rule bright magenta, live,
// in-game. If the tint lands exactly on the visible sun/moon disc, the
// classification is confirmed; if not, it isn't - this exists to prove or
// disprove the hypothesis, not to assert it.
//
// Classification (must ALL hold - same signature the F9 capture found):
//   - primitive count <= 8 (a couple of quads at most - this is what
//     actually separates the sun/moon disc from the sky dome/gradient mesh:
//     both can share fixed-function + zero-translation-VIEW + large WORLD
//     scale, since they're all skybox layers, but the dome is a large mesh
//     and the disc is one or two triangles)
//   - fixed-function vertex AND pixel processing (no VS/PS bound)
//   - alpha blend AND alpha test both on
//   - a texture bound on stage 0
//   - D3DTS_VIEW has (near) zero translation - the classic skybox trick:
//     camera position doesn't move it, only camera rotation does
//   - D3DTS_WORLD has a large scale (this is a big billboard, not a tiny
//     screen-space UI quad that also happens to be fixed-function)
// Read-only w.r.t. game state otherwise: only ever swaps the pixel shader
// for the duration of one draw call, then restores it, identically to the
// established WaterHighlight.h pattern this mirrors.
namespace celestialhighlight {
using Microsoft::WRL::ComPtr;
bool enabled=false;
std::vector<DWORD> code;
bool codeReady=false;

void Configure(const std::wstring&) {
    const char* source="float4 color:register(c0);float4 main():COLOR{return color;}";
    ComPtr<ID3DBlob> blob,error;
    if(FAILED(D3DCompile(source,strlen(source),nullptr,nullptr,nullptr,"main","ps_2_0",0,0,blob.GetAddressOf(),error.GetAddressOf())))
        return;
    code.resize(blob->GetBufferSize()/sizeof(DWORD));
    memcpy(code.data(),blob->GetBufferPointer(),blob->GetBufferSize());
    codeReady=true;
}
void ReloadTuning(const std::wstring& base) {
    auto ini=base+L"GraphicsEffects.ini";
    enabled=GetPrivateProfileIntW(L"Atmosphere",L"DebugCelestialCandidateHighlight",0,ini.c_str())!=0;
}

struct Scope {
    IDirect3DDevice9* device=nullptr;
    bool changed=false;
    explicit Scope(IDirect3DDevice9* d,UINT primitiveCount,bool skip=false) noexcept {
        if(skip||!enabled||!codeReady)return;
        if(primitiveCount>8)return;
        try {
            ComPtr<IDirect3DVertexShader9> vs;
            if(FAILED(d->GetVertexShader(vs.GetAddressOf()))||vs)return;
            ComPtr<IDirect3DPixelShader9> ps;
            if(FAILED(d->GetPixelShader(ps.GetAddressOf()))||ps)return;

            DWORD alphaBlend=0,alphaTest=0;
            d->GetRenderState(D3DRS_ALPHABLENDENABLE,&alphaBlend);
            d->GetRenderState(D3DRS_ALPHATESTENABLE,&alphaTest);
            if(!alphaBlend||!alphaTest)return;

            ComPtr<IDirect3DBaseTexture9> texture;
            if(FAILED(d->GetTexture(0,texture.GetAddressOf()))||!texture)return;

            D3DMATRIX view{};
            if(FAILED(d->GetTransform(D3DTS_VIEW,&view)))return;
            if(fabsf(view.m[3][0])>0.01f||fabsf(view.m[3][1])>0.01f||fabsf(view.m[3][2])>0.01f)return;

            D3DMATRIX world{};
            if(FAILED(d->GetTransform(D3DTS_WORLD,&world)))return;
            float scaleSq=world.m[0][0]*world.m[0][0]+world.m[0][1]*world.m[0][1]+world.m[0][2]*world.m[0][2];
            if(scaleSq<4.0f)return;

            ComPtr<IDirect3DPixelShader9> replacement;
            if(FAILED(d->CreatePixelShader(code.data(),replacement.GetAddressOf())))return;
            if(FAILED(d->SetPixelShader(replacement.Get())))return;
            static const float magenta[4]={1,0,1,1};
            device=d;changed=true;
            if(FAILED(d->SetPixelShaderConstantF(0,magenta,1)))Restore();
        } catch(...) { Restore(); }
    }
    void Restore() noexcept {
        if(changed){device->SetPixelShader(nullptr);changed=false;}
    }
    ~Scope(){Restore();}
    Scope(const Scope&)=delete;
    Scope& operator=(const Scope&)=delete;
};
}
