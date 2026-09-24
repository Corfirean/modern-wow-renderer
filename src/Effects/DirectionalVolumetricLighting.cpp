#include "DirectionalVolumetricLighting.h"
#include <d3dcompiler.h>
#include <algorithm>
#include <cmath>
#include <vector>
#include "../D3D9/ScopedRenderState.h"
#include "../Diagnostics/PerformanceProfiler.h"

#pragma comment(lib, "d3dcompiler.lib")

namespace renderer
{
    namespace
    {
        const char* const g_downsampleDepthSource = R"(
sampler2D fullDepth : register(s0);
float4 main(float2 uv : TEXCOORD0) : COLOR0 {
    return tex2D(fullDepth, uv);
}
)";

        const char* const g_raymarchSource = R"(
sampler2D depthMap : register(s0);
sampler2D shadowMap : register(s1);

float4 cameraPos : register(c0);       // xyz = world camera position
float4 sunDirWorld : register(c1);     // xyz = normalized world sun direction, w = strength
float4 sunColor : register(c2);        // rgb = light color, w = anisotropyG
float4 projUnpack : register(c3);      // x=P22, y=P32, z=P00, w=P11
float4 invView0 : register(c4);        // inverse view rotation row 0
float4 invView1 : register(c5);        // inverse view rotation row 1
float4 invView2 : register(c6);        // inverse view rotation row 2
float4 tuning0 : register(c7);         // x=maxDistance, y=sampleCount, z=density, w=extinction
float4 tuning1 : register(c8);         // x=shadowBias, y=shadowMapSize, z=shadowEnabled, w=debugMode
float4x4 shadowMatrix : register(c9);  // c9..c12: world to shadow matrix
float4 jitterParams : register(c13);   // x=frameIndex, y=resolutionDivisor, z=jitterEnabled, w=shadowStride
float4 heightDensity : register(c14);  // x=baseHeight, y=falloff, z=lowLayer, w=maxZ
float4 extraParams : register(c15);    // x=shadowPCF, y=skySampleCount, z=skyMaxDistance, w=unused

float PhaseHG(float cosTheta, float g)
{
    float g2 = g * g;
    return (1.0 - g2) / pow(max(1.0 + g2 - 2.0 * g * cosTheta, 0.001), 1.5);
}

float InterleavedGradientNoise(float2 pixelPos, float frame)
{
    pixelPos += frame * float2(5.588238, 5.588238);
    float3 magic = float3(0.06711056, 0.00583715, 52.9829189);
    return frac(magic.z * frac(dot(pixelPos, magic.xy)));
}

float SampleShadow(float3 worldPos, float bias, float shadowSize, float usePCF, float frame)
{
    float4 sc = mul(float4(worldPos, 1.0), shadowMatrix);
    float u = 0.5 * sc.x + 0.5 + 0.5 / shadowSize;
    float v = -0.5 * sc.y + 0.5 + 0.5 / shadowSize;
    float z = sc.z;

    if (u < 0.001 || u > 0.999 || v < 0.001 || v > 0.999 || z < 0.0 || z > 1.0)
    {
        return 1.0;
    }

    if (usePCF > 0.5)
    {
        float2 texelSize = float2(1.0 / shadowSize, 1.0 / shadowSize);
        float2 uvBase = float2(u, v);
        float4 d;
        d.x = tex2D(shadowMap, uvBase).r;
        d.y = tex2D(shadowMap, uvBase + float2(texelSize.x, 0.0)).r;
        d.z = tex2D(shadowMap, uvBase + float2(0.0, texelSize.y)).r;
        d.w = tex2D(shadowMap, uvBase + texelSize).r;

        float4 inLight = (z - bias <= d) ? 1.0 : 0.0;
        float2 f = frac(uvBase * shadowSize);
        return lerp(lerp(inLight.x, inLight.y, f.x), lerp(inLight.z, inLight.w, f.x), f.y);
    }
    else
    {
        // Temporal pseudo-PCF jitter offset based on frame index
        float fmod4 = frac(frame * 0.25) * 4.0;
        float2 offset = float2(0.0, 0.0);
        if (fmod4 >= 2.5) offset = float2(0.5 / shadowSize, 0.5 / shadowSize);
        else if (fmod4 >= 1.5) offset = float2(0.0, 0.5 / shadowSize);
        else if (fmod4 >= 0.5) offset = float2(0.5 / shadowSize, 0.0);

        float d = tex2D(shadowMap, float2(u, v) + offset).r;
        return (z - bias <= d) ? 1.0 : 0.0;
    }
}

float4 main(float2 uv : TEXCOORD0, float2 vpos : VPOS) : COLOR0
{
    float maxDist = tuning0.x;
    int samples = int(tuning0.y);
    float densityScale = tuning0.z;
    float extinction = tuning0.w;
    float g = sunColor.w;
    float bias = tuning1.x;
    float shadowSize = tuning1.y;
    bool shadowOn = (tuning1.z > 0.5);
    int debugMode = int(tuning1.w);

    float rawDepth = tex2D(depthMap, uv).r;
    bool isSky = (rawDepth >= 0.9999);
    float linearZ = isSky ? heightDensity.w : (projUnpack.y / (rawDepth - projUnpack.x));
    linearZ = clamp(linearZ, 0.0, heightDensity.w);

    float3 viewDir = float3((uv.x * 2.0 - 1.0) / projUnpack.z, (1.0 - uv.y * 2.0) / projUnpack.w, 1.0);
    float viewDist = length(viewDir);
    float3 viewRay = viewDir / viewDist;
    float3 worldRay = viewRay.x * invView0.xyz + viewRay.y * invView1.xyz + viewRay.z * invView2.xyz;
    worldRay = normalize(worldRay);

    float sceneDist = linearZ * viewDist;
    float maxMarch = isSky ? min(sceneDist, extraParams.z) : min(sceneDist, maxDist);
    if (maxMarch <= 0.01)
        return float4(0, 0, 0, 0);

    int actualSamples = isSky ? int(extraParams.y) : samples;
    float stepLength = maxMarch / float(actualSamples);

    float jitter = 0.5;
    if (jitterParams.z > 0.5)
    {
        jitter = InterleavedGradientNoise(vpos, jitterParams.x);
    }

    float cosTheta = dot(worldRay, sunDirWorld.xyz);
    float phase = PhaseHG(cosTheta, g);

    float3 accumRadiance = float3(0, 0, 0);
    float transmittance = 1.0;
    float avgShadow = 0.0;
    float avgDensity = 0.0;

    int shadowStride = max(1, int(jitterParams.w));
    float cachedVis = 1.0;

    for (int i = 0; i < actualSamples; ++i)
    {
        float t = (float(i) + jitter) * stepLength;
        float3 samplePos = cameraPos.xyz + worldRay * t;

        float h = samplePos.z - heightDensity.x;
        // Fast exp2: 1.442695 = log2(e)
        float expVal = clamp(h * heightDensity.y * 1.442695, -11.5, 11.5);
        float localDensity = exp2(-expVal) * densityScale;
        avgDensity += localDensity;

        // Skip negligible density samples to save shadow fetches & math
        if (localDensity < 0.0001)
        {
            continue;
        }

        // Interleaved shadow sampling: sample every shadowStride steps, reuse otherwise
        if (shadowOn)
        {
            if ((i % shadowStride) == 0)
            {
                cachedVis = SampleShadow(samplePos, bias, shadowSize, extraParams.x, jitterParams.x);
            }
        }
        else
        {
            cachedVis = 1.0;
        }
        avgShadow += cachedVis;

        float scattering = localDensity * cachedVis * phase;
        accumRadiance += transmittance * scattering * sunColor.rgb * stepLength;

        // Fast transmittance via exp2
        float extVal = clamp(localDensity * extinction * stepLength * 1.442695, 0.0, 11.5);
        transmittance *= exp2(-extVal);

        // Early termination when transmittance is saturated
        if (transmittance < 0.02)
        {
            break;
        }
    }

    avgShadow /= float(actualSamples);
    avgDensity /= float(actualSamples);

    if (debugMode == 2)
    {
        return float4(avgShadow, avgShadow, avgShadow, 1.0);
    }
    if (debugMode == 3)
    {
        return float4(avgDensity, avgDensity, avgDensity, 1.0);
    }
    if (debugMode == 4)
    {
        float pNorm = saturate(phase * 0.1);
        return float4(pNorm, pNorm, pNorm, 1.0);
    }

    float strength = sunDirWorld.w;
    float3 finalScattering = accumRadiance * strength;

    return float4(finalScattering, 1.0 - transmittance);
}
)";

        const char* const g_temporalSource = R"(
