# Compact ground banks and detached local-light shadows

Follow-up to the user's working-light screenshots: local light occlusion had
two independent sources of detached silhouette artifacts. Visibility treated
every frontmost depth sample as an infinitely thick occluder. The half-float
light accumulation alpha also stored nonlinear hardware depth, losing receiver
distance precision. The composite then fell back to a different surface's
nearest light sample even when no valid depth match existed.

Visibility now accepts a bounded, biased surface thickness and applies softer
contact attenuation. Accumulation alpha stores linear view distance; upsampling
uses relative distance tolerance and rejects unmatched samples. The optional
light pass requires floating-point storage rather than silently clamping linear
depth into an 8-bit fallback. Native sun/moon shadow code remains untouched.
This remains screen-space contact occlusion, not omnidirectional shadow maps.

Local fog now consists of compact noise-shaped banks with a finite top and zero
density in gaps, instead of an everywhere-present density floor and unbounded
exponential height tail. World-space noise scales are 42/13 units; the banks'
height varies with their density. Lamp scattering uses the same bank mask/top,
preventing warm light from filling an otherwise empty vertical fog column.
LOCAL HEIGHT spans 1..8 units, default 3. The general height layer fades in at
18..55 units to preserve the near view. Global distance haze remains separate.
Ground is still estimated from the shared GPU probes described in the earlier
follow-up; steep terrain and wall-filled views remain limitations.

Validation: Release Win32 build; all active ps_3_0 shaders compiled (integration
494 slots); production D3D9 tests and proxy ON/OFF/Reset tests passed. New tests
verify clear air above banks, zero haze in noise gaps, and no infinitely extruded
foreground silhouette shadow on a distant receiver. Existing depth, water,
local light, constant restoration, and screenshot-area lamp tests still pass.
No live game visual match is claimed; the client was closed during installation.

## Neutral ground scattering follow-up

The user's low-wash night screenshot exposed a separate compositing problem:
compact local banks inherited the dark environment colour and were multiplied
by the global FOG WASH value (18%), leaving lamp scattering as their only clear
visual cue. Integration now combines a neutral whitish local scattering term
with separately wash-weighted global scattering/transmittance. Local bank
opacity is independent of FOG WASH; noise gaps and the finite bank top remain.
Lamp scattering keeps its existing wash multiplier, and surface lights are
unchanged. No client settings are overwritten.

Validation: all nine ps_3_0 shaders compile (integration 511/512 slots), Release
Win32 build, production D3D9 suite, and proxy ON/OFF/Reset tests pass. Added
GPU assertions exercise height=3, local density=100, daylight=0, wash=18%, and
no lamps: a visible neutral bank, wash independence, different noise densities,
and fully transparent gaps. These synthetic checks do not establish a live
visual match to the user's references; the terrain-probe limitations above
still apply.
