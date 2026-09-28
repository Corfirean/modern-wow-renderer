# Follow-up: ground fog volume and outdoor lamps

The user confirmed depth-correct fog was visible but only as a thin strip;
street lamps still did not contribute light. Two distinct omissions remained.

## Ground fog

The old LOCAL HEIGHT slider actually increased exponential *falloff*. At 1500,
density dropped by e every 0.67 world units. Further, each ray used its visible
endpoint as ground, so tree trunks and walls changed the fog reference height.

The replacement samples a shared median ground height from three lower-view
depth probes, entirely on GPU (no synchronous CPU readback). This is an estimate
of nearby ground, not a full terrain heightfield; steep terrain or a view wholly
filled by walls can still affect the estimate. Density now varies along XYZ,
with a continuous low-density base and denser moving banks. Quadratic marching
uses 12/16/24 samples concentrated near the player, independently of far haze.

LOCAL HEIGHT now uses HeightUnits=1..24, default 8; larger values mean thicker
banks. LOCAL DENSITY spans 0..100 and no longer depends on global FOG DENSITY.
The local coefficient is 0.003 per unit. Global fog also uses its aerial-density
coefficient along the actual ray length instead of only a normalized far curve.

## Outdoor sources

The old manifest contained only WMO interior/building lights. In particular,
Elwynn's lamppost M2 has zero M2Light records; it renders an unlit additive GLOW32
submesh. Increasing light intensity could never illuminate that absent source.

`WoWMaterialBuilder/wow_material_builder/m2_world_lights.py` adds ADT MDDF-placed
static lamp/fire models. It reads authored M2 lights where their root transform
can be resolved; otherwise it accepts only an unlit additive glow submesh from
the matching SKIN and a lamp/fire model name. Ordinary bright textures, characters,
and spell particles are not sources. Additive-only lamps use a supplemental warm
light at the actual glow-mesh centroid, with artist-set radius/intensity. This is
an enhancement, not a claim that the original M2 authored those lighting values.
Model/skin versions are selected with patch precedence, rather than merging all
versions. WMO lights are retained.

The actual Elwynn model's glow centroid is (0.094412, 0.974222, 2.860962).
Its nearby placement 54417 becomes (-9499.8376, 60.7080, 59.3262), about 13 units
from the camera in the user's latest scene. It is now selected by the runtime.
The EK scan added 1135 emitters: 630 authored M2 lights and 505 additive-glow
emitters, with no parse errors. Existing 5506 WMO entries remain.

Binary layout cross-check: [M2Light definitions](https://zxgit.org/RomanRom2/whoa/blame/commit/3b609b44fda6d297990b3f0359d354e38b90f5cf/src/model/M2Data.hpp),
[skin material assignment](https://github.com/wowdev/pywowlib/blob/master/m2_file.py).
Decoded layout and centroid were verified against this client's M2/SKIN bytes.

## Verification

- Release Win32 DLL built; active shaders compile (integration: 456/512 slots).
- Production D3D9 regression suite passed, including a ground plane, measurable
  fog above it, monotonic thickness, independent local density, lamp occlusion,
  water surface stopping depth, viewport-depth correction, state restoration,
  and selection of the actual screenshot-area outdoor lamp.
- Proxy ON/OFF and Reset suite passed. The fixture now has a near occluder and
  far wall, so spatial variation tests depth rather than sub-byte noise rounding.
- Elwynn additive-glow extraction matches its measured centroid; missing SKIN
  data does not manufacture an emitter.

The client was closed during installation. These checks do not establish visual
equivalence with the videos; the final in-game appearance remains to be checked.
Water, shadows, material relief, weather, and post-processing were not edited.
