# Local light artifacts and particle torch follow-up

2026-09-28: user's screenshots show distant orange influence patches in LIGHT
DEBUG=1, no attached torch lights, horizontal light stripes, and a second shadow.

Changes:
- Removed screen-depth contact visibility from the injected surface light pass.
  Native shadows remain owned by the game; this avoids a second inferior shadow.
  Reorienting native shadows for each point light would require a different
  light-space rendering pipeline, not moving a composited shadow image.
- Surface light quality now uses 50/75/100% resolution. Bilateral reconstruction
  tolerance includes the continuous receiver depth slope, using the smaller
  one-sided depth differences to avoid treating silhouettes as slopes.
- LIGHT DEBUG=1 now shows actual shaded, attenuated contribution, rather than
  a bright artificial visualization of each source's radius. Mode 3's injected
  contact-occlusion view is empty now that the duplicate shadow is removed.
- Volumetric lamp scattering additionally decays with squared perpendicular
  distance to the emitter; broad remote banks no longer receive uniform light.
- Torch observation supports verified unskinned c31..c33 shader layouts, indexed
  and nonindexed buffers, and both UP APIs. Known texture signatures include
  shipped mip payloads down to 16 pixels. Separated flames in one batch are
  clustered separately, avoiding a fictitious averaged midpoint source.
- The WMO manifest generator now uses the winning MPQ version of each model and
  ADT, instead of unioning lights/placements from superseded versions. WMO count
  drops 5506 -> 5277; total with preserved M2 sources is 6412. No obsolete WMO
  entry was found within 150 units of the Goldshire comparison point, so this
  cleanup is not presented as the cause of those particular screenshot patches.

Validation: ten active ps_3_0 shaders compile (surface 196, reconstruction 123,
volume integration 502 slots); Release Win32 build and production GPU suite pass.
The new sloped-plane comparison checks the local interpolation envelope against
full resolution (maximum dark deficit 2/255, tolerance 6), rather than demanding
identical pixels around a saturated nonlinear lamp core. Real flame mip tests
cover skinned and unskinned draws, UP/nonindexed/indexed APIs, animated movement,
two spatially separate flames, and rejection of an ordinary texture. Existing
water boundary, fog, viewport depth, device-state and proxy ON/OFF/Reset tests pass.

The live log confirmed attached=0 in the previous version; no new live game
capture is available yet. Unknown custom particle shaders and unreadable buffers
remain unsupported. Native shadow direction is unchanged; no complete local
shadow-map implementation is claimed. Native map shadows and baked shading
cannot generally be repositioned per lamp by changing the injected light pass.
