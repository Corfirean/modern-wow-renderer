#include "DrawCallClassifier.h"
#include <cmath>
#include <wrl/client.h>

namespace renderer
{
    DrawCallClassifier& DrawCallClassifier::Instance()
    {
        static DrawCallClassifier instance;
        return instance;
    }

    DrawClassification DrawCallClassifier::Classify(
        IDirect3DDevice9* device,
        const DrawCallContext& ctx,
        const float capturedViewTranslation[3],
        bool capturedViewValid,
        uint64_t cameraCaptureShaderHash)
    {
        uint32_t stateBits = (ctx.alphaBlend ? 1 : 0) |
                             (ctx.alphaTest ? 2 : 0) |
                             (ctx.zWrite ? 4 : 0) |
                             (ctx.zEnable ? 8 : 0);

        PipelineKey key{
            static_cast<const void*>(ctx.vertexShader),
            static_cast<const void*>(ctx.pixelShader),
            static_cast<const void*>(ctx.vertexDecl),
            stateBits
        };

        auto it = m_cache.find(key);
        if (it != m_cache.end())
        {
            ++m_cacheHits;
            return it->second;
        }

        DrawClassification result = ClassifyInternal(
            device, ctx, capturedViewTranslation, capturedViewValid, cameraCaptureShaderHash);

        ++m_cacheMisses;
        m_cache.emplace(key, result);

        return result;
    }

    DrawClassification DrawCallClassifier::ClassifyInternal(
        IDirect3DDevice9* device,
        const DrawCallContext& ctx,
        const float capturedViewTranslation[3],
        bool capturedViewValid,
        uint64_t cameraCaptureShaderHash)
    {
        DrawClassification dc;

        // 1. UI Check
        if (ctx.vsHash == 0xd9e7756460af6296ull || ctx.psHash == 0xc29c7060b723c0c6ull)
        {
            dc.material = MaterialType::UI;
            dc.isUI = true;
            dc.castsShadow = false;
            dc.receivesShadow = false;
            dc.receivesFog = false;
            return dc;
        }

        // 2. Water Check
        // Shader hashes are only a fast path. The client uses additional
        // shader variants for the same liquid under bridges, at tile seams,
        // near the camera and in expansion zones. Identify the material by
        // the same distinctive two-texture layout used by WaterEffect so the
        // classifier, SSR capture and replacement shader cannot disagree.
        bool waterShaderPair =
            (ctx.psHash == 0x17f042a7906ca126ull || ctx.psHash == 0x7d4f078fa1876a09ull) &&
            (ctx.vsHash == 0x206d861fd0a721ddull || ctx.vsHash == 0xfdd9528ed3ac30eaull ||
             ctx.vsHash == 0x48a82796bd612aebull || ctx.vsHash == 0x70faf83955e2b668ull);

        bool waterTextureLayout = false;
        if (device)
        {
            using Microsoft::WRL::ComPtr;
            ComPtr<IDirect3DBaseTexture9> tex0, tex1;
            if (SUCCEEDED(device->GetTexture(0, tex0.GetAddressOf())) && tex0 && tex0->GetType() == D3DRTYPE_TEXTURE &&
                SUCCEEDED(device->GetTexture(1, tex1.GetAddressOf())) && tex1 && tex1->GetType() == D3DRTYPE_TEXTURE)
            {
                D3DSURFACE_DESC d0{}, d1{};
                if (SUCCEEDED(static_cast<IDirect3DTexture9*>(tex0.Get())->GetLevelDesc(0, &d0)) &&
                    SUCCEEDED(static_cast<IDirect3DTexture9*>(tex1.Get())->GetLevelDesc(0, &d1)))
                {
                    waterTextureLayout = d0.Width == 8 && d0.Height == 64 &&
                                         d1.Width == 512 && d1.Height == 512;
                }
            }
        }

        if (waterShaderPair || waterTextureLayout)
        {
            dc.material = MaterialType::Water;
            dc.isWater = true;
            dc.castsShadow = false;
            dc.receivesShadow = false;
            dc.receivesFog = false;
            return dc;
        }

        // 3. Constant analysis for 3D Geometry
        if (!device)
            return dc;

        float original[9][4]{};
        if (SUCCEEDED(device->GetVertexShaderConstantF(0, original[0], 9)))
        {
            // M2 models (characters, trees, doodads, creatures): row-major projection in c2..c5
            bool isM2 = (fabsf(original[5][2] - 1.0f) < 0.02f &&
                         fabsf(original[5][0]) < 0.01f &&
                         fabsf(original[5][1]) < 0.01f &&
                         fabsf(original[5][3]) < 0.01f &&
                         original[2][0] > 0.1f &&
                         original[3][1] > 0.1f);

            if (isM2)
            {
                dc.material = MaterialType::M2;
                dc.castsShadow = (!ctx.alphaBlend && ctx.zWrite);
                dc.receivesShadow = true;
                dc.receivesFog = true;
                return dc;
            }

            // WMO objects and Terrain use column-major projection in c4..c7
            bool hasProj4 = (original[4][0] > 0.1f &&
                             original[5][1] > 0.1f &&
                             fabsf(original[6][3] - 1.0f) < 0.02f &&
                             original[7][2] < -0.01f);

            if (hasProj4)
            {
                bool isTerrain = ((cameraCaptureShaderHash != 0 && ctx.vsHash == cameraCaptureShaderHash) ||
                                  (capturedViewValid &&
                                   fabsf(original[3][0] - capturedViewTranslation[0]) < 0.01f &&
                                   fabsf(original[3][1] - capturedViewTranslation[1]) < 0.01f &&
                                   fabsf(original[3][2] - capturedViewTranslation[2]) < 0.01f));

                if (isTerrain)
                {
                    dc.material = MaterialType::Terrain;
                    dc.castsShadow = false; // terrain is bottom receiver
                    dc.receivesShadow = true;
                    dc.receivesFog = true;
                    return dc;
                }
                else
                {
                    dc.material = MaterialType::WMO;
                    dc.castsShadow = (!ctx.alphaBlend && ctx.zWrite);
                    dc.receivesShadow = true;
                    dc.receivesFog = true;
                    return dc;
                }
            }
        }

        // 4. Transparency fallback
        if (ctx.alphaBlend)
        {
            dc.material = MaterialType::Transparent;
            dc.isTransparent = true;
            dc.castsShadow = false;
            dc.receivesShadow = false;
            dc.receivesFog = true;
            return dc;
        }

        return dc;
    }

    void DrawCallClassifier::ClearCache()
    {
        m_cache.clear();
    }
}
