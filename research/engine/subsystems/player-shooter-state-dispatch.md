# Current admitted Shooter/state transition matrix

The hash-bound unpacked PC image is `bdaff1746d3092215699b09eab11315c6adf8fb71eaa8f46c280b11b3cd448fd`. Reproducible raw registry/vtable/constructor evidence is retained under ignored `interactive-animation/transition-rereview/source-dispatch.json` and `trace.py`. `ordinary exit` below means outgoing standing/crouch Shooter62e400; it is not an invented exit callback for arbitrary outgoing states. Ordinary exit precedes incoming entry via568960. `timed` means whole62ebe0, including retained posture and camera grace/context, not just animation time. `publication` means upper/independent-weapon raise/lower selection from95f774; timer-only preserves that message's timer/force semantics without new clips. `input` is Shooter975a88 dispatch, not every component's hardware processing or a claim to implement cinematic cameras.

| Admitted source state | State token | Ordinary exit upper | Entry forced0/target reset/ordinary camera2 | Timed update | Shooter hardware input |95f774 publication |
|---|---|---|---|---|---|---|
| Standing |956fa8|Keep/migrate crouched upper -16 when in source interval/parity|No such effect established by this audit|62ebe0|62ed80 ordinary target/fire/refresh/switch|Standing6313b0 via62fb50|
| Crouched |956fac|Keep/migrate standing upper +16 when in source interval/parity|No such effect established by this audit|62ebe0|62ed80 ordinary target/fire/refresh/switch|Crouched62e7d0|
| Generic jump start |95f78c|Retain|No;6649c0 selects body only|62ebe0|62ed80 ordinary|Timer-only62ea40|
| Generic jump flight/support fall |95f790|Retain|No;664480 selects body only|62ebe0|62ed80 ordinary|Standing6313b0 via62fb50|
| Generic jump ending |95f794|Retain|No;664800 selects body/raised landing alignment|62ebe0|Camera look only62e940 via65c3c0/+30|Timer-only62ea40|
| Dodge start |95f7b0|Clear|YES68a600: body selection,603aa0,forced95f774,then604600(2,0,1)|62ebe0|No Shooter input registration|Timer-only62ea40|
| Dodge middle/ballistic |95f7ac|Clear if entered directly from ordinary|No repeat;688fc0 body selection/603aa0|62ebe0|No Shooter input registration|Timer-only62ea40|
| Dodge ending |95f7b4|Clear if entered directly from ordinary|No repeat;68a2f0 body/rate/603aa0|62ebe0|No Shooter input registration|Timer-only62ea40|
| Ordinary damage |96a6e0|Clear|YES665880: forced95f774, camera2, then body|62ebe0|Camera look only62e940 via65c1c0/+2c|Timer-only62ea40|
| Sustained damage |96a6c8|Clear|No;681760 body/timers/contact, no forced message/camera2|62ebe0|No Shooter input registration|Timer-only62ea40|
| Melee/grapple |96a6c0|Clear|YES66bf30: forced95f774, camera2, then body|62ebe0|Camera look only62e940 via65c4a0/+28|Timer-only62ea40|
| Death |95f7a0|Clear|YES6839d0: native death setup, forced95f774, camera2, then selected body|62ebe0|No Shooter input registration|Timer-only62ea40|
| Ancient Wall |96a700|Clear|No forced95f774;69cb80/common613570; separate scope camera4|NO:62a2a0 replaces update with415ca0|No Shooter input registration|Timer-only62ea40|
| Rose Bomb |96a704|Clear|No forced95f774;69cfd0 separate body entry|62ebe0|No Shooter input registration|Timer-only62ea40|
| Will Power |96a708|Clear|No forced95f774;69d690/common613570|NO:62a380 replaces update with415ca0|No Shooter input registration|Timer-only62ea40|
| Sacrifice |96a70c|Clear|No forced95f774;69d810/common613570; separate scope camera7|NO:62a310 replaces update with415ca0|No Shooter input registration|Timer-only62ea40|
| Raven Panic |96a710|Clear|No forced95f774;69e1b0/common613570; cinematic scope distinct|62ebe0|No Shooter input registration|Timer-only62ea40|
| Thunder aiming/start and loop |96a714|Clear|YES forced0/target reset via5a5270(0,1), AFTER69ea30 body and separate scope camera10; NOT ordinary camera2|62ebe0|Camera look only62e940 via65c580/+1c|Timer-only62ea40|
| Thunder strike |96a718|Clear if entered directly from ordinary|No forced95f774;69f5c0 strike body/cinematic scope|62ebe0|No Shooter input registration|Timer-only62ea40|
| Thunder terminal |96a71c|Clear if entered directly from ordinary|No forced95f774;6a03f0 preserves current motion|62ebe0|No Shooter input registration|Timer-only62ea40|
| Tornado casting |96a720|Clear|YES forced0/target reset via5a5270(0,1), AFTER6a0780/common613570 and separate scope camera10; NOT ordinary camera2|62ebe0|Camera look only62e940 via62a5b0/+24|Timer-only62ea40|
| Tornado terminal |96a6d4|Clear if entered directly from ordinary|No forced95f774;6a0b80 scope transition preserves body|62ebe0|Camera look only62e940 via65c5a0/+20|Timer-only62ea40|
| Meteor (all represented phases) |96a724|Clear|No forced95f774;6a0c40/common613570 then cinematic scope|62ebe0|No Shooter input registration|Timer-only62ea40|

