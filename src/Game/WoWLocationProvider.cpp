#include "WoWLocationProvider.h"
#include "../Environment/EnvironmentDatabase.h"
#include <limits>
namespace renderer {
void WoWLocationProvider::Configure(const std::wstring& path) {
    available=verified=false;offsets={};rawSample={};validation="Not sampled";
    if(!client.base&&!client.Detect()) {status="Unsupported executable / PE headers";return;}
    // Only an exact executable fingerprint opts into custom RVAs. The stock
    // 12340 RVAs are documentation, never an automatic unknown-build fallback.
    wchar_t expected[80]{};GetPrivateProfileStringW(L"LocationProvider",L"ExeSHA256",L"",expected,80,path.c_str());
    std::string fingerprint;for(const wchar_t* c=expected;*c;++c)fingerprint+=char(*c);
    if(client.sha256.empty()||fingerprint!=client.sha256) {status="Unrecognized executable fingerprint; Default environment";return;}
    auto read=[&](const wchar_t* key)->uintptr_t {
        wchar_t value[64]{};GetPrivateProfileStringW(L"LocationProvider",key,L"",value,64,path.c_str());
        wchar_t* end=nullptr;errno=0;const auto n=wcstoull(value,&end,0);
        return errno||end==value||*end||n>std::numeric_limits<uintptr_t>::max()?0:static_cast<uintptr_t>(n);
    };
    offsets={read(L"MapRva"),read(L"ZoneRva"),read(L"AreaRva")};
    uint32_t probe=0;
    if(!offsets.map||!offsets.zone||!offsets.area||offsets.map==offsets.zone||offsets.map==offsets.area||offsets.zone==offsets.area||!client.Read(offsets.map,&probe,4)||!client.Read(offsets.zone,&probe,4)||!client.Read(offsets.area,&probe,4)) {status="Invalid/unreadable location RVAs; Default environment";return;}
    available=true;verified=GetPrivateProfileIntW(L"LocationProvider",L"VerifiedInWorld",0,path.c_str())!=0;
    status=verified?"Fingerprint matched; in-world verified RVAs":"Fingerprint matched; diagnostic RVAs (effects locked)";
}
LocationContext WoWLocationProvider::Sample(const EnvironmentDatabase& db,bool world) const {
    LocationContext a,b;
    rawSample={};
    if(!available||!world) {validation=available?"No render world context":"Provider unavailable";return a;}
    auto read=[&](LocationContext& v) {return client.Read(offsets.map,&v.mapId,4)&&client.Read(offsets.zone,&v.zoneId,4)&&client.Read(offsets.area,&v.areaId,4);};
    if(!read(a)||!read(b)||a.mapId!=b.mapId||a.zoneId!=b.zoneId||a.areaId!=b.areaId) {validation="Read failed or incoherent snapshot";return {};}
    rawSample=a;validation="IDs not validated against database";
    if(!db.Map(a.mapId)||!a.zoneId||a.areaId>1000000||a.zoneId>1000000)return {};
    const auto area=db.Area(a.areaId);const auto zone=db.Area(a.zoneId);
    if(!zone||(area&&area->mapId!=a.mapId)||(!area&&zone->mapId!=a.mapId))return {};
    // Unknown custom subareas can still inherit a known zone/map. Known areas
    // must belong to the reported zone, catching most coherent-looking garbage.
    if(area) {
        uint32_t parent=area->id;
        while(parent&&parent!=a.zoneId) {const auto p=db.Area(parent);if(!p)return {};parent=p->parentId;}
        if(parent!=a.zoneId)return {};
    }
    a.valid=true;validation=area?"Database hierarchy valid":a.areaId?"Unknown custom area; known zone/map valid":"No subarea; known zone/map valid";return a;
}
}
