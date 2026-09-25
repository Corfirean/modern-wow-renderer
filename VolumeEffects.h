#pragma once
// Shared shader for the isolated test and opt-in game integration.
// Height fog is integrated along the view ray. Shafts are SCREEN-SPACE visibility,
// not 3D shadow-map ray marching: off-screen occluders are not represented.
inline const char* volumePixelSource=R"HLSL(
sampler2D scene:register(s0);
sampler2D depthMap:register(s1);
sampler2D noiseMap:register(s2);
// x=projection A,y=projection B,z=projection scale X,w=scale Y
float4 projection:register(c0);
// x=base height,y=height falloff,z=density,w=max march distance
float4 medium:register(c1);
// xy=screen sun position,z=shaft strength,w=depth viewport maximum
float4 sun:register(c2);
float4 fogColor:register(c3);
float4 inverseView[4]:register(c4);
// x=0 linear depth debug, 1 height fog, 2 fog+screen-space shafts, 3 atmospheric composite
float4 mode:register(c8);
// x=time,y=world noise scale,z=variation,w=low-layer strength
float4 detail:register(c9);
// xyz=view-space direction toward the light
float4 lightDirection:register(c10);
float4 directColor:register(c11);
// xy=1/render size,z=softness in pixels,w=radial falloff
float4 rayTuning:register(c12);
float4 main(float2 uv:TEXCOORD0):COLOR0 {
    float raw=tex2D(depthMap,uv).r;
    float z=raw>=.9999 ? medium.w : projection.y/(raw/max(sun.w,.001)-projection.x);
    z=clamp(z,0,medium.w);
    if(mode.x<.5)return float4(z/medium.w,z/medium.w,z/medium.w,1);
    if(mode.x>5.5) {
        float clearThreshold=lerp(.9995,.955,step(sun.w,.99));
        // Depth-based occlusion ONLY - is this pixel actually open sky.
        // Geometry/cloud can attenuate this, never emit: brightness never
        // enters the emission term below.
        float openness=smoothstep(clearThreshold,1,raw);
        // Aspect-ratio-correct distance to the real, CelestialTracker-
        // confirmed disc position (sun.xy). Raw UV-space distance is
        // squashed on non-square screens - a 0.1 horizontal delta covers
        // far more pixels than 0.1 vertical at 16:9 - which skewed the mask
        // into an ellipse. Express both axes in the same physical unit
        // (screen-height pixels) before measuring.
        float aspect=rayTuning.y>0.0 ? rayTuning.y/max(rayTuning.x,1e-6) : 1.0;
        float2 delta=float2((uv.x-sun.x)*aspect,uv.y-sun.y);
        float sunDist=length(delta);
        // Pure position mask, tight and hard-clipped around the real disc.
        // A bright cloud anywhere else on screen contributes nothing here -
        // it can only show up via `openness` failing to be 1 (occlusion),
        // never by adding to `sunMask`. This is the fix for "bright cloud
        // becomes a second sun".
        float discFalloff=(sun.x<-.5) ? 0.0 : exp(-sunDist*sunDist*260.0);
        float discCutoff=1.0-smoothstep(0.10,0.16,sunDist);
        float sunMask=discFalloff*discCutoff;
        float amount=openness*sunMask*sun.z*(.62+mode.y*.35);
        if(mode.x>6.5)return float4(amount,amount,amount,1);
        float3 color=lerp(fogColor.rgb,max(directColor.rgb,.1)*1.35,.75);
        return float4(color*amount,1);
    }
    float3 viewEnd=float3((uv.x*2-1)/projection.z,(1-uv.y*2)/projection.w,1)*z;
    float3 origin=inverseView[3].xyz;
    float3 delta=viewEnd.x*inverseView[0].xyz+viewEnd.y*inverseView[1].xyz+viewEnd.z*inverseView[2].xyz;
    float rayLen=length(delta);

    // Analytic exponential height fog integral - continuous without horizon discontinuities
    float hCam=origin.z-medium.x;
    float dz=delta.z;
    float u=clamp(medium.y*dz,-12.0,12.0);
    float factor=(abs(u)<0.02) ? (1.0-0.5*u+(1.0/6.0)*u*u) : ((1.0-exp(-u))/u);
    float eCam=exp(-clamp(hCam*medium.y,-8.0,8.0));
    float optDepth=medium.z*rayLen*eCam*factor;
    optDepth=max(optDepth,0.0);

    // Atmospheric rolling noise modulation
    float2 wind=float2(detail.x*.0021,-detail.x*.0013);
    float3 hitPos=origin+delta;
    float broad=tex2Dlod(noiseMap,float4(hitPos.xy*detail.y*.32+wind*.22,0,0)).r;
    float cloud=smoothstep(.25,.75,broad);
    optDepth*=lerp(.75,1.25,cloud*detail.z);

    float transmission=exp(-clamp(optDepth,0.0,12.0));
    float isSky=step(.9995,raw);
    float distFog=1.0-exp(-min(z,medium.w)*medium.z*0.06);
    float fogAmount=max(1.0-transmission,distFog*0.35);
    // Smooth transition into sky distance fog rather than a hard cut
    fogAmount=lerp(fogAmount,distFog*0.15,isSky);

    float3 rayDir=normalize(viewEnd);
    float cosAngle=dot(rayDir,normalize(lightDirection.xyz));
    float miePhase=pow(saturate(cosAngle*0.5+0.5),6.0);
    float3 scattering=lerp(fogColor.rgb,max(directColor.rgb,.1)*1.45,miePhase*.72);
    float shaftAmount=sun.z*(.35+1.0*fogAmount)*(.40+.60*miePhase);
    if(mode.x>4.5||mode.z>.5)return float4(fogAmount,fogAmount,fogAmount,1);
    if(mode.x>2.5)return float4(scattering,fogAmount*saturate(mode.y*1.5));
    float4 base=tex2D(scene,uv);
    float3 fogged=base.rgb*transmission+lerp(base.rgb,fogColor.rgb*.82,mode.y)*fogAmount;
    return float4(fogged+scattering*shaftAmount*.4,base.a);
}
)HLSL";

