#include "DirectionalVolumetricLighting.h"
#include <d3dcompiler.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include "../D3D9/ScopedRenderState.h"
#include "../Diagnostics/PerformanceProfiler.h"
#include "EnvironmentFogCapture.h"

#pragma comment(lib, "d3dcompiler.lib")

namespace renderer
{
namespace
{
// Atmosphere boundary depth: scene depth everywhere, except at water
// pixels, where it's replaced with the water SURFACE's own depth. Water
// never writes the main depth buffer (WaterEffect samples "what's behind
// water" through it, which only works if water leaves it alone), so
// without this, every downstream pass - the low-res downsample, the
// bilateral upsample, temporal reprojection - would see straight through
// to the seabed and integrate air fog all the way down to it, producing a
// cyan/bright wash over underwater terrain that has nothing to do with
// air. watereffect::Scope writes water's own view-space depth (viewPos.z)
// into a second render target during the real water draws; re-encode it
// into the same raw hyperbolic depth convention as the real depth buffer
// (raw = A + B/z, the inverse of the z = B/(raw-A) reconstruction used
// everywhere else) so every downstream shader can keep treating this
// exactly like scene depth with no further changes.
const char* kBoundarySource = R"HLSL(
sampler2D sceneDepth:register(s0); sampler2D waterCoverage:register(s1); sampler2D waterDepth:register(s2);
float4 projection:register(c0); // x=A, y=B
float4 main(float2 uv:TEXCOORD0):COLOR0 {
 float raw=tex2D(sceneDepth,uv).r;
 float coverage=tex2D(waterCoverage,uv).r;
 if(coverage>.35) {
   float wz=tex2D(waterDepth,uv).r;
   if(wz>.05) raw=saturate(projection.x+projection.y/wz);
 }
 return raw.xxxx;
})HLSL";

// Debug-only: visualize kBoundarySource's output directly as linear
// distance, normalized by max fog distance. Over water this must read as
// the SURFACE distance, not the seabed - a quick way to confirm the water
// depth MRT write is actually reaching the atmosphere pass.
const char* kBoundaryDebugSource = R"HLSL(
sampler2D boundaryDepth:register(s0);
float4 projection:register(c0); // x=A,y=B,z=maxDistance
float4 main(float2 uv:TEXCOORD0):COLOR0 {
 float raw=tex2D(boundaryDepth,uv).r;
 float z=raw>=.9999?projection.z:projection.y/(raw-projection.x);
 return saturate(z/max(projection.z,1.0)).xxxx;
})HLSL";

// Ground-height reduction: samples an 8x8 grid across the boundary depth
// (already water-surface-aware, not seabed), reconstructs each sample's
// world-space Z and keeps the minimum - a cheap proxy for "the lowest
// terrain/water actually in view", i.e. roughly ground level, instead of
// the camera's own altitude. Read back asynchronously on the CPU (see
// Render()) and used as the reference height for height fog/ground mist
// so flying up doesn't drag the fog layer up with the camera.
const char* kGroundHeightSource = R"HLSL(
sampler2D boundaryDepth:register(s0);
float4 cameraPos:register(c0); float4 projection:register(c1); // x=A,y=B,z=scaleX,w=scaleY
float4 invView0:register(c2); float4 invView1:register(c3); float4 invView2:register(c4);
float4 main(float2 uv:TEXCOORD0):COLOR0 {
 float minZ=1e6;
 [unroll] for(int y=0;y<8;++y) {
  [unroll] for(int x=0;x<8;++x) {
   float2 suv=(float2(x,y)+0.5)/8.0;
   float raw=tex2Dlod(boundaryDepth,float4(suv,0,0)).r;
   if(raw<.9999) {
    float z=projection.y/(raw-projection.x);
    float3 view=float3((suv.x*2-1)/projection.z,(1-suv.y*2)/projection.w,1)*z;
    float worldZ=cameraPos.z+view.x*invView0.z+view.y*invView1.z+view.z*invView2.z;
    minZ=min(minZ,worldZ);
   }
  }
 }
 return minZ.xxxx;
})HLSL";

const char* kDepthSource = R"HLSL(
sampler2D fullDepth:register(s0);
float4 texel:register(c0);
float4 main(float2 uv:TEXCOORD0):COLOR0 {
 float d0=tex2D(fullDepth,uv+float2(-texel.x,-texel.y)).r;
 float d1=tex2D(fullDepth,uv+float2( texel.x,-texel.y)).r;
 float d2=tex2D(fullDepth,uv+float2(-texel.x, texel.y)).r;
 float d3=tex2D(fullDepth,uv+float2( texel.x, texel.y)).r;
 // Used to be min() of the 4 taps (bias toward the nearest, to keep thin
 // foreground objects from bleeding fog past them at low res). Right next
 // to any strong depth discontinuity - a cliff or coastline against open
 // water behind it - that systematically hands a whole neighbourhood of
 // low-res texels the NEAR (cliff) depth instead of the far water behind
 // it. Every one of those texels then integrates fog as if the ray only
 // reaches the cliff, and the later bilateral upsample can't find a good
 // depth match for the real (far) water pixels there either, so it falls
 // back to blending those already-wrong near-biased neighbours - producing
 // one flat, uniform, hard-edged patch of wrong fog across open water far
 // beyond the actual discontinuity (confirmed via AtmosphereDebugMode=9:
 // the bad flat patch is already present pre-upsample, anchored exactly on
 // a cliff silhouette). Averaging removes the systematic bias; the minor
 // cost is slightly softer edges around genuinely thin foreground objects
 // at this already-low resolution, which is a much smaller problem.
 return (d0+d1+d2+d3)*.25;
})HLSL";

