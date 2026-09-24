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

### 3. Screen-Space Contact Shadows & AO
- **Depth-Discontinuity Shadows:** Micro-occlusion and contact shadows generated directly from the scene depth buffer.
- **Directional Shadow Marching:** Casts screen-space shadows along the sun's view direction without requiring full 3D shadow maps.

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
└── GraphicsEffects.ini
```

Launch the game normally. Press **`F7`** in-game to adjust settings or **`F11`** to toggle effects.

---

## Repository Structure

```text
├── ModernWoWRenderer.cpp   # Proxy DLL entry point, D3D9 device hooks, and render passes
├── d3d9.def                # D3D9 proxy function exports
├── ModernWoWRenderer.sln   # Visual Studio solution
├── ModernWoWRenderer.vcxproj # Visual Studio project
├── VolumeIntegration.h    # Camera matrix capture, depth buffer management, pass pipeline
├── VolumeEffects.h        # HLSL shaders for fog, sun shafts, radial blur, and contact shadows
├── WaterEffect.h          # Water surface shaders, normal maps, and foam rendering
├── WaterReflection.h      # Screen-space reflections (SSR) and planar fallback
├── WaterHighlight.h       # Specular sun glint and lighting calculations
├── DistanceFog.h          # Legacy distance fog override and hook
├── TuningOverlay.h        # In-game interactive F7 tuning overlay
├── WaterDiagnostics.h     # Bounded draw-call capture and diagnostic utilities
├── ModernWoWRenderer.ini  # Main proxy configuration file
├── GraphicsEffects.ini    # Live graphical tuning parameters
├── .gitignore             # Git ignore rules for build artifacts
└── README.md              # Project documentation
```