// The shafts are generated at half resolution and softened in two separable
// passes.  Linear sampling plus a nine-tap Gaussian removes the pixel-grid
// stair steps without blurring the underlying world or UI.
inline const char* volumeBlurPixelSource=R"HLSL(
sampler2D image:register(s0);
// xy = one blur step in UV space
float4 blurStep:register(c0);
float4 main(float2 uv:TEXCOORD0):COLOR0 {
    float4 c=tex2D(image,uv)*.227027;
    c+=(tex2D(image,uv+blurStep.xy*1.384615)+tex2D(image,uv-blurStep.xy*1.384615))*.158108;
    c+=(tex2D(image,uv+blurStep.xy*3.230769)+tex2D(image,uv-blurStep.xy*3.230769))*.113546;
    return c;
}
)HLSL";

inline const char* volumeCopyPixelSource=R"HLSL(
sampler2D image:register(s0);
float4 main(float2 uv:TEXCOORD0):COLOR0 { return tex2D(image,uv); }
)HLSL";

// Reprojects no geometry, so history is aggressively clipped to the current
// four-neighbour envelope. This keeps shafts calm under foliage without the
// long camera-motion trails of an unconstrained temporal average.
inline const char* volumeTemporalPixelSource=R"HLSL(
sampler2D currentFrame:register(s0);
sampler2D historyFrame:register(s1);
// xy=one texel, z=history weight, w=history valid
float4 temporal:register(c0);
float4 main(float2 uv:TEXCOORD0):COLOR0 {
    float4 c=tex2D(currentFrame,uv);
    if(temporal.w<.5)return c;
    float4 a=tex2D(currentFrame,uv+float2(temporal.x,0));
    float4 b=tex2D(currentFrame,uv-float2(temporal.x,0));
    float4 d=tex2D(currentFrame,uv+float2(0,temporal.y));
    float4 e=tex2D(currentFrame,uv-float2(0,temporal.y));
    float4 lo=min(c,min(min(a,b),min(d,e)));
    float4 hi=max(c,max(max(a,b),max(d,e)));
    float4 h=clamp(tex2D(historyFrame,uv),lo,hi);
    return lerp(c,h,temporal.z);
}
)HLSL";

