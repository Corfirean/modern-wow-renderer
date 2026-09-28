# Modern WoW Renderer

A lightweight, non-invasive **Direct3D 9 proxy (`d3d9.dll`)** designed to modernize graphics in **World of Warcraft 3.3.5** (Ascension / Wrath of the Lich King).

The renderer hooks the D3D9 device on creation without touching game files, network code, or client memory. It captures camera matrices and reconstructed hardware depth buffers (`INTZ`) to inject modern volumetric lighting, atmospheric fog, screen-space reflections, and post-processing passes immediately before the UI is rendered.

---

## Features

### 1. Volumetric Sun Shafts (God Rays)
- **Celestial Source Tracking:** Computes the true screen-space position of the celestial sun and moon from view-space directional lighting vectors (`c24`).
- **Depth-Aware Occlusion:** World geometry (terrain, buildings, trees, player characters) acts as real-time light blockers, casting crisp silhouette beams.
- **Radial Scattering:** Transport blur radiating outward from the celestial source with configurable softness, falloff, and intensity.
- **Atmospheric Alignment:** Automatically respects zone lighting colors and day/night transitions (warm daylight vs. cool moonlight).

### 2. Atmospheric Height Fog
- **Analytic Exponential Fog:** Height-dependent fog integral that avoids horizon discontinuities.
- **Mie & Rayleigh Phase Scattering:** Realistic directional glow around the sun and forward scattering through fog.
- **Atmospheric Noise Modulation:** Rolling procedural noise simulating wind and shifting fog density.
- **Interactive Local Volumetric Fog:** A separate low world-space density
  bank is integrated inside the same depth-aware atmospheric raymarch. Player/
  camera movement opens a noise-shaped directional wake which decays and
  gradually refills; it is not a fullscreen overlay and therefore remains
  behind geometry, stops at the water surface, and never covers the UI.

### 3. Native Shadow Enhancement
- **Base client requirement:** WoW must actually create its native shadow
  cascades. The 3.3.5 test client is configured with its actual native shadow
  CVars: `extShadowQuality=5`, `mapShadows=1`, `shadowLOD=1`, and
  `projectedTextures=1`. The previously documented `shadowMode=3` belongs to
  the later 4.x shadow system and does not enable 3.3.5 exterior shadows by
  itself. The F7 controls enhance those native maps and cannot invent
  them when the client-side shadow mode is disabled.
- **WoW's own cascaded shadow maps as source of truth:** rather than reconstructing shadows from screen-space depth, the renderer enhances the game's existing 4-cascade shadow maps in place — position and direction are never touched, only quality.
- **Shadow Softness:** Scales the native PCF sample radius for softer, less aliased shadow edges.
- **Shadow Strength:** Darkens shadows beyond the game's default floor by swapping in a byte-patched copy of the confirmed receiver shaders (only the darkening constant changed, everything else byte-identical) — a runtime constant write doesn't work here since that value is compiled into the shader.
- Adjustable live via the `F7` menu (`NATIVE SHADOWS`, `SHADOW SOFTNESS`, `SHADOW STRENGTH`).

### 4. Modern Water Shader Overhaul
- **Screen-Space Reflections (SSR):** Reflects world geometry and characters across water surfaces with adaptive step sizes.
- **Dual Flow-Map Ripples:** Counter-rotating UV layers for natural surface perturbation.
- **Depth Absorption & Shoreline Foam:** Shallow-water color grading and procedural foam where water meets ground geometry.
- **Occluded Solar Glint & Fresnel:** A narrow, broken sun road based on the
  supplied references; pre-water scene depth suppresses direct glint behind
  buildings and terrain while ambient sky reflection remains.
- **Stable Liquid Matching:** Zone/spell pixel-shader variants are accepted by
  the verified water vertex family plus the liquid texture signature, so
  casting in water cannot make a whole tile fall back to unmodified rendering.

### 5. Post-Processing Pipeline
- **Sharpness Filter:** Contrast-preserving Laplacian unsharp mask for crisp textures and geometry edges.
- **Color Grading:** Real-time brightness, contrast, and gamma adjustments.

### 6. Dynamic Environment Lighting

