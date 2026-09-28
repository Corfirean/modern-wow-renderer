# Fog and lamp depth correction

The supplied on/off screenshots showed almost no atmosphere or lamp response.
Both production passes reconstructed INTZ as projection depth, although WoW
writes depth through a viewport whose MaxZ can be 0.94. With a 0.1 near plane,
a surface at 120 units was consequently reconstructed at roughly 1.6 units.
The existing legacy volume shader already divided out this viewport scale.

Changes in this repair (the checkout also contains earlier uncommitted work):

- Normalize viewport MinZ/MaxZ once in atmosphere boundary depth; retain water
  surface depths in projection space. Apply the equivalent A/B transform in
  local surface lighting without changing any water shader.
- Create atmosphere resources before shaders, since resource recreation resets
  shaders. Initial frames and quality changes now use valid production shaders.
- Restore c0..c63 after passes instead of only c0..c15. Local lights write above
  c15; these constants must not leak into subsequent native/UI draws.
- Preserve local surface lighting when atmosphere is disabled or unavailable.
- Integrate up to eight lamp volumes over their bounded ray/sphere intervals,
  clipped to scene depth. Small sources no longer depend on a world-march sample
  happening to land inside their radius. Scattering still requires enabled fog.
- Make zero global density remove its optical depth; restrict sky haze to the
  horizon and use consistent sky thresholds in temporal and bilateral passes.

Validation:

- Release/Win32 build passed.
- All nine active atmosphere/local-light pixel shaders compiled as ps_3_0.
  The integration shader uses 389 instruction slots (DX9 limit: 512).
- `tools/Test-Atmosphere.cmd` passed using the production renderer classes and a
  real D3D9 device: identical RGB with MaxZ=1 and .94, nonzero MinZ, first-frame
  rendering, quality recreation, zero density, independent local fog, water
  surface boundary, bounded lamp glow, foreground occlusion, surface lighting,
  and restoration of all 64 saved constant registers.
- `tools/Build-ProxyTest.cmd` and `tools/Test-FogProxy.ps1` passed: proxy camera
  capture, depth identity/remapping, pre-UI composition, 64-register and viewport
  restoration, master off, Present and Reset. Uses existing captured shaders.
- Legacy `Test-VolumeIntegration.ps1` and `Test-WaterEffect.ps1` were also tried.
  Their original assertions fail on both the pre-repair DLL and repaired DLL.
  The volume fixture omitted GraphicsEffects.ini, allowing a status plate to
  cover the sampled pixel; it also assumed fog could only increase blue.
  The isolated proxy test disables unrelated status plates and checks a real
  color change instead. The old water suite's exact original/reconstruction
  equality assertion fails identically before/after (baseline 560e7291,
  reconstruction d344765f); its state/Reset checks passed. Animated water modes
  cannot be compared by whole-frame hashes from different wall-clock times.

Reference screenshots and three time samples from each supplied video were
inspected. An in-game visual comparison after installation remains unverified:
the Ascension client was not running during deployment. No water, native-shadow,
material, weather, or post-processing implementation/settings were changed by
this repair. Only the current client's fog and dynamic-light switches are
enabled for viewing the result; its previous DLL and INI are backed up.
