# ASTG Parts J/K — Post-`bc2055b` Review and Remaining-Work Handoff

## Verdict

Do not treat the `bc2055b` implementation or the `b7493e1` evidence seal as completion of the prior corrective handoff. Several requested fixes are real, but the production-path, transactional-safety, and evidence claims remain partially supported or overstated.

Preserve the legitimate improvements: exact Part K packed-index copy count, 16-byte irradiance stride, API capacity checks, valid cluster-member counting, D3D12 severity iteration, deterministic light sorting, build identity, and the expanded diagnostic scaffolding.

Fix the issues below before resealing evidence.

## P0 — Reallocation is still not transactional

In `ASTGTransportEngine::update_group_dynamic_occlusion_gpu()`, the `need_realloc` branch calls `remove_single_group_light_dynamic_occlusion_gpu()` before allocating replacement membership and footprint ranges. It also ignores the removal function's return value. If either replacement allocation fails, the previously active state has already been removed and freed.

Required correction:

1. Keep the old allocation live.
2. Allocate both replacement ranges into temporary storage.
3. Check both allocation results and both GPU clear results.
4. Initialize the replacement ranges.
5. Remove the old GPU membership using its old record layout.
6. Publish the replacement allocation only after all prior steps succeed.
7. Free the old ranges last.
8. On any failure, release only temporary ranges and retain the old allocation unchanged and retryable.

Add failure injection after every stage and prove the old allocation remains active.

## P0 — Footprint resizing can overrun another allocation

`need_realloc` checks membership word count, record offset, and layout hash, but not `footprint_count`. Later, the code assigns the new bound count to `alloc.footprint_count` without proving the existing footprint range has that capacity. Increasing a group's bounds can therefore write beyond its allocated footprint range and overlap another allocation.

Required correction:

- Include required footprint capacity in the reallocation decision.
- Store allocated capacity separately from current used count.
- Validate `footprint_offset + used_count <= allocated capacity <= GPU capacity` before upload and dispatch.
- Test growing and shrinking bounds with live allocations immediately before and after the target range.

## P0 — GPU clear failures are ignored

The update path ignores the return from `rtx_clear_part_j_membership_words()`. It does not clear newly allocated footprint records. The removal path also ignores the return from the membership clear before freeing the range.

Required correction:

- Check every clear call.
- Clear both membership and footprint replacement ranges before publication.
- Do not free or reuse a range if its required clear failed.
- Ensure the zero-upload resource is large enough for the requested byte copy. Chunk large clears or use a dedicated clear kernel; never copy more source bytes than the zero buffer contains.

## P0 — The claimed asynchronous production path is not integrated

The engine still calls `rtx_dispatch_part_j_gpu()` and `rtx_dispatch_part_k_gpu()`. Both functions execute `WaitForGPU()`, copy full result buffers to readback resources, and map CPU-visible results. The new `rtx_dispatch_parts_jk_production_async()` entry point is not called by the engine.

The asynchronous function itself is not yet equivalent to the validated J/K path. It omits or differs from required setup, including compact-counter clearing, exact packed cluster-index upload/copy, irradiance-accumulator clearing/binding, complete barriers/timestamps, and the validated root-parameter layout used by the normal Part K dispatch. Do not switch to it until equivalence tests pass.

Required correction:

1. Define one shared command-recording implementation used by blocking diagnostics and asynchronous production.
2. Production must record the complete J1–J5 and K1–K6 pipeline with the exact same resources and root bindings.
3. Production returns a fence/token and performs no wait, map, or readback.
4. Diagnostic mode may wait and read back after invoking the same core recorder.
5. Integrate the production function into the actual engine path.
6. Add an end-to-end test proving the engine selects it and that normal production produces zero readback/map calls.

## P0 — “Batching” is at the wrong level

Part K now aggregates receiver groups, but that aggregation is performed inside a single-group update method. Each call to update one group walks every dynamic group and can redispatch all receivers. Part J still creates pairs for only the currently updated group. This is not a frame-level batch of all dirty J and K work.

Required correction:

- Separate mutation/dirty marking from frame execution.
- Build one frame-level list of dirty Part J group/light pairs.
- Build one frame-level Part K receiver batch.
- Dispatch each pipeline once per frame or documented capacity chunk.
- Scatter or retain results only after the fence completes.
- Prove dispatch counts do not scale one-for-one with the number of calls to the per-group mutation API.

