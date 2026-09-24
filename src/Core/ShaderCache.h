#pragma once
#include <d3d9.h>
#include <cstdint>
#include <unordered_map>
#include <vector>
#include <mutex>

namespace renderer
{
    struct ShaderInfo
    {
        uint64_t hash = 0;
        uint32_t bytecodeSize = 0;
        bool isKnownTerrain = false;
        bool isKnownWater = false;
        bool isKnownUI = false;
    };

    class ShaderCache
    {
    public:
        static ShaderCache& Instance();

        uint64_t GetShaderHash(IDirect3DVertexShader9* vs);
        uint64_t GetShaderHash(IDirect3DPixelShader9* ps);

        const ShaderInfo* GetShaderInfo(IDirect3DVertexShader9* vs);
        const ShaderInfo* GetShaderInfo(IDirect3DPixelShader9* ps);

        void Clear();

        uint64_t GetHits() const { return m_hits; }
        uint64_t GetMisses() const { return m_misses; }

    private:
        ShaderCache() = default;

        template<typename T>
        const ShaderInfo* FetchOrCreate(T* shader);

        std::unordered_map<const void*, ShaderInfo> m_cache;
        uint64_t m_hits = 0;
        uint64_t m_misses = 0;
    };
}
