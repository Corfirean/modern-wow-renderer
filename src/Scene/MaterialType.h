#pragma once
#include <cstdint>

namespace renderer
{
    enum class MaterialType
    {
        Unknown,
        Terrain,
        WMO,
        M2,
        Character,
        Water,
        Sky,
        Particle,
        Transparent,
        UI
    };

    struct DrawClassification
    {
        MaterialType material = MaterialType::Unknown;
        bool castsShadow = false;
        bool receivesShadow = false;
        bool receivesFog = true;
        bool isTransparent = false;
        bool isWater = false;
        bool isUI = false;
    };
}
