#pragma once
#include "../Core/MathTypes.h"

namespace renderer
{
    // Result of one read-only probe attempt. `addressesReadable` and
    // `valuesPlausible` must both be checked before trusting anything else
    // in here - this is unverified, third-party-derived hypothesis data,
    // not a proven data source. See CelestialMemoryProbe.cpp for exactly
    // what is and isn't validated before a value reaches this struct.
    struct CelestialProbeSample
    {
        bool addressesReadable = false;
        bool valuesPlausible = false;

        float day = 0.0f;

        Vec3 sunRaw{};
        Vec3 moonRaw{};
        Vec3 referenceRaw{};

        // Two independent candidate directions - which one (if either)
        // corresponds to the actually visible celestial body is exactly
        // what the side-by-side debug markers are for determining. Neither
        // is preferred/selected here.
        bool sunCandidateValid = false;
        Vec3 sunToLightWorld{};

        bool moonCandidateValid = false;
        Vec3 moonToLightWorld{};
    };

    // Read-only probe of a handful of client-process globals believed - from
    // external, third-party reverse engineering of CoAVolFog.dll (a closed-
    // source mod targeting this exact Ascension/CoA 3.3.5a build 12340) - to
    // hold the day/night fraction and the world-space sun/moon direction
    // vectors WoW's own renderer uses. This project has NOT independently
    // verified those addresses or the sun/moon selection logic; both are
    // still open questions (see PR discussion). This probe:
    //   - NEVER writes to process memory,
    //   - validates every address is committed + readable before touching it,
    //   - wraps the actual read in a structured exception handler,
    //   - never lets an unread or non-finite value reach anything beyond a
    //     diagnostic log line / debug marker.
    class CelestialMemoryProbe
    {
    public:
        static CelestialMemoryProbe& Instance();

        CelestialProbeSample Sample() const;
    };
}