const char* kIntegrateSource = R"HLSL(
sampler2D depthMap:register(s0); sampler2D noiseMap:register(s1);
float4 cameraPos:register(c0); float4 celestial:register(c1);
float4 ambientAerial:register(c2); float4 directExtinction:register(c3);
float4 projection:register(c4);
float4 invView0:register(c5); float4 invView1:register(c6); float4 invView2:register(c7);
float4 distanceTuning:register(c8); float4 heightTuning:register(c9);
float4 mistTuning:register(c10); float4 noiseTuning:register(c11);
float4 phaseTuning:register(c12); float4 frameTuning:register(c13);
float4 sourceScreen:register(c14); // xy=confirmed disc UV, z=aspect, w=valid
float noiseAt(float3 p) {
 float2 wind=float2(frameTuning.x*.00008,-frameTuning.x*.00005);
 float large=tex2Dlod(noiseMap,float4(p.xy*noiseTuning.x+wind,0,0)).r;
 float small=tex2Dlod(noiseMap,float4(p.xy*noiseTuning.y-wind*1.7+.37,0,0)).g;
 return 1+(large-.5)*noiseTuning.z+(small-.5)*noiseTuning.w;
}
float4 main(float2 uv:TEXCOORD0,float2 vpos:VPOS):COLOR0 {
 float raw=tex2D(depthMap,uv).r;
 // Only the true background clear value (nothing drawn there at all) counts
 // as sky. The old (.9985,.9999) window also caught real far geometry -
 // most visibly open water stretching to the horizon, which never writes a
 // clean depth and reads close to 1 well before the actual far plane. Those
 // pixels were snapped to maxDistance below and got full aerial density,
 // which is what erased the water into a flat grey wall at a fixed radius
 // instead of fading it in with its own real distance.
 float sky=smoothstep(.99990,.99999,raw);
 float z=projection.y/(raw-projection.x);
 z=lerp(clamp(z,0,distanceTuning.y),distanceTuning.y,sky);
 float3 view=float3((uv.x*2-1)/projection.z,(1-uv.y*2)/projection.w,1);
 float viewScale=length(view); float3 viewRay=view/viewScale;
 float3 ray=normalize(viewRay.x*invView0.xyz+viewRay.y*invView1.xyz+viewRay.z*invView2.xyz);
 float marchDistance=min(z*viewScale,distanceTuning.y);
 float horizon=lerp(1,lerp(.34,1,saturate(1-abs(ray.z)*1.7)),sky);
 int count=(int)distanceTuning.z; float stepLength=marchDistance/max((float)count,1);
 float jitter=frac(52.9829189*frac(dot(vpos,float2(.06711056,.00583715))+frameTuning.x*.071));
 // CelestialTracker's view-space vector is captured from the same billboard
 // projection as this pixel. Comparing in view space avoids mixing its D3D
 // fixed-function matrix convention with CameraCapture's shader matrices.
 float cosTheta=dot(viewRay,normalize(celestial.xyz));
 // HG is useful for the broad directional lean, but its angular cone changes
 // apparent pixel size as the source moves off-axis in a perspective image.
 // Anchor the visible halo to the confirmed disc UV instead. Measuring X in
 // screen-height units makes the radius circular and stable at every yaw/FOV.
 float2 sourceDelta=float2((uv.x-sourceScreen.x)*sourceScreen.z,uv.y-sourceScreen.y);
 float sourceRadius=length(sourceDelta);
 float wideHalo=exp(-sourceRadius*sourceRadius*phaseTuning.x);
 float forwardHalo=exp(-sourceRadius*sourceRadius*phaseTuning.y);
 float screenPhase=(phaseTuning.z*wideHalo+phaseTuning.w*forwardHalo)*sourceScreen.w;
 // Retain a weak directional atmosphere outside the visible halo, but never
 // let the fog add a white emitter directly on top of the sun sprite.
 float directionalLean=pow(saturate(cosTheta*.5+.5),6)*.08;
 float discProtection=lerp(.06,1,smoothstep(.025,.055,sourceRadius));
 float phase=(screenPhase*.55+directionalLean)*discProtection;
 float T=1, optical=0, densitySum=0, heightSum=0, mistSum=0, noiseSum=0;
 float3 ambientSum=0, directSum=0;
 [loop] for(int i=0;i<8;++i) {
   if(i>=count) break;
   float t=(i+jitter)*stepLength; float3 p=cameraPos.xyz+ray*t;
   float aerial=ambientAerial.w*smoothstep(distanceTuning.x,distanceTuning.y,t)*horizon;
   float hd=heightTuning.z*exp(-clamp((p.z-heightTuning.x)*heightTuning.y,-3,8));
   float bank=noiseAt(p);
   float md=mistTuning.z*exp(-clamp((p.z-mistTuning.x)*mistTuning.y,-2,10))*lerp(.35,1.35,saturate(bank));
   // Height fog/mist have no distance term - only aerial haze fades in with
   // t above. Both height layers are defined relative to CAMERA height
   // (cameraPos.z + offset), so they sit at their peak density right next
   // to the camera by construction, not just far away. On a boat/dock that
   // peak lands almost exactly at the water surface a few units out, which
   // is what was washing nearby water back out after the absorption fix.
   // A short near-camera fade keeps the far-distance look (which was fine)
   // and stops the layer from slamming what's immediately next to you.
   float nearFade=smoothstep(0.0,10.0,t);
   float n=lerp(1,bank,mistTuning.w); float density=max(0,(aerial+(hd+md)*nearFade)*n);
   float od=density*directExtinction.w*stepLength; float segment=1-exp(-od);
   float3 amb=ambientAerial.rgb*segment; float3 dir=directExtinction.rgb*phase*celestial.w*segment;
   ambientSum+=T*amb; directSum+=T*dir; T*=exp(-od); optical+=od;
   densitySum+=density; heightSum+=hd; mistSum+=md; noiseSum+=n;
 }
 float invCount=1/max((float)count,1); int debugMode=(int)frameTuning.y;
 if(debugMode==1)return optical.xxxx; if(debugMode==2)return T.xxxx;
 if(debugMode==3)return (densitySum*invCount*25).xxxx;
 if(debugMode==4)return (heightSum*invCount*25).xxxx;
 if(debugMode==5)return (mistSum*invCount*25).xxxx;
 if(debugMode==6)return (noiseSum*invCount).xxxx;
 if(debugMode==7)return float4(ambientSum,1-T);
 if(debugMode==8)return float4(directSum,1-T);
 return float4(ambientSum+directSum,1-T);
})HLSL";

