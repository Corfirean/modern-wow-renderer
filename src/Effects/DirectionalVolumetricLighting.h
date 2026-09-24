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
        None = 0,
        Raw = 1,
        ShadowVisibility = 2,
        Density = 3,
        Phase = 4,
        History = 5,
        Upsample = 6
    };

    struct VolumetricSettings
    {
        bool enabled = true;
        float strength = 1.0f;
        float density = 1.0f;
        float extinction = 0.8f;
        float anisotropyG = 0.72f;
        float maxDistance = 220.0f;
        uint32_t sampleCount = 16;
        float resolutionPercent = 50.0f; // 50% = half-res, 25% = quarter-res
        bool temporalEnabled = true;
        float temporalBlend = 0.78f;
        bool shadowEnabled = true;
        float shadowBias = 0.0015f;
        bool jitterEnabled = true;
        float moonStrength = 0.25f;
        VolumetricDebugMode debugMode = VolumetricDebugMode::None;

        // Height density tuning
        float baseHeight = 0.0f;
        float heightFalloff = 0.005f;
        float lowLayer = 0.2f;
    };

    class DirectionalVolumetricLighting
    {
    public:
        static DirectionalVolumetricLighting& Instance();

        void Configure(const std::wstring& basePath);
        void Reset(IDirect3DDevice9* device);

        // Executes the raymarch, temporal, upsample, and composite passes
        bool Render(IDirect3DDevice9* device, const FrameContext& frameContext, IDirect3DSurface9* targetSurface);

        VolumetricSettings& Settings() { return m_settings; }
        const VolumetricSettings& Settings() const { return m_settings; }

        IDirect3DTexture9* GetUpsampledTexture() const { return m_upsampleTexture.Get(); }
        IDirect3DTexture9* GetRaymarchTexture() const { return m_raymarchTexture.Get(); }

    private:
        DirectionalVolumetricLighting() = default;

        bool EnsureShaders(IDirect3DDevice9* device);
        bool EnsureResources(IDirect3DDevice9* device, uint32_t fullWidth, uint32_t fullHeight);
        void DrawScreenQuad(IDirect3DDevice9* device, uint32_t width, uint32_t height);

        VolumetricSettings m_settings;

        IDirect3DDevice9* m_owner = nullptr;
        uint32_t m_fullWidth = 0;
        uint32_t m_fullHeight = 0;
        uint32_t m_lowWidth = 0;
        uint32_t m_lowHeight = 0;

        // Render targets
        ComPtr<IDirect3DTexture9> m_raymarchTexture;
        ComPtr<IDirect3DSurface9> m_raymarchSurface;

        ComPtr<IDirect3DTexture9> m_lowDepthTexture;
        ComPtr<IDirect3DSurface9> m_lowDepthSurface;

        // Temporal ping-pong history
        ComPtr<IDirect3DTexture9> m_historyTexture[2];
        ComPtr<IDirect3DSurface9> m_historySurface[2];
        uint32_t m_historyReadIndex = 0;
        bool m_historyValid = false;

        // Depth-aware upsampled texture
        ComPtr<IDirect3DTexture9> m_upsampleTexture;
        ComPtr<IDirect3DSurface9> m_upsampleSurface;

        // Shaders
        ComPtr<IDirect3DPixelShader9> m_raymarchShader;
        ComPtr<IDirect3DPixelShader9> m_temporalShader;
        ComPtr<IDirect3DPixelShader9> m_upsampleShader;
        ComPtr<IDirect3DPixelShader9> m_compositeShader;
        ComPtr<IDirect3DPixelShader9> m_downsampleDepthShader;
    };
}
