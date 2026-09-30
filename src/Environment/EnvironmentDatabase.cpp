#include "EnvironmentDatabase.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <set>
namespace renderer {
bool EnvironmentDatabase::Load(const std::wstring& path,std::string& error) {
    std::ifstream input{std::filesystem::path(path)};
    std::string line;
    if(!std::getline(input,line)||line!="EnvironmentDatabase 1") { error="Missing or unsupported environment database";return false; }
    std::map<uint32_t,EnvironmentArea> nextAreas;
    std::map<uint32_t,std::string> nextMaps;
    bool nextAuthoritative=false;
    while(std::getline(input,line)) {
        if(line=="# provenance=verified-archive-order"||line=="# provenance=explicit-client-vfs-export"||line=="# provenance=identical-all-readable-variants"||line=="# provenance=verified-live-client-tables")nextAuthoritative=true;
        if(line.empty()||line[0]=='#')continue;
        std::istringstream row(line);char kind=0;row>>kind;
        if(kind=='A') {
            EnvironmentArea a;
            if(!(row>>a.id>>a.mapId>>a.parentId>>a.flags>>std::quoted(a.name))||!a.id||a.name.size()>1024||nextAreas.count(a.id)) {error="Invalid/duplicate area record";return false;}
            nextAreas.emplace(a.id,std::move(a));
        } else if(kind=='M') {
            uint32_t id;std::string name;
            if(!(row>>id>>std::quoted(name))||name.size()>1024||nextMaps.count(id)) {error="Invalid/duplicate map record";return false;}
            nextMaps.emplace(id,std::move(name));
        } else {error="Unknown database record";return false;}
        row>>std::ws;if(!row.eof()) {error="Trailing database data";return false;}
        if(nextAreas.size()>100000||nextMaps.size()>10000) {error="Database record limit exceeded";return false;}
    }
    if(nextAreas.empty()||nextMaps.empty()) {error="Empty database";return false;}
    for(const auto& entry:nextAreas) {
        const auto& a=entry.second;
        // Ascension patch-M contains cross-map parent links. Parent is an
        // authored hierarchy reference, not an assertion of map equality.
        if(!nextMaps.count(a.mapId)||(a.parentId&&!nextAreas.count(a.parentId))) {error="Invalid area references";return false;}
        std::set<uint32_t> visited;uint32_t parent=a.id;
        while(parent) {if(!visited.insert(parent).second) {error="Cyclic area hierarchy";return false;}parent=nextAreas.at(parent).parentId;}
    }
    areas.swap(nextAreas);maps.swap(nextMaps);authoritative=nextAuthoritative;error.clear();return true;
}
const EnvironmentArea* EnvironmentDatabase::Area(uint32_t id) const {auto i=areas.find(id);return i==areas.end()?nullptr:&i->second;}
const std::string* EnvironmentDatabase::Map(uint32_t id) const {auto i=maps.find(id);return i==maps.end()?nullptr:&i->second;}
}
