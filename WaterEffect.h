#pragma once
#include <unordered_set>
#include <cstdio>
#include "src/D3D9/TrackedRenderState.h"
namespace watereffect {
using Microsoft::WRL::ComPtr;
bool enabled=false, active=true, keyDown=false, hotkey=true;
bool effectEnabled=true,wavesEnabled=true,geometryWavesEnabled=false,reflectionsEnabled=true,refractionEnabled=true,depthEnabled=true,foamEnabled=true,sunGlintEnabled=true;
float strength=.003f;
float normalStrength=.16f, specularStrength=.55f;
float reflectionStrength=.70f,environmentStrength=.24f,reflectionDistance=120.f,reflectionThickness=1.8f;
float flowSpeed=1.f,flowRotation=.32f;
float refractionStrength=.12f,absorptionStrength=.18f,shoreFoamStrength=.12f,shoreFoamWidth=2.5f;
float swellStrength=.45f,sunGlintStrength=1.35f,fresnelStrength=1.f,crestFoamStrength=.18f;
float geometryWaveAmplitude=.10f;
float waterDebugMode=0;
IDirect3DTexture9* reflectionScene=nullptr;
IDirect3DTexture9* reflectionDepth=nullptr;
float reflectionData[12]{};
std::vector<DWORD> code,vertexCode;
inline ComPtr<IDirect3DPixelShader9> cachedReplacementPS;
inline ComPtr<IDirect3DVertexShader9> cachedReplacementVS;
inline IDirect3DDevice9* cachedShaderDevice = nullptr;
bool showStatus=true;
unsigned frameMatches=0;
std::wstring logPath;
std::wstring mainIni,tuningIni;
const char* vertexSource=R"HLSL(
float4 projection[4]:register(c0);
float4 fogParams:register(c4);
float4 modelView[4]:register(c5);
float4 uvAX:register(c9);float4 uvAY:register(c10);float4 uvAOffset:register(c12);
float4 uvBX:register(c13);float4 uvBY:register(c14);float4 uvBOffset:register(c16);
float4 lightDirection:register(c33);float4 ambient:register(c34);float4 diffuse:register(c35);
// x=time, y=world-space displacement amplitude, z=long-wave strength
float4 waveControl:register(c200);
struct Output {float4 pos:POSITION;float4 color:COLOR0;float2 uv0:TEXCOORD0;float2 uv1:TEXCOORD1;float fog:FOG;float3 viewPos:TEXCOORD2;float3 viewNormal:TEXCOORD3;};
Output main(float4 pos:POSITION,float3 normal:NORMAL,float4 color:COLOR0,float2 uv0:TEXCOORD0,float2 uv1:TEXCOORD1) {
    Output o;
    float phaseA=(uv0.x*.83+uv0.y*.56)*6.28318+waveControl.x*.72;
    float phaseB=(uv0.x*-.47+uv0.y*.91)*6.28318-waveControl.x*.43;
    pos.z+=waveControl.y*(sin(phaseA)+sin(phaseB)*.55)*(.55+.45*waveControl.z);
    float4 p=modelView[0]*pos.x+modelView[1]*pos.y+modelView[2]*pos.z+modelView[3];
    o.pos=projection[0]*p.x+projection[1]*p.y+projection[2]*p.z+projection[3]*p.w;
    o.fog=min(pow(max(p.z*fogParams.x+fogParams.y,0),fogParams.z),1);
    o.uv1=uvAX.xy*uv0.x+uvAY.xy*uv0.y+uvAOffset.xy;
    o.uv0=uvBX.xy*uv1.x+uvBY.xy*uv1.y+uvBOffset.xy;
    float3 n=modelView[0].xyz*normal.x+modelView[1].xyz*normal.y+modelView[2].xyz*normal.z;
    o.color=float4(diffuse.rgb*saturate(dot(-lightDirection.xyz,n))+ambient.rgb,1)*color;
    o.viewPos=p.xyz;o.viewNormal=n;return o;
}
)HLSL";
const char* source=R"HLSL(
sampler2D baseTexture:register(s0);
sampler2D surfaceTexture:register(s1);
sampler2D sceneTexture:register(s2);
sampler2D sceneDepth:register(s3);
float4 fogColor:register(c4);
float4 controls:register(c200);
float4 lightDirection:register(c201);
float4 lightColor:register(c202);
// c203=(projection A,B,scaleX,scaleY)
float4 reflectionProjection:register(c203);
// x=SSR strength,y=1/width,z=1/height,w=viewport depth maximum
float4 reflectionControl:register(c204);
// x=environment strength,y=max trace distance,z=base thickness,w=enabled
float4 reflectionStyle:register(c205);
// x=animation speed, y=counter-rotating layer strength.  The latter mirrors
// the modern client's animated UV transforms without depending on its ADT data.
float4 flowControl:register(c206);
// x=refraction, y=depth absorption, z=shore foam, w=foam depth width
float4 waterStyle:register(c207);
// x=long swell strength, y=sun glitter, z=Fresnel response, w=crest foam
float4 foreverStyle:register(c208);
float3 safeNormalize(float3 v) {return v*rsqrt(max(dot(v,v),1e-8));}
float hash21(float2 p) {
    float3 q=frac(float3(p.x,p.y,p.x)*.1031);
    q+=dot(q,q.yzx+33.33);
    return frac((q.x+q.y)*q.z);
}
// Smooth random height and its analytical gradient; no repeating sine lattice.
float3 noiseGradient(float2 p) {
    float2 i=floor(p),f=frac(p);
    float a=hash21(i),b=hash21(i+float2(1,0));
    float c=hash21(i+float2(0,1)),d=hash21(i+1);
    float2 u=f*f*(3-2*f),du=6*f*(1-f);
    float k=a-b-c+d;
    return float3(a+(b-a)*u.x+(c-a)*u.y+k*u.x*u.y,
                  du.x*((b-a)+k*u.y),du.y*((c-a)+k*u.x));
}
float4 main(float4 color:COLOR0,float2 uv0:TEXCOORD0,float2 uv1:TEXCOORD1,float fog:FOG,float3 viewPos:TEXCOORD2,float3 viewNormal:TEXCOORD3):COLOR0 {
    // Preserve the original alpha: surfaceTexture never contributes alpha.
    float4 base=tex2D(baseTexture,uv0);
    float time=controls.x*flowControl.x;
    float2 drift=float2(time*.19,-time*.11);
    // Preserve WoW's two material UV transforms and add a slower
    // counter-rotating component.  This keeps a coherent current instead of
    // making the reflection look like a stationary mirror with noisy normals.
    float angle=time*.035;
    float s=sin(angle),c=cos(angle);
    float2 rotated=float2(c*uv0.x-s*uv0.y,s*uv0.x+c*uv0.y);
    float2 flowingUv=lerp(uv1,rotated,flowControl.y);
    float2 p=float2(dot(flowingUv,float2(.91,.41)),dot(flowingUv,float2(-.41,.91)))*float2(14,31);
    float3 swell=noiseGradient(p*.055+drift*.085);
    float3 swellCross=noiseGradient(float2(-p.y*.043,p.x*.043)-drift*.057+31.7);
    float3 low=noiseGradient(p*.37+drift*.43+float2(swell.x,swellCross.x)*1.8);
    float2 warped=p+float2(low.x,low.x*.71)*1.3;
    float3 a=noiseGradient(warped+drift);
    float3 b=noiseGradient(float2(warped.x*.8+warped.y*.6,-warped.x*.6+warped.y*.8)*1.83-drift*.67+17.2);
    float2 longSlopes=swell.yz+float2(-swellCross.z,swellCross.y)*.72;
    // Keep the pre-refactor wave energy: the reduced slope made every later
    // effect (normal lighting, refraction, SSR and foam) nearly flat.
    float2 slopes=(a.yz+float2(.8*b.y-.6*b.z,.6*b.y+.8*b.z)*.43)*.72+
                  longSlopes*foreverStyle.x;
    float filter=rsqrt(1+16*dot(fwidth(p),fwidth(p)));
    slopes*=filter;
    float2 offset=slopes*controls.y;
    float flowGate=saturate(controls.y*100+controls.z*4);
    float3 surfaceA=tex2D(surfaceTexture,uv1+offset+drift*.010*flowGate).rgb;
    float3 surfaceB=tex2D(surfaceTexture,rotated-offset*.63-drift*.006).rgb;
    float3 surface=lerp(surfaceA,surfaceB,flowControl.y*.45*flowGate);
    float3 rgb=surface+color.rgb*base.rgb;
    float3 v=safeNormalize(-viewPos);
    float3 n=safeNormalize(viewNormal);
    // Water can be drawn from either side. Orient only the added lighting normal.
    n*=dot(n,v)<0?-1:1;
    float3 px=ddx(viewPos),py=ddy(viewPos);
    float2 ux=ddx(uv1),uy=ddy(uv1);
    float det=ux.x*uy.y-ux.y*uy.x;
    float3 tangent=safeNormalize((px*uy.y-py*ux.y)*(det<0?-1:1));
    tangent=safeNormalize(tangent-n*dot(n,tangent));
    float3 bitangent=safeNormalize(cross(n,tangent));
    float3 wave=safeNormalize(n+controls.z*(tangent*slopes.x+bitangent*slopes.y));
    // The captured pre-water colour and depth allow shallow refraction and a
    // depth transition without changing the original map or liquid data.
    float2 screenUV=float2(.5+.5*viewPos.x*reflectionProjection.z/max(viewPos.z,.05),
                           .5-.5*viewPos.y*reflectionProjection.w/max(viewPos.z,.05));
    float behindRaw=tex2Dlod(sceneDepth,float4(screenUV,0,0)).r;
    float behindZ=behindRaw>=.9999?100000:reflectionProjection.y/(behindRaw/max(reflectionControl.w,.001)-reflectionProjection.x);
    // Depending on the liquid batch winding, view-space depth can arrive on
    // either side of the copied terrain depth. The absolute separation is the
    // stable shallow-water measure; visibility has already been depth-tested.
    float waterDepth=abs(behindZ-viewPos.z);
    float captureValid=reflectionStyle.w;
    float depthValid=captureValid*step(behindZ,99999);
    float2 refractOffset=slopes*float2(reflectionControl.y,reflectionControl.z)*(15+waterStyle.x*120);
    float2 refractUV=screenUV+(depthValid?refractOffset:float2(0,0));
    float3 refracted=tex2Dlod(sceneTexture,float4(refractUV,0,0)).rgb;
    float shallow=saturate(waterDepth/max(waterStyle.w,1e-3));
    float3 deepTint=lerp(fogColor.rgb*.42,float3(.025,.105,.135),.62);
    float absorption=depthValid*(1-exp(-waterDepth*waterStyle.y*.14));
    float shallowEdge=1-smoothstep(.08,.08+max(waterStyle.w,.1),waterDepth);
    float shore=depthValid*shallowEdge;
    float crest=saturate(.55+a.x*.45+dot(slopes,float2(.18,-.12)));
    float3 l=safeNormalize(-lightDirection.xyz);
    float ndh=saturate(dot(wave,safeNormalize(l+v)));
    float ndl=saturate(dot(wave,l));
    float broad=pow(ndh,14);
    float sparkle=pow(ndh,112);
    float glitterVariation=.28+1.45*pow(saturate(a.x*.52+b.x*.31+swell.x*.17),3);
    float foreverFresnel=pow(1-saturate(dot(wave,v)),2);
    float fresnel=saturate(.08+foreverStyle.z*(.10+.62*foreverFresnel));
    float pathBreakup=.22+1.35*smoothstep(.42,.88,a.x*.46+b.x*.34+swell.x*.20);
    float celestialPath=pow(ndh,9)*pathBreakup*foreverStyle.y*(.45+1.15*fresnel)*ndl;
    float spec=(.22*broad+sparkle*glitterVariation)*foreverStyle.y*(.65+fresnel)*ndl+celestialPath*.38;
    float shading=1+controls.z*.6*(dot(wave,l)-dot(n,l));
    float energy=max(spec*controls.w,0);
    energy=.68*energy/(.42+energy);
    float luminance=dot(max(lightColor.rgb,0),float3(.2126,.7152,.0722));
    float3 sheenColor=lerp(luminance.xxx,max(lightColor.rgb,0),.45);
    rgb=rgb*shading+sheenColor*energy;

    // 1. Water body: bottom refracted scene + depth absorption
    float3 waterBody=rgb;
    if(captureValid>.5) {
        float3 bottom=lerp(refracted,deepTint,saturate(absorption*(.85-.22*fresnel)));
        waterBody=lerp(rgb,bottom,saturate(.25+waterStyle.x*.75));
    }

    // 2. Screen-Space Reflections (SSR) with wave normal perturbation
    float3 environment=lerp(fogColor.rgb,max(sheenColor,.08),.28);
    float3 reflected=environment;
    float hit=0,edge=0;
    if(reflectionStyle.w>.5 && reflectionControl.x>.001) {
        float3 ray=safeNormalize(reflect(-v,wave));
        float3 hitColor=environment;
        float bestConfidence=0;
        [unroll] for(int stepIndex=0;stepIndex<12;++stepIndex) {
            float q=(stepIndex+1)/12.0;
            float traceDistance=.45+q*q*reflectionStyle.y;
            float3 samplePosition=viewPos+wave*.15+ray*traceDistance;
            float2 sampleUV=float2(.5+.5*samplePosition.x*reflectionProjection.z/max(samplePosition.z,.05),
                                   .5-.5*samplePosition.y*reflectionProjection.w/max(samplePosition.z,.05));
            float inside=step(0,sampleUV.x)*step(sampleUV.x,1)*step(0,sampleUV.y)*step(sampleUV.y,1)*step(.05,samplePosition.z);
            float raw=tex2Dlod(sceneDepth,float4(sampleUV,0,0)).r;
            float sceneZ=raw>=.9999?100000:reflectionProjection.y/(raw/max(reflectionControl.w,.001)-reflectionProjection.x);
            float difference=samplePosition.z-sceneZ;
            float stepLength=max(reflectionStyle.y*(2*q-1.0/12.0)/(12.0),.25);
            float thickness=reflectionStyle.z+stepLength*.72+traceDistance*.018;
            float range=saturate(1-abs(difference)/max(thickness,.01));
            float sided=step(-thickness*.28,difference)*step(difference,thickness);
            float border=saturate(min(min(sampleUV.x,sampleUV.y),min(1-sampleUV.x,1-sampleUV.y))*8);
            float confidence=inside*step(sceneZ,99999)*sided*range*border;
            float replace=step(bestConfidence+.0001,confidence);
            hitColor=lerp(hitColor,tex2Dlod(sceneTexture,float4(sampleUV,0,0)).rgb,replace);
            edge=lerp(edge,border,replace);
            bestConfidence=max(bestConfidence,confidence);
        }
        hit=saturate(bestConfidence*1.8);
        edge*=hit;
        reflected=lerp(environment,hitColor,edge);
    }

    // 3. Surface reflection according to Fresnel and SSR strength
    float reflectionAmount=saturate((.10+.65*foreverFresnel)*(reflectionStyle.x*.55+reflectionControl.x*edge*1.6));
    reflectionAmount=max(reflectionAmount, absorption*(.15+.35*foreverFresnel)*saturate(reflectionStyle.x*.55+reflectionControl.x*edge));
    rgb=lerp(waterBody,reflected,reflectionAmount);

    // 4. Celestial reflection path & shore/crest foam
    float pathEnergy=saturate(celestialPath*controls.w*.85);
    rgb+=max(sheenColor,float3(.20,.22,.24))*pathEnergy;
    float waveFoam=smoothstep(.70,.91,a.x*.42+b.x*.30+swell.x*.28+length(longSlopes)*.10);
    float foamAmount=(shore+waveFoam*foreverStyle.w)*crest*waterStyle.z;
    rgb+=float3(.42,.48,.43)*foamAmount;
    if(flowControl.z>.5)return float4(1,0,1,1);
    float baseAlpha=color.a*base.a;
    float depthAlpha=absorption*(.78-.20*fresnel);
    return float4(fog*(rgb-fogColor.rgb)+fogColor.rgb,saturate(baseAlpha+(1-baseAlpha)*depthAlpha));
}
)HLSL";
int ReadTuning(const wchar_t* key,int fallback){wchar_t value[64]{};GetPrivateProfileStringW(L"Water",key,L"",value,std::size(value),tuningIni.c_str());return value[0]?int(wcstol(value,nullptr,10)):GetPrivateProfileIntW(L"Water",key,fallback,mainIni.c_str());}
void ReloadTuning(){
    effectEnabled=ReadTuning(L"Enabled",1)!=0;
    wavesEnabled=ReadTuning(L"WavesEnabled",1)!=0;geometryWavesEnabled=ReadTuning(L"GeometryWavesEnabled",0)!=0;
    reflectionsEnabled=ReadTuning(L"ReflectionsEnabled",1)!=0;refractionEnabled=ReadTuning(L"RefractionEnabled",1)!=0;
    depthEnabled=ReadTuning(L"DepthEnabled",1)!=0;foamEnabled=ReadTuning(L"FoamEnabled",1)!=0;sunGlintEnabled=ReadTuning(L"SunGlintEnabled",1)!=0;
    strength=std::clamp(ReadTuning(L"RipplePermille",18),0,50)*.001f;
    normalStrength=std::clamp(ReadTuning(L"NormalPercent",28),0,60)*.01f;
    specularStrength=std::clamp(ReadTuning(L"SpecularPercent",115),0,400)*.01f;
    reflectionStrength=std::clamp(ReadTuning(L"ReflectionPercent",80),0,200)*.01f;
    environmentStrength=std::clamp(ReadTuning(L"EnvironmentPercent",45),0,200)*.01f;
    reflectionDistance=float(std::clamp(ReadTuning(L"ReflectionDistance",120),20,300));
    reflectionThickness=std::clamp(ReadTuning(L"ReflectionThicknessPercent",350),25,600)*.01f;
    flowSpeed=std::clamp(ReadTuning(L"FlowSpeedPercent",100),0,300)*.01f;
    flowRotation=std::clamp(ReadTuning(L"FlowRotationPercent",32),0,100)*.01f;
    refractionStrength=std::clamp(ReadTuning(L"RefractionPercent",14),0,100)*.01f;
    absorptionStrength=std::clamp(ReadTuning(L"DepthAbsorptionPercent",38),0,100)*.01f;
    shoreFoamStrength=std::clamp(ReadTuning(L"ShoreFoamPercent",28),0,60)*.01f;
    shoreFoamWidth=std::clamp(ReadTuning(L"ShoreFoamWidthPercent",120),25,800)*.01f;
    swellStrength=std::clamp(ReadTuning(L"SwellPercent",55),0,150)*.01f;
    sunGlintStrength=std::clamp(ReadTuning(L"SunGlintPercent",160),0,400)*.01f;
    fresnelStrength=std::clamp(ReadTuning(L"FresnelPercent",100),0,200)*.01f;
    crestFoamStrength=std::clamp(ReadTuning(L"CrestFoamPercent",20),0,100)*.01f;
    geometryWaveAmplitude=std::clamp(ReadTuning(L"GeometryWavePercent",10),0,35)*.01f;
    waterDebugMode=float(std::clamp(ReadTuning(L"WaterDebugMode",0),0,1));
    if(!logPath.empty()){std::ofstream out(std::filesystem::path(logPath),std::ios::app);out<<"tuning ripple="<<strength<<" normal="<<normalStrength<<" specular="<<specularStrength<<" reflection="<<reflectionStrength<<" environment="<<environmentStrength<<" distance="<<reflectionDistance<<" thickness="<<reflectionThickness<<" flow="<<flowSpeed<<','<<flowRotation<<" refract="<<refractionStrength<<" absorption="<<absorptionStrength<<" foam="<<shoreFoamStrength<<','<<shoreFoamWidth<<" forever="<<swellStrength<<','<<sunGlintStrength<<','<<fresnelStrength<<','<<crestFoamStrength<<" debug="<<waterDebugMode<<'\n';}
}
void Configure(const std::wstring& base) {
    mainIni=base+L"ModernWoWRenderer.ini";tuningIni=base+L"GraphicsEffects.ini";
    logPath=base+L"WaterEffect.log";
    enabled=GetPrivateProfileIntW(L"Water",L"Enabled",0,mainIni.c_str())!=0;
    if(!enabled)return;
    ReloadTuning();
    showStatus=GetPrivateProfileIntW(L"Water",L"ShowStatus",1,mainIni.c_str())!=0;
    ComPtr<ID3DBlob> blob,error;
    HRESULT hr=D3DCompile(source,strlen(source),nullptr,nullptr,nullptr,"main","ps_3_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,blob.GetAddressOf(),error.GetAddressOf());
    std::ofstream log(std::filesystem::path(logPath),std::ios::app);
    log<<"compile="<<hr<<" ripple="<<strength<<'\n';
    if(FAILED(hr)){enabled=false;if(error)log<<static_cast<const char*>(error->GetBufferPointer());return;}
    code.resize(blob->GetBufferSize()/4);memcpy(code.data(),blob->GetBufferPointer(),blob->GetBufferSize());
    blob.Reset();error.Reset();
    hr=D3DCompile(vertexSource,strlen(vertexSource),nullptr,nullptr,nullptr,"main","vs_3_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,blob.GetAddressOf(),error.GetAddressOf());
    log<<"vertex_compile="<<hr<<" normal="<<normalStrength<<" specular="<<specularStrength<<'\n';
    if(FAILED(hr)){enabled=false;if(error)log<<static_cast<const char*>(error->GetBufferPointer());return;}
    vertexCode.resize(blob->GetBufferSize()/4);memcpy(vertexCode.data(),blob->GetBufferPointer(),blob->GetBufferSize());
}
void Present() {
    if(!enabled)return;
    const bool down=(GetAsyncKeyState(VK_F9)&0x8000)!=0;
    DWORD pid=0;GetWindowThreadProcessId(GetForegroundWindow(),&pid);
    if(hotkey&&down&&!keyDown&&pid==GetCurrentProcessId()) {active=!active;std::ofstream(std::filesystem::path(logPath),std::ios::app)<<"active="<<active<<'\n';}
    keyDown=down;
    frameMatches=0;
}
inline void Reset(IDirect3DDevice9* d) {
    if(cachedShaderDevice == d) {
        cachedReplacementPS.Reset();
        cachedReplacementVS.Reset();
        cachedShaderDevice = nullptr;
    }
}
void DrawStatus(IDirect3DDevice9* d,const char* external=nullptr,unsigned line=0) noexcept {
    if(!external&&(!enabled||!showStatus))return;
    try {
        const char* label=external?external:!active?"WATER OFF":frameMatches?"WATER ON":"WATER WAIT";
        const char* letters="WATERONFIHZX";
        const char* glyphs[]={"10001100011000110101101011101110001","01110100011000111111100011000110001","11111001000010000100001000010000100","11111100001000011110100001000011111","11110100011000111110101001001010001","01110100011000110001100011000101110","10001110011010110011100011000110001","11111100001000011110100001000010000","11111001000010000100001000010011111","10001100011000111111100011000110001","11111000010001000100010001000011111","10001100010101000100010101000110001"};
        auto rect=[&](float x,float y,float width,float height,DWORD color){
            y+=line*28.f;
            D3DRECT r{static_cast<LONG>(x),static_cast<LONG>(y),
                      static_cast<LONG>(x+width),static_cast<LONG>(y+height)};
            d->Clear(1,&r,D3DCLEAR_TARGET,color,1.f,0);
        };
        rect(16,40,160,25,0xff101820);
        DWORD color=strstr(label,"OFF")?0xffaaaaaa:strstr(label,"WAIT")?0xffffcc55:0xff66ff99;
        for(unsigned i=0;label[i];++i){const char* found=strchr(letters,label[i]);if(!found)continue;auto glyph=glyphs[found-letters];for(int y=0;y<7;++y)for(int x=0;x<5;++x)if(glyph[y*5+x]=='1')rect(22.f+i*12.f+x*2.f,46.f+y*2.f,2,2,color);}
    }catch(...){}
}
struct Scope {
    ComPtr<IDirect3DPixelShader9> original,replacement;
    ComPtr<IDirect3DVertexShader9> originalVertex,replacementVertex;
    IDirect3DDevice9* device=nullptr;
    float old[36]{},oldVertexControls[4]{};
    ComPtr<IDirect3DBaseTexture9> oldExtraTextures[2];
    DWORD oldSampler[2][6]{};bool extraState=false;
    explicit Scope(IDirect3DDevice9* d,bool skip=false) noexcept {
        if(skip||!enabled||!active||!effectEnabled)return;
        // Fast early exit: only water shaders need the rest of this expensive setup
        uint64_t psHash = renderer::g_trackedState.psHash;
        uint64_t vsHash = renderer::g_trackedState.vsHash;
        // 0x48a82796bd612aeb: extra near-camera water vertex shader (same
        // pixel shader, same 8x64/512x512 texture layout below - confirmed
        // via WaterDiag capture, it was just missing from this list).
        if ((psHash!=0x17f042a7906ca126ull && psHash!=0x7d4f078fa1876a09ull) ||
            (vsHash!=0x206d861fd0a721ddull && vsHash!=0xfdd9528ed3ac30eaull && vsHash!=0x48a82796bd612aebull))
            return;
        try {
            // Check texture layout: slot 0 is 8x64 ripple LUT, slot 1 is 512x512 wave normal
            for(unsigned slot=0;slot<2;++slot) {
                ComPtr<IDirect3DBaseTexture9> tex;
                if(FAILED(d->GetTexture(slot,tex.GetAddressOf()))||!tex||tex->GetType()!=D3DRTYPE_TEXTURE)return;
                D3DSURFACE_DESC desc{};
                if(FAILED(static_cast<IDirect3DTexture9*>(tex.Get())->GetLevelDesc(0,&desc)))return;
                if(slot==0&&(desc.Width!=8||desc.Height!=64))return;
                if(slot==1&&(desc.Width!=512||desc.Height!=512))return;
            }
            original = renderer::g_trackedState.currentPS;
            originalVertex = renderer::g_trackedState.currentVS;
            if(!original || !originalVertex)return;
            if(FAILED(d->GetPixelShaderConstantF(200,old,9)))return;
            if(FAILED(d->GetVertexShaderConstantF(200,oldVertexControls,1)))return;
            float controls[36]={float(GetTickCount64()%600000)*.001f,strength,normalStrength,specularStrength};
            if(FAILED(d->GetVertexShaderConstantF(33,controls+4,1))||FAILED(d->GetVertexShaderConstantF(35,controls+8,1)))return;
            memcpy(controls+12,reflectionData,sizeof(reflectionData));
            controls[16]=reflectionsEnabled&&reflectionScene&&reflectionDepth?reflectionStrength:0;
            controls[20]=reflectionsEnabled?environmentStrength:0;controls[21]=reflectionDistance;controls[22]=reflectionThickness;controls[23]=reflectionScene&&reflectionDepth?1.f:0.f;
            controls[24]=flowSpeed;controls[25]=flowRotation;
            controls[26]=waterDebugMode;
            controls[28]=refractionEnabled?refractionStrength:0;controls[29]=depthEnabled?absorptionStrength:0;controls[30]=foamEnabled?shoreFoamStrength:0;controls[31]=shoreFoamWidth;
            controls[1]=wavesEnabled?strength:0;controls[2]=wavesEnabled?normalStrength:0;
            controls[32]=wavesEnabled?swellStrength:0;controls[33]=sunGlintEnabled?sunGlintStrength:0;controls[34]=fresnelStrength;controls[35]=foamEnabled?crestFoamStrength:0;
            float vertexControls[4]={controls[0],geometryWavesEnabled?geometryWaveAmplitude:0,wavesEnabled?swellStrength:0,0};
            if(cachedShaderDevice != d || !cachedReplacementPS || !cachedReplacementVS) {
                cachedReplacementPS.Reset();
                cachedReplacementVS.Reset();
                cachedShaderDevice = d;
                if(FAILED(d->CreatePixelShader(code.data(), cachedReplacementPS.GetAddressOf()))) return;
                if(FAILED(d->CreateVertexShader(vertexCode.data(), cachedReplacementVS.GetAddressOf()))) return;
            }
            replacement = cachedReplacementPS;
            replacementVertex = cachedReplacementVS;
            const D3DSAMPLERSTATETYPE samplerStates[]={D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER,D3DSAMP_MIPFILTER,D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV,D3DSAMP_SRGBTEXTURE};
            if(reflectionScene&&reflectionDepth){
                for(unsigned slot=0;slot<2;++slot){if(FAILED(d->GetTexture(2+slot,oldExtraTextures[slot].GetAddressOf())))return;for(unsigned state=0;state<6;++state)if(FAILED(d->GetSamplerState(2+slot,samplerStates[state],&oldSampler[slot][state])))return;}
                device=d;
                if(FAILED(d->SetTexture(2,reflectionScene))||FAILED(d->SetTexture(3,reflectionDepth))){Restore();return;}
                for(unsigned slot=0;slot<2;++slot){d->SetSamplerState(2+slot,D3DSAMP_MINFILTER,slot?D3DTEXF_POINT:D3DTEXF_LINEAR);d->SetSamplerState(2+slot,D3DSAMP_MAGFILTER,slot?D3DTEXF_POINT:D3DTEXF_LINEAR);d->SetSamplerState(2+slot,D3DSAMP_MIPFILTER,D3DTEXF_NONE);d->SetSamplerState(2+slot,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);d->SetSamplerState(2+slot,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);d->SetSamplerState(2+slot,D3DSAMP_SRGBTEXTURE,FALSE);}extraState=true;
            }
            if(FAILED(d->SetPixelShader(replacement.Get()))){Restore();return;}
            device=d;
            static bool reported=false;
            if(!reported){std::ofstream log(std::filesystem::path(logPath),std::ios::app);log<<"matched water PS + texture layout; replacement active\n";log<<"direction="<<controls[4]<<','<<controls[5]<<','<<controls[6]<<" color="<<controls[8]<<','<<controls[9]<<','<<controls[10]<<" specular="<<specularStrength<<'\n';reported=true;}
            static bool reflectionReported=false;if(extraState&&!reflectionReported){std::ofstream(std::filesystem::path(logPath),std::ios::app)<<"SSR scene+depth input active strength="<<reflectionStrength<<" environment="<<environmentStrength<<'\n';reflectionReported=true;}
            if(FAILED(d->SetVertexShader(replacementVertex.Get()))||FAILED(d->SetPixelShaderConstantF(200,controls,9))||FAILED(d->SetVertexShaderConstantF(200,vertexControls,1))){Restore();return;}
            ++frameMatches;
        }catch(...){Restore();}
    }
    void Restore() noexcept {if(device){device->SetPixelShader(original.Get());device->SetVertexShader(originalVertex.Get());device->SetPixelShaderConstantF(200,old,9);device->SetVertexShaderConstantF(200,oldVertexControls,1);if(extraState){const D3DSAMPLERSTATETYPE states[]={D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER,D3DSAMP_MIPFILTER,D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV,D3DSAMP_SRGBTEXTURE};for(unsigned slot=0;slot<2;++slot){device->SetTexture(2+slot,oldExtraTextures[slot].Get());for(unsigned state=0;state<6;++state)device->SetSamplerState(2+slot,states[state],oldSampler[slot][state]);}}device=nullptr;}}
    ~Scope(){Restore();}
    Scope(const Scope&)=delete;
    Scope& operator=(const Scope&)=delete;
};
}