sampler2D currentMap : register(s0);
sampler2D historyMap : register(s1);

float4 temporalTuning : register(c0);
float4 texelSize : register(c1);

float4 main(float2 uv : TEXCOORD0) : COLOR0
{
    float4 cur = tex2D(currentMap, uv);
    if (temporalTuning.z < 0.5)
    {
        return cur;
    }

    float2 ts = texelSize.xy;
    // 5-tap cross clamp (center, left, right, up, down)
    float4 cL = tex2D(currentMap, uv - float2(ts.x, 0.0));
    float4 cR = tex2D(currentMap, uv + float2(ts.x, 0.0));
    float4 cU = tex2D(currentMap, uv - float2(0.0, ts.y));
    float4 cD = tex2D(currentMap, uv + float2(0.0, ts.y));

    float4 minVal = min(cur, min(min(cL, cR), min(cU, cD)));
    float4 maxVal = max(cur, max(max(cL, cR), max(cU, cD)));

    float expand = temporalTuning.y;
    minVal = max(minVal - expand, float4(0, 0, 0, 0));
    maxVal = maxVal + expand;

    float4 hist = tex2D(historyMap, uv);
    float4 clampedHist = clamp(hist, minVal, maxVal);

    float alpha = temporalTuning.x;
    return lerp(cur, clampedHist, alpha);
}
)";

        const char* const g_upsampleCompositeSource = R"(
