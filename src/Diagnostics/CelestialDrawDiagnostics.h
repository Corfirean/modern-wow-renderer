#pragma once
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>
#include <set>
#include <mutex>
#include <atomic>
#include <wrl/client.h>

// Observation only. No resource locks, state changes, retained COM objects,
// or GPU readbacks - same safety contract as WaterDiagnostics.h, which this
// mirrors. Captures every draw call for exactly one frame and flags ones
// that loosely match "could plausibly be a sky-phase sun/moon billboard"
// (small primitive count, alpha blended, no z-write). That heuristic is a
// filter to make the log readable, NOT a claim about what any given draw
// actually is - the point of this file is to gather evidence for that
// question, not answer it in advance.
namespace celestialdiag {
using Microsoft::WRL::ComPtr;
std::atomic<bool> enabled{false};
std::mutex mutex;
bool active=false, keyDown=false;
unsigned draws=0, candidates=0, maxDraws=4096;
unsigned presentsSinceStart=0, totalDrawCallsSeen=0;
std::filesystem::path root, folder;
std::ofstream log;
std::set<std::string> dumped;
IDirect3DDevice9* target=nullptr;

void Configure(const std::wstring& base) {
    auto ini=base+L"ModernWoWRenderer.ini";
    enabled=GetPrivateProfileIntW(L"CelestialDiagnostics",L"Enabled",0,ini.c_str())!=0;
    root=std::filesystem::path(base)/L"CelestialDiagnostics";
}
void Finish(const char* why) {
    if(active) {
        log<<"END reason="<<why<<" draws="<<draws<<" candidates="<<candidates
           <<" presents_since_start="<<presentsSinceStart
           <<" total_draw_calls_seen_this_session="<<totalDrawCallsSeen<<'\n';
        log.close();
    }
    active=false; target=nullptr;
}
void Start(IDirect3DDevice9* d) {
    SYSTEMTIME t{}; GetLocalTime(&t);
    std::wostringstream name;
    name<<t.wYear<<L'-'<<t.wMonth<<L'-'<<t.wDay<<L'_'<<t.wHour<<L'-'<<t.wMinute<<L'-'<<t.wSecond;
    folder=root/name.str();
    std::filesystem::create_directories(folder);
    log.open(folder/L"celestial_candidates.txt");
    if(!log) { enabled=false; return; }
    draws=0; candidates=0; presentsSinceStart=0; dumped.clear(); target=d; active=true;
    log<<"celestial draw capture, F9 to start.\n";
    log<<"CANDIDATE=1 heuristic: primitives<=8, alphaBlend=1, zWrite=0. A filter to make this readable,\n";
    log<<"not a verdict - check the flagged draws by eye (texture dims/hash, VSF values) before trusting one.\n";
}
void Present(IDirect3DDevice9* d) noexcept {
    if(!enabled.load(std::memory_order_relaxed)) return;
    try {
        std::lock_guard lock(mutex);
        if(active && target==d) {
            ++presentsSinceStart;
            // Finish once we've actually captured something from at least one
            // full Present-to-Present interval, OR bail out after a generous
            // number of empty frames so a capture can never hang silently
            // forever if draws truly aren't reaching this hook for some reason.
            if(draws>0 && presentsSinceStart>=2) Finish("present");
            else if(presentsSinceStart>=180) Finish("timeout-no-draws");
        }
        const bool down=(GetAsyncKeyState(VK_F9)&0x8000)!=0;
        DWORD pid=0; GetWindowThreadProcessId(GetForegroundWindow(),&pid);
        if(down&&!keyDown&&pid==GetCurrentProcessId()&&!active) Start(d);
        keyDown=down;
    } catch(...) { enabled=false; active=false; }
}
void Reset(IDirect3DDevice9* d) noexcept {
    if(!enabled.load(std::memory_order_relaxed)) return;
    try { std::lock_guard lock(mutex); if(active&&target==d) Finish("reset"); }
    catch(...) { enabled=false; active=false; }
}
template<class Shader> std::string HashShader(Shader* shader,const char* stage,HRESULT hr,bool dumpIfNew) {
    if(FAILED(hr)) return "query-error";
    if(!shader) return "fixed-function";
    UINT size=0;
    if(FAILED(shader->GetFunction(nullptr,&size))||!size||size>65536) return "bytecode-unavailable";
    std::vector<unsigned char> bytes(size);
    if(FAILED(shader->GetFunction(bytes.data(),&size))) return "bytecode-read-error";
    uint64_t hash=14695981039346656037ull;
    for(unsigned char c:bytes) { hash^=c; hash*=1099511628211ull; }
    std::ostringstream key; key<<stage<<'_'<<std::hex<<hash;
    const std::string id=key.str();
    if(dumpIfNew && !dumped.count(id) && dumped.size()<128) {
        std::ofstream out(folder/(id+".bin"),std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());
        dumped.insert(id);
    }
    return id;
}
void Draw(IDirect3DDevice9* d,const char* kind,D3DPRIMITIVETYPE type,UINT count) noexcept {
    if(!enabled.load(std::memory_order_relaxed)) return;
    try {
        std::lock_guard lock(mutex);
        ++totalDrawCallsSeen; // unconditional - lets Finish() report whether Draw() is even reached
        if(!active||target!=d) return;
        if(draws>=maxDraws) { Finish("draw-limit"); return; }

        DWORD alphaBlend=0, zWrite=0, zEnable=0, alphaTest=0, cull=0;
        d->GetRenderState(D3DRS_ALPHABLENDENABLE,&alphaBlend);
        d->GetRenderState(D3DRS_ZWRITEENABLE,&zWrite);
        d->GetRenderState(D3DRS_ZENABLE,&zEnable);
        d->GetRenderState(D3DRS_ALPHATESTENABLE,&alphaTest);
        d->GetRenderState(D3DRS_CULLMODE,&cull);

        bool candidate = (count<=8) && alphaBlend && !zWrite;
        if(candidate) ++candidates;

        unsigned ordinal=draws++;
        log<<"DRAW "<<ordinal<<" kind="<<kind<<" topology="<<type<<" primitives="<<count
           <<" CANDIDATE="<<(candidate?1:0)<<" alphaBlend="<<alphaBlend<<" zWrite="<<zWrite
           <<" zEnable="<<zEnable<<" alphaTest="<<alphaTest<<" cull="<<cull<<'\n';

        ComPtr<IDirect3DVertexShader9> vs; ComPtr<IDirect3DPixelShader9> ps;
        HRESULT vh=d->GetVertexShader(vs.GetAddressOf()), ph=d->GetPixelShader(ps.GetAddressOf());
        log<<"  VS="<<HashShader(vs.Get(),"vs",vh,candidate)<<" PS="<<HashShader(ps.Get(),"ps",ph,candidate)<<'\n';

        for(DWORD slot=0;slot<2;++slot) {
            ComPtr<IDirect3DBaseTexture9> texture;
            HRESULT hr=d->GetTexture(slot,texture.GetAddressOf());
            log<<"  T"<<slot<<" object="<<texture.Get();
            if(SUCCEEDED(hr)&&texture&&texture->GetType()==D3DRTYPE_TEXTURE) {
                D3DSURFACE_DESC desc{};
                if(SUCCEEDED(static_cast<IDirect3DTexture9*>(texture.Get())->GetLevelDesc(0,&desc)))
                    log<<" size="<<desc.Width<<'x'<<desc.Height<<" format="<<desc.Format;
            }
            log<<'\n';
        }

        if(candidate) {
            float vsConst[8][4]{};
            HRESULT vch=d->GetVertexShaderConstantF(0,vsConst[0],8);
            log<<"  VSF[0..7] hr="<<vch;
            if(SUCCEEDED(vch)) for(auto& row:vsConst) for(float v:row) log<<' '<<v;
            log<<'\n';

            // Fixed-function draws (no vertex shader) place geometry via the
            // classic transform pipeline instead of shader constants - this
            // is the only way to recover where such a draw actually sits.
            for(auto xf:{D3DTS_WORLD,D3DTS_VIEW,D3DTS_PROJECTION}) {
                D3DMATRIX m{};
                HRESULT th=d->GetTransform(xf,&m);
                log<<"  XF"<<int(xf)<<" hr="<<th;
                if(SUCCEEDED(th)) for(auto& row:m.m) for(float v:row) log<<' '<<v;
                log<<'\n';
            }
            ComPtr<IDirect3DVertexDeclaration9> decl;
            HRESULT dh=d->GetVertexDeclaration(decl.GetAddressOf());
            log<<"  VDECL hr="<<dh<<" object="<<decl.Get()<<'\n';
            DWORD fvf=0; HRESULT fh=d->GetFVF(&fvf);
            log<<"  FVF hr="<<fh<<" value="<<fvf<<'\n';
        }

        if(!log) { Finish("write-error"); enabled=false; }
    } catch(...) { enabled=false; active=false; }
}
}
