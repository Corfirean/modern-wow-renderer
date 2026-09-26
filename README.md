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

### 3. Native Shadow Enhancement
- **WoW's own cascaded shadow maps as source of truth:** rather than reconstructing shadows from screen-space depth, the renderer enhances the game's existing 4-cascade shadow maps in place — position and direction are never touched, only quality.
- **Shadow Softness:** Scales the native PCF sample radius for softer, less aliased shadow edges.
- **Shadow Strength:** Darkens shadows beyond the game's default floor by swapping in a byte-patched copy of the confirmed receiver shaders (only the darkening constant changed, everything else byte-identical) — a runtime constant write doesn't work here since that value is compiled into the shader.
- Adjustable live via the `F7` menu (`NATIVE SHADOWS`, `SHADOW SOFTNESS`, `SHADOW STRENGTH`).

### 4. Modern Water Shader Overhaul
- **Screen-Space Reflections (SSR):** Reflects world geometry and characters across water surfaces with adaptive step sizes.
- **Dual Flow-Map Ripples:** Counter-rotating UV layers for natural surface perturbation.
- **Depth Absorption & Shoreline Foam:** Shallow-water color grading and procedural foam where water meets ground geometry.
- **Solar Glint & Fresnel:** High-frequency specular glint and angle-dependent reflection power.

### 5. Post-Processing Pipeline
- **Sharpness Filter:** Contrast-preserving Laplacian unsharp mask for crisp textures and geometry edges.
- **Color Grading:** Real-time brightness, contrast, and gamma adjustments.

### 6. Live In-Game Tuning & Controls
- **`F7`** — Opens the in-game grouped tuning overlay menu with interactive sliders.
- **`F11`** — Master toggle to enable/disable all injected graphical enhancements.
- **`F12`** — Hot-reloads `GraphicsEffects.ini` from disk instantly (no client restart required).

---

## Configuration Files

The project includes two primary configuration files:

- **`ModernWoWRenderer.ini`** — Core proxy settings, master feature switches, depth format preferences, and diagnostic hooks.
- **`GraphicsEffects.ini`** — Detailed real-time graphics parameters divided into:
  - `[Atmosphere]` (Fog density, shaft strength, sun vertical scale, softness, falloff)
  - `[Water]` (Wave speed, ripples, reflections, foam, absorption, glint)
  - `[NativeShadows]` (Shadow enable, softness, strength — enhances the game's own cascaded shadow maps)
  - `[PostProcess]` (Brightness, contrast, gamma, sharpness)
  - `[DistanceFog]` (Legacy world distance fog adjustments)

---

## Building

### Automatic builds and releases

GitHub Actions builds the Release/Win32 DLL on pushes and pull requests to `main`. Successful pushes to `main` (including merged pull requests) publish a GitHub Release tagged `build-<commit SHA>` with `ModernWoWRenderer-Win32.zip`. The ZIP contains `d3d9.dll`, `ModernWoWRenderer.ini`, and `GraphicsEffects.ini`, ready to extract into the game directory.

Download the ZIP from the repository's **Releases** page. Branch and pull request builds provide the same ZIP under the workflow run's **Artifacts**, without publishing a release. Maintainers can also run **Build and release** manually from the **Actions** tab; selecting `main` publishes a release. Re-running a build for the same commit updates its existing release asset.

The workflow uses Visual Studio 2022 on `windows-2022` and overrides the platform toolset to `v143`; the project's local toolset selection is preserved. No additional secrets are required: release publishing uses the built-in `GITHUB_TOKEN` with `contents: write` only in the release job.

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
└── GraphicsEffects.ini
```

Launch the game normally. Press **`F7`** in-game to adjust settings or **`F11`** to toggle effects.

---

## Repository Structure & Architecture

The codebase has undergone a modular architectural refactoring to decouple Direct3D9 low-level hooks from high-level rendering effects, enabling future expansion to advanced techniques (Cascaded Shadow Maps, GTAO/HBAO, improved SSR, temporal effects, bloom, tonemapping, and potential D3D11/D3D12 backends):

```text
ModernWoWRenderer/
├── src/
│   ├── Core/                  # Foundational math, frame context, and caching
│   │   ├── MathTypes.h        # Vector2/3/4 and Matrix4 abstractions
│   │   ├── FrameContext.h/.cpp # Frame-level view/projection, depth, and lighting state
│   │   └── ShaderCache.h/.cpp # O(1) thread-safe FNV-1a shader hashing & pointer cache
│   ├── D3D9/                  # Direct3D 9 low-level hardware capture & state
│   │   ├── DepthCapture.h/.cpp # INTZ depth stencil buffer replacement & hook redirects
│   │   └── CameraCapture.h/.cpp# WoW vertex constant (c0..c26) extraction & celestial tracking
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
  - Temporal Anti-Aliasing (TAA) / Motion Vectors
- [ ] **Phase 5: Modern Backend Abstraction**
  - Potential D3D11 / D3D12 render path via proxy translation