sampler2D sceneMap : register(s0);
sampler2D lowResVolumetric : register(s1);
sampler2D fullResDepthMap : register(s2);
sampler2D lowResDepthMap : register(s3);

float4 texelSizes : register(c0);
float4 projUnpack : register(c1);
float4 compositeTuning : register(c2);

float Linearize(float raw, float P22, float P32, float maxZ)
{
    return (raw >= 0.9999) ? maxZ : (P32 / (raw - P22));
}

float4 main(float2 uv : TEXCOORD0) : COLOR0
{
    int debugMode = int(compositeTuning.x);
    float2 lowTexel = texelSizes.zw;

    float4 vol = float4(0, 0, 0, 0);

    if (projUnpack.w < 0.5)
    {
        // Direct fast bilinear sample
        vol = tex2D(lowResVolumetric, uv);
    }
    else
    {
        // 2x2 low-depth footprint
        float2 offsets[4] = {
            float2(-0.5, -0.5),
            float2( 0.5, -0.5),
            float2(-0.5,  0.5),
            float2( 0.5,  0.5)
        };

        float rawLow0 = tex2D(lowResDepthMap, uv + offsets[0] * lowTexel).r;
        float rawLow1 = tex2D(lowResDepthMap, uv + offsets[1] * lowTexel).r;
        float rawLow2 = tex2D(lowResDepthMap, uv + offsets[2] * lowTexel).r;
        float rawLow3 = tex2D(lowResDepthMap, uv + offsets[3] * lowTexel).r;

        float lowZ0 = Linearize(rawLow0, projUnpack.x, projUnpack.y, projUnpack.z);
        float lowZ1 = Linearize(rawLow1, projUnpack.x, projUnpack.y, projUnpack.z);
        float lowZ2 = Linearize(rawLow2, projUnpack.x, projUnpack.y, projUnpack.z);
        float lowZ3 = Linearize(rawLow3, projUnpack.x, projUnpack.y, projUnpack.z);

        float minLowZ = min(min(lowZ0, lowZ1), min(lowZ2, lowZ3));
        float maxLowZ = max(max(lowZ0, lowZ1), max(lowZ2, lowZ3));

        // If depth variance in the 2x2 neighborhood is small, geometry is flat: do fast bilinear fetch
        if ((maxLowZ - minLowZ) < 1.5)
        {
            vol = tex2D(lowResVolumetric, uv);
        }
        else
        {
            // Near silhouette discontinuity: nearest-depth low-res sample
            float rawFull = tex2D(fullResDepthMap, uv).r;
            float fullZ = Linearize(rawFull, projUnpack.x, projUnpack.y, projUnpack.z);

            float d0 = abs(fullZ - lowZ0);
            float d1 = abs(fullZ - lowZ1);
            float d2 = abs(fullZ - lowZ2);
            float d3 = abs(fullZ - lowZ3);

            float2 bestOffset = offsets[0];
            float bestD = d0;
            if (d1 < bestD) { bestD = d1; bestOffset = offsets[1]; }
            if (d2 < bestD) { bestD = d2; bestOffset = offsets[2]; }
            if (d3 < bestD) { bestD = d3; bestOffset = offsets[3]; }

            vol = tex2D(lowResVolumetric, uv + bestOffset * lowTexel);
        }
    }

    if (debugMode > 0)
    {
        return float4(vol.rgb, 1.0);
    }

    float3 scene = tex2D(sceneMap, uv).rgb;
    float3 result = scene * (1.0 - vol.a) + vol.rgb;
    return float4(result, 1.0);
}
)";
    }

    DirectionalVolumetricLighting& DirectionalVolumetricLighting::Instance()
    {
        static DirectionalVolumetricLighting instance;
        return instance;
    }

    void DirectionalVolumetricLighting::Configure(const std::wstring& basePath)
    {
        std::wstring iniPath = basePath + L"GraphicsEffects.ini";

        m_settings.enabled = GetPrivateProfileIntW(L"Atmosphere", L"DirectionalVolumetricEnabled", 1, iniPath.c_str()) != 0;

        int strength = GetPrivateProfileIntW(L"Atmosphere", L"VolumetricStrengthPercent", 100, iniPath.c_str());
        m_settings.strength = static_cast<float>(strength) / 100.0f;

        int density = GetPrivateProfileIntW(L"Atmosphere", L"VolumetricDensityPercent", 100, iniPath.c_str());
        m_settings.density = static_cast<float>(density) / 100.0f;

        int extinction = GetPrivateProfileIntW(L"Atmosphere", L"VolumetricExtinctionPercent", 80, iniPath.c_str());
        m_settings.extinction = static_cast<float>(extinction) / 100.0f;

        int anisotropy = GetPrivateProfileIntW(L"Atmosphere", L"VolumetricAnisotropyPercent", 72, iniPath.c_str());
        m_settings.anisotropyG = std::clamp(static_cast<float>(anisotropy) / 100.0f, 0.0f, 0.95f);

        int maxDist = GetPrivateProfileIntW(L"Atmosphere", L"VolumetricMaxDistance", 220, iniPath.c_str());
        m_settings.maxDistance = static_cast<float>(maxDist);

        int sampleCount = GetPrivateProfileIntW(L"Atmosphere", L"VolumetricSampleCount", 8, iniPath.c_str());
        m_settings.sampleCount = static_cast<uint32_t>(std::clamp(sampleCount, 4, 32));

        int skySamples = GetPrivateProfileIntW(L"Atmosphere", L"VolumetricSkySampleCount", 6, iniPath.c_str());
        m_settings.skySampleCount = static_cast<uint32_t>(std::clamp(skySamples, 2, 16));

        int resPercent = GetPrivateProfileIntW(L"Atmosphere", L"VolumetricResolutionPercent", 25, iniPath.c_str());
        m_settings.resolutionPercent = static_cast<float>(std::clamp(resPercent, 20, 100));

        m_settings.temporalEnabled = GetPrivateProfileIntW(L"Atmosphere", L"VolumetricTemporalEnabled", 1, iniPath.c_str()) != 0;

        int temporalBlend = GetPrivateProfileIntW(L"Atmosphere", L"VolumetricTemporalPercent", 80, iniPath.c_str());
        m_settings.temporalBlend = static_cast<float>(temporalBlend) / 100.0f;

        m_settings.shadowEnabled = GetPrivateProfileIntW(L"Atmosphere", L"VolumetricShadowEnabled", 0, iniPath.c_str()) != 0;
        m_settings.shadowPCF = GetPrivateProfileIntW(L"Atmosphere", L"VolumetricShadowPCF", 0, iniPath.c_str()) != 0;

        int stride = GetPrivateProfileIntW(L"Atmosphere", L"VolumetricShadowStride", 2, iniPath.c_str());
        m_settings.shadowStride = static_cast<uint32_t>(std::clamp(stride, 1, 4));

        int shadowBias = GetPrivateProfileIntW(L"Atmosphere", L"VolumetricShadowBiasPermille", 15, iniPath.c_str());
        m_settings.shadowBias = static_cast<float>(shadowBias) / 10000.0f;

        m_settings.jitterEnabled = GetPrivateProfileIntW(L"Atmosphere", L"VolumetricJitterEnabled", 1, iniPath.c_str()) != 0;

        int moonStrength = GetPrivateProfileIntW(L"Atmosphere", L"MoonVolumetricStrengthPercent", 25, iniPath.c_str());
        m_settings.moonStrength = static_cast<float>(moonStrength) / 100.0f;

        m_settings.edgeAwareBilateral = GetPrivateProfileIntW(L"Atmosphere", L"VolumetricUpsampleBilateral", 1, iniPath.c_str()) != 0;

        int debugMode = GetPrivateProfileIntW(L"Atmosphere", L"VolumetricDebugMode", 0, iniPath.c_str());
        m_settings.debugMode = static_cast<VolumetricDebugMode>(debugMode);

        int baseH = GetPrivateProfileIntW(L"Atmosphere", L"FogBaseHeight", 0, iniPath.c_str());
        m_settings.baseHeight = static_cast<float>(baseH);

        int falloff = GetPrivateProfileIntW(L"Atmosphere", L"HeightFalloffPermille", 5, iniPath.c_str());
        m_settings.heightFalloff = static_cast<float>(falloff) * 0.001f;

        int lowLayer = GetPrivateProfileIntW(L"Atmosphere", L"LowLayerPercent", 20, iniPath.c_str());
        m_settings.lowLayer = static_cast<float>(lowLayer) * 0.01f;
    }

    void DirectionalVolumetricLighting::Reset(IDirect3DDevice9* device)
    {
        if (m_owner == device || m_owner == nullptr)
        {
            m_raymarchSurface.Reset();
            m_raymarchTexture.Reset();
            m_lowDepthSurface.Reset();
            m_lowDepthTexture.Reset();
            m_historySurface[0].Reset();
            m_historyTexture[0].Reset();
            m_historySurface[1].Reset();
            m_historyTexture[1].Reset();

            m_downsampleDepthShader.Reset();
            m_raymarchShader.Reset();
            m_temporalShader.Reset();
            m_upsampleCompositeShader.Reset();

            m_owner = nullptr;
            m_fullWidth = 0;
            m_fullHeight = 0;
            m_lowWidth = 0;
            m_lowHeight = 0;
            m_historyValid = false;
        }
    }

    bool DirectionalVolumetricLighting::EnsureShaders(IDirect3DDevice9* device)
    {
        if (m_raymarchShader && m_temporalShader && m_upsampleCompositeShader && m_downsampleDepthShader)
            return true;

        auto compile = [device](const char* src, IDirect3DPixelShader9** outShader) -> bool {
            ComPtr<ID3DBlob> blob, errors;
            HRESULT hr = D3DCompile(src, strlen(src), nullptr, nullptr, nullptr, "main", "ps_3_0",
                D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, blob.GetAddressOf(), errors.GetAddressOf());
            if (FAILED(hr) || !blob)
                return false;
            return SUCCEEDED(device->CreatePixelShader(
                static_cast<DWORD*>(blob->GetBufferPointer()), outShader));
        };

        if (!compile(g_downsampleDepthSource, m_downsampleDepthShader.GetAddressOf())) return false;
        if (!compile(g_raymarchSource, m_raymarchShader.GetAddressOf())) return false;
        if (!compile(g_temporalSource, m_temporalShader.GetAddressOf())) return false;
        if (!compile(g_upsampleCompositeSource, m_upsampleCompositeShader.GetAddressOf())) return false;

        return true;
    }

    bool DirectionalVolumetricLighting::EnsureResources(IDirect3DDevice9* device, uint32_t fullWidth, uint32_t fullHeight)
    {
        float scale = m_settings.resolutionPercent / 100.0f;
        uint32_t lowW = std::max<uint32_t>(1, static_cast<uint32_t>(fullWidth * scale));
        uint32_t lowH = std::max<uint32_t>(1, static_cast<uint32_t>(fullHeight * scale));

        if (m_owner == device && m_fullWidth == fullWidth && m_fullHeight == fullHeight &&
            m_lowWidth == lowW && m_lowHeight == lowH &&
            m_raymarchTexture && m_lowDepthTexture && m_historyTexture[0] && m_historyTexture[1])
        {
            return true;
        }

        Reset(device);
        m_owner = device;
        m_fullWidth = fullWidth;
        m_fullHeight = fullHeight;
        m_lowWidth = lowW;
        m_lowHeight = lowH;

        D3DFORMAT hdrFormat = D3DFMT_A16B16G16R16F;
        IDirect3D9* d3d = nullptr;
        if (SUCCEEDED(device->GetDirect3D(&d3d)) && d3d)
        {
            D3DCAPS9 caps{};
            device->GetDeviceCaps(&caps);
            HRESULT hr = d3d->CheckDeviceFormat(caps.AdapterOrdinal, caps.DeviceType,
                D3DFMT_X8R8G8B8, D3DUSAGE_RENDERTARGET, D3DRTYPE_TEXTURE, D3DFMT_A16B16G16R16F);
            if (FAILED(hr))
            {
                hdrFormat = D3DFMT_A8R8G8B8;
            }
            d3d->Release();
        }

        // 1. Low-res raymarch target
        if (FAILED(device->CreateTexture(lowW, lowH, 1, D3DUSAGE_RENDERTARGET, hdrFormat, D3DPOOL_DEFAULT,
                m_raymarchTexture.GetAddressOf(), nullptr)) ||
            FAILED(m_raymarchTexture->GetSurfaceLevel(0, m_raymarchSurface.GetAddressOf())))
            return false;

        // 2. Low-res depth target
        if (FAILED(device->CreateTexture(lowW, lowH, 1, D3DUSAGE_RENDERTARGET, D3DFMT_R32F, D3DPOOL_DEFAULT,
                m_lowDepthTexture.GetAddressOf(), nullptr)) ||
            FAILED(m_lowDepthTexture->GetSurfaceLevel(0, m_lowDepthSurface.GetAddressOf())))
        {
            if (FAILED(device->CreateTexture(lowW, lowH, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT,
                    m_lowDepthTexture.GetAddressOf(), nullptr)) ||
                FAILED(m_lowDepthTexture->GetSurfaceLevel(0, m_lowDepthSurface.GetAddressOf())))
                return false;
        }

        // 3. Ping-pong temporal history targets
        for (int i = 0; i < 2; ++i)
        {
            if (FAILED(device->CreateTexture(lowW, lowH, 1, D3DUSAGE_RENDERTARGET, hdrFormat, D3DPOOL_DEFAULT,
                    m_historyTexture[i].GetAddressOf(), nullptr)) ||
                FAILED(m_historyTexture[i]->GetSurfaceLevel(0, m_historySurface[i].GetAddressOf())))
                return false;
        }

        m_historyValid = false;
        return true;
    }

    void DirectionalVolumetricLighting::DrawScreenQuad(IDirect3DDevice9* device, uint32_t width, uint32_t height)
    {
        struct V { float x, y, z, rhw, u, v; };
        float w = static_cast<float>(width) - 0.5f;
        float h = static_cast<float>(height) - 0.5f;
        V q[] = {
            { -0.5f, -0.5f, 0.0f, 1.0f, 0.0f, 0.0f },
            { w,     -0.5f, 0.0f, 1.0f, 1.0f, 0.0f },
            { -0.5f, h,     0.0f, 1.0f, 0.0f, 1.0f },
            { w,     h,     0.0f, 1.0f, 1.0f, 1.0f }
        };
        device->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1);
        device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, q, sizeof(V));
    }

    bool DirectionalVolumetricLighting::Render(
        IDirect3DDevice9* device,
        const FrameContext& frameContext,
        IDirect3DSurface9* targetSurface)
    {
        if (!m_settings.enabled || !device || !targetSurface || !frameContext.cameraValid || !frameContext.depthAvailable)
            return false;

        float lightIntensity = frameContext.daylightFactor + m_settings.moonStrength * frameContext.moonlightFactor;
        if (lightIntensity < 0.01f && m_settings.debugMode == VolumetricDebugMode::None)
            return false;

        if (!EnsureShaders(device))
            return false;

        D3DSURFACE_DESC targetDesc{};
        if (FAILED(targetSurface->GetDesc(&targetDesc)))
            return false;

        if (!EnsureResources(device, targetDesc.Width, targetDesc.Height))
            return false;

        // Specialized lightweight scoped render state (NO D3DSBT_ALL)
        ScopedRenderState scopedState(device);

        for (D3DRENDERSTATETYPE s : {
            D3DRS_ZENABLE, D3DRS_ZWRITEENABLE, D3DRS_ALPHATESTENABLE, D3DRS_STENCILENABLE,
            D3DRS_SCISSORTESTENABLE, D3DRS_FOGENABLE, D3DRS_LIGHTING, D3DRS_SRGBWRITEENABLE,
            D3DRS_ALPHABLENDENABLE })
        {
            device->SetRenderState(s, FALSE);
        }
        device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        device->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);

        // -------------------------------------------------------------
        // Pass 0: Downsample scene depth to low-res
        // -------------------------------------------------------------
        {
            D3DVIEWPORT9 lowVp{ 0, 0, m_lowWidth, m_lowHeight, 0.0f, 1.0f };
            device->SetViewport(&lowVp);
            device->SetRenderTarget(0, m_lowDepthSurface.Get());
            device->SetPixelShader(m_downsampleDepthShader.Get());
            device->SetTexture(0, frameContext.depthTexture);
            device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
            device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
            device->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
            device->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
            DrawScreenQuad(device, m_lowWidth, m_lowHeight);
            device->SetTexture(0, nullptr);
        }

        // -------------------------------------------------------------
        // Pass 1: Low-Resolution Raymarch
        // -------------------------------------------------------------
        {
            ScopedCpuTimer rayTimer(PerfStage::DirectionalVolumetricRaymarch);

            D3DVIEWPORT9 lowVp{ 0, 0, m_lowWidth, m_lowHeight, 0.0f, 1.0f };
            device->SetViewport(&lowVp);
            device->SetRenderTarget(0, m_raymarchSurface.Get());
            device->SetPixelShader(m_raymarchShader.Get());

            device->SetTexture(0, frameContext.depthTexture);
            device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
            device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
            device->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
            device->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);

            device->SetTexture(1, frameContext.shadowTexture);
            device->SetSamplerState(1, D3DSAMP_MINFILTER, D3DTEXF_POINT);
            device->SetSamplerState(1, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
            device->SetSamplerState(1, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
            device->SetSamplerState(1, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);

            float c0[4] = { frameContext.cameraPosition.x, frameContext.cameraPosition.y, frameContext.cameraPosition.z, 1.0f };
            device->SetPixelShaderConstantF(0, c0, 1);

            float totalStrength = m_settings.strength * lightIntensity;
            float c1[4] = { frameContext.sunDirectionWorld.x, frameContext.sunDirectionWorld.y, frameContext.sunDirectionWorld.z, totalStrength };
            device->SetPixelShaderConstantF(1, c1, 1);

            float c2[4] = {
                frameContext.directionalLightColor.x,
                frameContext.directionalLightColor.y,
                frameContext.directionalLightColor.z,
                m_settings.anisotropyG
            };
            device->SetPixelShaderConstantF(2, c2, 1);

            device->SetPixelShaderConstantF(3, frameContext.projUnpack, 1);

            float invV[3][4] = {
                { frameContext.inverseView.m[0][0], frameContext.inverseView.m[0][1], frameContext.inverseView.m[0][2], 0.0f },
                { frameContext.inverseView.m[1][0], frameContext.inverseView.m[1][1], frameContext.inverseView.m[1][2], 0.0f },
                { frameContext.inverseView.m[2][0], frameContext.inverseView.m[2][1], frameContext.inverseView.m[2][2], 0.0f }
            };
            device->SetPixelShaderConstantF(4, invV[0], 3);

            float c7[4] = {
                m_settings.maxDistance,
                static_cast<float>(m_settings.sampleCount),
                m_settings.density,
                m_settings.extinction
            };
            device->SetPixelShaderConstantF(7, c7, 1);

            float shadowOn = (m_settings.shadowEnabled && frameContext.shadowMapValid) ? 1.0f : 0.0f;
            float c8[4] = {
                m_settings.shadowBias,
                static_cast<float>(frameContext.shadowMapSize),
                shadowOn,
                static_cast<float>(static_cast<uint32_t>(m_settings.debugMode))
            };
            device->SetPixelShaderConstantF(8, c8, 1);

            device->SetPixelShaderConstantF(9, &frameContext.shadowMatrix.m[0][0], 4);

            float c13[4] = {
                static_cast<float>(frameContext.frameIndex % 1024),
                100.0f / m_settings.resolutionPercent,
                m_settings.jitterEnabled ? 1.0f : 0.0f,
                static_cast<float>(m_settings.shadowStride)
            };
            device->SetPixelShaderConstantF(13, c13, 1);

            float c14[4] = {
                m_settings.baseHeight,
                m_settings.heightFalloff,
                m_settings.lowLayer,
                frameContext.depthMaxZ
            };
            device->SetPixelShaderConstantF(14, c14, 1);

            float c15[4] = {
                m_settings.shadowPCF ? 1.0f : 0.0f,
                static_cast<float>(m_settings.skySampleCount),
                std::min(m_settings.maxDistance, 120.0f),
                0.0f
            };
            device->SetPixelShaderConstantF(15, c15, 1);

            DrawScreenQuad(device, m_lowWidth, m_lowHeight);

            device->SetTexture(0, nullptr);
            device->SetTexture(1, nullptr);
        }

        // -------------------------------------------------------------
        // Pass 2: Temporal Accumulation (5-tap cross clamp)
        // -------------------------------------------------------------
        uint32_t writeHistoryIndex = 1 - m_historyReadIndex;
        IDirect3DTexture9* currentVolumetric = m_raymarchTexture.Get();

        if (m_settings.temporalEnabled && m_settings.debugMode == VolumetricDebugMode::None)
        {
            ScopedCpuTimer tempTimer(PerfStage::VolumetricTemporal);

            D3DVIEWPORT9 lowVp{ 0, 0, m_lowWidth, m_lowHeight, 0.0f, 1.0f };
            device->SetViewport(&lowVp);
            device->SetRenderTarget(0, m_historySurface[writeHistoryIndex].Get());
            device->SetPixelShader(m_temporalShader.Get());

            device->SetTexture(0, m_raymarchTexture.Get());
            device->SetTexture(1, m_historyTexture[m_historyReadIndex].Get());

            for (DWORD s = 0; s < 2; ++s)
            {
                device->SetSamplerState(s, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
                device->SetSamplerState(s, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
                device->SetSamplerState(s, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
                device->SetSamplerState(s, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
            }

            float c0[4] = {
                m_settings.temporalBlend,
                0.05f,
                m_historyValid ? 1.0f : 0.0f,
                0.0f
            };
            device->SetPixelShaderConstantF(0, c0, 1);

            float c1[4] = { 1.0f / static_cast<float>(m_lowWidth), 1.0f / static_cast<float>(m_lowHeight), 0.0f, 0.0f };
            device->SetPixelShaderConstantF(1, c1, 1);

            DrawScreenQuad(device, m_lowWidth, m_lowHeight);

            device->SetTexture(0, nullptr);
            device->SetTexture(1, nullptr);

            currentVolumetric = m_historyTexture[writeHistoryIndex].Get();
            m_historyReadIndex = writeHistoryIndex;
            m_historyValid = true;
        }

        // -------------------------------------------------------------
        // Pass 3: Merged Upsample + Composite directly onto targetSurface
        // -------------------------------------------------------------
        {
            ScopedCpuTimer upTimer(PerfStage::VolumetricUpsample);

            D3DVIEWPORT9 fullVp{ 0, 0, m_fullWidth, m_fullHeight, 0.0f, 1.0f };
            device->SetViewport(&fullVp);
            device->SetRenderTarget(0, targetSurface);
            device->SetPixelShader(m_upsampleCompositeShader.Get());

            device->SetTexture(0, frameContext.sceneColor);
            device->SetTexture(1, currentVolumetric);
            device->SetTexture(2, frameContext.depthTexture);
            device->SetTexture(3, m_lowDepthTexture.Get());

            for (DWORD s = 0; s < 4; ++s)
            {
                device->SetSamplerState(s, D3DSAMP_MINFILTER, (s >= 2) ? D3DTEXF_POINT : D3DTEXF_LINEAR);
                device->SetSamplerState(s, D3DSAMP_MAGFILTER, (s >= 2) ? D3DTEXF_POINT : D3DTEXF_LINEAR);
                device->SetSamplerState(s, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
                device->SetSamplerState(s, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
            }

            // c0: texel sizes [1/fullW, 1/fullH, 1/lowW, 1/lowH]
            float c0[4] = {
                1.0f / static_cast<float>(m_fullWidth),
                1.0f / static_cast<float>(m_fullHeight),
                1.0f / static_cast<float>(m_lowWidth),
                1.0f / static_cast<float>(m_lowHeight)
            };
            device->SetPixelShaderConstantF(0, c0, 1);

            // c1: projUnpack [P22, P32, maxZ, edgeBilateralOn]
            float c1[4] = {
                frameContext.projUnpack[0],
                frameContext.projUnpack[1],
                frameContext.depthMaxZ,
                m_settings.edgeAwareBilateral ? 1.0f : 0.0f
            };
            device->SetPixelShaderConstantF(1, c1, 1);

            // c2: composite tuning [debugMode, 0, 0, 0]
            float isDebug = (m_settings.debugMode != VolumetricDebugMode::None) ? 1.0f : 0.0f;
            float c2[4] = { isDebug, 0.0f, 0.0f, 0.0f };
            device->SetPixelShaderConstantF(2, c2, 1);

            DrawScreenQuad(device, m_fullWidth, m_fullHeight);

            device->SetTexture(0, nullptr);
            device->SetTexture(1, nullptr);
            device->SetTexture(2, nullptr);
            device->SetTexture(3, nullptr);
        }

        return true;
    }
}
