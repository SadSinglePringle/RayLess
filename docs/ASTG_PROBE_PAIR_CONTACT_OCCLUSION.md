# ASTG Probe-Pair Contact Occlusion

Status: proposed future work; not implemented or validated.

## Purpose

Add a GPU-driven contact-shadow approximation that reuses Part K surface probes and the existing dynamic receiver hierarchy instead of evaluating short contact-shadow rays for every visible pixel.

This feature is intended to restore small grounding and near-contact cues that can be lost when direct-diffuse lighting is reconstructed from probes. Typical cases include feet touching floors, hands holding objects, furniture resting on the ground, limbs touching one another, and moving objects approaching walls.

The proposal is a local correction to direct-diffuse visibility. It is not a replacement for Part J visibility, triangle-accurate shadowing, specular lighting, or general ambient occlusion.

## Core idea

When surface probes owned by different objects become sufficiently close, use their separation, normals, light direction, and estimated coverage to determine whether one surface is a plausible local blocker for the other. Apply a cached contact-occlusion factor to the receiver probe's direct-diffuse visibility.

Proximity alone must never create a shadow. Two surfaces can be close while both remain directly illuminated, while the light lies between them, or while their orientations make occlusion impossible.

For a receiver probe `r`, possible blocker probe `b`, and light sample `l`:

```text
contact_occlusion(r, b, l) =
    proximity_weight(r, b)
  * facing_weight(r, b)
  * blocker_alignment(r, b, l)
  * coverage_estimate(r, b)
  * confidence(r, b, l)

corrected_visibility(r, l) =
    part_j_visibility(r, l)
  * (1 - saturate(contact_occlusion(r, b, l)))
```

When several blockers affect one receiver/light pair, combine them with a bounded visibility product or another energy-conserving aggregation. Do not add unbounded darkness.

## Required conditions

A pair may contribute only when all applicable conditions pass:

1. The probes have different surface or object ownership. Same-object pairs require an explicit self-contact mode and separate validation.
2. Their world-space distance is below a configurable contact radius derived from local probe spacing and object scale.
3. The candidate surfaces face one another or otherwise have a geometrically plausible contact relationship.
4. The blocker is on the receiver-to-light side of the receiver, within a conservative local shadow cone or capsule.
5. The light does not lie between the two surfaces.
6. The light affects the receiver and is not already rejected by Part J or the light hierarchy.
7. The pair generation and lighting generations are current.

These gates prevent distance-only dark halos around nearby objects.

## Intended GPU pipeline

The production path must remain GPU-resident. The CPU may upload dirty object/light transforms and dispatch parameters, but it must not enumerate probe pairs, loop over relevant lights per probe, calculate contact weights, or accumulate receiver lighting.

```text
dirty moving object/cluster bounds
    -> query nearby foreign object/cluster bounds
    -> generate compact cross-object probe-pair candidates
    -> reject by distance, ownership, normals, and generation
    -> join with compact relevant-light lists
    -> reject by light direction and influence
    -> compute contact weight/confidence
    -> optionally compact ambiguous pairs for exact short RayQuery
    -> accumulate bounded contact visibility per receiver/light
    -> temporally filter cached contact state
    -> consume during Part K direct-diffuse reconstruction
```

Candidate discovery should reuse the Part K receiver BVH, cluster hierarchy, or a GPU spatial hash. An all-probes-against-all-probes comparison is forbidden.

## Suggested GPU records

Exact layouts should follow the repository's GPU buffer layout rules, but the logical data is:

```text
ContactPairCandidate
    receiver_probe_id
    blocker_probe_id
    receiver_generation
    blocker_generation

ContactLightCandidate
    receiver_probe_id
    blocker_probe_id
    light_id
    pair_generation

ContactVisibilityState
    receiver_probe_id
    light_id
    visibility_multiplier
    confidence
    last_update_generation
```

Compact counters must record candidates considered, hierarchy rejections, distance rejections, normal rejections, light-direction rejections, exact-ray candidates, and accepted contact contributions.

## Approximation and exact refinement

The base mode uses only the probe-pair estimate. An optional refinement mode may issue one or a small bounded number of short inline `RayQuery` tests for candidates whose confidence falls inside an ambiguous range.

High-confidence cases should remain rayless. Low-confidence cases should contribute nothing. Only the uncertain middle band should be eligible for refinement. The refinement budget must be capped and prioritized by projected importance, contact strength, motion, or temporal instability.

Bounding boxes and probe proximity are broadphase evidence, not proof of triangle-level occlusion. Results from the approximation must be labeled accordingly in diagnostics.