const char* kTemporalSource = R"HLSL(
sampler2D currentMap:register(s0); sampler2D historyMap:register(s1);
sampler2D currentDepth:register(s2); sampler2D historyDepth:register(s3);
float4 temporal:register(c0); float4 projection:register(c1);
float4 inv0:register(c2); float4 inv1:register(c3); float4 inv2:register(c4); float4 camera:register(c5);
float4 prev0:register(c6); float4 prev1:register(c7); float4 prev2:register(c8); float4 prev3:register(c9);
float4 prevProjection:register(c10);
float4 main(float2 uv:TEXCOORD0):COLOR0 {
 float4 cur=tex2D(currentMap,uv); if(temporal.w<.5)return cur;
 float raw=tex2D(currentDepth,uv).r; float sky=step(.999,raw); float2 prevUv;
 if(sky>.5) {
   float3 vr=normalize(float3((uv.x*2-1)/projection.z,(1-uv.y*2)/projection.w,1));
   float3 wr=normalize(vr.x*inv0.xyz+vr.y*inv1.xyz+vr.z*inv2.xyz);
   // Row-vector D3D convention: world * view. The shader constants contain
   // matrix rows, therefore each output component is one matrix column.
   float3 pv=float3(
     wr.x*prev0.x+wr.y*prev1.x+wr.z*prev2.x,
     wr.x*prev0.y+wr.y*prev1.y+wr.z*prev2.y,
     wr.x*prev0.z+wr.y*prev1.z+wr.z*prev2.z);
   if(pv.z<=.02)return cur;
   prevUv=float2(.5+.5*pv.x/pv.z*prevProjection.z,.5-.5*pv.y/pv.z*prevProjection.w);
   // Never pull bright sky history across a moving geometry silhouette.
   if(tex2D(historyDepth,prevUv).r<.999)return cur;
 } else {
   float z=projection.y/(raw-projection.x);
   float3 vp=float3((uv.x*2-1)/projection.z,(1-uv.y*2)/projection.w,1)*z;
   float3 wp=camera.xyz+vp.x*inv0.xyz+vp.y*inv1.xyz+vp.z*inv2.xyz;
   float3 pv=float3(
     wp.x*prev0.x+wp.y*prev1.x+wp.z*prev2.x+prev3.x,
     wp.x*prev0.y+wp.y*prev1.y+wp.z*prev2.y+prev3.y,
     wp.x*prev0.z+wp.y*prev1.z+wp.z*prev2.z+prev3.z);
   if(pv.z<=.02)return cur;
   prevUv=float2(.5+.5*pv.x/pv.z*prevProjection.z,.5-.5*pv.y/pv.z*prevProjection.w);
   float oldRaw=tex2D(historyDepth,prevUv).r;
   float oldZ=oldRaw>=.999?100000:prevProjection.y/(oldRaw-prevProjection.x);
   if(abs(oldZ-pv.z)>max(2.0,pv.z*.035))return cur;
 }
 if(any(prevUv<0)||any(prevUv>1))return cur;
 float2 ts=temporal.xy; float4 lo=cur,hi=cur,v;
 v=tex2D(currentMap,uv+float2(ts.x,0));lo=min(lo,v);hi=max(hi,v);
 v=tex2D(currentMap,uv-float2(ts.x,0));lo=min(lo,v);hi=max(hi,v);
 v=tex2D(currentMap,uv+float2(0,ts.y));lo=min(lo,v);hi=max(hi,v);
 v=tex2D(currentMap,uv-float2(0,ts.y));lo=min(lo,v);hi=max(hi,v);
 return lerp(cur,clamp(tex2D(historyMap,prevUv),lo,hi),temporal.z);
})HLSL";

const char* kUpsampleSource = R"HLSL(
sampler2D lowMap:register(s0); sampler2D fullDepth:register(s1); sampler2D lowDepth:register(s2);
float4 texels:register(c0); float4 projection:register(c1); float4 tuning:register(c2);
float Linear(float r){return r>=.999?projection.z:projection.y/(r-projection.x);}
float4 main(float2 uv:TEXCOORD0):COLOR0 {
 float2 o[4]={float2(0,0),float2(1,0),float2(0,1),float2(1,1)};
 float2 lowSize=1/texels.zw;
 float2 lowPixel=uv*lowSize-.5;
 float2 base=floor(lowPixel),fraction=frac(lowPixel);
 float full=Linear(tex2D(fullDepth,uv).r),sum=0; float4 result=0; float best=1e9; float4 nearest=0;
 [unroll]for(int i=0;i<4;++i){
   float2 q=(base+o[i]+.5)*texels.zw;
   float z=Linear(tex2D(lowDepth,q).r); float d=abs(full-z);
   float2 axisWeight=1-abs(o[i]-fraction);
   float spatial=max(axisWeight.x*axisWeight.y,.001);
   float w=spatial*exp(-d/max(.35,full*.012));
   float4 c=tex2D(lowMap,q); result+=c*w; sum+=w;
   if(d<best){best=d;nearest=c;}
 }
 result=sum>.05?result/sum:nearest;
 if(tuning.x>10.5)return float4(result.rgb,1); return result;
})HLSL";

