#pragma once
#include "WoWClientContext.h"
#include "../Environment/LocationContext.h"
namespace renderer {
class EnvironmentDatabase;
struct WoWLocationOffsets { uintptr_t map=0,zone=0,area=0; };
class WoWLocationProvider {
public:
    void Configure(const std::wstring& path);
    LocationContext Sample(const EnvironmentDatabase& database,bool worldContext) const;
    bool Verified() const {return available&&verified;}
    const std::string& Status() const {return status;}
    const WoWClientContext& Client() const {return client;}
    const LocationContext& RawSample() const {return rawSample;}
    const std::string& ValidationStatus() const {return validation;}
private:
    mutable LocationContext rawSample;
    mutable std::string validation="Not sampled";
    WoWClientContext client;
    WoWLocationOffsets offsets;
    bool available=false,verified=false;
    std::string status="Location provider not configured";
};
}
