#include "CelestialMemoryProbe.h"
#include <windows.h>
#include <cmath>
#include <cstdint>

namespace renderer
{
    namespace
    {
        // Candidate addresses from external reverse engineering of
        // CoAVolFog.dll, NOT from this project's own disassembly. Treated as
        // an unverified hypothesis: every read below is guarded, and nothing
        // downstream trusts these values beyond logging/debug markers until
        // cross-checked against the real visible sun/moon in-game.
        constexpr uintptr_t kDayValueAddress        = 0x00D38B04;
        constexpr uintptr_t kSunVectorAddress        = 0x00D38E28;
        constexpr uintptr_t kMoonVectorAddress       = 0x00D38E48;
        constexpr uintptr_t kReferenceVectorAddress  = 0x00D38B18;

        bool IsReadableRange(const void* addr, size_t size)
        {
            if (!addr || size == 0)
                return false;

            MEMORY_BASIC_INFORMATION mbi{};
            if (VirtualQuery(addr, &mbi, sizeof(mbi)) != sizeof(mbi))
                return false;
            if (mbi.State != MEM_COMMIT)
                return false;
            if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))
                return false;

            constexpr DWORD kReadableMask = PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ |
                PAGE_EXECUTE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_WRITECOPY;
            if ((mbi.Protect & kReadableMask) == 0)
                return false;

            uintptr_t regionEnd = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
            uintptr_t rangeEnd = reinterpret_cast<uintptr_t>(addr) + size;
            return rangeEnd <= regionEnd;
        }

        // Isolated with only POD locals: MSVC does not allow __try/__except
        // in a function scope that also has C++ objects needing unwinding
        // under /EHsc.
        bool SafeReadFloats(uintptr_t address, float* out, int count)
        {
            const void* ptr = reinterpret_cast<const void*>(address);
            if (!IsReadableRange(ptr, sizeof(float) * static_cast<size_t>(count)))
                return false;

            __try
            {
                const float* src = static_cast<const float*>(ptr);
                for (int i = 0; i < count; ++i)
                    out[i] = src[i];
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        bool AllFinite(const float* v, int count)
        {
            for (int i = 0; i < count; ++i)
                if (!std::isfinite(v[i]))
                    return false;
            return true;
        }

        bool PlausibleDirection(const Vec3& toLight, Vec3& outNormalized)
        {
            float len = Length(toLight);
            if (!std::isfinite(len) || len <= 1e-3f || len >= 1.0e6f)
                return false;
            outNormalized = Normalize(toLight);
            return true;
        }
    }

    CelestialMemoryProbe& CelestialMemoryProbe::Instance()
    {
        static CelestialMemoryProbe instance;
        return instance;
    }

    CelestialProbeSample CelestialMemoryProbe::Sample() const
    {
        CelestialProbeSample sample;

        float dayValue = 0.0f;
        float sunVec[3]{}, moonVec[3]{}, refVec[3]{};

        bool readDay = SafeReadFloats(kDayValueAddress, &dayValue, 1);
        bool readSun = SafeReadFloats(kSunVectorAddress, sunVec, 3);
        bool readMoon = SafeReadFloats(kMoonVectorAddress, moonVec, 3);
        bool readRef = SafeReadFloats(kReferenceVectorAddress, refVec, 3);

        sample.addressesReadable = readDay && readSun && readMoon && readRef;
        if (!sample.addressesReadable)
            return sample;

        if (!AllFinite(&dayValue, 1) || !AllFinite(sunVec, 3) || !AllFinite(moonVec, 3) || !AllFinite(refVec, 3))
            return sample;

        sample.day = dayValue;
        sample.sunRaw = { sunVec[0], sunVec[1], sunVec[2] };
        sample.moonRaw = { moonVec[0], moonVec[1], moonVec[2] };
        sample.referenceRaw = { refVec[0], refVec[1], refVec[2] };

        sample.sunCandidateValid = PlausibleDirection(sample.sunRaw - sample.referenceRaw, sample.sunToLightWorld);
        sample.moonCandidateValid = PlausibleDirection(sample.moonRaw - sample.referenceRaw, sample.moonToLightWorld);
        sample.valuesPlausible = sample.sunCandidateValid || sample.moonCandidateValid;

        return sample;
    }
}
