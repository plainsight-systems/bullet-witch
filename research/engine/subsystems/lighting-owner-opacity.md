# Model opacity and the Main/Trans owner boundary

The renderer now implements this source contract through explicit owner
publication. Missing gameplay fade producers remain a separate boundary.
Original executable SHA256:
`bdaff1746d3092215699b09eab11315c6adf8fb71eaa8f46c280b11b3cd448fd`.
The retained complete executable disassembly and the original command-list/state
witnesses live under ignored `research/evidence/reconstruction/lighting-source-fidelity/output-lifecycle/`.

## Ownership and shader publication

Constructor `56a190` initializes model owner opacity at `+f8` to 1.
Setter `56b9f0` writes the supplied float there, obtains the model through
virtual slot `+1c`, reads its float4 at `+860` with `428630`, replaces only
its alpha, and writes it back through `42b980`. RGB is preserved. This is
replacement, not repeated multiplication. `421d20` copies that model color to
submission `+500`; `427220` publishes it as pixel constant c0, the indirect
color consumed by T material programs. Final alpha therefore includes owner
opacity once through indirect color, alongside material/texture/vertex alpha
and the selected alpha-Fresnel operation. S depth shader cutout has its own
source expression and must not acquire this T-only multiplier by analogy.

## Queue selection and alpha reference

`56a520` routes opacity >=1 to four Main chunk lists. Values <=0 are omitted
from this color submission; a NaN also does not enter either route. Partial
opacity, strictly between0 and1, selects all four Trans chunk lists instead.
At each list it first queues `569f80` with integer conversion of opacity*128.
The actual constant at `6c2010` is **128.0**, not255. Conversion helper
`500140` truncates toward zero; positive partial opacity thus uses floor.
Command vtable `842008` dispatches `56a480`, which sets D3D9 render state24
(ALPHAREF) to that integer. It does not set a shader constant or blend factor.
The default reference128 is restored by each masked setup before owner replay;
each partial owner's queued reference then precedes its own model draws.
Reference writes in unmasked chunks have no effect while alpha test is off.
Material-command execution remains later and can override inherited state;
represented material paths only have their already-audited blend operations.

`56a640` aliases the same submitted owner into S and T for chunks0/1, and
publishes chunks2/3 separately, preserving insertion order. Trans is an owner
section, not chunk2 and not a new transparent-material classifier. Main draws
finish before the separate Trans preparation and color lists execute.

The literal prior builder/list/executor witnesses establish:

| Trans phase | Depth | Alpha test | Blend |
| --- | --- | --- | --- |
| S chunk0 | greater-equal, writes on | off | off |
| S chunk1 | greater-equal, writes on | GE owner reference against depth-valued alpha | off |
| T chunk0 | greater-equal, writes on | off | source alpha / one-minus-source-alpha |
| T chunk1 | greater-equal, writes on | GE owner reference against completed material alpha | source alpha / one-minus-source-alpha |
| T chunk2 | inherited greater-equal, writes on | off | enabled before material commands |
| T chunk3 | inherited greater-equal, writes on | off | reset off, then authored material blend |

The Trans S/T setup comes from `461170/4614e0`; color executor `457020`
forces depth on/write on and alpha blending even though earlier builder commands
requested depth off. The reference command replays in both shared S/T lists.
D3D fixed-alpha precision still follows the explicitly documented native target
correspondence in `lighting-material-redesign.md`, generalized by reference.

## Minimal native contract and limits

Publish optional owner opacity independently of material chunk, instance color
visibility and shadow-caster selection. Absence can preserve diagnostic material
alpha; scene owners should explicitly publish their real value (initially1).
Validate finite inputs before any batch mutation. Replace indirect alpha once,
choose Main/Trans/omitted color route, and retain the fixed-reference integer.
Do not multiply the already replaced indirect alpha a second time in shaders.
For partial owners, schedule their own preparation/color section after Main,
with the source depth/blend states above; retain per-instance identity and order.

`56a520` is a color-submission function. It does not unregister shadow owners or
change the separately traced shadow selection callbacks. Zero color opacity
must not implicitly clear caster eligibility. Owner lifecycle/fade producers
must eventually call the publication boundary; implementing this renderer API
does not reconstruct missing actors, event systems, or particles. Unsupported
activation of absent producers remains an explicit upstream boundary.

Nine original-executable setter/conversion/state-executor cases independently
verify RGB preservation, alpha replacement, positive fractional truncation and
ALPHAREF publication. The witness replaces only model getter and D3D setter;
conversion and model color accessors execute unchanged. Results are retained
under ignored `research/evidence/reconstruction/lighting-owner-opacity/`.

## Implemented verification

The existing native memory scenario passes owner-opacity checks on the production
HDR target: alpha replacement rather than repeated multiplication; the partial
masked reference on both S depth and T color; Main late chunks preceding Trans
early preparation; full/zero admission, omitted-update retention, and whole-batch
rollback on nonfinite input. A zero-color-opacity caster still shadows its
independent receiver. These are reconstructed device contracts, not matched
original live-frame evidence. Source setters and conversion were independently
executed across nine supplied cases by the source investigation.

Both interface and implementation phases queried Core I.1/E.4 and performance
MEM.9. Opacity updates validate before mutation, retain existing draw identity,
and use the prepared phase index with no per-frame sorting or allocation.