## Temporal behavior

Contact relationships must not pop as animated probes cross the contact radius. Cache state by stable receiver/blocker/light identity and use:

- separate enter and exit thresholds;
- generation checks after animation, topology, or light changes;
- bounded temporal blending;
- immediate invalidation for teleports, destroyed geometry, or ownership changes;
- faster response when contact strengthens than when it weakens, if this does not cause visible trails.

Temporal filtering must not preserve a shadow after the blocker has moved away or the light has moved to the opposite side.

## Performance constraints

The expected advantage over conventional contact shadows comes from evaluating persistent probe relationships instead of screen-resolution samples. That advantage is a hypothesis until measured.

The implementation must:

- scale with dirty nearby probe pairs and relevant lights, not framebuffer resolution;
- update static contact pairs only when their surfaces, lights, or generations change;
- impose a strict candidate-neighbor cap per probe;
- cull lights before pair-light expansion whenever possible;
- avoid CPU readback in the production lighting path;
- retain compact persistent state that can be reused by all pixels influenced by a receiver probe;
- report overflow explicitly rather than silently dropping candidates.

## Known limitations

The approximation may miss or blur features smaller than the probe spacing. It may also produce false occlusion when probes poorly represent concave or thin geometry. It does not inherently provide:

- pixel-accurate contact edges;
- detailed alpha-tested or transparent shadows;
- exact self-shadowing of fingers, cloth folds, hair, or thin props;
- view-dependent direct specular occlusion;
- area-light penumbra reconstruction outside the available emitter samples;
- correct transmission through participating or translucent media.

Probe density, source-sample density, and confidence/refinement policy determine the achievable quality.

## Validation plan

Required correctness scenes:

1. Foot approaches, touches, and leaves a floor under overhead, side, and below-floor lights.
2. Object approaches a wall while the light moves from the blocker side to between the surfaces.
3. Two close parallel surfaces remain lit when neither blocks the receiver-to-light direction.
4. Hand grips a moving prop across skeletal animation frames.
5. Two limbs approach and separate with self-contact disabled and enabled.
6. Thin blocker and concave blocker cases compare approximation-only and exact refinement modes.
7. Area light produces stable contact hardening as separation changes.
8. Teleport, destruction, probe remap, and light-generation changes leave no stale contact state.
9. Dense multi-object pileup exercises candidate caps and overflow reporting.
10. Camera movement with static geometry proves the result is view-independent and does not trigger recomputation.

Quality must be compared against triangle-level GPU visibility ground truth. Report false darkening, missed occlusion, per-probe visibility error, image-space error, temporal variance, and disocclusion latency. A visually plausible screenshot is not sufficient evidence of correctness.

Required performance comparisons:

- probe-pair approximation only;
- probe-pair approximation plus bounded exact refinement;
- short per-pixel RayQuery contact shadows;
- screen-space contact shadows, when available.

Measure GPU timestamp intervals for every GPU stage, CPU submission cost, candidate and accepted-pair counters, persistent memory, transient memory, and overflow counts. Derived throughput or savings must be labeled as derived; estimates must not be exported as measured data.

## Acceptance criteria

The feature is ready for production consideration only when:

- no CPU fallback performs probe-pair or pair-light evaluation;
- proximity without directional plausibility produces zero contact darkening;
- stale generations and teleports produce zero persistent shadow state;
- approximation error and exact-refinement improvement are quantified against ground truth;
- candidate overflow is zero in supported workloads or visibly and diagnostically reported;
- GPU time and memory scale acceptably with probes, moving objects, contacts, and lights;
- measured performance demonstrates an advantage in at least the intended contact-heavy workloads;
- all exported fields state `MEASURED_CPU`, `MEASURED_GPU`, `MEASURED_COUNTER`, `DERIVED`, or `ESTIMATE` provenance.

## Architectural placement

This should be implemented as a later extension of Parts J/K:

- Part J remains authoritative for general B0 light visibility and exact occlusion updates.
- Part K supplies animated/static surface probes, ownership, transforms, hierarchy traversal, and receiver-light accumulation.
- Probe-pair contact occlusion supplies a local, cached direct-diffuse visibility correction.
- Optional short rays refine only ambiguous contact candidates.
- Final material shading continues to own direct specular and other view-dependent terms.

The feature should not mutate the static ASTG DAG merely because two dynamic receivers become close. Contact state belongs in a separate dynamic cache keyed by stable probe, object, light, and generation identifiers.