const char* kCompositeSource = R"HLSL(
sampler2D sceneMap:register(s0); sampler2D atmosphereMap:register(s1); sampler2D depthMap:register(s2);
// x=debug passthrough, y=wash (overall artistic multiplier, applies to
// both terms below - kept for the existing FOG WASH slider), z=extinction
// scale, w=scatter scale (ROUND 5 Phase 16-17: these two are independent
// knobs on top of wash - "how much distant geometry disappears" vs "how
// much light the atmosphere emits toward camera" - a dense fog can hide
// something without necessarily glowing. Both default 1.0, which makes
// this mathematically identical to the old single-wash blend; tuning them
// apart is a deliberate future step, not a default behaviour change.
float4 tuning:register(c0); float4 sourceScreen:register(c1);
float4 main(float2 uv:TEXCOORD0):COLOR0 {
 float4 a=tex2D(atmosphereMap,uv); if(tuning.x>.5)return float4(a.rgb,1);
 float4 scene=tex2D(sceneMap,uv);
 // Proper radiative form: T is the fraction of the scene that survives
 // extinction through the medium, not "how much fog to paint over the
 // screen". a.a is stored as (1-T) by the integrate pass.
 float T=saturate(1-a.a);
 float wash=tuning.y;
 float extinctionAmt=lerp(1.0,T,saturate(wash*tuning.z));
 float3 result=scene.rgb*extinctionAmt+a.rgb*wash*tuning.w;
 // Preserve the game's own sun disc luminance. Atmosphere supplies the halo,
 // not a second emitter painted over the sprite. The sky-depth gate prevents
 // this protection mask from punching through foreground occluders.
 float2 delta=float2((uv.x-sourceScreen.x)*sourceScreen.z,uv.y-sourceScreen.y);
 float disc=(1-smoothstep(.022,.045,length(delta)))*sourceScreen.w;
 disc*=smoothstep(.9985,.9999,tex2D(depthMap,uv).r);
 result=lerp(result,scene.rgb,disc);
 // No water-specific handling here any more. The integrate/upsample/
 // temporal pipeline now marches air only down to the water SURFACE (see
 // kBoundarySource), not the seabed, so its output over water is already
 // correct - distant water gets natural haze, nearby water isn't washed
 // out, and there's no separate on/off or distance-gradient hack needed.
 return float4(result,scene.a);
})HLSL";

