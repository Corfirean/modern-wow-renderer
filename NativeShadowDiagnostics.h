#pragma once

#include <windows.h>
#include <d3d9.h>
#include <d3dcompiler.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>
#include "src/D3D9/ScopedRenderState.h"

namespace nativeshadowdiag
{
struct TargetRecord
{
    const void* texture = nullptr;
    UINT width = 0;
    UINT height = 0;
    D3DFORMAT format = D3DFMT_UNKNOWN;
    uint64_t lastProducerVs = 0;
    uint64_t lastProducerPs = 0;
    uint64_t lastProducerFrame = 0;
    uint32_t lastProducerDraw = 0;
    bool reuseLogged = false;
    bool depthTarget = false;
};

inline bool enabled = false;
inline bool logConstants = true;
inline uint32_t summaryInterval = 300;
inline uint32_t maxUniqueCandidates = 256;
inline std::wstring logPath;
inline uint64_t frame = 0;
inline uint32_t drawOrdinal = 0;
inline UINT backBufferWidth = 0;
inline UINT backBufferHeight = 0;
inline const void* currentRtTexture = nullptr;
inline D3DSURFACE_DESC currentRtDesc{};
inline bool currentRtValid = false;
inline const void* currentDsTexture = nullptr;
inline D3DSURFACE_DESC currentDsDesc{};
inline bool currentDsValid = false;
inline std::vector<TargetRecord> targets;
inline std::unordered_set<uint64_t> loggedSignatures;
inline uint32_t candidateDrawsThisFrame = 0;
inline uint32_t rtReuseEventsThisFrame = 0;
inline std::array<bool, 16> boundFormerRt{};
inline std::array<IDirect3DTexture9*, 16> previewTextureByStage{};
inline std::unordered_set<uint64_t> dumpedVertexShaders;
inline std::unordered_set<uint64_t> dumpedPixelShaders;
inline std::wstring dumpDirectory;
inline bool previewEnabled = false;
inline bool previewKeyDown = false;
inline IDirect3DDevice9* previewOwner = nullptr;
inline IDirect3DPixelShader9* previewShader = nullptr;

inline uint64_t Mix(uint64_t h, uint64_t v)
{
    h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
    return h;
}

inline const char* FormatName(D3DFORMAT f)
{
    switch (f)
    {
    case D3DFMT_A8R8G8B8: return "A8R8G8B8";
    case D3DFMT_X8R8G8B8: return "X8R8G8B8";
    case D3DFMT_R5G6B5: return "R5G6B5";
    case D3DFMT_A16B16G16R16F: return "A16B16G16R16F";
    case D3DFMT_R16F: return "R16F";
    case D3DFMT_R32F: return "R32F";
    case D3DFMT_D16: return "D16";
    case D3DFMT_D24X8: return "D24X8";
    case D3DFMT_D24S8: return "D24S8";
    case D3DFMT_D24X4S4: return "D24X4S4";
    default: return "OTHER";
    }
}

inline const char* PrimitiveName(D3DPRIMITIVETYPE t)
{
    switch (t)
    {
    case D3DPT_POINTLIST: return "POINTLIST";
    case D3DPT_LINELIST: return "LINELIST";
    case D3DPT_LINESTRIP: return "LINESTRIP";
    case D3DPT_TRIANGLELIST: return "TRIANGLELIST";
    case D3DPT_TRIANGLESTRIP: return "TRIANGLESTRIP";
    case D3DPT_TRIANGLEFAN: return "TRIANGLEFAN";
    default: return "UNKNOWN";
    }
}

inline void Append(const std::string& text)
{
    if (logPath.empty()) return;
    std::ofstream out(logPath, std::ios::app);
    if (out) out << text;
}

inline const void* TextureContainer(IDirect3DSurface9* surface)
{
    if (!surface) return nullptr;
    IDirect3DTexture9* texture = nullptr;
    if (FAILED(surface->GetContainer(__uuidof(IDirect3DTexture9), reinterpret_cast<void**>(&texture))) || !texture)
        return nullptr;
    const void* identity = texture;
    texture->Release();
    return identity;
}

inline TargetRecord* FindTarget(const void* texture)
{
    if (!texture) return nullptr;
    auto it = std::find_if(targets.begin(), targets.end(), [texture](const TargetRecord& r) { return r.texture == texture; });
    return it == targets.end() ? nullptr : &*it;
}

inline void Configure(const std::wstring& basePath)
{
    const std::wstring ini = basePath + L"GraphicsEffects.ini";
    enabled = GetPrivateProfileIntW(L"ShadowDiagnostics", L"Enabled", 1, ini.c_str()) != 0;
    logConstants = GetPrivateProfileIntW(L"ShadowDiagnostics", L"LogConstants", 1, ini.c_str()) != 0;
    summaryInterval = static_cast<uint32_t>(std::clamp(static_cast<int>(GetPrivateProfileIntW(L"ShadowDiagnostics", L"SummaryIntervalFrames", 300, ini.c_str())), 60, 3600));
    maxUniqueCandidates = static_cast<uint32_t>(std::clamp(static_cast<int>(GetPrivateProfileIntW(L"ShadowDiagnostics", L"MaxUniqueCandidates", 256, ini.c_str())), 32, 1024));
    logPath = basePath + L"NativeShadowDiagnostics.log";
    dumpDirectory = basePath + L"NativeShadowShaders";
    previewEnabled = GetPrivateProfileIntW(L"ShadowDiagnostics", L"PreviewEnabled", 1, ini.c_str()) != 0;
    CreateDirectoryW(dumpDirectory.c_str(), nullptr);
    if (enabled)
    {
        std::ofstream out(logPath, std::ios::trunc);
        if (out)
            out << "Native WoW shadow diagnostics (read-only)\n"
                << "Candidates: offscreen RT, depth-only draw, projected blended draw, or prior RT sampled as texture.\n"
                << "No draw replay and no render output modification are performed.\n\n";
    }
}

inline void Reset()
{
    frame = 0;
    drawOrdinal = 0;
    currentRtTexture = nullptr;
    currentRtValid = false;
    currentDsTexture = nullptr;
    currentDsValid = false;
    targets.clear();
    loggedSignatures.clear();
    candidateDrawsThisFrame = 0;
    rtReuseEventsThisFrame = 0;
    boundFormerRt.fill(false);
    for (auto*& texture : previewTextureByStage)
    {
        if (texture) texture->Release();
        texture = nullptr;
    }
    if (previewShader) previewShader->Release();
    previewShader = nullptr;
    previewOwner = nullptr;
    dumpedVertexShaders.clear();
    dumpedPixelShaders.clear();
}

inline void OnSetRenderTarget(DWORD index, IDirect3DSurface9* surface)
{
    if (!enabled || index != 0 || !surface) return;
    D3DSURFACE_DESC desc{};
    if (FAILED(surface->GetDesc(&desc))) return;
    currentRtDesc = desc;
    currentRtValid = true;
    currentRtTexture = TextureContainer(surface);

    if (!currentRtTexture)
    {
        backBufferWidth = std::max(backBufferWidth, desc.Width);
        backBufferHeight = std::max(backBufferHeight, desc.Height);
        return;
    }

    TargetRecord* record = FindTarget(currentRtTexture);
    if (!record)
    {
        targets.push_back({ currentRtTexture, desc.Width, desc.Height, desc.Format, 0, 0, 0, 0, false, false });
        record = &targets.back();
        std::ostringstream s;
        s << "[RT_DISCOVER] frame=" << frame << " texture=" << currentRtTexture
          << " size=" << desc.Width << 'x' << desc.Height
          << " format=" << FormatName(desc.Format) << '(' << unsigned(desc.Format) << ")\n";
        Append(s.str());
    }
}

inline void OnSetDepth(IDirect3DSurface9* surface)
{
    if (!enabled) return;
    currentDsTexture = nullptr;
    currentDsValid = false;
    if (!surface) return;
    D3DSURFACE_DESC desc{};
    if (FAILED(surface->GetDesc(&desc))) return;
    currentDsDesc = desc;
    currentDsValid = true;
    currentDsTexture = TextureContainer(surface);
    if (!currentDsTexture) return;

    TargetRecord* record = FindTarget(currentDsTexture);
    if (!record)
    {
        targets.push_back({ currentDsTexture, desc.Width, desc.Height, desc.Format, 0, 0, 0, 0, false, true });
        std::ostringstream s;
        s << "[DS_DISCOVER] frame=" << frame << " texture=" << currentDsTexture
          << " size=" << desc.Width << 'x' << desc.Height
          << " format=" << FormatName(desc.Format) << '(' << unsigned(desc.Format) << ")\n";
        Append(s.str());
    }
}

inline void OnSetTexture(DWORD stage, IDirect3DBaseTexture9* texture)
{
    if (!enabled) return;
    if (stage < boundFormerRt.size()) boundFormerRt[stage] = texture && FindTarget(texture);
    if (stage < previewTextureByStage.size())
    {
        IDirect3DTexture9* next = nullptr;
        if (texture && texture->GetType() == D3DRTYPE_TEXTURE)
        {
            TargetRecord* record = FindTarget(texture);
            if (record && record->depthTarget) next = static_cast<IDirect3DTexture9*>(texture);
        }
        if (next) next->AddRef();
        if (previewTextureByStage[stage]) previewTextureByStage[stage]->Release();
        previewTextureByStage[stage] = next;
    }
    if (!texture) return;
    TargetRecord* record = FindTarget(texture);
    if (!record || record->lastProducerFrame == 0 || record->reuseLogged) return;
    record->reuseLogged = true;
    ++rtReuseEventsThisFrame;
    std::ostringstream s;
    s << "[RT_REUSED_AS_TEXTURE] frame=" << frame << " draw=" << drawOrdinal
      << " stage=s" << stage << " texture=" << texture
      << " size=" << record->width << 'x' << record->height
      << " format=" << FormatName(record->format)
      << " role=" << (record->depthTarget ? "depth" : "color")
      << " producer_frame=" << record->lastProducerFrame
      << " producer_draw=" << record->lastProducerDraw
      << " producer_vs=0x" << std::hex << record->lastProducerVs
      << " producer_ps=0x" << record->lastProducerPs << std::dec << "\n";
    Append(s.str());
}

inline void LogConstantsBlock(IDirect3DDevice9* d, std::ostringstream& s)
{
    if (!logConstants) return;
    float constants[16][4]{};
    if (SUCCEEDED(d->GetVertexShaderConstantF(0, constants[0], 16)))
    {
        s << "  VS c0-c15:\n";
        for (int i = 0; i < 16; ++i)
            s << "    c" << i << "={" << constants[i][0] << ',' << constants[i][1] << ',' << constants[i][2] << ',' << constants[i][3] << "}\n";
    }
    if (SUCCEEDED(d->GetPixelShaderConstantF(0, constants[0], 16)))
    {
        s << "  PS c0-c15:\n";
        for (int i = 0; i < 16; ++i)
            s << "    c" << i << "={" << constants[i][0] << ',' << constants[i][1] << ',' << constants[i][2] << ',' << constants[i][3] << "}\n";
    }
}

template<typename Shader>
inline void DumpShader(Shader* shader, uint64_t hash, const wchar_t* stage, std::unordered_set<uint64_t>& dumped)
{
    if (!shader || !hash || !dumped.insert(hash).second) return;
    UINT size = 0;
    if (FAILED(shader->GetFunction(nullptr, &size)) || !size || size > 65536) return;
    std::vector<BYTE> bytes(size);
    if (FAILED(shader->GetFunction(bytes.data(), &size))) return;

    wchar_t name[96]{};
    swprintf_s(name, L"\\%s_%016llx.bin", stage, static_cast<unsigned long long>(hash));
    HANDLE file = CreateFileW((dumpDirectory + name).c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE)
    {
        DWORD written = 0;
        WriteFile(file, bytes.data(), size, &written, nullptr);
        CloseHandle(file);
    }

    ID3DBlob* disassembly = nullptr;
    if (SUCCEEDED(D3DDisassemble(bytes.data(), size, D3D_DISASM_ENABLE_INSTRUCTION_NUMBERING, nullptr, &disassembly)) && disassembly)
    {
        swprintf_s(name, L"\\%s_%016llx.asm", stage, static_cast<unsigned long long>(hash));
        file = CreateFileW((dumpDirectory + name).c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE)
        {
            DWORD written = 0;
            WriteFile(file, disassembly->GetBufferPointer(), static_cast<DWORD>(disassembly->GetBufferSize()), &written, nullptr);
            CloseHandle(file);
        }
        disassembly->Release();
    }
}

inline bool EnsurePreviewShader(IDirect3DDevice9* d)
{
    if (previewOwner == d && previewShader) return true;
    if (previewShader) previewShader->Release();
    previewShader = nullptr;
    previewOwner = d;
    static const char source[] =
        "sampler2D shadowMap:register(s0); float4 tuning:register(c0);"
        "float4 main(float2 uv:TEXCOORD0):COLOR0{"
        "float depth=tex2D(shadowMap,uv).r;"
        "float visible=pow(saturate((1-depth)*tuning.x),.35);"
        "return float4(visible.xxx,1);}";
    ID3DBlob* code = nullptr;
    ID3DBlob* errors = nullptr;
    const HRESULT hr = D3DCompile(source, sizeof(source) - 1, nullptr, nullptr, nullptr, "main", "ps_3_0",
        D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
    if (errors) errors->Release();
    if (FAILED(hr) || !code) return false;
    const HRESULT createHr = d->CreatePixelShader(static_cast<const DWORD*>(code->GetBufferPointer()), &previewShader);
    code->Release();
    return SUCCEEDED(createHr);
}

inline void DrawPreview(IDirect3DDevice9* d)
{
    if (!enabled || !previewEnabled || !d || !EnsurePreviewShader(d)) return;
    int firstStage = -1;
    for (int start : { 5, 4 })
    {
        bool complete = true;
        for (int i = 0; i < 4; ++i) complete = complete && previewTextureByStage[start + i];
        if (complete) { firstStage = start; break; }
    }
    if (firstStage < 0) return;

    renderer::ScopedRenderState state(d);
    D3DVIEWPORT9 viewport{};
    if (FAILED(d->GetViewport(&viewport))) return;
    struct Vertex { float x, y, z, rhw, u, v; };
    const float tile = std::clamp(float(viewport.Height) * .16f, 128.f, 230.f);
    const float gap = 5.f;
    const float left = float(viewport.X + viewport.Width) - (tile + gap) * 4.f - gap;
    const float top = float(viewport.Y) + gap;

    d->SetVertexShader(nullptr);
    d->SetPixelShader(previewShader);
    d->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1);
    d->SetRenderState(D3DRS_ZENABLE, FALSE);
    d->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    d->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    d->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    d->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    d->SetRenderState(D3DRS_COLORWRITEENABLE, 0xf);
    const float tuning[4] = { 64.f, 0, 0, 0 };
    d->SetPixelShaderConstantF(0, tuning, 1);
    d->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
    d->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
    d->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    d->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    for (int i = 0; i < 4; ++i)
    {
        const float x0 = left + (tile + gap) * i;
        const float x1 = x0 + tile;
        const float y0 = top;
        const float y1 = y0 + tile;
        Vertex quad[] = {
            { x0 - .5f, y0 - .5f, 0, 1, 0, 0 }, { x1 - .5f, y0 - .5f, 0, 1, 1, 0 },
            { x0 - .5f, y1 - .5f, 0, 1, 0, 1 }, { x1 - .5f, y1 - .5f, 0, 1, 1, 1 }
        };
        d->SetTexture(0, previewTextureByStage[firstStage + i]);
        d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(Vertex));
    }
    d->SetTexture(0, nullptr);
}

