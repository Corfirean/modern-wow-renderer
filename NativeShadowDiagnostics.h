#pragma once

#include <windows.h>
#include <d3d9.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

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
inline std::vector<TargetRecord> targets;
inline std::unordered_set<uint64_t> loggedSignatures;
inline uint32_t candidateDrawsThisFrame = 0;
inline uint32_t rtReuseEventsThisFrame = 0;
inline std::array<bool, 8> boundFormerRt{};

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
    targets.clear();
    loggedSignatures.clear();
    candidateDrawsThisFrame = 0;
    rtReuseEventsThisFrame = 0;
    boundFormerRt.fill(false);
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
        targets.push_back({ currentRtTexture, desc.Width, desc.Height, desc.Format });
        record = &targets.back();
        std::ostringstream s;
        s << "[RT_DISCOVER] frame=" << frame << " texture=" << currentRtTexture
          << " size=" << desc.Width << 'x' << desc.Height
          << " format=" << FormatName(desc.Format) << '(' << unsigned(desc.Format) << ")\n";
        Append(s.str());
    }
}

inline void OnSetTexture(DWORD stage, IDirect3DBaseTexture9* texture)
{
    if (!enabled) return;
    if (stage < boundFormerRt.size()) boundFormerRt[stage] = texture && FindTarget(texture);
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

    std::array<IDirect3DBaseTexture9*, 8> rawTextures{};
    for (DWORD stage = 0; stage < rawTextures.size(); ++stage)
    {
        d->GetTexture(stage, &rawTextures[stage]);
    }

    ++candidateDrawsThisFrame;
    uint64_t signature = 0xcbf29ce484222325ull;
    signature = Mix(signature, vsHash); signature = Mix(signature, psHash);
    signature = Mix(signature, currentRtDesc.Width); signature = Mix(signature, currentRtDesc.Height);
    signature = Mix(signature, uint32_t(currentRtDesc.Format)); signature = Mix(signature, colorWrite);
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
          << '(' << unsigned(depthDesc.Format) << ") viewport={" << viewport.X << ',' << viewport.Y << ','
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
