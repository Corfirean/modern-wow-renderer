#pragma once
#include <d3d9.h>
#include <wrl/client.h>
#include <string>
#include "../Core/FrameContext.h"

namespace renderer
{
    using Microsoft::WRL::ComPtr;

    enum class VolumetricDebugMode : uint32_t
    {
        None = 0, OpticalDepth, Transmission, TotalDensity, HeightDensity,
        GroundMistDensity, NoiseDensity, AmbientInscatter, CelestialInscatter,
        RawLowResolution, Temporal, Upsampled,
        // Raw boundary depth itself (pre-integration): scene depth
        // everywhere, water surface depth wherever water covers a pixel.
        // Over water this must show the SURFACE distance, not the seabed -
        // if it still shows seabed distance, the water depth MRT write
        // isn't reaching this texture and the fix isn't actually engaging.
        BoundaryDepth
    };

    struct VolumetricSettings
    {
        bool enabled = true;
        uint32_t quality = 1;
        uint32_t sampleCount = 6;
        float resolutionScale = 0.25f;
        float densityScale = 1.0f;
        float maxDistance = 520.0f;
        float aerialStart = 35.0f;
        float aerialDensity = 0.002f;
        float heightDensity = 0.005f;
        float heightFalloff = 0.055f;
        float fogBaseOffset = -6.0f;
        float mistDensity = 0.009f;
        float mistFalloff = 0.42f;
        float mistBaseOffset = -3.0f;
        float noiseAmount = 0.22f;
        float extinction = 1.0f;
        float moonStrength = 0.16f;
        bool temporalEnabled = true;
        float temporalBlend = 0.88f;
        bool edgeAwareBilateral = true;
        // Halo around the confirmed sun/moon disc, inside the haze itself -
        // independent of the screen-space god-ray shaft pass. 0 = no halo.
        float sunGlowStrength = 0.8f;
        // How strongly the atmosphere replaces/dims the scene behind it vs
        // letting it show through. Lower preserves distant detail (e.g.
        // open water) instead of washing it to a flat colour.
        float fogWash = 0.55f;
        // Derive aerial start/end distance and ambient fog colour from
        // WoW's own authored fog (captured read-only from real shader
        // constants - see EnvironmentFogCapture) instead of one fixed
        // synthetic profile for every zone. The FOG DISTANCE/DENSITY
        // sliders still apply as a scale on top of the captured baseline,
        // not a replacement for it. Off falls back to the old fixed
        // profile, e.g. to A/B compare or if a zone's capture looks wrong.
        bool useEnvironmentBaseline = true;
        VolumetricDebugMode debugMode = VolumetricDebugMode::None;
    };

    // Historical class name retained to avoid unrelated API churn. This is
    // the dedicated production atmosphere pipeline, not the old shadow march.
    class DirectionalVolumetricLighting
    {
    public:
        static DirectionalVolumetricLighting& Instance();
        void Configure(const std::wstring& basePath);
        void Reset(IDirect3DDevice9* device);
        bool Render(IDirect3DDevice9* device, const FrameContext& frameContext, IDirect3DSurface9* targetSurface);
        VolumetricSettings& Settings() { return m_settings; }
        const VolumetricSettings& Settings() const { return m_settings; }
        IDirect3DTexture9* GetRaymarchTexture() const { return m_integratedTexture.Get(); }

    private:
        DirectionalVolumetricLighting() = default;
        bool EnsureShaders(IDirect3DDevice9* device);
        bool EnsureResources(IDirect3DDevice9* device, uint32_t fullWidth, uint32_t fullHeight, D3DFORMAT targetFormat);
        void DrawScreenQuad(IDirect3DDevice9* device, uint32_t width, uint32_t height);
        bool ValidateHistory(const FrameContext& frameContext);

        VolumetricSettings m_settings;
        IDirect3DDevice9* m_owner = nullptr;
        uint32_t m_fullWidth = 0, m_fullHeight = 0, m_lowWidth = 0, m_lowHeight = 0;
        D3DFORMAT m_targetFormat = D3DFMT_UNKNOWN;
        ComPtr<IDirect3DTexture9> m_integratedTexture;
        ComPtr<IDirect3DSurface9> m_integratedSurface;
        ComPtr<IDirect3DTexture9> m_historyTexture[2];
        ComPtr<IDirect3DSurface9> m_historySurface[2];
        ComPtr<IDirect3DTexture9> m_depthTexture[2];
        ComPtr<IDirect3DSurface9> m_depthSurface[2];
        ComPtr<IDirect3DTexture9> m_upsampledTexture;
        ComPtr<IDirect3DSurface9> m_upsampledSurface;
        // Full-res "where does air stop" depth: scene depth everywhere,
        // except water pixels, where it's the water SURFACE depth instead
        // of the seabed depth already sitting in the main depth buffer
        // (water doesn't write that). Computed once per frame and fed into
        // both the low-res downsample and the full-res bilateral upsample,
        // so the atmosphere never integrates air through water to the
        // seabed - see DirectionalVolumetricLighting.cpp.
        ComPtr<IDirect3DTexture9> m_boundaryTexture;
        ComPtr<IDirect3DSurface9> m_boundarySurface;
        uint32_t m_historyReadIndex = 0;
        bool m_historyValid = false;
        ComPtr<IDirect3DPixelShader9> m_depthShader;
        ComPtr<IDirect3DPixelShader9> m_integrateShader;
        ComPtr<IDirect3DPixelShader9> m_temporalShader;
        ComPtr<IDirect3DPixelShader9> m_upsampleShader;
        ComPtr<IDirect3DPixelShader9> m_compositeShader;
        ComPtr<IDirect3DPixelShader9> m_boundaryShader;
        ComPtr<IDirect3DPixelShader9> m_boundaryDebugShader;
        Vec3 m_previousCamera{};
        float m_previousProjection[4]{};
        Matrix4 m_previousView{};
        bool m_previousViewValid = false;
        bool m_previousWasMoon = false;
        float m_previousCelestialIntensity = 0.0f;
    };
}
