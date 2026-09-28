# Terrain-following fog and local water highlights

The previous fog used one screen-probed ground height for the entire land
image, an eight-unit horizontal slab, and evenly aligned integration samples.
On hills this created floating banks and plane cuts. Averaging raw neighbouring
depths also displaced sloped receivers relative to their integration rays.

## Changes

- `GroundSurfaceCapture.h` replays confirmed terrain and liquid draws into a
  persistent 1024-square, 512-world-unit GPU height atlas. It uses the draw's
  model-view transform and the captured camera inverse, not map IDs or guessed
  elevations. Highest surface wins, including water above the seabed. Heights
  are stored relative to a fixed atlas elevation to preserve half-float precision.
- The atlas is aligned to the world and recentres in 128-unit increments.
  Each ray sample looks up its own terrain height. Missing coverage is explicitly
  masked instead of inventing elevated ground. A legacy estimate remains the
  fallback if no supported geometry has been captured at all.
- Local density has a continuous Gaussian height profile and smoother bank
  transitions. No hard horizontal slab boundaries. Slow world-space advection
  advances on elapsed time even when the camera is stationary.
- Quality levels use 48/64/96 stratified samples. The spatial offsets are fixed,
  with no frame-random slice displacement. Depth downsampling uses the actual
  ray sample, and bilateral upsampling accounts for continuous surface slopes.
- Lamp scattering includes a small ambient aerosol component above banks and
  is independent of global Fog Wash. Light Rays still controls its strength.
- Water evaluates local point-light specular reflection using its existing wave
  normal. This adds a colored reflection highlight, not a mirror image of flame
  geometry. It follows the water reflection toggle/strength and emitter radius.
  Selection comes from the previous completed frame, since water is drawn before
  the lighting composite. No extra screen-space shadows were introduced.
- All touched render state is restored, including sampler 4, VS constants and
  additional render targets. The height atlas is explicitly released on device
  reset/replacement. Its singleton has process lifetime to avoid AMD driver
  calls from CRT destruction under loader lock; the proxy exit regression caught
  and verified the fix for that teardown issue.

## Validation

Release Win32 build; 12 production shaders compile. Real D3D9 regression tests
cover sloped height capture, camera rotation, water-over-ground ordering, missing
coverage, state restoration and device reset. Fog on the slope differs by at
most 1/255 from an independent 8192-step numerical integration. The original
depth-range, fog toggles, edge fog, water boundary, wake and torch attachment
tests also pass. The complete water shader is accepted by the device; its actual
local-reflection block produces a warm highlight, zero outside the emitter
radius, and zero with reflections disabled. Proxy ON/OFF, Present, Reset and
normal process exit pass.

## Practical limits

The height atlas contains submitted terrain/liquid geometry, retaining previously
seen areas until recenter/reset. It is not a full collision map: unsupported
terrain variants and WMO floors are not captured. It adds one inexpensive shader
replay per matching draw and a 12 MB GPU atlas/depth allocation. Fog is an advected
density field with a procedural wake, not a fluid-dynamics simulation. Actual
mounted-motion appearance and performance still need validation in the client.
User graphics settings and unrelated material/shadow features are preserved.

## Follow-up: fog disappearing after camera movement

The first implementation recognized only four terrain **pixel** shaders. The
client's native-shadow log instead pairs terrain VS `938d1ef758aa6085` with PS
`634793193e26059d`, along with six other verified terrain VS variants. None of
those PS variants was in the original allowlist. A water draw could activate an
atlas with no land coverage, switching off the fallback and erasing land fog.

Geometry recognition now includes the seven captured terrain vertex shaders
(their transform/terrain-UV ABI was checked in the native dumps). Water-only
atlases do not become authoritative for land. Zero-primitive marker draws cannot
activate height capture. The regression now goes through `Observe` with a real
native-shadow terrain VS/PS pair, tests water-first ordering, and rejects a WMO
vertex shader. The ground density function and appearance are unchanged.

Lamp ray strength now uses a square-root response so the user's value of 15 is
visibly useful; zero still disables lamp scattering, and 100 remains unchanged.
