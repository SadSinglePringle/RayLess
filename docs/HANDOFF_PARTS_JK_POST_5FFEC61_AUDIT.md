# RayLess ASTG Parts J/K Corrective Handoff After `5ffec61`

## Scope and verdict

Continue from `origin/main` after:

- implementation commit `5ffec61a72d1b7c41ec13e9c3e659eb911e320f8`
- evidence commit `00b3160363ba74d05c0601e2ae78cf88c7e0bc11`

The implementation commit contains useful corrections: persistent Part J word allocations, explicit packed versus actual light IDs, GPU word-work dispatch, indirect K5 dispatch arguments, bone-transform offsets, added UAV barriers, and real timestamp query plumbing.

Do not describe Parts J/K as complete or the current evidence as empirical. The committed evidence includes hard-coded benchmark rows and hard-coded correctness claims, while several tests never validate their stated behavior. There are also unresolved runtime correctness defects in Part J persistence and Part K cluster/transform handling.

This handoff is corrective. Fix correctness and evidence integrity before adding contact occlusion or another ASTG feature.

## P0: Quarantine unsupported evidence claims

The following artifacts must be treated as invalid until regenerated from real test records:

- `results/latest/part_j_gpu_correctness.json`
- `results/latest/part_j_gpu_persistence.json`
- `results/latest/part_j_gpu_scaling.csv`
- `results/latest/part_k_gpu_correctness.json`
- `results/latest/part_k_gpu_hierarchy.json`
- `results/latest/part_k_gpu_scaling.csv`
- `results/latest/parts_jk_anti_fallback.json`
- `results/latest/parts_jk_d3d12_validation.json`
- `results/latest/parts_jk_gpu_timestamps.json`

Why:

1. `src/diagnostics/astg_transport_diagnostics.h:11007-11010` writes literal Part J timing rows. No sweep produced those values.
2. `src/diagnostics/astg_transport_diagnostics.h:11073-11077` writes literal Part K timing rows. No sweep produced those values.
3. `part_j_gpu_persistence.json` exports literal allocation records and claims `modulo_slot_recycling=true`, but the runtime uses monotonically increasing offsets and implements no modulo recycling.
4. Multiple JSON fields such as `part_j_gpu_pipeline`, `clean_decrements`, `unblocked_transitions_generated`, `actual_light_id_preserved`, `self_occlusion_valid`, `gpu_counters_positive`, `failure_injection_tested`, `d3d12_debug_layer_active`, `resource_transitions_valid`, and `fabricated_timings_detected` are emitted as constants rather than computed observations.
5. The scaling CSV reports nonzero `j3_ms`, but the runtime sets `gpu_j3_ms = 0.0` because J3 is embedded in J2. A separate J3 timing cannot be claimed unless it is a separately timestamped dispatch. Otherwise export a combined `j2_j3_traversal_and_exact_visibility_ms` field.
6. Newly sealed test records have empty build/source/binary hashes and `UNKNOWN` workload provenance even while being presented as hardware evidence.

Required immediate action:

- Remove literal PASS/true/zero-error output fields.
- Generate every exported value from an in-memory result captured by the exact test invocation.
- If a property was not exercised, emit `NOT_EXERCISED` or omit it.
- If a metric is a formula, label it `DERIVED` and export its inputs.
- Do not seal replacement evidence until all P0 correctness work below is complete.

## P0: Replace vacuous and unconditional tests

`test_parts_jk_gpu_runtime_correctness_and_persistence()` currently contains tests that cannot fail or do not inspect the property named by the assertion.

### Aggregate blocker test

At `astg_transport_diagnostics.h:9175`, `part_j_test_aggregate_blocker_pass` is assigned `true` without reading GPU or engine blocker counts.

Required test:

1. Reset Part J persistent state and telemetry.
2. Create at least one real B0 record intersected by two independently owned dynamic groups.
3. Dispatch group A and assert the GPU aggregate count is 1.
4. Dispatch group B and assert it is 2.
5. Remove A and assert the count is 1 and visibility remains blocked.
6. Remove B and assert the count is 0 and exactly one aggregate unblocked transition occurs.
7. Verify the sequence from GPU-produced state/readback: `0 -> 1 -> 2 -> 1 -> 0`.

### Underflow test

At `astg_transport_diagnostics.h:9194`, the test assigns PASS without causing a removal from zero or inspecting the counter.

Required test:

- Inject a controlled previous-membership/current-membership delta that attempts a decrement at zero.
- Assert the count remains zero.
- Assert `gpu_j4_underflow_errors` increments by exactly one for this invocation.
- Ensure an ordinary removal from one to zero does not increment the error counter.

### K4/K5 conservation test

The current expression at `astg_transport_diagnostics.h:9245-9246` is invalid:

```cpp
conservation || (telem.gpu_k4_work_emitted > 0)
```

Any positive emitted count passes even when consumed differs. Zero emitted and zero consumed also passes vacuously. The assertion status is then forced to PASS.

Required predicate:

```cpp
emitted > 0 &&
consumed == emitted &&
overflow == 0 &&
dispatch_groups_x == ceil(emitted / 64)
```

Telemetry must be reset immediately before this workload and resolved immediately after it. Do not reuse global telemetry left by an unrelated test.

### Noncontiguous cluster test

At `astg_transport_diagnostics.h:9286`, the test assigns PASS after dispatch without checking probe membership or output.

Required test:

- Use interleaved membership such as cluster 0 = probes `{0,4,8,...}`, cluster 1 = `{1,5,9,...}`.
- Give each probe a unique analytically predictable position and light result.
- Read back transformed cluster centers, radii, emitted work IDs, and final irradiance.
- Assert each cluster consumes exactly its declared members and no adjacent nonmember.

### Bone rotation and normal-transform tests

At `astg_transport_diagnostics.h:9324` and `9343`, the tests assign PASS without reading GPU results. The selected normals also fail to detect rotation-direction errors.

Required test:

- Read back transformed probe positions and normals.
- Compare against a double-precision CPU matrix reference.
- Include rotations around all three axes and a normal not aligned with a rotation axis.
- Include nonuniform scale combined with rotation and shear; diagonal scale alone cannot distinguish inverse from inverse-transpose.
- Check unit length and tangent orthogonality with a stated tolerance.

### Dense-probe/self-occlusion test

The current test only asserts the CPU vector contains 256 probes, then claims exact GPU visibility and self-occlusion.

Required test:

- Assert K2 transformed 256 probes, K4 emitted the expected nonzero work, K5 consumed it, and K6 reduced 256 receivers.
- Place a known same-character blocking bound between selected probes and the light.
- Verify expected blocked and visible probe IDs against a reference.
- Do not use the word `exact` when the production test is only segment-versus-AABB.

All assertion statuses must be derived from their predicates. No unconditional `STATUS_PASS` is allowed.

## P0: Fix Part K noncontiguous cluster membership

The host stores arbitrary `member_probe_indices`, but the GPU representation stores only `probe_offset` and `probe_count`. `CSTransformReceiverClusters` and `CSCullReceiverHierarchy` then iterate the contiguous range:

```text
probe_offset ... probe_offset + probe_count - 1
```

This is incorrect for interleaved or otherwise noncontiguous cluster membership. The new diagnostic constructs exactly that layout but never validates the result.

Required implementation:

1. Add a packed `cluster_probe_indices` GPU buffer.
2. For every cluster, store `member_index_offset` and `member_index_count` into that buffer.
3. K3 and K4 must dereference the packed member index before reading a probe.
4. Validate every referenced probe index and report invalid-member and overflow counters.
5. Keep a fast contiguous representation only if it is explicitly identified and proven contiguous during upload.

Do not silently reorder the public probe array unless every stable probe ID and downstream cache is remapped transactionally.

## P0: Correct the GPU normal transform

`CSTransformSurfaceProbes` constructs cofactors and then multiplies them as columns. With the repository's row-major `M * p` convention, this applies the wrong orientation for general rotation/shear and does not reliably implement `transpose(inverse(M)) * n`.

Required implementation:

- Define the matrix convention in both C++ and HLSL.
- Implement an explicit inverse-transpose 3x3 multiply consistent with that convention.
- Add a singular-transform policy: reject, mark invalid, or use a documented fallback. Do not silently claim physical correctness for a singular matrix.
- Verify using combined rotation, nonuniform scale, and shear against the CPU reference.

## P0: Fix local/world bound double transformation and bone ownership

When building Part K inputs, `local_min/max` are populated from `ob.world_bounds`, and K1 transforms them again. Moving rigid and skeletal objects can therefore receive a second transform.

Required implementation:

- Upload `ob.local_bounds` as local bounds.
- Let K1 produce world bounds exactly once from the current transform.
- Compare K1 output to `RTXMatrix4x4::transform_aabb(ob.local_bounds)` for translated, rotated, scaled, and articulated objects.

The current bone-bound upload also derives the transform from the bound vector index. Use the bound's declared `ob.bone_id`, validate it against the group's bone count, and add the group's global transform base. Do not assume `bound_index == bone_id`.

## P0: Make Part J deletion, disable, and layout invalidation GPU-correct

`clear_group_dynamic_state()` clears CPU-side maps but does not clear the group's `g_previous_membership` words or decrement `g_persistent_states` on the GPU. A disabled/unregistered group can therefore remain a persistent blocker on later dispatches.

Required implementation:

1. Introduce an explicit GPU remove-all operation for every `(group_id, actual_light_id)` allocation.
2. Process previous membership against zero current membership through the same guarded delta logic.
3. Emit aggregate transitions only when the shared blocker count crosses `1 -> 0`.
4. Clear or retire the allocation only after the GPU removal completes.
5. Cover group disable, unregister, mode change, empty bounds, light removal, and hierarchy rebuild.

Part J record state is currently indexed by the packed `global_rec` offset of the latest full light upload. That offset is not a stable identity when lights are inserted, removed, reordered, or their B0 record counts change.

Required implementation:

- Define a stable record identity such as `(actual_light_id, stable_b0_record_id, layout_generation)`.
- Either preserve stable GPU slots across layout changes or perform an explicit migration/reset that removes old memberships before installing the new layout.
- Never update an allocation's `record_offset` while retaining membership bits created for a different record layout.
- Add tests for light insertion/removal, unordered-map rehash, changed record count, and hierarchy regeneration.

## P0: Add real allocation capacity and reclamation

`next_membership_word_pool_offset` and `next_footprint_pool_offset` grow monotonically. Erasing a map entry does not reclaim its GPU storage. The shader silently skips footprint writes beyond `footprint_capacity`, while membership writes have no equivalent bounds check.

Required implementation:

- Track actual buffer capacities in words/records, not magic constants.
- Check every allocation before publishing it.
- Implement a free list, generation-safe slot reuse, or bounded compaction/rebuild.
- Clear reused previous-membership words before assigning them to another key.
- Export allocation failures, high-water marks, reclaimed slots, and overflow counters.
- On overflow, return a visible failure; never silently drop footprints or write outside a UAV.

Remove the unsupported `modulo_slot_recycling` claim unless modulo recycling is actually implemented and collision-safe.

## P1: Correct transition semantics and compaction

The GPU only emits transitions on aggregate `0 -> 1` and `1 -> 0`. It does not emit records for `1 -> 2` or `2 -> 1`, so the CPU-side `b0_record_blocker_counts` map cannot truthfully represent the aggregate blocker count by incrementing/decrementing only on transitions. Decide whether the CPU mirror stores binary visibility or an actual count and name/update it accordingly.

`CSCompactB0Transitions` writes valid records back at `tr_idx` without a separate compact output counter. If stale generations are ever present, holes remain while the CPU still reads the first `transition_count` records. Implement a real append counter or guarantee and assert that all input transitions have the current generation.

Add explicit counters for transition-buffer overflow. `g_transition_counter` may exceed 65,536 even though writes are dropped; a truncated readback is not sufficient evidence of correctness.

## P1: Stop calling AABB proxy tests “exact visibility”

Both J3 and K5 use `SegmentIntersectsAABB`. This is an exact segment/AABB test, but it is not exact surface or triangle visibility and can produce false blockers for sparse, concave, or articulated geometry.

Choose and document one policy:

- bounds-authoritative mode: AABBs intentionally define occlusion; label results `AABB_PROXY_VISIBILITY`; or
- broadphase-plus-refinement mode: compact ambiguous AABB candidates and run inline DXR `RayQuery` against the appropriate TLAS/BLAS for final visibility.

Do not export `exact_visibility`, `exact_shadowing`, or `self_occlusion_valid` unless triangle-level ground truth was actually evaluated and compared.

## P1: Make failure behavior transactional

The engine ignores return values from the static upload and dynamic input upload APIs. If J or K dispatch fails, execution continues. K then copies value-initialized output probes back into the receiver state, potentially turning the receiver black while merely setting `gpu_dispatch_failed`.

Required implementation:

- Check every upload/update/dispatch return value.
- On failure, preserve the last known valid GPU receiver and B0 state.
- Return a visible error without entering the CPU reference path.
- Do not partially apply J transitions if K fails when the update is intended to be atomic.
- Add failure injection at each stage, not only the global status gate.

## P1: Reduce CPU packing, synchronization, and readback

The current path executes GPU kernels but is not yet a low-overhead persistent GPU runtime. Every update can:

- enumerate every light on the CPU;
- rebuild and upload all frames, ranges, B0 records, hit positions, and BVH nodes;
- repack all transforms, bounds, clusters, and probes;
- dispatch K separately for every receiver group;
- call `WaitForGPU()` for J and again for each K dispatch;
- read back all transitions, telemetry, and receiver probes.

Required production direction:

1. Upload static B0/light/hierarchy data only when its generation changes.
2. Keep receiver probes, memberships, contributions, and final irradiance resident on the GPU.
3. Upload compact dirty transforms, dirty bounds, and changed light records only.
4. Batch all dirty receiver groups into one or a small bounded number of K dispatches.
5. Consume transition and irradiance buffers directly in downstream GPU work.
6. Make telemetry/readback diagnostic-only and asynchronous.
7. Remove per-dispatch `WaitForGPU()` from the production frame path; use fences without blocking the CPU.

