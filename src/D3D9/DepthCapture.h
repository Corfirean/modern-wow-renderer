#pragma once
#include <d3d9.h>
#include <wrl/client.h>
#include <cstdint>

namespace renderer
{
    using Microsoft::WRL::ComPtr;

    using SetDepthFn = HRESULT(WINAPI*)(IDirect3DDevice9*, IDirect3DSurface9*);
    using GetDepthFn = HRESULT(WINAPI*)(IDirect3DDevice9*, IDirect3DSurface9**);

    class DepthCapture
    {
    public:
        static DepthCapture& Instance();

        void SetFunctions(SetDepthFn setDepth, GetDepthFn getDepth);

        // Vtable hook targets
        HRESULT HookSetDepth(IDirect3DDevice9* device, IDirect3DSurface9* surface);
        HRESULT HookGetDepth(IDirect3DDevice9* device, IDirect3DSurface9** outSurface);

        // Raw calls to original D3D9 methods
        HRESULT RawSetDepth(IDirect3DDevice9* device, IDirect3DSurface9* surface);
        HRESULT RawGetDepth(IDirect3DDevice9* device, IDirect3DSurface9** outSurface);

        // Capture lifecycle hooks
        void BeforeClear(IDirect3DDevice9* device, DWORD count, DWORD flags, float z);
        void OnFrameEnd(IDirect3DDevice9* device);
        void Reset(IDirect3DDevice9* device);

        // Depth resources
        IDirect3DTexture9* GetDepthTexture() const { return m_depthTexture.Get(); }
        IDirect3DSurface9* GetDepthSurface() const { return m_depthSurface.Get(); }
        IDirect3DSurface9* GetOriginalDepth() const { return m_originalDepth.Get(); }
        IDirect3DSurface9* GetRenderTarget() const { return m_target.Get(); }
        IDirect3DDevice9* GetOwner() const { return m_owner; }

        bool HasDepth() const { return m_depthTexture != nullptr && m_depthSurface != nullptr; }
        uint32_t GetDepthFrames() const { return m_depthFrames; }

    private:
        DepthCapture() = default;

        SetDepthFn m_origSetDepth = nullptr;
        GetDepthFn m_origGetDepth = nullptr;

        ComPtr<IDirect3DTexture9> m_depthTexture;
        ComPtr<IDirect3DSurface9> m_depthSurface;
        ComPtr<IDirect3DSurface9> m_originalDepth;
        ComPtr<IDirect3DSurface9> m_target;
        IDirect3DDevice9* m_owner = nullptr;

        uint32_t m_depthFrames = 0;
    };
}
