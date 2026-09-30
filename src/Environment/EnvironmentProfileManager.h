#pragma once
#include "EnvironmentProfile.h"
#include "EnvironmentDatabase.h"
#include "../Game/WoWLocationProvider.h"
#include <vector>
namespace renderer {
class EnvironmentProfileManager {
public:
    static EnvironmentProfileManager& Instance();
    void Configure(const std::wstring& base);
    void Reload();
    void Update(uint64_t now,bool worldContext);
    const ResolvedEnvironmentState& State() const {return current;}
    const LocationContext& Location() const {return location;}
    bool LocationTuningVerified() const {return provider.Verified()&&database.Authoritative();}
    std::vector<std::string> DebugLines() const;
    std::vector<std::string> CompactDebugLines() const;
    bool CreateOverride(bool area);
private:
    void Retarget();
    void Log(const std::string& message) const;
    std::wstring basePath;
    EnvironmentDatabase database;
    WoWLocationProvider provider;
    EnvironmentConfiguration config;
    EnvironmentResolution resolved;
    ResolvedEnvironmentState current,from,target;
    LocationContext location;
    LocationDebouncer debounce;
    uint64_t lastPoll=0,blendStart=0,lastNow=0;
    float progress=1;
};
}
