#pragma once
#include <d3d9.h>
#include <cstdint>

namespace renderer
{
    struct TrackedRenderState
    {
        IDirect3DVertexShader9* currentVS = nullptr;
        IDirect3DPixelShader9* currentPS = nullptr;
        IDirect3DVertexDeclaration9* currentVDecl = nullptr;
        DWORD currentFVF = 0;
        uint64_t vsHash = 0;
        uint64_t psHash = 0;
        bool alphaBlend = false;
        bool alphaTest = false;
        bool zWrite = true;
        bool zEnable = true;
    };

    inline TrackedRenderState g_trackedState;
}
