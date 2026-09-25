# Round 6: native shadow architecture findings

Runtime capture: Ascension test client, 2560x1440, 25 September 2026.

## Confirmed architecture

The client uses native cascaded shadow maps. It is not a projected blob-only
system and it does not apply the world shadows in a later fullscreen pass.

Four cascades are rendered every world frame into separate 2048x2048 `D24X8`
depth textures. A 2048x2048 `A8R8G8B8` color target is bound alongside them,
but caster draws use `COLORWRITEENABLE=0`; authoritative shadow information is
therefore the native depth texture. The same four depth resources are bound as
textures for the normal world/material receiver draws later in the frame.

One additional 2048x2048 depth resource is present. It does not participate in
the same four-cascade producer sequence and is treated as a main/view depth
resource until a later diagnostic proves a different role.

## Native caster pass

Common state:

- render target: 2048x2048 `A8R8G8B8` (dummy colour target)
- depth target: 2048x2048 `D24X8` texture, one per cascade
- viewport: 2048x2048
- colour write mask: 0
- alpha blend: off
- depth test/write: on/on
- stencil: off
- primitive type: triangle list

Observed caster shader families:

| Stage | Hashes |
|---|---|
| VS | `202b452e580a7742`, `8535271f53b044b4`, `ea99ea307a197a84` |
| PS | `4f20b99325ed9c47`, `78641e03a615f46a` |

The VS variants cover different caster/material layouts. The two PS variants
track opaque/alpha-tested caster handling. No generic draw replay is needed or
permitted: the game already renders all authoritative caster geometry.

## Native receiver path

The four depth textures are sampled by the normal opaque/alpha-tested world
draws. There is a receiver shader family rather than one receiver pass. The
capture observed the cascade textures on the high texture slots (normally
`s5`-`s8`; some state transitions expose the four-resource set one slot lower).

Repeated receiver PS hashes include:

- `e6931331d14bdbba`
- `2e4e1d13da88efee`
- `827c05c3635b689f`
- `777239a63edea3a1`
- `ac008ac06cd451a9`
- `42c3a115d145164a`
- `f71ca77414ce780e`
- `b52eba6fbc7f34f2`

These pair with several material-specific vertex shaders (WMO, terrain, M2 and
alpha-tested variants). Enhancing a single fullscreen draw would miss native
receivers and is explicitly rejected.

## Cascade evidence

Within the same frame the four depth resources finish at progressively later
draw ordinals, for example approximately 73, 182, 461 and 1955 in one stable
scene. They are subsequently rebound together to receiver shader texture slots.
This producer-to-consumer relationship repeats every frame and remained stable
while the camera was rotated.

## Constants

The diagnostic captured VS/PS `c0`-`c15` for every unique candidate. Caster
projection scale changes per cascade and receiver constants change with the
material. Register semantics are not declared validated yet: the next stage
must disassemble the exact receiver variants and compare values over camera and
sun-time changes before naming projection/light registers.

## Safe next step

1. Capture sampler state for all four native cascade slots.
2. Dump/disassemble the confirmed receiver shaders to locate comparison/filter
   operations and the exact constant registers they consume.
3. Add a developer-only cascade preview/receiver highlight.
4. Improve filtering only through the native shadow representation (sampler or
   receiver shader variant), preserving WoW's matrices, caster geometry and
   receiver placement.

The retired custom `ShadowDraw` replay and long screen-space directional march
remain compile-time unreachable.
