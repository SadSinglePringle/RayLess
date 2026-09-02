# ASTG Parts J/K — Post-`f7a17a4` Corrective Handoff

## Scope and baseline

Continue from `origin/main` at `f7a17a4` and audit the implementation commits beginning at `44b1e30` through the evidence-sealing commit. Preserve the GPU-first architecture. Do not replace GPU work with CPU reference calculations, CPU mirrors, fabricated constants, or inferred pass results.

The latest pass made real progress: the D3D12 debug layer is enabled before device creation, Part J has a compact counter, Part K uses packed cluster membership, bone-local bounds are uploaded, normals use an inverse-transpose transform, K5 uses atomic accumulation, and the scaling sweeps now issue GPU dispatches. Those items should remain.

The work is not complete. The current report overstates allocator safety, removal correctness, test coverage, debug-layer evidence, and production overhead. Complete the requirements below before describing Parts J/K as hardened or fully empirical.

## P0 — Make Part J allocation transactional and failure-visible

Current problem: `ASTGTransportEngine::update_group_dynamic_occlusion_gpu()` calls `allocate_membership_words()` and `allocate_footprints()` without checking their boolean results. A failed allocation leaves zero-initialized offsets that can alias another group/light allocation. Existing storage is also freed before replacement storage is known to be available.

Required implementation:

1. Allocate both replacement ranges into temporary variables.
2. Check both return values.
3. If the second allocation fails, release the first temporary allocation.
4. Do not modify or free the active allocation until both replacements are secured and initialized.
5. Return a visible failure from the update path and set a specific execution/error status. Do not silently skip, clamp, wrap, or publish offset zero.
6. Validate `offset + count` against the actual GPU buffer capacity before every upload and dispatch.
7. Add allocation high-water, failed-allocation, and live-word/live-footprint counters.

Required tests:

- Exhaust membership storage while existing allocations are active; verify the call fails and no live allocation changes.
- Exhaust footprint storage; verify rollback of any temporary membership allocation.
- Free a middle range, reuse it, and prove it does not alias neighboring live allocations.
- Exercise maximum legal capacity and one element beyond capacity.

## P0 — Correct deletion, invalidation, and slot reuse

Current problem: `remove_group_dynamic_occlusion_gpu()` ignores the return from `rtx_update_part_j_dynamic_inputs()` and frees persistent ranges even when upload or dispatch fails. `invalidate_light_hierarchy()` frees allocations directly without applying GPU removal deltas. Reused membership ranges are not guaranteed to be cleared. These paths can leave stale `previous_membership` bits or persistent blocker counts.

Required implementation:

1. Make removal a transaction: upload removal inputs, dispatch J4/J5, wait only as required by the current synchronization design, verify successful completion, then free the allocation.
2. On upload or dispatch failure, retain the allocation and report failure. Never free storage whose blocker removal was not committed.
3. Route light-hierarchy invalidation through the same removal transaction for every affected group/light allocation.
4. Explicitly zero newly allocated membership words and footprint records before first use. Do not assume a free-list range contains zeros.
5. Ensure persistent blocker counters reach the correct value when a group is deleted, a light is removed, a light hierarchy is rebuilt, or an allocation changes size.
6. Prevent double-removal and make repeated removal idempotent.

Required tests:

- Create actual GPU membership `1`, remove the group, and verify previous membership, persistent count, and generated transition.
- Inject upload failure and dispatch failure; verify storage remains live and state remains retryable.
- Invalidate a light with active blockers and verify all contributions are removed.
- Reuse the exact freed slot for a different group/light and verify its initial membership is zero.
- Remove the same group twice and verify no underflow and no second transition.

## P0 — Stabilize record identity across B0 layout changes

Current problem: persistent membership is retained by group/light allocation, while `record_offset` is recomputed from the current flattened B0 record order. If light order or B0 record layout changes without changing word count, old membership bits can refer to different records.

Required implementation:

- Introduce an explicit layout generation/key covering light identity and the ordered stable identities of its B0 records.
- Reuse membership only when the layout key matches exactly.
- When the key changes, remove old memberships against the old layout before freeing them, then allocate/clear state for the new layout. Alternatively implement a validated stable-ID migration on GPU.
- Do not use unordered-container iteration order as persistent record identity.

Required tests:

- Insert and remove unrelated lights so flattened offsets change while the target light remains.
- Reorder B0 records with the same record count.
- Change record count and hierarchy generation.
- In every case compare blocker counts and visibility against a clean rebuild and require exact agreement.

## P0 — Replace weak or vacuous diagnostics

The following tests/exports currently claim more than they measure:

- Underflow prevention does not force a removal from a known membership bit of `1` and does not require the underflow telemetry result it describes.
- Rotating-bone validation compares position but exports normals as valid from the same boolean.
- The dense 192+ probe test checks only nonzero work, yet exports no clamping and valid self-occlusion.
- Sparse-light testing checks allocation bookkeeping rather than proving GPU outputs preserve actual sparse light IDs.
- Order invariance relies on CPU-side aggregate state and may share global GPU state between engine instances.
- Exported values such as `diff_count`, `clean_decrements`, `actual_light_id_preserved`, `clamping_detected`, and `self_occlusion_valid` are literals or aliases rather than directly recorded observations.

Required implementation and tests:

1. Give each test isolated, reset GPU state.
2. Underflow: establish membership `1`, remove once, verify count becomes zero; attempt a second removal and verify the counter stays zero and the underflow guard/error telemetry changes exactly as specified.
3. Rotating bones: read back and compare both position and normal against independent double-precision CPU reference math for multiple nontrivial yaw/pitch/roll transforms.
4. Nonuniform scale/shear: compare GPU normal numerically against an independently computed inverse-transpose result, not only one orthogonality dot product.
5. Dense probes: require transformed count equals requested count, emitted equals expected visible work, consumed equals emitted, no overflow/truncation counters fire, and readback values match a reference scene.
6. Sparse lights: prove GPU transition/output records carry light IDs `17`, `203`, and `401`, not merely that allocator keys exist.
7. Order invariance: compare complete GPU-visible memberships, persistent counts, and transitions for both orderings after a hard reset between runs.
8. Export actual recorded values. No literal success booleans or zero counts may appear unless the test recorded that exact value.

## P1 — Fix D3D12 debug-message accounting

Current problem: `rtx_get_d3d12_debug_status()` assigns `GetNumStoredMessagesAllowedByRetrievalFilter()` to `error_count`. That API returns the total number of stored messages allowed by the filter, not the number of error-severity messages. Warning and corruption counts remain hard-coded zero.

Required implementation:

- Clear the info queue immediately before the scoped Parts J/K run.
- Iterate stored messages with `ID3D12InfoQueue::GetMessage` and classify each by severity.
- Export separate corruption, error, warning, and informational counts.
- Preserve message IDs/descriptions for warnings and above in a diagnostic artifact.
- Fail validation on corruption or error. State the explicit policy for warnings; do not report them as zero without counting them.
- Record whether GPU-based validation was enabled separately from the ordinary debug layer.

## P1 — Bound and normalize all Part K buffer layouts

Required fixes:

1. Add capacity checks before every mapped upload copy for transforms, bounds, clusters, packed indices, and probes.
2. Pass and retain the actual packed-index count. Copy only that many indices during dispatch; do not copy the fixed 131,072-entry capacity whenever any cluster exists.
3. Standardize irradiance accumulation stride. The current buffer allocation/clear uses 16 bytes per probe while HLSL addresses 12 bytes per probe. Choose one documented layout and use it consistently in allocation, clear, atomic write, and readback.
4. In K3, divide a cluster center by the number of valid member indices, not the declared count when invalid indices are encountered. Prefer failing the dispatch/input validation rather than producing a biased center.
5. Validate every cluster `(index_offset, index_count)` range against the uploaded packed-index count.

Required tests:

- Exact-capacity and capacity-plus-one tests for each input buffer.
- Invalid cluster member and out-of-range packed span tests.
- A noncontiguous cluster reference test that checks transformed center and final irradiance numerically.

## P1 — Finish the GPU-first production dataflow

Current production flow still rebuilds and uploads broad static/static-like arrays, dispatches Part K per receiver group, performs unconditional `WaitForGPU()`, and reads back full result ranges. Passing GPU kernels alone does not establish low CPU overhead.

Required architecture:

- Keep light frames, B0 ranges/records/BVH data, transforms, bounds, clusters, and probe data resident on the GPU.
- Upload only dirty spans or append/remove command records.
- Batch all dirty Part J group/light pairs into shared dispatches.
- Batch Part K receiver groups into shared dispatches using offsets/counts; no one-dispatch-per-group production loop.
- Keep J outputs feeding K inputs on GPU. CPU readback is diagnostic-only and must be disabled in production mode.
- Replace unconditional waits with fences and frame-latency-safe resource ownership. Do not add hidden CPU reference work.

Evidence required:

- Independently measure CPU preparation, upload, submission, synchronization/wait, and readback phases.
- Provide scaling sweeps with fixed hardware/build identity and meaningful workloads.
- Show CPU time versus dirty group count, light count, B0 record count, receiver count, and probe count.
- A numeric zero is not an acceptable substitute for an unmeasured timing. Emit `null`/omit it and mark provenance `NOT_MEASURED`.

## Evidence and clean-commit rules

1. First commit all source and test changes.
2. Build from a clean checkout of that exact source commit with the commit SHA embedded in every tested binary.
3. Run diagnostics and E2E tests on the target RTX GPU.
4. Seal results in a separate evidence commit.
5. Every artifact must include run UUID, source commit, build commit, binary SHA-256, device/driver identity, workload description, and per-field provenance.
6. Separate `MEASURED_CPU_TIMER`, `MEASURED_GPU_TIMESTAMP`, `MEASURED_COUNTER`, `DERIVED_FORMULA`, `CONFIG_VALUE`, and `NOT_MEASURED` fields.
7. Derived fields must contain their formula and source fields. Configuration values must not be described as measured.
8. Do not claim all exported results are empirical: identities, configurations, derived rates, and not-measured fields are not direct measurements.

## Definition of done

Parts J/K are ready for another hardening review only when:

- allocation failure cannot publish aliases or destroy active state;
- deletion/invalidation/reuse is transactional and proven with GPU-visible state;
- B0 membership survives or correctly resets across layout changes;
- every claimed test property is directly asserted from observed output;
- D3D12 severities are actually classified;
- all input ranges are capacity checked;
- packed cluster copies and accumulation layouts use exact documented sizes;
- production batches work, retains data on GPU, avoids normal-path readback, and has measured CPU overhead;
- the evidence is generated from the exact clean source commit and contains no fabricated or hard-coded success fields.

Do not add new feature scope until these correctness and evidence conditions pass.