## Bounded implementation obligations

- Derive capabilities from actual controller-owned generic-jump phase versus Dodge phase and exclusive source action; an empty exclusive owner does not identify standing.
- Grounded exclusive admission rejects non-grounded phase before mutation. Generic jump is a distinct admitted controller route; do not guess airborne damage/spell handoffs.
- Outgoing ordinary upper retirement must reach both accepted exclusive entry and hardware Dodge entry. Generic jump destinations retain source exception. Within-action followups/phase changes do not repeat the ordinary exit.
- Force effects are distinct from upper retirement and from timer-only95f774 handling. Dodge start joins proven melee/damage/death force entries with ordinary camera2. Thunder aiming and Tornado casting separately force posture/target reset AFTER requesting scope camera10; do not overwrite that scope request with camera2.
- Preserve independent weapon/additive owners on upper retirement; ordinary95f774 publication may legitimately change weapon playback only in source-admitted standing/crouch/generic flight states.
- Generic common-magic inputs must explicitly identify their Shooter timed-update contract; arbitrary slot identity cannot infer it. Typed spell factory supplies the verified state contract. No default-known semantics for unknown future actors/spells.
- Spell/cinematic camera4/7/10 scopes remain existing externally required capabilities; this matrix does not authorize replacing them with camera2, nor claim the inspection camera implements them.
- Full ordinary input admission includes the source weapon switch path. Existing Stage replace_weapon already rejects active exclusive actions and non-grounded changes before mutation; this audit found no live B/Q bypass. General scripted-flight switching is still excluded by the explicitly grounded-only weapon adapter, not silently claimed supported.

Entry effects were checked in complete exported bodies, including5a5270 message construction (not only literal95f774 matches).5a5270 stores message95f774, supplied duration at+20 and force byte at+24. Raw literal searches alone would miss the Thunder/Tornado entry effects.

## Implemented delivery and verification boundary

`AliciaActionOwner` uses the controller-owned synchronous Shooter sink. The
controller borrows the same targeting owner read by Stage; no pending entry
latch or single-entry restriction is introduced. Source entry ordering is
preserved for immediate body selection, including multiple accepted entries
before any model tick. Actual controller scripted/Dodge phase selects the
hardware and posture-message consumer; typed spell descriptors select timed
update/no-op behavior. Common magic requires explicit registration metadata.

The actual-root fixture covers natural lowering into admitted action families,
nonexpired targeting into damage/death, same-frame replacement and interruption,
zero-time/paused publication, and Gatling's authored .75-second lifetime during
typed Wall/Raven actions. The latter distinguishes whole timed-update no-op from
timer-only expiry without ordinary lower animation. These are presentation
contracts; no hit, spell effect, cinematic camera or death-world outcome is
fabricated. Final run status belongs to `alicia-animation-acceptance.md`.