// Depth-derived screen-space directional contact shadows. A ray is marched
// from each visible surface toward the real sun; only a depth crossing along
// that ray darkens the pixel, so flat ground is never globally multiplied down.
inline const char* contactShadowPixelSource=R"HLSL(
sampler2D depthMap:register(s0);
// xy=1/full render size, z=strength, w=max view-space trace distance
float4 tuning:register(c0);
// x=projection A, y=projection B, z=P00, w=P11
float4 projection:register(c1);
// xyz=view-space direction toward the sun, w=viewport depth maximum
float4 light:register(c2);
float linearZ(float raw) {
    return raw>=.9999 ? 100000 : projection.y/(raw/max(light.w,.001)-projection.x);
}
float4 main(float2 uv:TEXCOORD0):COLOR0 {
    float raw=tex2D(depthMap,uv).r;
    float center=linearZ(raw);
    if(center>99999)return 1;
    float3 viewPos=float3((uv.x*2-1)*center/projection.z,
                          (1-uv.y*2)*center/projection.w,center);
    float3 towardLight=normalize(light.xyz);
    float occlusion=0;
    [unroll] for(int i=0;i<12;++i) {
        float q=(i+1)/12.0;
        float traceDistance=.30+q*q*tuning.w;
        float3 samplePos=viewPos+towardLight*traceDistance;
        float2 sampleUv=float2(.5+.5*samplePos.x*projection.z/max(samplePos.z,.05),
                               .5-.5*samplePos.y*projection.w/max(samplePos.z,.05));
        float inside=step(0,sampleUv.x)*step(sampleUv.x,1)*step(0,sampleUv.y)*step(sampleUv.y,1)*step(.05,samplePos.z);
        float sceneZ=linearZ(tex2Dlod(depthMap,float4(sampleUv,0,0)).r);
        float delta=samplePos.z-sceneZ;
        float bias=.055+center*.0012;
        float thickness=.30+traceDistance*.075;
        float crossing=smoothstep(bias,bias+thickness*.25,delta)*
                       (1-smoothstep(thickness,thickness*1.45,delta));
        occlusion=max(occlusion,inside*step(sceneZ,99999)*crossing*(1-q*.35));
    }
    float screenEdge=saturate(min(min(uv.x,uv.y),min(1-uv.x,1-uv.y))*24);
    float distanceFade=saturate(1-center/320.0);
    float shade=1-tuning.z*saturate(occlusion)*distanceFade*screenEdge;
    return float4(shade,shade,shade,1);
}
)HLSL";

inline const char* rayCompositePixelSource=R"HLSL(
sampler2D rays:register(s0);
float4 main(float2 uv:TEXCOORD0):COLOR0 {
    float4 ray=tex2D(rays,uv);
    return ray;
}
)HLSL";

inline const char* solarRadialPixelSource=R"HLSL(
sampler2D sourceMask:register(s0);
// xy=sun position, z=total ray reach, w=per-sample decay
float4 radial:register(c0);
float4 main(float2 uv:TEXCOORD0):COLOR0 {
    float2 stepUv=(radial.xy-uv)*(radial.z/16.0);
    float illumination=1;
    float weightSum=0;
    float4 light=0;
    float2 q=uv;
    [unroll] for(int i=0;i<16;++i) {
        q+=stepUv;
        float weight=illumination;
        light+=tex2D(sourceMask,q)*weight;
        weightSum+=weight;
        illumination*=radial.w;
    }
    light.rgb=light.rgb/max(weightSum,.001)*1.1;
    light.a=1;
    return light;
}
)HLSL";