inline void OnDraw(IDirect3DDevice9* d, const char* api, D3DPRIMITIVETYPE primitiveType, UINT primitiveCount,
                   uint64_t vsHash, uint64_t psHash, DWORD fvf, const void* vertexDecl,
                   bool alphaBlend, bool alphaTest, bool zEnable, bool zWrite)
{
    if (!enabled || !d) return;
    ++drawOrdinal;

    DWORD colorWrite = 0xf;
    DWORD srcBlend = 0, dstBlend = 0, cull = 0, stencil = 0;
    d->GetRenderState(D3DRS_COLORWRITEENABLE, &colorWrite);
    const bool offscreen = currentRtValid && currentRtTexture &&
        (currentRtDesc.Width < std::max(1u, backBufferWidth) || currentRtDesc.Height < std::max(1u, backBufferHeight));
    const bool depthOnly = colorWrite == 0 && zEnable && zWrite;
    const bool projectedBlend = !currentRtTexture && alphaBlend && zEnable && !zWrite;
    const bool samplesPriorRt = std::any_of(boundFormerRt.begin(), boundFormerRt.end(), [](bool value) { return value; });
    const bool candidate = offscreen || depthOnly || projectedBlend || samplesPriorRt;
    if (!candidate) return;

    d->GetRenderState(D3DRS_SRCBLEND, &srcBlend);
    d->GetRenderState(D3DRS_DESTBLEND, &dstBlend);
    d->GetRenderState(D3DRS_CULLMODE, &cull);
    d->GetRenderState(D3DRS_STENCILENABLE, &stencil);
    D3DVIEWPORT9 viewport{};
    d->GetViewport(&viewport);

    std::array<IDirect3DBaseTexture9*, 16> rawTextures{};
    for (DWORD stage = 0; stage < rawTextures.size(); ++stage)
    {
        d->GetTexture(stage, &rawTextures[stage]);
    }

    ++candidateDrawsThisFrame;
    uint64_t signature = 0xcbf29ce484222325ull;
    signature = Mix(signature, vsHash); signature = Mix(signature, psHash);
    signature = Mix(signature, currentRtDesc.Width); signature = Mix(signature, currentRtDesc.Height);
    signature = Mix(signature, uint32_t(currentRtDesc.Format)); signature = Mix(signature, colorWrite);
    signature = Mix(signature, reinterpret_cast<uintptr_t>(currentDsTexture));
    const uint64_t stateBits = uint64_t(alphaBlend) | (uint64_t(alphaTest) << 1) |
        (uint64_t(zEnable) << 2) | (uint64_t(zWrite) << 3);
    signature = Mix(signature, stateBits);
    signature = Mix(signature, srcBlend | (uint64_t(dstBlend) << 16));

    if (currentRtTexture)
    {
        if (TargetRecord* record = FindTarget(currentRtTexture))
        {
            record->lastProducerVs = vsHash;
            record->lastProducerPs = psHash;
            record->lastProducerFrame = frame;
            record->lastProducerDraw = drawOrdinal;
        }
    }
    if (currentDsTexture)
    {
        if (TargetRecord* record = FindTarget(currentDsTexture))
        {
            record->lastProducerVs = vsHash;
            record->lastProducerPs = psHash;
            record->lastProducerFrame = frame;
            record->lastProducerDraw = drawOrdinal;
        }
    }

    const bool first = loggedSignatures.size() < maxUniqueCandidates && loggedSignatures.insert(signature).second;
    if (first)
    {
        IDirect3DSurface9* depth = nullptr;
        D3DSURFACE_DESC depthDesc{};
        if (SUCCEEDED(d->GetDepthStencilSurface(&depth)) && depth)
        {
            depth->GetDesc(&depthDesc);
            depth->Release();
        }

        std::ostringstream s;
        s << std::setprecision(6)
          << "[CANDIDATE] frame=" << frame << " draw=" << drawOrdinal << " api=" << api
          << " primitive=" << PrimitiveName(primitiveType) << " count=" << primitiveCount << " reasons="
          << (offscreen ? "offscreen_rt," : "") << (depthOnly ? "depth_only," : "")
          << (projectedBlend ? "projected_blend," : "") << (samplesPriorRt ? "samples_prior_rt," : "") << "\n"
          << "  VS=0x" << std::hex << vsHash << " PS=0x" << psHash << std::dec
          << " vdecl=" << vertexDecl << " fvf=0x" << std::hex << fvf << std::dec << "\n"
          << "  RT=" << currentRtDesc.Width << 'x' << currentRtDesc.Height << ' ' << FormatName(currentRtDesc.Format)
          << '(' << unsigned(currentRtDesc.Format) << ") texture=" << currentRtTexture << "\n"
          << "  DS=" << depthDesc.Width << 'x' << depthDesc.Height << ' ' << FormatName(depthDesc.Format)
          << '(' << unsigned(depthDesc.Format) << ") texture=" << currentDsTexture
          << " viewport={" << viewport.X << ',' << viewport.Y << ','
          << viewport.Width << ',' << viewport.Height << ',' << viewport.MinZ << ',' << viewport.MaxZ << "}\n"
          << "  state colorWrite=0x" << std::hex << colorWrite << std::dec
          << " alphaBlend=" << alphaBlend << " src=" << srcBlend << " dst=" << dstBlend
          << " alphaTest=" << alphaTest << " zEnable=" << zEnable << " zWrite=" << zWrite
          << " cull=" << cull << " stencil=" << stencil << "\n";

        for (DWORD stage = 0; stage < rawTextures.size(); ++stage)
        {
            IDirect3DBaseTexture9* texture = rawTextures[stage];
            if (!texture) continue;
            D3DRESOURCETYPE type = texture->GetType();
            if (type == D3DRTYPE_TEXTURE)
            {
                D3DSURFACE_DESC td{};
                static_cast<IDirect3DTexture9*>(texture)->GetLevelDesc(0, &td);
                s << "  s" << stage << '=' << texture << ' ' << td.Width << 'x' << td.Height << ' '
                  << FormatName(td.Format) << '(' << unsigned(td.Format) << ')'
                  << (FindTarget(texture) ? " [FORMER_RT]" : "") << "\n";
            }
            else s << "  s" << stage << '=' << texture << " type=" << unsigned(type) << "\n";
        }
        LogConstantsBlock(d, s);
        for (DWORD stage = 4; stage <= 8; ++stage)
        {
            if (!rawTextures[stage] || !FindTarget(rawTextures[stage]) || !FindTarget(rawTextures[stage])->depthTarget) continue;
            DWORD minFilter = 0, magFilter = 0, mipFilter = 0, addressU = 0, addressV = 0, border = 0;
            d->GetSamplerState(stage, D3DSAMP_MINFILTER, &minFilter);
            d->GetSamplerState(stage, D3DSAMP_MAGFILTER, &magFilter);
            d->GetSamplerState(stage, D3DSAMP_MIPFILTER, &mipFilter);
            d->GetSamplerState(stage, D3DSAMP_ADDRESSU, &addressU);
            d->GetSamplerState(stage, D3DSAMP_ADDRESSV, &addressV);
            d->GetSamplerState(stage, D3DSAMP_BORDERCOLOR, &border);
            s << "  sampler s" << stage << " min=" << minFilter << " mag=" << magFilter << " mip=" << mipFilter
              << " addressU=" << addressU << " addressV=" << addressV << " border=0x" << std::hex << border << std::dec << "\n";
        }
        if (samplesPriorRt)
        {
            IDirect3DVertexShader9* vs = nullptr;
            IDirect3DPixelShader9* ps = nullptr;
            if (SUCCEEDED(d->GetVertexShader(&vs)) && vs)
            {
                DumpShader(vs, vsHash, L"vs", dumpedVertexShaders);
                vs->Release();
            }
            if (SUCCEEDED(d->GetPixelShader(&ps)) && ps)
            {
                DumpShader(ps, psHash, L"ps", dumpedPixelShaders);
                ps->Release();
            }
        }
        s << '\n';
        Append(s.str());
    }

    for (auto* texture : rawTextures) if (texture) texture->Release();
}

inline void OnPresent(IDirect3DDevice9* d)
{
    if (!enabled) return;
    ++frame;
    if (d && (!backBufferWidth || !backBufferHeight))
    {
        IDirect3DSurface9* rt = nullptr;
        if (SUCCEEDED(d->GetRenderTarget(0, &rt)) && rt)
        {
            D3DSURFACE_DESC desc{};
            if (SUCCEEDED(rt->GetDesc(&desc))) { backBufferWidth = desc.Width; backBufferHeight = desc.Height; }
            rt->Release();
        }
    }
    if (frame % summaryInterval == 0)
    {
        std::ostringstream s;
        s << "[SUMMARY] frame=" << frame << " unique_candidates=" << loggedSignatures.size()
          << " tracked_texture_rts=" << targets.size() << " last_frame_candidate_draws=" << candidateDrawsThisFrame
          << " last_frame_rt_reuse_events=" << rtReuseEventsThisFrame << "\n";
        Append(s.str());
    }
    drawOrdinal = 0;
    candidateDrawsThisFrame = 0;
    rtReuseEventsThisFrame = 0;
    for (auto& target : targets) target.reuseLogged = false;
}
}
