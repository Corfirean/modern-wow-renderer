#include "DepthCapture.h"

namespace renderer
{
    DepthCapture& DepthCapture::Instance()
    {
        static DepthCapture instance;
        return instance;
    }

    void DepthCapture::SetFunctions(SetDepthFn setDepth, GetDepthFn getDepth)
    {
        m_origSetDepth = setDepth;
        m_origGetDepth = getDepth;
    }

    HRESULT DepthCapture::HookSetDepth(IDirect3DDevice9* device, IDirect3DSurface9* surface)
    {
        if (!m_origSetDepth)
            return D3DERR_INVALIDCALL;

        IDirect3DSurface9* target = surface;
        if (m_owner == device && surface && surface == m_originalDepth.Get())
        {
            target = m_depthSurface.Get();
        }

        return m_origSetDepth(device, target);
    }

    HRESULT DepthCapture::HookGetDepth(IDirect3DDevice9* device, IDirect3DSurface9** outSurface)
    {
        if (!m_origGetDepth)
            return D3DERR_INVALIDCALL;

        HRESULT hr = m_origGetDepth(device, outSurface);
        if (SUCCEEDED(hr) && outSurface && m_owner == device && *outSurface == m_depthSurface.Get() && m_originalDepth)
        {
            (*outSurface)->Release();
            *outSurface = m_originalDepth.Get();
            (*outSurface)->AddRef();
        }
        return hr;
    }

    HRESULT DepthCapture::RawSetDepth(IDirect3DDevice9* device, IDirect3DSurface9* surface)
    {
        if (!m_origSetDepth)
            return D3DERR_INVALIDCALL;
        return m_origSetDepth(device, surface);
    }

    HRESULT DepthCapture::RawGetDepth(IDirect3DDevice9* device, IDirect3DSurface9** outSurface)
    {
        if (!m_origGetDepth)
            return D3DERR_INVALIDCALL;
        return m_origGetDepth(device, outSurface);
    }

    void DepthCapture::BeforeClear(IDirect3DDevice9* device, DWORD count, DWORD flags, float z)
    {
        if (!m_origGetDepth || !m_origSetDepth || !(flags & D3DCLEAR_ZBUFFER))
            return;

        if (m_owner == device)
        {
            return;
        }

        if (count != 0 || z != 1.0f || m_owner != nullptr)
            return;

        ComPtr<IDirect3DSurface9> rt, back, ds;
        D3DSURFACE_DESC desc{}, rd{};
        D3DVIEWPORT9 vp{};

        if (FAILED(device->GetRenderTarget(0, rt.GetAddressOf())) ||
            FAILED(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, back.GetAddressOf())) ||
            rt.Get() != back.Get())
            return;

        if (FAILED(m_origGetDepth(device, ds.GetAddressOf())) || !ds ||
            FAILED(ds->GetDesc(&desc)) || FAILED(rt->GetDesc(&rd)) ||
            FAILED(device->GetViewport(&vp)))
            return;

        if (desc.MultiSampleType != D3DMULTISAMPLE_NONE ||
            desc.Width != rd.Width || desc.Height != rd.Height ||
            vp.X != 0 || vp.Y != 0 || vp.Width != rd.Width || vp.Height != rd.Height)
            return;

        if (desc.Format != D3DFMT_D24X8 && desc.Format != D3DFMT_D24S8)
            return;

        ComPtr<IDirect3DTexture9> tex;
        ComPtr<IDirect3DSurface9> replacement;
        if (FAILED(device->CreateTexture(desc.Width, desc.Height, 1,
                D3DUSAGE_DEPTHSTENCIL,
                static_cast<D3DFORMAT>(MAKEFOURCC('I','N','T','Z')),
                D3DPOOL_DEFAULT, tex.GetAddressOf(), nullptr)) ||
            FAILED(tex->GetSurfaceLevel(0, replacement.GetAddressOf())) ||
            FAILED(m_origSetDepth(device, replacement.Get())))
            return;

        m_depthTexture = tex;
        m_depthSurface = replacement;
        m_originalDepth = ds;
        m_target = rt;
        m_owner = device;
        ++m_depthFrames;
    }

    void DepthCapture::OnFrameEnd(IDirect3DDevice9* device)
    {
        if (m_owner && m_owner != device)
            return;

        if (m_owner && m_origGetDepth && m_origSetDepth)
        {
            ComPtr<IDirect3DSurface9> current;
            if (SUCCEEDED(m_origGetDepth(device, current.GetAddressOf())) && current.Get() == m_depthSurface.Get())
            {
                m_origSetDepth(device, m_originalDepth.Get());
            }
        }

        m_depthSurface.Reset();
        m_depthTexture.Reset();
        m_originalDepth.Reset();
        m_target.Reset();
        m_owner = nullptr;
    }

    void DepthCapture::Reset(IDirect3DDevice9* device)
    {
        OnFrameEnd(device);
    }
}
