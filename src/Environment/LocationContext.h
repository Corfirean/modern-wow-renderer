#pragma once
#include <cstdint>
namespace renderer {
struct LocationContext {
    uint32_t mapId=0, zoneId=0, areaId=0;
    bool valid=false;
    uint64_t revision=0;
    bool SamePlace(const LocationContext& b) const {
        return valid==b.valid && (!valid || (mapId==b.mapId && zoneId==b.zoneId && areaId==b.areaId));
    }
};
class LocationDebouncer {
    LocationContext candidate;
    uint64_t since=0;
    unsigned samples=0;
public:
    void Reset() {candidate={};samples=0;since=0;}
    bool Confirm(const LocationContext& sample,const LocationContext& current,uint64_t now,uint32_t debounceMs) {
        if(!samples||!sample.SamePlace(candidate)) {candidate=sample;since=now;samples=1;}
        else if(samples<3)++samples;
        const uint64_t wait=sample.valid&&current.valid&&sample.mapId!=current.mapId?125:debounceMs;
        return !sample.SamePlace(current)&&samples>=3&&now-since>=wait;
    }
};
}
