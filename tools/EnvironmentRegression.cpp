#include "../src/Environment/EnvironmentProfileManager.h"
#include "../src/Environment/LocationTuning.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <cmath>
using namespace renderer;
void Require(bool v,const char* message) {if(!v)throw std::runtime_error(message);std::cout<<"PASS "<<message<<'\n';}
bool Near(float a,float b){return std::abs(a-b)<.0001f;}
static uint32_t testMap=0,testZone=12,testArea=87;
int main() try {
 const auto tuningDir=std::filesystem::absolute("build/location-tuning-test");std::filesystem::create_directories(tuningDir);
 const auto tuningBase=tuningDir.wstring()+L"\\";
 std::filesystem::remove(tuningDir/L"LocationGraphics.ini");
 {std::ofstream f(tuningDir/L"GraphicsEffects.ini");f<<"[Atmosphere]\nShaftPercent=60\n";}
 locationtuning::Sync(tuningBase,{571,65,4164,true},true);
 Require(locationtuning::Save(L"Atmosphere",L"ShaftPercent",170),"save exact location preset");
 Require(locationtuning::ReadInt(L"Atmosphere",L"ShaftPercent",0,locationtuning::baseFile.c_str())==170,"read saved local value");
 locationtuning::Sync(tuningBase,{619,4494,4623,true},true);
 Require(locationtuning::ReadInt(L"Atmosphere",L"ShaftPercent",0,locationtuning::baseFile.c_str())==60,"instance inherits baseline without outdoor preset leaking");
 Require(locationtuning::Save(L"Atmosphere",L"ShaftPercent",20),"save separate instance preset");
 locationtuning::Sync(tuningBase,{571,65,4164,true},true);
 Require(locationtuning::ReadInt(L"Atmosphere",L"ShaftPercent",0,locationtuning::baseFile.c_str())==170,"return restores original outdoor preset");
 Require(GetPrivateProfileIntW(L"Atmosphere",L"ShaftPercent",0,locationtuning::baseFile.c_str())==60,"local edits preserve shared graphics file");
 locationtuning::Sync(tuningBase,{530,10127,0,true},true);
 Require(locationtuning::Save(L"Water",L"Enabled",0),"zero area saves zone preset");
 locationtuning::Sync(tuningBase,{530,10127,123,true},true);
 Require(locationtuning::ReadInt(L"Water",L"Enabled",1,locationtuning::baseFile.c_str())==0,"subarea inherits zone override including zero values");
 locationtuning::Sync(tuningBase,{},true);
 Require(!locationtuning::Save(L"Water",L"Enabled",1),"loading screen cannot save a preset");
 locationtuning::Sync(tuningBase,{571,65,4164,true},false);
 Require(!locationtuning::Save(L"Water",L"Enabled",1),"unverified database blocks location writes");
 locationtuning::Sync(L"",{},false);
 EnvironmentConfiguration c;std::string error;
 const std::string text="[EnvironmentSystem]\nTransitionSeconds=4\nAreaDebounceMs=300\n[Default]\nFogDensityMultiplier=0.8\n[Environment.Forest]\nRayIntensityMultiplier=1.2\nLocalFogMultiplier=0.7\n[Environment.City]\nProfile=Forest\nLocalFogMultiplier=0.1\n[Map.0]\nFogDensityMultiplier=0.5\n[Zone.12]\nProfile=City\nFogDensityMultiplier=0.6\n[Area.87]\nRayIntensityMultiplier=0.3\n";
 Require(ParseEnvironmentConfiguration(text,c,error),"INI parser accepts complete profile");
 LocationContext l{0,12,87,true};auto r=ResolveEnvironment(c,l);
 Require(Near(r.state[FogDensity],.6f)&&Near(r.state[LocalFog],.1f)&&Near(r.state[Rays],.3f)&&r.source=="Area","Default < Map < Zone < Area with named inheritance");
 l.areaId=12345;r=ResolveEnvironment(c,l);
 Require(r.source=="Zone"&&Near(r.state[Rays],1.2f),"unknown custom area inherits zone");
 l.zoneId=12345;r=ResolveEnvironment(c,l);Require(r.source=="Map"&&Near(r.state[FogDensity],.5f),"unknown zone inherits map zero");
 l.valid=false;r=ResolveEnvironment(c,l);Require(r.source=="Default"&&Near(r.state[FogDensity],.8f),"unavailable location uses Default");
 c.enabled=false;r=ResolveEnvironment(c,l);Require(Near(r.state[FogDensity],1),"disabled system is identity");c.enabled=true;
 for(const auto* bad:{"[Default]\nFogDensityMultiplier=nan\n","[Default]\nFogDensityMultiplier=5\n","[Environment.A]\nProfile=B\n[Environment.B]\nProfile=A\n","[Zone.12]\nProfile=Missing\n","[Map.bad]\n","[Default]\nFogDensityMultiplier=1\nFogDensityMultiplier=2\n"}) {
  Require(!ParseEnvironmentConfiguration(bad,c,error),"reject corrupt config transactionally");
 }
 Require(c.named.count("City")==1,"failed reload preserves previous config");
 ResolvedEnvironmentState a,b;b.values[Rays]=3;
 Require(Near(BlendEnvironment(a,b,0)[Rays],1)&&Near(BlendEnvironment(a,b,.5f)[Rays],2)&&Near(BlendEnvironment(a,b,1)[Rays],3),"smoothstep interpolation endpoints and midpoint");
 auto interrupted=BlendEnvironment(a,b,.5f);ResolvedEnvironmentState next;next.values[Rays]=0;
 Require(Near(BlendEnvironment(interrupted,next,0)[Rays],2),"interrupted transition starts at visible current state");
 LocationDebouncer debounce;LocationContext original{0,12,87,true},other{0,12,88,true},invalid;
 Require(!debounce.Confirm(other,original,1000,300)&&!debounce.Confirm(other,original,1125,300)&&!debounce.Confirm(other,original,1250,300)&&debounce.Confirm(other,original,1375,300),"area requires stable 300ms and three samples");
 debounce.Reset();Require(!debounce.Confirm(other,original,1000,300)&&!debounce.Confirm(original,original,1125,300)&&!debounce.Confirm(other,original,1250,300),"border flapping resets candidate timer");
 debounce.Reset();Require(!debounce.Confirm(invalid,original,1000,300)&&!debounce.Confirm(invalid,original,1125,300)&&!debounce.Confirm(original,original,1250,300),"one invalid/loading sample cannot switch profile");
 auto file=std::filesystem::temp_directory_path()/"modern-wow-environment-regression.txt";
 {std::ofstream f(file);f<<"EnvironmentDatabase 1\nM 0 \"Eastern Kingdoms\"\nA 12 0 0 65 \"Elwynn Forest\"\nA 87 0 12 0 \"Goldshire\"\n";}
 EnvironmentDatabase db;Require(db.Load(file.wstring(),error)&&db.AreaCount()==2&&db.Area(87)->parentId==12,"database parser and hierarchy");
 {std::ofstream f(file);f<<"EnvironmentDatabase 1\nM 0 \"Map\"\nA 12 0 87 0 \"Cycle\"\nA 87 0 12 0 \"Cycle\"\n";}
 Require(!db.Load(file.wstring(),error)&&db.Area(87)->name=="Goldshire","database rejects cycles and preserves previous state");std::filesystem::remove(file);
 WoWClientContext client;Require(client.Detect(),"detect x86 test executable");uint32_t value=0;
 Require(!client.Read(client.imageSize,&value,4)&&!client.Read(UINTPTR_MAX,&value,4),"bad RVAs fail without crash");
 WoWLocationProvider provider;provider.Configure(L"nonexistent-environment.ini");Require(!provider.Verified()&&!provider.Sample(db,true).valid,"unrecognized build never reads reference offsets");
 const auto providerFile=std::filesystem::temp_directory_path()/"modern-wow-location-regression.ini";
 auto writeProvider=[&](uintptr_t mapRva) {std::ofstream ini(providerFile);ini<<"[LocationProvider]\nExeSHA256="<<client.sha256<<"\nMapRva="<<mapRva<<"\nZoneRva="<<(reinterpret_cast<uintptr_t>(&testZone)-client.base)<<"\nAreaRva="<<(reinterpret_cast<uintptr_t>(&testArea)-client.base)<<"\nVerifiedInWorld=1\n";};
 writeProvider(reinterpret_cast<uintptr_t>(&testMap)-client.base);provider.Configure(providerFile.wstring());
 Require(provider.Verified()&&provider.Sample(db,true).valid&&provider.Sample(db,true).areaId==87,"exact-hash provider reads validated live test-process image globals");
 Require(!provider.Sample(db,false).valid,"provider rejects absent world context");
 testArea=12345;Require(provider.Sample(db,true).valid,"provider accepts unknown custom area with known zone and map");testArea=87;
 testArea=0;Require(provider.Sample(db,true).valid&&provider.Sample(db,true).areaId==0,"zero subarea preserves validated zone/map without inventing AreaID");testArea=87;
 writeProvider(client.imageSize+4);provider.Configure(providerFile.wstring());Require(!provider.Verified()&&!provider.Sample(db,true).valid,"matching hash with invalid configured RVA safely disables provider");std::filesystem::remove(providerFile);
 const auto actual=std::filesystem::path("build/client-research/areas.txt");
 if(std::filesystem::exists(actual)) {EnvironmentDatabase exported;Require(exported.Load(actual.wstring(),error)&&exported.AreaCount()==2849&&!exported.Authoritative(),"real Ascension custom database loads with diagnostic-only gate");}
 std::cout<<"Environment regression passed\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
