#pragma once
#include <d3d9.h>

namespace renderer
{
    // Clear(0, nullptr, ...) and an explicit rectangle covering the viewport
    // both initialize every pixel that the renderer can subsequently sample.
    // Partial clears must not create a replacement depth surface because the
    // pixels outside the rectangles would contain undefined data.
    inline bool ClearsFullViewport(DWORD count, const D3DRECT* rects, const D3DVIEWPORT9& viewport)
    {
        if (viewport.Width == 0 || viewport.Height == 0)
            return false;
        if (count == 0)
            return true;
        if (!rects)
            return false;

        const LONG right = static_cast<LONG>(viewport.X + viewport.Width);
        const LONG bottom = static_cast<LONG>(viewport.Y + viewport.Height);
        for (DWORD i = 0; i < count; ++i)
        {
            const D3DRECT& rect = rects[i];
            if (rect.x1 <= static_cast<LONG>(viewport.X) &&
                rect.y1 <= static_cast<LONG>(viewport.Y) &&
                rect.x2 >= right && rect.y2 >= bottom)
                return true;
        }
        return false;
    }

    inline bool CanReplaceDepthFormat(D3DFORMAT format)
    {
        // INTZ carries 24-bit depth and 8-bit stencil. Plain depth formats do
        // not require stencil preservation; D24S8 uses the matching layout.
        return format == D3DFMT_D16 || format == D3DFMT_D32 ||
               format == D3DFMT_D24X8 || format == D3DFMT_D24S8;
    }
}
