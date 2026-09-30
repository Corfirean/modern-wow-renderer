# Presets

Ready-made `GraphicsEffects.ini` files for the current renderer, including
local fog, dynamic lighting, and weather. Common controls are available under
`F7`; advanced settings can be edited in the file and reloaded with `F12`.
A preset is just a starting point, not a locked mode.

## Load presets (pick one)

| Preset | What it trades off |
|---|---|
| [`performance/`](performance/GraphicsEffects.ini) | Injected atmosphere, local fog, dynamic lights, water reflections, and post-processing off. Native distance fog and shadows stay on. Weather stays on, with lens droplets, splashes, and weather haze off. Best FPS. |
| [`balanced/`](balanced/GraphicsEffects.ini) | HIGH atmosphere, moderate local fog and dynamic lights, reflective water, and weather. The recommended starting point. |
| [`cinematic/`](cinematic/GraphicsEffects.ini) | ULTRA atmosphere, taller local fog, HIGH dynamic lights with stronger volumetric scattering, and stronger reflections. Highest GPU cost. |

## Style variants (built on top of Balanced)

| Preset | Look |
|---|---|
| [`balanced-natural/`](balanced-natural/GraphicsEffects.ini) | Subdued, realistic — less sun glow/glare, lighter shadows, soft sharpening. |
| [`balanced-vivid/`](balanced-vivid/GraphicsEffects.ini) | Punchy and saturated — stronger rays/glint, higher contrast, crisper shadows. |
| [`balanced-foggy/`](balanced-foggy/GraphicsEffects.ini) | Leans into mist and god rays — denser haze, shorter visibility, softer everything. |

Natural also reduces local fog and local-light scattering and disables rain
lens droplets. Vivid increases dynamic-light intensity. Foggy increases local
fog height/density and weather haze.

All six files explicitly disable AI Materials, experimental geometry waves,
legacy injected shadow paths, profiling, debug views, and status overlays.
Native distance fog stays neutral at 100/100. Cinematic uses the current
world-space dynamic-light system; the older screen-space torch glow is off
to avoid adding a second glow over the same lights.

Presets cover live visual settings only. Proxy-wide switches in
`ModernWoWRenderer.ini` still apply; use the file supplied with the same
renderer version. These profiles have been checked against the current INI
readers, but visual quality and FPS still need verification in the game.

## How to install a preset

1. Close the game.
2. Copy the preset's `GraphicsEffects.ini` into your WoW client folder,
   overwriting the one that's there (next to `d3d9.dll`).
3. Launch the game. Press `F7` to fine-tune from there, or `F12` after
   editing the file by hand to hot-reload without restarting.

Switching presets while the game is running also works: overwrite
`GraphicsEffects.ini`, alt-tab back into the game, press `F12`.