Measure CPU preparation, upload, submission, wait, readback, and GPU stages separately before claiming CPU-overhead reduction.

## P1: Fix K6 scaling

`CSAccumulateReceiverIrradiance` loops over the complete work queue for every probe, producing `O(probes * emitted_work)` behavior.

Replace it with one of:

- sorted/compacted per-probe contribution ranges;
- atomic accumulation into per-probe buffers with a defined floating-point reproducibility policy; or
- segmented parallel reduction.

Validate numerical equivalence, multi-light superposition, deterministic bounds, and scaling up to the supported maximum. The current hard-coded scaling CSV is not evidence.

## P1: Enable and verify the D3D12 debug layer truthfully

The code queries `ID3D12InfoQueue`, but it does not enable the D3D12 debug layer before device creation. A null info queue currently returns zero errors, while the exporter unconditionally states `d3d12_debug_layer_active=true`.

Required implementation:

- In validation builds, call `D3D12GetDebugInterface`, enable the debug layer before device creation, and record whether that succeeded.
- Clear the info queue before the scoped workload.
- Count warning/error/corruption severities separately after GPU completion.
- Treat an unavailable debug layer as `NOT_ACTIVE`, not zero errors.
- Export actual activation state, message counts, and relevant message text/IDs.

## Required benchmark and evidence workflow

After implementation:

1. Commit source and tests only.
2. Clean-build the DLL and diagnostics with that exact commit SHA embedded.
3. Run on the hardware GPU with the debug layer active for correctness validation.
4. Run correctness tests with nonzero GPU work and per-test telemetry reset/scope.
5. Run real J and K workload sweeps; collect timestamp queries for each actual dispatch.
6. Run CPU preparation/end-to-end timers separately.
7. Compare outputs against independent CPU/analytic or triangle-level ground truth as applicable.
8. Export artifacts from captured result structs. No literal benchmark rows or success booleans.
9. Run a contradiction detector that fails on:
   - PASS with zero required work;
   - claimed exact visibility with only AABB evidence;
   - claimed debug validation when the layer was inactive;
   - nonzero timing for an undispatched/untimestamped stage;
   - empty source/build/binary identity on hardware evidence;
   - overflow with PASS;
   - a result file value not traceable to a measured or derived field.
10. Seal artifacts in a second commit and state both the source commit and evidence commit.

## Mandatory acceptance matrix

Do not mark Parts J/K complete until all rows pass with real observations:

| Area | Mandatory proof |
|---|---|
| Part J persistence | Nonzero membership survives unchanged frames without duplicate deltas |
| Multi-blocker | GPU count follows `0,1,2,1,0`; visibility changes only at aggregate boundaries |
| Removal | Disable, unregister, empty bounds, mode change, and light removal clear GPU state |
| Sparse/reordered lights | Stable actual light IDs and stable record ownership after insertion/removal/rehash |
| Capacity | Boundary and overflow workloads fail visibly with zero out-of-bounds writes |
| Noncontiguous clusters | GPU consumes the declared index list exactly |
| Bone transforms | Position, AABB, and normal output match independent references |
| K4/K5 | Positive emitted work equals consumed work; indirect arguments match |
| Visibility labeling | AABB proxy and triangle-refined results are never conflated |
| Failure injection | Last valid state survives every failed upload/dispatch stage; CPU fallback remains zero |
| GPU residency | Production path does not synchronously read back probes/transitions every update |
| Evidence | Every field has measured/derived provenance and exact build identity |

## Explicitly forbidden shortcuts

- Do not set a test result to `true` merely because a dispatch returned without crashing.
- Do not use vector size as proof that GPU probes were transformed, lit, or self-occluded.
- Do not accept `0 emitted == 0 consumed` as proof of K4/K5 conservation.
- Do not write benchmark numbers as string literals.
- Do not publish booleans such as `debug_layer_active`, `exact_match`, or `no_fabrication` as constants.
- Do not call AABB intersection triangle-exact visibility.
- Do not silently clamp or drop work and still report PASS.
- Do not move the implementation back to CPU loops to make tests pass.
- Do not begin the proposed probe-pair contact-occlusion feature until this acceptance matrix is satisfied.

## Deliverable expected from the next AI

Return:

1. implementation commit SHA;
2. concise mapping from each issue above to changed functions/kernels;
3. tests with their actual nonzero GPU counters and numerical assertions;
4. measured CPU/GPU timing tables generated by the test run;
5. D3D12 debug-layer activation and scoped message counts;
6. evidence commit SHA;
7. an explicit list of anything still approximate, unmeasured, or unsupported.

Do not return another completion walkthrough based only on PASS labels. The evidence must demonstrate the named behavior from the same invocation that produced each result.
