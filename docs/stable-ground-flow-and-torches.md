# Stable ground flow, water, wake and attached torches

Follow-up to the user's five screenshots on 2026-09-28.

- Wind uses elapsed seconds (about 0.025 world units/second at the broad noise
  scale), not frame count. Vertical noise shear is reduced. Integration spends
  24/32/48 samples inside the ground slab, with stable quadrature instead of
  animated sample jitter; this reduces moving bands and falling-slice artifacts.
- A 2x1 RGBA32F GPU ping-pong stores ground height, a central character silhouette
  estimate and planar velocity. Height changes are bounded to 2 units/second and
  held when the camera is stationary. There is no production GPU readback.
- Wake is centred on the reconstructed silhouette rather than camera XY, clears
  ahead, and introduces alternating lateral density variation behind it; velocity
  and strength settle over time. It is a procedural approximation, not a fluid
  solver or a direct player-object position hook. Occlusion, off-centre characters,
  camera orbit and other central geometry can affect the estimate. One smoothed
  ground reference cannot describe an entire sloping terrain heightfield.
- Water no longer bypasses atmosphere in the near field. The integration clips
  at actual water surface depth and uses that surface's height for the local bank.
  Water shading/reflection/refraction code is unchanged.
- WORLD EDGE FOG controls an independent depth-based horizon blend, including
  when global/local fog are both off. High sky remains clear. DistancePercent is
  retained as the persisted legacy key, now defining the onset in world units;
  PowerPercent controls extinction. Native shader-register rescaling is skipped
  while the compositor owns the effect, avoiding duplicate fog; F11 remains wired.
- Recognized FLAMELICKSMALL M2 draws create moving warm lights from actual indexed
  flame vertices, rigid bone matrices (c31+3*index) and inverse view. Only captured
  shader layouts and verified mip hashes qualify. These cover the shipped classic
  handheld torch and matching fire meshes, not every custom fire/particle shader.
  Vertex/index buffers must be readable; otherwise capture is skipped and logged.
  No position is guessed from screen brightness. Sources expire after 250 ms.

Validation: Release Win32 build, ten ps_3_0 shader compilations (integration
500/512 slots, persistent-state pass 254), production GPU regression suite and
proxy ON/OFF/Present/Reset checks pass. New checks cover near-water air fog,
independent edge fog and clear high sky, moving silhouette wake and stable ground,
real flame texture capture, animated attachment movement/deduplication and rejection
of ordinary textures. The 128x128 DXT5 fixture is mip zero of the user's client
FLAMELICKSMALL.BLP, common.MPQ; its FNV-1a is 8354521e60f27a8c. Signatures also
include the client's patch-TW variant and decoded BGRA payloads. Shader layouts
were inspected in the client's NativeShadowShaders captures. Asset inspection
scripts are in the adjacent WoWMaterialBuilder workspace (inspect_torches.py and
inspect_torch_hashes.py).

No live in-game visual match or frame-rate measurement is claimed. Settings are
preserved on installation; the DLL and settings from before installation are
backed up under the client's RendererBackups directory.
