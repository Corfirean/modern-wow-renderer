# Presets

Ready-made `GraphicsEffects.ini` files. Every setting in them is still a live
slider under `F7` — a preset is just a starting point, not a locked mode.

## Load presets (pick one)

| Preset | What it trades off |
|---|---|
| [`performance/`](performance/GraphicsEffects.ini) | Volumetric atmosphere and water reflections off. Native shadows stay on (cheap). Best FPS. |
| [`balanced/`](balanced/GraphicsEffects.ini) | Everything on at moderate settings. The recommended default. |
| [`cinematic/`](cinematic/GraphicsEffects.ini) | Everything on at high settings, plus local torch/fire glow. Highest GPU cost. |

## Style variants (built on top of Balanced)

| Preset | Look |
|---|---|
| [`balanced-natural/`](balanced-natural/GraphicsEffects.ini) | Subdued, realistic — less sun glow/glare, lighter shadows, soft sharpening. |
| [`balanced-vivid/`](balanced-vivid/GraphicsEffects.ini) | Punchy and saturated — stronger rays/glint, higher contrast, crisper shadows. |
| [`balanced-foggy/`](balanced-foggy/GraphicsEffects.ini) | Leans into mist and god rays — denser haze, shorter visibility, softer everything. |

## How to install a preset

1. Close the game.
2. Copy the preset's `GraphicsEffects.ini` into your WoW client folder,
   overwriting the one that's there (next to `d3d9.dll`).
3. Launch the game. Press `F7` to fine-tune from there, or `F12` after
   editing the file by hand to hot-reload without restarting.

Switching presets while the game is running also works: overwrite
`GraphicsEffects.ini`, alt-tab back into the game, press `F12`.