bool Compile(IDirect3DDevice9* d,const char* source,IDirect3DPixelShader9** shader)
{
 ComPtr<ID3DBlob> blob,errors;
 if(FAILED(D3DCompile(source,strlen(source),nullptr,nullptr,nullptr,"main","ps_3_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,blob.GetAddressOf(),errors.GetAddressOf()))||!blob)return false;
 return SUCCEEDED(d->CreatePixelShader(static_cast<DWORD*>(blob->GetBufferPointer()),shader));
}
}

DirectionalVolumetricLighting& DirectionalVolumetricLighting::Instance(){static DirectionalVolumetricLighting v;return v;}

void DirectionalVolumetricLighting::Configure(const std::wstring& basePath)
{
 const std::wstring ini=basePath+L"GraphicsEffects.ini";
 auto read=[&](const wchar_t* key,int fallback)->int{return static_cast<int>(GetPrivateProfileIntW(L"Atmosphere",key,fallback,ini.c_str()));};
 m_settings.enabled=read(L"DirectionalVolumetricEnabled",1)!=0;
 m_settings.quality=static_cast<uint32_t>(std::clamp(read(L"AtmosphereQuality",1),0,2));
 static constexpr uint32_t samples[]={4,6,8}; static constexpr float scales[]={.25f,.25f,.5f};
 m_settings.sampleCount=samples[m_settings.quality];m_settings.resolutionScale=scales[m_settings.quality];
 m_settings.densityScale=std::clamp(read(L"DensityPermille",5),0,20)/5.f;
 m_settings.maxDistance=float(std::clamp(read(L"AtmosphereMaxDistance",520),120,1200));
 m_settings.aerialStart=float(std::clamp(read(L"AerialStartDistance",35),0,300));
 m_settings.aerialDensity=std::clamp(read(L"AerialDensityPermille",2),0,20)*.001f;
 m_settings.heightDensity=std::clamp(read(L"HeightDensityPermille",5),0,30)*.001f;
 m_settings.heightFalloff=std::clamp(read(L"HeightFalloffPermille",55),5,500)*.001f;
 m_settings.fogBaseOffset=float(std::clamp(read(L"FogBaseOffset",-6),-100,100));
 m_settings.mistBaseOffset=float(std::clamp(read(L"GroundMistOffset",-3),-50,50));
 m_settings.mistDensity=std::clamp(read(L"GroundMistDensityPermille",9),0,40)*.001f;
 m_settings.mistFalloff=std::clamp(read(L"GroundMistFalloffPermille",420),20,2000)*.001f;
 m_settings.noiseAmount=std::clamp(read(L"AtmosphereNoisePercent",22),0,30)*.01f;
 m_settings.extinction=std::clamp(read(L"AtmosphereExtinctionPercent",100),20,200)*.01f;
 m_settings.moonStrength=std::clamp(read(L"MoonVolumetricStrengthPercent",16),0,40)*.01f;
 m_settings.temporalEnabled=read(L"AtmosphereTemporalEnabled",1)!=0;
 m_settings.temporalBlend=std::clamp(read(L"AtmosphereTemporalPercent",88),0,95)*.01f;
 // Same keys the tuning overlay's SUN GLOW / FOG WASH sliders write - this
 // is the pipeline that actually renders the sun halo and the composited
 // fog now, so it must be the one reading them, not the retired full-res
 // analytic shader these keys used to belong to.
 m_settings.sunGlowStrength=std::clamp(read(L"SunGlowPercent",80),0,300)*.01f;
 // Deliberately NOT the old "FogWashPercent" key - that one is still on
 // disk in deployed GraphicsEffects.ini copies at values tuned for the
 // retired full-res analytic shader (e.g. 8), which would read here as
 // "almost no fog" and silently gut this pass. New key, own default.
 m_settings.fogWash=std::clamp(read(L"AtmosphereWashPercent",55),0,100)*.01f;
 // ROUND 5 Phase 16-17. Not exposed in the tuning overlay - deliberate
 // internal knobs for now, defaults keep the old single-wash behaviour.
 m_settings.extinctionStrength=std::clamp(read(L"AtmosphereExtinctionPercentInternal",100),0,300)*.01f;
 m_settings.scatterStrength=std::clamp(read(L"AtmosphereScatterPercentInternal",100),0,300)*.01f;
 // Default off: live testing showed a case where this fogged the whole
 // sky (sun invisible even at zenith, independent of in-game weather) -
 // now restricted to the one register with real confidence (water), but
 // defaulting off until that narrower version is confirmed safe across
 // more zones. The toggle in the overlay still works for testing it.
 m_settings.useEnvironmentBaseline=read(L"UseEnvironmentFog",0)!=0;
 m_settings.debugMode=static_cast<VolumetricDebugMode>(std::clamp(read(L"AtmosphereDebugMode",0),0,12));
 m_historyValid=false;
}

void DirectionalVolumetricLighting::Reset(IDirect3DDevice9* device)
{
 if(m_owner&&m_owner!=device)return;
 m_integratedSurface.Reset();m_integratedTexture.Reset();m_upsampledSurface.Reset();m_upsampledTexture.Reset();
 m_boundarySurface.Reset();m_boundaryTexture.Reset();
 for(int i=0;i<2;++i){m_historySurface[i].Reset();m_historyTexture[i].Reset();m_depthSurface[i].Reset();m_depthTexture[i].Reset();m_groundHeightSurface[i].Reset();m_groundHeightTexture[i].Reset();m_groundHeightStaging[i].Reset();m_groundHeightIssued[i]=false;}
 m_groundHeightValid=false;m_smoothedGroundHeight=0.f;
 m_depthShader.Reset();m_integrateShader.Reset();m_temporalShader.Reset();m_upsampleShader.Reset();m_compositeShader.Reset();m_boundaryShader.Reset();m_boundaryDebugShader.Reset();m_groundHeightShader.Reset();
 m_owner=nullptr;m_fullWidth=m_fullHeight=m_lowWidth=m_lowHeight=0;m_targetFormat=D3DFMT_UNKNOWN;m_historyValid=false;m_previousViewValid=false;
}

bool DirectionalVolumetricLighting::EnsureShaders(IDirect3DDevice9* d)
{
 if(m_depthShader&&m_integrateShader&&m_temporalShader&&m_upsampleShader&&m_compositeShader&&m_boundaryShader&&m_boundaryDebugShader&&m_groundHeightShader)return true;
 return Compile(d,kDepthSource,m_depthShader.GetAddressOf())&&Compile(d,kIntegrateSource,m_integrateShader.GetAddressOf())&&Compile(d,kTemporalSource,m_temporalShader.GetAddressOf())&&Compile(d,kUpsampleSource,m_upsampleShader.GetAddressOf())&&Compile(d,kCompositeSource,m_compositeShader.GetAddressOf())&&Compile(d,kBoundarySource,m_boundaryShader.GetAddressOf())&&Compile(d,kBoundaryDebugSource,m_boundaryDebugShader.GetAddressOf())&&Compile(d,kGroundHeightSource,m_groundHeightShader.GetAddressOf());
}

bool DirectionalVolumetricLighting::EnsureResources(IDirect3DDevice9* d,uint32_t w,uint32_t h,D3DFORMAT format)
{
 uint32_t lw=std::max(1u,uint32_t(w*m_settings.resolutionScale)),lh=std::max(1u,uint32_t(h*m_settings.resolutionScale));
 if(m_owner==d&&m_fullWidth==w&&m_fullHeight==h&&m_lowWidth==lw&&m_lowHeight==lh&&m_targetFormat==format&&m_upsampledTexture)return true;
 Reset(d);m_owner=d;m_fullWidth=w;m_fullHeight=h;m_lowWidth=lw;m_lowHeight=lh;m_targetFormat=format;
 D3DFORMAT hdr=D3DFMT_A16B16G16R16F;
 auto tex=[&](UINT tw,UINT th,D3DFORMAT f,ComPtr<IDirect3DTexture9>& t,ComPtr<IDirect3DSurface9>& s){return SUCCEEDED(d->CreateTexture(tw,th,1,D3DUSAGE_RENDERTARGET,f,D3DPOOL_DEFAULT,t.GetAddressOf(),nullptr))&&SUCCEEDED(t->GetSurfaceLevel(0,s.GetAddressOf()));};
 if(!tex(lw,lh,hdr,m_integratedTexture,m_integratedSurface)){hdr=D3DFMT_A8R8G8B8;if(!tex(lw,lh,hdr,m_integratedTexture,m_integratedSurface))return false;}
 for(int i=0;i<2;++i){if(!tex(lw,lh,hdr,m_historyTexture[i],m_historySurface[i]))return false;if(!tex(lw,lh,D3DFMT_R32F,m_depthTexture[i],m_depthSurface[i])&&!tex(lw,lh,D3DFMT_A8R8G8B8,m_depthTexture[i],m_depthSurface[i]))return false;}
 if(!tex(w,h,D3DFMT_R32F,m_boundaryTexture,m_boundarySurface)&&!tex(w,h,D3DFMT_A8R8G8B8,m_boundaryTexture,m_boundarySurface))return false;
 // Ground-height reduction target + its async readback staging surfaces.
 // Best-effort: if R32F render targets or system-memory offscreen
 // surfaces aren't creatable, the ground-height feature just stays
 // disabled (m_groundHeightValid stays false) and fogBase/mistBase fall
 // back to the old camera-relative behaviour - not fatal to the rest of
 // the atmosphere pass.
 for(int i=0;i<2;++i){
  m_groundHeightSurface[i].Reset();m_groundHeightTexture[i].Reset();m_groundHeightStaging[i].Reset();
  if(tex(1,1,D3DFMT_R32F,m_groundHeightTexture[i],m_groundHeightSurface[i]))
   d->CreateOffscreenPlainSurface(1,1,D3DFMT_R32F,D3DPOOL_SYSTEMMEM,m_groundHeightStaging[i].GetAddressOf(),nullptr);
  m_groundHeightIssued[i]=false;
 }
 m_groundHeightValid=false;
 return tex(w,h,hdr,m_upsampledTexture,m_upsampledSurface);
}

void DirectionalVolumetricLighting::DrawScreenQuad(IDirect3DDevice9* d,uint32_t w,uint32_t h)
{
 struct V{float x,y,z,rhw,u,v;};float fw=float(w)-.5f,fh=float(h)-.5f;
 V q[]={{-.5f,-.5f,0,1,0,0},{fw,-.5f,0,1,1,0},{-.5f,fh,0,1,0,1},{fw,fh,0,1,1,1}};
 d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1);d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,q,sizeof(V));
}