- Captures native D3D9 point/spot lights and accepts only trusted world-space entries from `MaterialCache/local_light_manifest.json`.
- Adds local diffuse and restrained specular light after the native scene, so lamps naturally brighten areas already covered by the game's sunlight shadows without changing shadow strength.
- Reconstructs normals from five depth samples and uses a half-resolution, edge-aware accumulation buffer. The four strongest lights receive short screen-space occlusion; sky, water, UI, login screens, characters, and spell particles are excluded from this pass.
- Injects the two strongest lights into the quarter-resolution atmospheric raymarch. Volumetric rays therefore require actual fog/mist and are composed before UI.
- F7 exposes `DYNAMIC LIGHTS`, intensity, ray strength, quality, and debug views (`1 Sources`, `2 Surface`, `3 Occlusion`, `4 Volumetric`). Shader/resource failure disables this feature only.
- The offline `wow_material_builder.local_lights` scanner decodes authoritative WMO `MOLT` records. Candidates without a trusted world placement and emissive-only M2 candidates are written to `LocalLightReview.json` and never enabled automatically.

### 7. AI Materials, Micro-Relief & Contact Self-Shadowing
- **Conservative Offline AI Inference:** Normal and Poisson height maps are generated offline using `DeepBump` running via DirectML (`onnxruntime-directml`) on AMD Radeon hardware. Zero neural network runtime cost.
- **Reverse-Engineered WoW 3.3.5 ADT Terrain Splatting:** Supports all four confirmed terrain shader families (1–4 diffuse layers). Two-layer terrain uses `s2` as its blend map, while four-layer terrain uses `s0..s3` for diffuse and `s4.r/g/b` for sequential layer weights. Relief is therefore masked by the real RGBA splat map rather than accidentally sampling a diffuse layer as control data.
- **Texture-Locked Parallax (POM):** A 12-step height-field raymarch displaces diffuse, normal, and height UVs together in a full-colour terrain replacement pass. The view slope and maximum shift are bounded, preserving strong overhead and side relief without allowing a detached lighting layer to hover over stationary stone.
- **Directional Horizon Self-Shadowing:** A 12-sample horizon search follows the crosshair-verified Sun/Moon world direction from `CelestialTracker`. It compares the maximum neighbouring height slope with the celestial elevation, while shadow reach has a minimum independent of POM depth. `SelfShadowStrength=0` is genuinely off and `150` produces the strongest opacity.
- **Oblique-View Relief Compensation:** Normal response and height-based cavity AO rise smoothly as the camera becomes more oblique. This preserves the approved overhead appearance while keeping stone faces and mortar joints legible at the camera angle used during normal play, without increasing UV displacement.
- **Dark-Joint Protection:** Cavity AO is attenuated where the source artwork
  already contains dark mortar, preventing double-darkened black seams while
  retaining normal/parallax depth on stone and paved terrain.
- **Anti-Grid / Distance Mip-Fade:** Uses explicit screen-space UV derivatives (`tex2Dgrad(..., ddx, ddy)`) with progressive mip-fading to neutral flat normal (`(0.5, 0.5, 1.0)` by mip 3), completely eliminating distance moiré, sparkling, and grid patterns.
- **Full-Colour Terrain Replay:** The second depth-equal draw reconstructs WoW's native sequential splat blend, vertex lighting, detail-alpha lighting, and fog, then applies relief to mapped material weights only. This is required for real parallax: a multiplicative lighting-only pass cannot move the underlying diffuse texture.

### 7. Live In-Game Tuning & Controls
- **`F7`** — Opens the in-game grouped tuning overlay menu with interactive sliders (including atmosphere, water, native shadows, and AI materials).
- **`F8`** — Instant hotkey toggle for AI Materials and Relief.
- **`F11`** — Master toggle to enable/disable all injected graphical enhancements.
- **`F12`** — Hot-reloads `GraphicsEffects.ini` from disk instantly (no client restart required).

---

## Configuration Files

The project includes two primary configuration files:

- **`ModernWoWRenderer.ini`** — Core proxy settings, master feature switches, depth format preferences, and diagnostic hooks.
- **`GraphicsEffects.ini`** — Detailed real-time graphics parameters divided into:
  - `[Atmosphere]` (Fog density, shaft strength, sun vertical scale, softness, falloff)
  - `[LocalFog]` (Local density/height plus player wake strength, radius, and trail length)
  - `[Water]` (Wave speed, ripples, reflections, foam, absorption, glint)
  - `[NativeShadows]` (Shadow enable, softness, strength — enhances the game's own cascaded shadow maps)
  - `[AIMaterials]` (Global normal strength, parallax depth, contact shadow intensity)
  - `[PostProcess]` (Brightness, contrast, gamma, sharpness)
  - `[DistanceFog]` (Legacy world distance fog adjustments)

---

## Building

### Requirements
- **Visual Studio 2022+** (v143 or v145 toolset)
- **C++20** standard support
- **Platform:** `Win32` (`x86` 32-bit)

### Build Steps
1. Open `ModernWoWRenderer.sln` in Visual Studio.
2. Select **Release** configuration and **Win32** platform.
3. Build the solution (`Ctrl + Shift + B`) or via command line:
   ```cmd
   msbuild ModernWoWRenderer.vcxproj /p:Configuration=Release /p:Platform=Win32
   ```
4. Output files will be generated in `build\Release\`:
   - `d3d9.dll`
   - `ModernWoWRenderer.ini`

---

## Installation

Place the following files directly into your WoW client directory (alongside `Ascension.exe` or `WoW.exe`):

```text
Game Directory/
├── d3d9.dll
├── ModernWoWRenderer.ini
├── GraphicsEffects.ini
└── MaterialCache/            # Generated offline by WoWMaterialBuilder
    ├── manifest.json
    └── entries/
```

Launch the game normally. Press **`F7`** in-game to adjust settings or **`F8`** to toggle AI materials.

---

## Repository Structure & Architecture

The codebase has undergone a modular architectural refactoring to decouple Direct3D9 low-level hooks from high-level rendering effects:

```text
ModernWoWRenderer/
├── src/
│   ├── Core/                  # Foundational math, frame context, and caching
│   │   ├── MathTypes.h        # Vector2/3/4 and Matrix4 abstractions
│   │   ├── FrameContext.h/.cpp # Frame-level view/projection, depth, and lighting state
│   │   └── ShaderCache.h/.cpp # O(1) thread-safe FNV-1a shader hashing & pointer cache
│   ├── D3D9/                  # Direct3D 9 low-level hardware capture & state
│   │   ├── DepthCapture.h/.cpp # INTZ depth stencil buffer replacement & hook redirects
│   │   ├── CameraCapture.h/.cpp# WoW vertex constant (c0..c26) extraction & celestial tracking
│   │   ├── CelestialTracker.h/.cpp # Real-time Sun & Moon world-space vector calculation
│   │   └── TrackedRenderState.h # Global pipeline state tracking
│   ├── Materials/             # AI Material Cache & Micro-Relief System
│   │   ├── MaterialCacheManager.h/.cpp # Runtime texture intercept, splat blending & POM pass
│   │   ├── TextureHashLookup.h # Fast GPU texture FNV-1a content hashing
│   │   └── WicTextureLoader.h  # WIC texture loading with Toksvig normal mipmap generation
│   ├── Scene/                 # Scene analysis and draw-call classification
│   │   ├── MaterialType.h     # Material classification flags (M2, WMO, Terrain, Water, UI)
│   │   ├── DrawCallContext.h  # Draw-call pipeline state key & hashing
│   │   └── DrawCallClassifier.h/.cpp # Fast cached draw-call classification
│   ├── Diagnostics/           # Telemetry and diagnostics
│   │   └── RendererDiagnostics.h/.cpp # Frame & draw metrics aggregated every 600 frames
│   └── Effects/               # Modular post-processing and lighting passes
│
├── ModernWoWRenderer.cpp      # D3D9 proxy DLL exports & main hook dispatch
├── VolumeIntegration.h        # Volumetric lighting & atmospheric pipeline
├── VolumeEffects.h            # HLSL shaders (god rays, radial blur, height fog, contact shadows)
├── NativeShadowDiagnostics.h  # Native cascaded shadow map enhancement (softness/strength)
├── WaterEffect.h              # Water replacement shaders and vertex displacement
├── WaterReflection.h          # Screen-space reflections (SSR)
├── WaterHighlight.h           # Water specular glint
├── DistanceFog.h              # Fog override hooks
├── TuningOverlay.h            # In-game interactive F7 tuning overlay
├── ModernWoWRenderer.ini      # Core proxy settings
├── GraphicsEffects.ini        # Live graphical tuning parameters
├── ModernWoWRenderer.sln      # Solution file
├── ModernWoWRenderer.vcxproj  # Project file
├── d3d9.def                   # Proxy export definitions
├── .gitignore                 # Minimal git rules (no binaries or test dumps)
└── README.md                  # Project documentation
```

---

## AI Materials & Reverse-Engineered WoW 3.3.5 Terrain Architecture

### 1. Reverse-Engineered WoW 3.3.5 Terrain Pixel Shader
By dumping and analyzing the runtime pixel shader bytecode from Ascension via `D3DDisassemble`:
```hlsl
ps_3_0
def c0, 0.300000012, 0.699999988, 1, 0
dcl_color v0.xyz
dcl_color1 v1.xyz
dcl_texcoord v2.xy    // UV0: Layer 0 diffuse texture
dcl_texcoord1 v3.xy   // UV1: Layer 1 diffuse texture
dcl_texcoord2 v4.xy   // UV2: Alpha Splat Map (64x64)
dcl_fog v5.x
dcl_2d s0             // Sampler 0: Layer 0
dcl_2d s1             // Sampler 1: Layer 1
dcl_2d s2             // Sampler 2: Alpha Splat Map
texld r0, v2, s0      // Sample Layer 0
texld r1, v3, s1      // Sample Layer 1
texld r2, v4, s2      // Sample Alpha Splat Map
lrp r3, r2.x, r1, r0  // Blend: r3 = lerp(r0, r1, r2.x)
```
- **Layer 0 (`s0`)**: Base diffuse texture (e.g. Cobblestone or Grass).
- **Layer 1 (`s1`)**: Overlay diffuse texture (e.g. Grass or Dirt).
- **Splat Map (`s2`)**: 64x64 alpha texture whose red/x channel controls blending (`r2.x`).

### 2. Multi-Layer Splat Blending Math ("Что выше то и главное")
To prevent cobblestone relief from protruding through grass painted over road borders:
1. The current pixel-shader hash selects the confirmed 1-, 2-, 3-, or 4-layer terrain layout.
2. The final material weights reproduce WoW's sequential `lrp` operations. For four layers:
   ```hlsl
   float4 weights = float4(
       (1-r) * (1-g) * (1-b),
       r * (1-g) * (1-b),
       g * (1-b),
       b);
   ```
3. `matWeight = dot(weights, hasMaterial)` restricts POM, normals, AO, and self-shadowing to layers with generated material data. The diffuse layer, its normal, and its height are sampled from the same displaced UV.

### 3. Height Data Contract and Grazing-Angle Stabilized POM
The generated `normal.png` files are RGB normal maps with opaque alpha (`255`); height is stored separately in grayscale `height.png`. Runtime must load both files for every material. Reading `normal.png.a` produces a constant height of `1.0`, which disables both POM intersections and self-shadow occlusion even though the normal bump still appears active.

At extreme grazing angles ($V_z \to 0$), conventional planar POM dividing by $V_z$ results in a division explosion ($1 / 0.1 \to 10\times$), causing severe lateral stretching, tearing, and texture swimming.
Our stabilized POM implementation solves this with:
1. **Real Height Texture:** POM samples `heightMap.r`; it never assumes height is packed into normal alpha.
2. **Bounded View Slope:** `min(length(V.xy) / max(V.z, 0.20), 1.35)` prevents division explosion while retaining side-view displacement.
3. **Texture Lock:** Diffuse, normal, and height maps share the POM intersection UV; only shifting the normal/height lighting is forbidden because it creates the detached effect visible in early prototypes.
4. **Hard Displacement Clamp:**
   ```hlsl
   float maxShift = materialDepth * 1.15f;
   if (length(maxOffset) > maxShift) maxOffset = normalize(maxOffset) * maxShift;
   ```
5. **12-Step Raymarch with Derivative-Based Distance Mip-Fade:** Uses `tex2Dgrad` with base UV derivatives (`ddx(uv0)`, `ddy(uv0)`) to eliminate quad-derivative divergence and distance grid/mesh patterns.

### 4. Directional Contact Self-Shadowing
`CelestialTracker` calculates the exact world-space position and direction of the Sun and Moon based on WoW's view-space lighting constants (`c24`). This vector is transformed into terrain tangent space:
```cpp
float tx = -sy;
float ty = -sx;
float tz = std::max(0.15f, sz);
```
The terrain replacement shader samples twelve points toward the exact visible celestial body. For every point it measures the height-field horizon slope and compares it with `L.z / length(L.xy)`. This avoids the old failure mode where the test ray climbed to height `1.0` too quickly and therefore never found an occluder. Shadow reach uses at least `0.040` UV depth independently of POM, while the slider maps linearly to final opacity; value `0` bypasses the search and `150` is the strongest supported setting.

### 5. Elwynn WMO/M2 Object Materials
Elwynn object coverage is derived from the zone's ADT files rather than a filename guess: the offline builder walks their WMO/M2 dependencies and retains every distinct BLP payload found across the client's MPQ patches. Runtime hashing therefore matches the texture version actually selected by the client.

Opaque stage-0 materials on trees, buildings, towers, fences, bridges, props, and rocks receive a depth-equal relief replay after their normal draw. The exact tracked Sun/Moon vector is transformed to view space and then into the current face's UV basis using `ddx/ddy`, so vertical and rotated faces do not reuse the horizontal terrain tangent frame. Object POM uses a restrained six-step depth and the horizon shadow uses eight samples. Binary-alpha foliage has a separate conservative profile: no POM silhouette shift and only soft micro-normal/cavity response inside pixels already accepted by the original depth-writing cutout. Blended windows, flames, glows, and particles remain untouched.

### 6. Roughness, Material Profiles, and UV-Seam Control
Generated linear data is packed into `height.png` as `R=height`, `G=roughness`, `B=stone/material weight`. Pure profiles store a constant material weight, while mixed atlases store a per-pixel mask. This avoids extra samplers: the four-layer terrain path already uses 13 of D3D9's 16 pixel-sampler slots. The WIC mip builder must average packed channels independently; copying red into every channel destroys roughness and mixed-material masks after mip 0.

Roughness controls a restrained Blinn-style material highlight rather than replacing WoW's original lighting. High roughness broadens and suppresses the highlight; smooth metal may retain a stronger response. Height-derived cavity is deliberately limited to 5–16% on object profiles, while terrain stone uses 22%, so cracks remain readable under diffuse light without overwhelming the shadows painted into the original art.

Object manifests assign one of the `stone`, `mixed`, `wood`, `bark`, `metal`, `plaster`, `roof`, `foliage`, or `generic` profiles. The F7 Normal/Parallax/Self Shadow values remain global multipliers, while profiles scale them to material-safe ranges. Tower and abbey masonry is explicitly classified as `stone`, except known multi-material atlases which use `mixed`. In particular, bark uses 22% of global parallax, wood 40%, stone 100%, metal 12%, plaster 18%, roof 35%, and foliage 0%. This lets useful global depth return without making tree bark look inflated.

Object maps are generated with edge clamping instead of terrain-style periodic wrap because most WMO/M2 textures are UV atlases. Runtime additionally detects large UV derivatives around island/triangle discontinuities and fades parallax, directional normal response, cavity, and roughness highlight there. These thresholds must remain profile-aware and deliberately loose: tiled WMO stone commonly has large valid derivatives, and treating them as seams makes an entire tower flat. Stone therefore keeps a high minimum relief weight and a much slower footprint fade, while bark and foliage remain conservative. Parallax also fades with texture footprint (distance) and UV anisotropy only at truly extreme grazing angles. Terrain retains periodic generation, a relaxed grazing fade, 22% height cavity, and a deliberately weak roughness highlight so the road preserves its established depth.

The recovery baseline after the roughness/profile integration is `NormalStrengthPercent=45`, `ParallaxDepthPercent=24`, and `SelfShadowStrength=35`. If stone or roads suddenly become flat again while maps still load, first inspect the profile assignment and derivative/footprint fade rather than regenerating the material maps.

### 7. Structural Stone and Albedo-Ratio POM

The generic WMO/M2 replay remains a multiplicative pass (`ZERO`, `SRCCOLOR`) over the already rendered object, which is important because the original client has already applied vertex light, fog, and its art-directed colour. A plain lighting multiplier cannot visibly displace the diffuse texture, however. The stone path therefore also samples the original BLP at both the base UV and final POM UV, computes a bounded `shifted / base` RGB ratio, and multiplies that ratio into the finished frame. Brick borders and surface marks now move with the height ray while the original WoW lighting and palette remain intact. Dark-pixel stabilization and bounded ratios prevent mortar from producing colour explosions.

PBRnxt v1.1 adds a structural stone generator. It divides out broad painted illumination, converts locally dark mortar and bevels into decisively low channels, builds broad high brick plateaus, percentile-normalizes the macro range, and regenerates the normal from that exact final height at a stone-specific strength. All `stone` cache entries must be rebuilt without `--resume` after changing this algorithm; `build --profile stone` performs that targeted rebuild. D3D9 POM still cannot alter the actual mesh silhouette, but it now provides genuine interior texture displacement instead of bump-only shading.

### 8. Mixed-Material Atlases

WoW frequently packs stone, timber, plaster, and metal into one WMO BLP, so filename-level profiles alone are insufficient. PBRnxt v1.2 packs a per-pixel stone weight into the formerly reserved blue channel of `height.png` (`R=height`, `G=roughness`, `B=stone weight`). The `mixed` runtime profile uses this mask to interpolate POM depth, normal response, cavity, self-shadow, and displaced-albedo strength. Known timber/stone combo atlases and the ruined-tower stone/metal base receive dedicated mask modes; unknown combo atlases use a conservative continuous estimate. This avoids inflating wood or metal while retaining full stone depth inside the same draw call.

The object replay accepts an enabled alpha-blend state only when the draw still writes depth and uses either replace blending or conventional source-alpha blending. Other blended draws remain excluded and emit bounded `[ObjectMaterialSkip]` diagnostics. This covers effectively opaque WMO sections that the client labels as blended without applying relief to windows, glow, particles, or additive effects.

### 9. Authoritative Material Segmentation

Normal/height inference and material recognition are separate problems. DeepBump/PBRnxt can infer local relief, but it cannot reliably decide whether a stylized baked WoW pixel is stone, timber, metal, plaster, glass, or painted trim. Global sliders and filename-only profiles therefore cannot provide a stable result for every WMO section or mixed atlas.

The authoritative pipeline should classify each WMO material slot/texture independently and support a curated sidecar override keyed by texture content hash. Pure textures receive one profile. Mixed textures receive a reviewed `material_mask.png` with independent channels (`R=stone`, `G=wood`, `B=metal`, `A=plaster/other`). Automatic colour clustering, texture statistics, names, and WMO flags may bootstrap the mask, but ambiguous atlases require a one-time human correction. The object pass has enough sampler capacity for this dedicated mask; terrain does not need it.

Offline generation then runs the appropriate relief model per channel and composites compatible height, normal, and roughness maps with padded/soft mask boundaries. Runtime samples the material weights and blends POM depth, normal strength, cavity, self-shadow, roughness, and specular response per pixel. This avoids searching for one compromise that inevitably makes stone flat, wood inflated, or metal look carved. Zone rollout should prioritize the relatively small set of mixed atlases for review instead of manually tuning every one of the complete Elwynn texture set.

### 10. MPQ Layout and Material Review Queue

MPQ files are version/expansion/patch layers, not reliable zone or material packages. The client mounts them as a case-insensitive virtual filesystem; a higher-priority patch may replace the same internal path from `common.MPQ`, `expansion.MPQ`, `lichking.MPQ`, or an earlier patch. Custom Ascension archives add many more override layers, and an archive letter does not imply a zone or material type.

Internal paths provide useful but non-authoritative hints. Terrain commonly appears under `Tileset/<zone-or-biome>`, map placement under `World/Maps/<map>`, WMOs under `World/WMO/<continent>/<asset-family>`, reusable props under `World/Generic`, and many outdoor-building textures under the historically named `Dungeons/Textures`. Assets are routinely reused across zones and expansions. Therefore extraction must begin from ADT dependencies, recursively traverse WMO/M2 references, retain archive priority/variants, and preserve the exact `model -> group/submesh -> batch/material slot -> BLP` relation instead of selecting an MPQ or folder by zone name.

Classification writes uncertain results to `material_review.json`, keyed by content hash. Each entry records every referring model, WMO group or M2 submesh, batch/material ID, virtual texture path, winning archive variant, predicted material weights, confidence, contributing evidence, disagreement reasons, and preview/mask paths. High-confidence pure materials may proceed automatically; low-confidence or classifier-disagreement cases and every newly detected mixed atlas remain queued for human review. Reviewed decisions become durable hash-based overrides rather than filename exceptions.

The optional de-lit albedo blend is intentionally deferred. The current PBRnxt worker produces normal/roughness/height, not a reviewed de-lit albedo; synthesizing one and blending it blindly would risk changing WoW's palette and baked art direction.

### 11. Eastern Kingdoms Material Rollout

The first continent-wide cache is derived from every discoverable ADT on map
`Azeroth`, which is the Eastern Kingdoms world map in this 3.3.5 client. The
extractor scanned 687 ADT paths / 1166 payload variants and recursively followed
5080 WMO/M2/MDX dependencies. It retained 348 terrain payload variants and 4575
object texture variants across the mounted MPQ patch layers.

Runtime manifests contain 348 terrain materials, 3290 opaque object materials,
and 203 binary-alpha foliage materials. All 3841 entries generated successfully.
The remaining 1082 object variants are translucent or otherwise non-binary-alpha
and intentionally stay outside the depth-equal relief pass so that windows,
glows, mist, flames, and particles are not treated as solid geometry.

`MaterialCache/material_review.json` consolidates 2535 uncertain automatic
classifications. For object entries it records exact model-to-texture provenance,
the virtual MPQ path, archive payload variant, generated-map paths, and every ADT
that placed a referring model. Exact WMO group/batch/material-slot decoding is
still pending and is not inferred. Until a reviewer changes a decision, pending
materials remain enabled so continent coverage is broad rather than silently
limited to filename keywords.

Fifteen identical BLP payloads are shared by terrain and object dependencies.
They use one cache entry by content hash; the final maps are generated with
periodic terrain edges to prevent seams, while object profile metadata is kept
compatible. Manifest `enabled=false` is now honored by the DLL, allowing a bad
automatic decision or utility texture to be disabled without deleting its maps.

### 12. Weather Visuals and Native-Shadow State Isolation

Weather is a final full-screen pass and therefore must explicitly switch to a
pre-transformed `XYZRHW` screen quad. Reusing whichever vertex shader the game
left bound makes the precipitation pixel shader execute with incompatible
inputs and can produce no visible result. The pass now always copies scene
colour (not only when lens droplets are enabled), binds its own fixed-function
quad state, uses the captured `FrameContext::projUnpack` values for depth
linearization, and restores the complete D3D9 state afterward. The low-alpha
Classic rain texture is normalized before user intensity is applied. F10 is the
weather toggle; the obsolete F10 atmosphere-quad toggle was removed, while F9
remains reserved for existing diagnostics/water tooling.

Precipitation and lens composition do not require a captured INTZ buffer. When
depth capture is unavailable, the module binds a neutral fallback depth texture
and disables only roof/depth occlusion. Previously it returned before every
weather draw while the overlay still reported `RAIN ON`, producing a misleading
working status with no visual difference.

Injected material and weather passes must not feed their temporary texture
bindings back into native-shadow discovery. In particular, four-layer terrain
replay occupies samplers 4--12, overlapping WoW's native cascade samplers. The
SetTexture hook now ignores both material replay and weather composition while
those passes are active. Without this isolation the tracker forgets the four
2048x2048 D24X8 cascades and both Native Shadows sliders become inert even
though their INI values reload correctly.

### Native weather integration correction

The initial WeatherVisuals implementation did not match its design document.
It tiled rain or snow textures over a fullscreen quad in `Present`, after WoW
had already rendered the UI. Consequently it appeared on glue/login screens,
covered interface elements, moved in screen space, and continued during clear
weather because its manual mode had no connection to the client's weather.

That fullscreen precipitation and lens composition is disabled. WeatherVisuals
now observes WoW's own alpha-blended, depth-tested world precipitation draws
and substitutes the corresponding Classic/Forever texture only for the native
particle draw, restoring texture stage 0 immediately afterward. A terrain draw
must have occurred in the same frame, excluding login/glue scenes. When WoW
renders no precipitation (clear weather), the renderer injects none. The status
therefore reports the observed native result instead of echoing a manual mode.

Rain, snow, and sandstorm are identified by their captured native texture and
vertex-shader signatures. Rain uses a neutral luminance-only tint so zone light
cannot turn it cyan. Sandstorm uses a generated soft dust puff instead of the
client's hard-edged 64x64 square, while remaining on the native world-space,
depth-tested particle draw. Detection persists briefly across frame ordering so
the F7 status does not flicker or misleadingly remain on `WEATHER AUTO`.

Native-shadow receiver shader hashes are the authoritative safety gate for the
softness and strength controls. Cascade texture format probing is diagnostic
only: D3D9 wrappers may expose the four depth maps through driver FourCC formats
rather than `D24X8`; rejecting those formats previously made both sliders inert.

Rain/snow particle candidates are written to `WeatherVisuals.log` with texture
dimensions, content hash, primitive count, and shader hashes. This bounded log
is the authoritative way to refine exact client-specific signatures without
mistaking spell, fire, smoke, or UI particles for weather. Old intensity,
speed, wind, roof-occlusion, and lens controls remain in the INI for future
world-space work but are intentionally removed from the live menu while they
do not control a truthful implementation.

Live rain/snow capture identified the exact Ascension signatures. Rain is a
16x128 texture (`de9331cb560b9814`) drawn as 6144 primitives by vertex shader
`8832bcc5ed4ff2a6`; snow is 64x64 (`50aa504baa78b237`) with 6143 primitives
and vertex shader `ec9ee827b8bf5afa`. Both use the fixed-function pixel path.
These complete signatures replace the unsafe dimension heuristic. The imported
rain asset is almost black (RGB average about 4, maximum 55), so it must be
converted at load time to a cool-white RGB mask while retaining its authored
coverage as alpha; otherwise native modulation produces black streaks.

Exact texture and shader signatures are sufficient to exclude glue screens, so
native-particle replacement no longer depends on terrain having appeared
earlier in the same frame. That ordering assumption broke after alt-tab/device
reset and could also suppress rain while the tuning overlay changed draw order.
A dedicated `ps_2_0` shader now runs only on the identified native rain/snow
draw. It samples the replacement texture, preserves restrained native vertex
lighting, removes extreme zone tint from precipitation, and gives Weather
Strength a real per-particle alpha meaning. Rain uses a conservative multiplier
while snow is deliberately stronger so the enabled/disabled difference is
visible without ever becoming a screen-space overlay.

### Water shader variants and the two moons

Water is classified by its stable material layout (an 8x64 liquid ripple LUT
in sampler 0 plus a 512x512 surface normal in sampler 1), not by a shader hash.
The client changes vertex and pixel shaders across liquid tiles, under bridges,
near the camera, after spell interaction, and between expansion zones. Live
capture found another such vertex shader (`70faf83955e2b668`) on otherwise
identical water. A shader whitelist split one continuous surface into enhanced
and native triangles, which appeared as missing angular chunks in Stormwind,
Stranglethorn, Booty Bay, and Northrend. The renderer and draw classifier now
use the same texture-based identity so SSR capture and replacement agree.
The pre-water scene capture follows the same rule. Capturing only when a later
whitelisted shader appeared meant that earlier liquid tiles were already in
the supposedly pre-water copy; subsequent tiles then sampled a different
SSR/refraction source. This was the remaining cause of the large geometric,
camera-dependent boundary visible across Booty Bay.

The Sun and Moon billboards may both be drawn in the same frame. The captured
day/night blend now chooses the tracked Moon in moon-lit conditions and the
tracked Sun in daylight instead of blindly letting any visible sun candidate
win. This keeps the radial source on the body that actually drives the scene
lighting: no full solar halo at night while the moon remains without shafts,
and still only one physically coherent directional source.