inline const char* localLightPixelSource=R"HLSL(
sampler2D scene:register(s0);
sampler2D depthMap:register(s1);
float4 localTuning:register(c0);
float4 localMode:register(c1);
float4 depthInfo:register(c2);
float3 sourceAt(float2 uv) {
    float3 c=tex2D(scene,uv).rgb;
    float peak=max(c.r,max(c.g,c.b));
    float bright=saturate((peak-localTuning.w)/max(1-localTuning.w,.01));
    float warm=smoothstep(.010,.11,c.r-c.g)*smoothstep(.025,.20,c.g-c.b);
    float blueShoulder=smoothstep(.001,.025,c.b);
    float amberRatio=1-smoothstep(.90,.98,c.g/max(c.r,.01));
    float raw=tex2D(depthMap,uv).r;
    float clearThreshold=lerp(.9995,.955,step(depthInfo.x,.99));
    float worldGeometry=1-smoothstep(clearThreshold,1,raw);
    return c*bright*warm*blueShoulder*amberRatio*worldGeometry;
}
float4 main(float2 uv:TEXCOORD0):COLOR0 {
    float3 raw=tex2D(scene,uv).rgb;
    float rawPeak=max(raw.r,max(raw.g,raw.b));
    float rawBright=saturate((rawPeak-localTuning.w)/max(1-localTuning.w,.01));
    float3 glow=(localMode.x>.5?float3(rawBright,0,0):sourceAt(uv))*localTuning.z*24.0;
    return float4(glow,1);
}
)HLSL";

// Debug-only crosshair drawn at the exact screen position rays/glare use for
// the celestial source. If this ring does not sit on the real sun/moon disc
// at any yaw/pitch, the source projection is still wrong - this is the check.
inline const char* celestialMarkerPixelSource=R"HLSL(
// xy=target uv, z=valid, w=unused
float4 markerTarget:register(c0);
float4 markerColor:register(c1);
// xy=1/render size (pixels -> uv)
float4 markerSize:register(c2);
float4 main(float2 uv:TEXCOORD0):COLOR0 {
    float valid=markerTarget.z;
    float2 d=(uv-markerTarget.xy)/markerSize.xy;
    float dist=length(d);
    float ring=abs(dist-16.0);
    float ringMask=1-smoothstep(1.2,2.6,ring);
    float onXAxis=(1-smoothstep(1.2,2.2,abs(d.y)))*step(6.0,dist)*step(dist,26.0);
    float onYAxis=(1-smoothstep(1.2,2.2,abs(d.x)))*step(6.0,dist)*step(dist,26.0);
    float mask=saturate(ringMask+onXAxis+onYAxis)*valid;
    return float4(markerColor.rgb*mask,mask);
}
)HLSL";

inline const char* postProcessPixelSource=R"HLSL(
sampler2D scene:register(s0);
// x=brightness, y=contrast, z=gamma, w=sharpness
float4 postParams:register(c0);
// xy=1/renderWidth, 1/renderHeight
float4 renderSize:register(c1);
float4 main(float2 uv:TEXCOORD0):COLOR0 {
    float4 c=tex2D(scene,uv);
    float3 col=c.rgb;
    if(postParams.w>.005) {
        float2 off=renderSize.xy;
        float3 up=tex2D(scene,uv+float2(0,-off.y)).rgb;
        float3 down=tex2D(scene,uv+float2(0,off.y)).rgb;
        float3 left=tex2D(scene,uv+float2(-off.x,0)).rgb;
        float3 right=tex2D(scene,uv+float2(off.x,0)).rgb;
        float3 lap=col*4.0-(up+down+left+right);
        col=saturate(col+lap*(postParams.w*.75));
    }
    col=col+postParams.x;
    col=(col-.5)*postParams.y+.5;
    col=pow(max(col,0.0),1.0/max(postParams.z,.01));
    return float4(saturate(col),c.a);
}
)HLSL";