bool DirectionalVolumetricLighting::ValidateHistory(const FrameContext& f)
{
 if(!m_historyValid||!m_previousViewValid||!f.previousViewValid||m_previousWasMoon!=f.celestialIsMoon||std::abs(m_previousCelestialIntensity-f.celestialIntensity)>.5f)return false;
 float dx=f.cameraPosition.x-m_previousCamera.x,dy=f.cameraPosition.y-m_previousCamera.y,dz=f.cameraPosition.z-m_previousCamera.z;
 if(dx*dx+dy*dy+dz*dz>2500)return false;
 for(int i=0;i<4;++i)if(std::abs(f.projUnpack[i]-m_previousProjection[i])>.01f*std::max(1.f,std::abs(m_previousProjection[i])))return false;
 return true;
}

bool DirectionalVolumetricLighting::Render(IDirect3DDevice9* d,const FrameContext& f,IDirect3DSurface9* target)
{
 if(!m_settings.enabled||!d||!target||!f.cameraValid||!f.depthAvailable||!f.sceneColor||!f.atmosphereNoise)return false;
 D3DSURFACE_DESC desc{};if(FAILED(target->GetDesc(&desc))||!EnsureShaders(d)||!EnsureResources(d,desc.Width,desc.Height,desc.Format))return false;
 ScopedRenderState state(d);
 for(auto s:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ALPHATESTENABLE,D3DRS_STENCILENABLE,D3DRS_SCISSORTESTENABLE,D3DRS_FOGENABLE,D3DRS_LIGHTING,D3DRS_SRGBWRITEENABLE,D3DRS_ALPHABLENDENABLE})d->SetRenderState(s,FALSE);
 d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);d->SetRenderState(D3DRS_COLORWRITEENABLE,0xF);d->SetVertexShader(nullptr);
 const uint32_t write=1-m_historyReadIndex;D3DVIEWPORT9 low{0,0,m_lowWidth,m_lowHeight,0,1},full{0,0,m_fullWidth,m_fullHeight,0,1};
 // WoW's own authored fog as the baseline (ROUND 5, Phase 11-15): read-
 // only capture of the game's real fog curve/colour, updated every
 // matched draw this frame regardless of shader family (terrain/model/
 // water all feed the same EnvironmentFogCapture state). FOG DISTANCE
 // still applies as a scale on top of the captured zone-authored distance
 // rather than replacing it, so the slider stays meaningful while zone
 // identity (Elwynn vs a coastal fog bank) comes through instead of one
 // fixed synthetic profile everywhere.
 const auto& envFog=renderer::EnvironmentFogCapture::Instance().State();
 bool useEnv=m_settings.useEnvironmentBaseline&&envFog.valid;
 float envScale=m_settings.maxDistance/520.f;
 float aerialStart=useEnv?envFog.startDistance*envScale:m_settings.aerialStart;
 float aerialEnd=useEnv?envFog.endDistance*envScale:m_settings.maxDistance;
 aerialEnd=std::max(aerialEnd,aerialStart+10.f);
 float ambR=(useEnv&&envFog.colorValid)?envFog.ambientFogColor[0]:.36f;
 float ambG=(useEnv&&envFog.colorValid)?envFog.ambientFogColor[1]:.46f;
 float ambB=(useEnv&&envFog.colorValid)?envFog.ambientFogColor[2]:.58f;
 {
  // Boundary depth first: everything below (low-res downsample, upsample,
  // temporal) reads this instead of the raw scene depth, so water's own
  // surface - not the seabed behind it - is what the atmosphere treats as
  // "where air stops".
  ScopedGpuTimer timer(d,GpuPerfStage::AtmosphereBoundary);
  d->SetViewport(&full);d->SetRenderTarget(0,m_boundarySurface.Get());d->SetPixelShader(m_boundaryShader.Get());
  d->SetTexture(0,f.depthTexture);d->SetTexture(1,f.waterMaskTexture);d->SetTexture(2,f.waterDepthTexture);
  for(DWORD s=0;s<3;++s){d->SetSamplerState(s,D3DSAMP_MINFILTER,D3DTEXF_POINT);d->SetSamplerState(s,D3DSAMP_MAGFILTER,D3DTEXF_POINT);}
  d->SetPixelShaderConstantF(0,f.projUnpack,1);
  DrawScreenQuad(d,m_fullWidth,m_fullHeight);
  d->SetTexture(0,nullptr);d->SetTexture(1,nullptr);d->SetTexture(2,nullptr);
 }
 if(m_groundHeightShader&&m_groundHeightSurface[0]&&m_groundHeightSurface[1]){
  // Read back the OTHER ring slot first - it was issued 1-2 frames ago,
  // so its GetRenderTargetData copy should be complete by now (no stall).
  uint32_t readIdx=1-m_groundHeightWriteIndex;
  if(m_groundHeightIssued[readIdx]&&m_groundHeightStaging[readIdx]){
   D3DLOCKED_RECT lr{};
   if(SUCCEEDED(m_groundHeightStaging[readIdx]->LockRect(&lr,nullptr,D3DLOCK_READONLY))){
    float v=*reinterpret_cast<float*>(lr.pBits);
    m_groundHeightStaging[readIdx]->UnlockRect();
    if(std::isfinite(v)&&v<1e5f){
     m_smoothedGroundHeight=m_groundHeightValid?(m_smoothedGroundHeight*.9f+v*.1f):v;
     m_groundHeightValid=true;
    }
   }
  }
  D3DVIEWPORT9 one{0,0,1,1,0,1};
  d->SetViewport(&one);d->SetRenderTarget(0,m_groundHeightSurface[m_groundHeightWriteIndex].Get());d->SetPixelShader(m_groundHeightShader.Get());
  d->SetTexture(0,m_boundaryTexture.Get());d->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_POINT);d->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_POINT);
  float gc0[4]={f.cameraPosition.x,f.cameraPosition.y,f.cameraPosition.z,0};
  float inv0[4]={f.inverseView.m[0][0],f.inverseView.m[0][1],f.inverseView.m[0][2],0};
  float inv1[4]={f.inverseView.m[1][0],f.inverseView.m[1][1],f.inverseView.m[1][2],0};
  float inv2[4]={f.inverseView.m[2][0],f.inverseView.m[2][1],f.inverseView.m[2][2],0};
  d->SetPixelShaderConstantF(0,gc0,1);d->SetPixelShaderConstantF(1,f.projUnpack,1);d->SetPixelShaderConstantF(2,inv0,1);d->SetPixelShaderConstantF(3,inv1,1);d->SetPixelShaderConstantF(4,inv2,1);
  DrawScreenQuad(d,1,1);d->SetTexture(0,nullptr);
  if(m_groundHeightStaging[m_groundHeightWriteIndex]&&SUCCEEDED(d->GetRenderTargetData(m_groundHeightSurface[m_groundHeightWriteIndex].Get(),m_groundHeightStaging[m_groundHeightWriteIndex].Get())))
   m_groundHeightIssued[m_groundHeightWriteIndex]=true;
  m_groundHeightWriteIndex=1-m_groundHeightWriteIndex;
 }
 if(m_settings.debugMode==VolumetricDebugMode::BoundaryDepth){
  d->SetRenderTarget(0,target);d->SetPixelShader(m_boundaryDebugShader.Get());d->SetTexture(0,m_boundaryTexture.Get());
  d->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_POINT);d->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_POINT);
  float c0[4]={f.projUnpack[0],f.projUnpack[1],aerialEnd,0};d->SetPixelShaderConstantF(0,c0,1);
  DrawScreenQuad(d,m_fullWidth,m_fullHeight);d->SetTexture(0,nullptr);
  return true;
 }
 d->SetViewport(&low);d->SetRenderTarget(0,m_depthSurface[write].Get());d->SetPixelShader(m_depthShader.Get());d->SetTexture(0,m_boundaryTexture.Get());float depthTexel[4]={.5f/m_fullWidth,.5f/m_fullHeight,0,0};d->SetPixelShaderConstantF(0,depthTexel,1);DrawScreenQuad(d,m_lowWidth,m_lowHeight);d->SetTexture(0,nullptr);
 {
  ScopedGpuTimer timer(d,GpuPerfStage::AtmosphereIntegrate);d->SetRenderTarget(0,m_integratedSurface.Get());d->SetPixelShader(m_integrateShader.Get());d->SetTexture(0,m_depthTexture[write].Get());d->SetTexture(1,f.atmosphereNoise);
  for(DWORD s=0;s<2;++s){d->SetSamplerState(s,D3DSAMP_MINFILTER,s?D3DTEXF_LINEAR:D3DTEXF_POINT);d->SetSamplerState(s,D3DSAMP_MAGFILTER,s?D3DTEXF_LINEAR:D3DTEXF_POINT);d->SetSamplerState(s,D3DSAMP_ADDRESSU,s?D3DTADDRESS_WRAP:D3DTADDRESS_CLAMP);d->SetSamplerState(s,D3DSAMP_ADDRESSV,s?D3DTADDRESS_WRAP:D3DTADDRESS_CLAMP);}
  float c0[4]={f.cameraPosition.x,f.cameraPosition.y,f.cameraPosition.z,1};d->SetPixelShaderConstantF(0,c0,1);
  float intensity=f.celestialIntensity*(f.celestialIsMoon?m_settings.moonStrength:1.f);float c1[4]={f.sunDirectionView.x,f.sunDirectionView.y,f.sunDirectionView.z,intensity};d->SetPixelShaderConstantF(1,c1,1);
  float c2[4]={ambR,ambG,ambB,m_settings.aerialDensity*m_settings.densityScale};float c3[4]={f.celestialIsMoon?.28f:1.f,f.celestialIsMoon?.34f:.62f,f.celestialIsMoon?.46f:.30f,m_settings.extinction};d->SetPixelShaderConstantF(2,c2,1);d->SetPixelShaderConstantF(3,c3,1);d->SetPixelShaderConstantF(4,f.projUnpack,1);
  float inv[3][4]={{f.inverseView.m[0][0],f.inverseView.m[0][1],f.inverseView.m[0][2],0},{f.inverseView.m[1][0],f.inverseView.m[1][1],f.inverseView.m[1][2],0},{f.inverseView.m[2][0],f.inverseView.m[2][1],f.inverseView.m[2][2],0}};d->SetPixelShaderConstantF(5,inv[0],3);
  // Ground reference for height fog/mist: the smoothed min-visible-height
  // estimate when we have one (tracks actual terrain/water, not the
  // camera), falling back to camera height only until the first readback
  // lands (~1-2 frames after startup/a map load) or if nothing but sky was
  // ever visible in the reduction sample.
  float groundRef=m_groundHeightValid?m_smoothedGroundHeight:f.cameraPosition.z;
  float c8[4]={aerialStart,aerialEnd,float(m_settings.sampleCount),0};float c9[4]={groundRef+m_settings.fogBaseOffset,m_settings.heightFalloff,m_settings.heightDensity*m_settings.densityScale,0};float c10[4]={groundRef+m_settings.mistBaseOffset,m_settings.mistFalloff,m_settings.mistDensity*m_settings.densityScale,m_settings.noiseAmount};float c11[4]={1.f/140.f,1.f/28.f,.30f,.18f};float c12[4]={32.f,220.f,.25f*m_settings.sunGlowStrength,.75f*m_settings.sunGlowStrength};float c13[4]={float(f.frameIndex%100000),float(uint32_t(m_settings.debugMode)),0,0};float c14[4]={f.sunScreenX,f.sunScreenY,float(m_fullWidth)/float(m_fullHeight),f.celestialIntensity>0.f?1.f:0.f};
  d->SetPixelShaderConstantF(8,c8,1);d->SetPixelShaderConstantF(9,c9,1);d->SetPixelShaderConstantF(10,c10,1);d->SetPixelShaderConstantF(11,c11,1);d->SetPixelShaderConstantF(12,c12,1);d->SetPixelShaderConstantF(13,c13,1);d->SetPixelShaderConstantF(14,c14,1);DrawScreenQuad(d,m_lowWidth,m_lowHeight);d->SetTexture(0,nullptr);d->SetTexture(1,nullptr);
 }
 IDirect3DTexture9* atmosphere=m_integratedTexture.Get();bool historyOk=ValidateHistory(f);
 const bool runTemporal=m_settings.temporalEnabled&&(m_settings.debugMode==VolumetricDebugMode::None||m_settings.debugMode==VolumetricDebugMode::Temporal||m_settings.debugMode==VolumetricDebugMode::Upsampled);
 if(runTemporal){ScopedGpuTimer timer(d,GpuPerfStage::AtmosphereTemporal);d->SetRenderTarget(0,m_historySurface[write].Get());d->SetPixelShader(m_temporalShader.Get());d->SetTexture(0,m_integratedTexture.Get());d->SetTexture(1,m_historyTexture[m_historyReadIndex].Get());d->SetTexture(2,m_depthTexture[write].Get());d->SetTexture(3,m_depthTexture[m_historyReadIndex].Get());for(DWORD s=0;s<4;++s){d->SetSamplerState(s,D3DSAMP_MINFILTER,s<2?D3DTEXF_LINEAR:D3DTEXF_POINT);d->SetSamplerState(s,D3DSAMP_MAGFILTER,s<2?D3DTEXF_LINEAR:D3DTEXF_POINT);d->SetSamplerState(s,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);d->SetSamplerState(s,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);}float c0[4]={1.f/m_lowWidth,1.f/m_lowHeight,m_settings.temporalBlend,historyOk?1.f:0.f};d->SetPixelShaderConstantF(0,c0,1);d->SetPixelShaderConstantF(1,f.projUnpack,1);float inv[3][4]={{f.inverseView.m[0][0],f.inverseView.m[0][1],f.inverseView.m[0][2],0},{f.inverseView.m[1][0],f.inverseView.m[1][1],f.inverseView.m[1][2],0},{f.inverseView.m[2][0],f.inverseView.m[2][1],f.inverseView.m[2][2],0}};d->SetPixelShaderConstantF(2,inv[0],3);float cam[4]={f.cameraPosition.x,f.cameraPosition.y,f.cameraPosition.z,0};d->SetPixelShaderConstantF(5,cam,1);d->SetPixelShaderConstantF(6,&m_previousView.m[0][0],4);d->SetPixelShaderConstantF(10,m_previousProjection,1);DrawScreenQuad(d,m_lowWidth,m_lowHeight);for(DWORD s=0;s<4;++s)d->SetTexture(s,nullptr);atmosphere=m_historyTexture[write].Get();}
 {
  ScopedGpuTimer timer(d,GpuPerfStage::AtmosphereUpsample);d->SetViewport(&full);d->SetRenderTarget(0,m_upsampledSurface.Get());d->SetPixelShader(m_upsampleShader.Get());d->SetTexture(0,atmosphere);d->SetTexture(1,m_boundaryTexture.Get());d->SetTexture(2,m_depthTexture[write].Get());for(DWORD s=0;s<3;++s){d->SetSamplerState(s,D3DSAMP_MINFILTER,D3DTEXF_POINT);d->SetSamplerState(s,D3DSAMP_MAGFILTER,D3DTEXF_POINT);}float c0[4]={1.f/m_fullWidth,1.f/m_fullHeight,1.f/m_lowWidth,1.f/m_lowHeight};float c1[4]={f.projUnpack[0],f.projUnpack[1],aerialEnd,0};float c2[4]={float(uint32_t(m_settings.debugMode)),0,0,0};d->SetPixelShaderConstantF(0,c0,1);d->SetPixelShaderConstantF(1,c1,1);d->SetPixelShaderConstantF(2,c2,1);DrawScreenQuad(d,m_fullWidth,m_fullHeight);for(DWORD s=0;s<3;++s)d->SetTexture(s,nullptr);
 }
 {
  ScopedGpuTimer timer(d,GpuPerfStage::AtmosphereComposite);d->SetRenderTarget(0,target);d->SetPixelShader(m_compositeShader.Get());d->SetTexture(0,f.sceneColor);d->SetTexture(1,m_upsampledTexture.Get());d->SetTexture(2,f.depthTexture);d->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_POINT);d->SetSamplerState(1,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);d->SetSamplerState(2,D3DSAMP_MINFILTER,D3DTEXF_POINT);float debug[4]={m_settings.debugMode==VolumetricDebugMode::None?0.f:1.f,m_settings.fogWash,m_settings.extinctionStrength,m_settings.scatterStrength};float source[4]={f.sunScreenX,f.sunScreenY,float(m_fullWidth)/float(m_fullHeight),f.celestialIntensity>0.f?1.f:0.f};d->SetPixelShaderConstantF(0,debug,1);d->SetPixelShaderConstantF(1,source,1);DrawScreenQuad(d,m_fullWidth,m_fullHeight);d->SetTexture(0,nullptr);d->SetTexture(1,nullptr);d->SetTexture(2,nullptr);
 }
 m_historyReadIndex=write;m_historyValid=true;m_previousCamera=f.cameraPosition;std::copy(std::begin(f.projUnpack),std::end(f.projUnpack),m_previousProjection);m_previousView=f.viewRaw;m_previousViewValid=f.viewRawValid;m_previousWasMoon=f.celestialIsMoon;m_previousCelestialIntensity=f.celestialIntensity;return true;
}
}