## P1 — Static-data residency still rebuilds CPU arrays

`last_uploaded_static_b0_hash` avoids some GPU uploads, but every update still constructs frames, ranges, flattened records, hit positions, and BVH-node vectors before comparing/uploading. Calling this fully resident or low-overhead is overstated.

Required correction:

- Maintain a versioned resident static layout.
- Rebuild flattened CPU staging data only when a light/B0 generation is dirty.
- Upload only changed spans where practical.
- Measure unchanged-frame preparation separately and demonstrate that work is not proportional to all lights/B0 records.

## P1 — CPU timing labels are inaccurate

`cpu_submission_ms` wraps a blocking dispatch that includes GPU execution, wait, readback copies, and mapping. `cpu_readback_ms` starts after the dispatch already performed the readback and measures only CPU transition processing. These are not independent submission and readback timings.

Required correction:

- Measure command preparation/recording, queue submission, fence wait, resource mapping/copy, and CPU result consumption independently.
- In asynchronous production, submission ends after queue signal.
- Mark unavailable phases `NOT_MEASURED`; do not use zero or a mislabeled enclosing interval.
- Update provenance files and CSV/JSON schemas accordingly.

## P1 — Harden batched skeletal inputs

The batched K upload passes one `is_skeletal` value derived from the group whose update method happened to run, even though the batch can contain mixed rigid and skeletal receiver groups. Confirm whether the shader consumes this flag. Remove it if redundant; otherwise make skeletal mode per group/probe/bound rather than global.

Add a mixed batch containing at least two rigid groups and two skeletal groups with different bone counts and transforms. Compare every probe and bound against an independent reference.

## P1 — Diagnostics still do not prove all exported claims

The newly added suites improve coverage but several are weaker than their descriptions:

- Allocator exhaustion directly exercises host allocator methods; it does not verify update-path rollback with an existing live GPU allocation.
- Middle-range reuse checks only that the offset is reused, not that neighboring GPU state is unmodified.
- Layout-shift Suite 14 primarily proves that two hashes differ; it does not prove old GPU memberships are removed against the old layout, replacement state is installed, or final visibility matches a clean rebuild.
- Sparse-light evidence must inspect actual GPU transition/output light IDs and counts, not only allocation maps.
- Dense-probe evidence exports `self_occlusion_valid` and `clamping_detected`; these must come from explicit numeric assertions, not work-count conservation alone.
- Underflow output says `underflow_errors_trapped: 0`. Clearly define whether zero means the guarded second removal was ignored or whether an attempted underflow should increment telemetry. Make the test and label agree.
- D3D12 severity validation should include a controlled message-classification unit test rather than only observing zero messages in one run.

Remove literal success fields from exporters. Export the observed values used by each assertion.

## Required acceptance tests

Before another completion claim, add and pass all of these:

1. Live allocation plus forced membership-allocation failure.
2. Live allocation plus forced footprint-allocation failure.
3. Forced membership-clear and footprint-clear failure.
4. Bound count growth/shrink with guarded neighboring ranges.
5. Layout reorder with unchanged record count, compared against clean rebuild.
6. Light insertion/removal shifting flattened offsets, compared against clean rebuild.
7. Engine-level production invocation proving no wait/readback/map.
8. Blocking diagnostic and asynchronous production result equivalence.
9. Frame-level batching with dispatch-count assertions.
10. Mixed rigid/skeletal multi-group receiver batch.
11. Dense-probe numerical visibility/self-occlusion reference.
12. Sparse actual-light-ID GPU output verification.

## Evidence workflow

- Commit source/tests first.
- Rebuild from that exact clean commit with embedded SHA.
- Run blocking diagnostic validation and a separate engine-level asynchronous production test.
- Seal evidence in a later commit.
- Report unsupported requirements as incomplete. Passing the existing suite is not sufficient when the suite does not exercise the required failure or production path.

## Definition of done

Completion requires a genuinely transactional allocation swap, safe footprint resizing, checked GPU initialization, actual engine integration of a complete nonblocking J/K recorder, frame-level dirty batching, accurate timing provenance, and tests that observe the claimed GPU state rather than proxy host bookkeeping.
