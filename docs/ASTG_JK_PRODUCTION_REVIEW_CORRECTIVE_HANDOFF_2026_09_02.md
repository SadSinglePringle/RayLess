# ASTG Parts J/K — Production Review Corrective Handoff

## Objective and baseline

Finish the GPU-first moving-object occlusion and bone-anchored receiver implementation without regressing B1+ transport, receiver updates, persistent state, or evidence accuracy.

Start from the current working tree in `C:\Users\Brand\Documents\hermes\Rayless`. HEAD is `b7493e1`, but substantial implementation, build, test, shader, and evidence changes are UNCOMMITTED. Those changes are part of the baseline. Do not reset them, overwrite them with HEAD, delete unrelated untracked files, commit, or push without explicit user authorization.

Read this document and inspect the actual source before implementing. Line references below identify the reviewed working tree and will move as edits are made.

The preceding completion claim is withdrawn: 12/12 acceptance tests and 101/101 E2E tests did not prove the full production behavior. Preserve legitimate improvements, but do not use those counts as evidence that the issues below are resolved.

## Architectural intent — do not reinterpret

- Part J uses continuous source-relative bounding-box angular footprints to query stored B0 directions, followed by exact visibility checks, membership deltas, and aggregate blocker transitions.
- Do not replace this with a coarse angular mask or B0 DAG-edge scanning.
- Part K lights surface probes anchored to rigid objects or bones, using the bound/cluster hierarchy to reduce work.
- Moving occluders must affect receivers even when those receivers did not move.
- B1+ occlusion and dependent transport updates remain required in modes that enable them.
- Production projection, candidate traversal, visibility, membership updates, receiver evaluation, and affected transport updates must remain GPU work. CPU reference implementations are permitted only as independent test oracles.
- CPU orchestration may manage handles, versions, dirty notifications, bounded upload spans, and command submission. Do not move per-ray, per-probe, or per-edge production work back onto the CPU to make tests pass.

## 1. P1 — Separate transform count from bound count

### Confirmed defect

The frame call in `src/astg/astg_transport_engine.h:3716` passes `transforms.size()` to `rtx_dispatch_parts_jk_production_async()`. The recorder interprets that argument as the number of `ASTGBoneBoundGPU` entries to copy and transform (`src/rtx/rtx_raytracer.cpp:3482`, K1 dispatch near 3524). The constant buffer separately receives the actual bound count.

A rigid group may have one transform and many boxes; a skeleton may have many transforms but few boxes. These counts are not interchangeable.

### Required implementation

1. Rename ambiguous dispatch parameters to `bound_count` wherever they refer to bounds.
2. Supply `receiver_bounds.size()` to bound copy and K1 dispatch.
3. Retain a separate transform count for validating every bound/probe bone-transform index.
4. Make staging sizes, constant-buffer counts, dispatch dimensions, and resource capacities agree.
5. Validate pointer/count relationships and integer-safe spans before any upload or command recording. Failure must be visible and must not publish partial state.

### Acceptance tests

- One rigid transform with multiple distinct boxes; move a box beyond the first entry into/out of a light segment and verify the expected visibility change.
- More than 64 boxes with fewer than 64 transforms, so an incorrectly sized dispatch cannot accidentally cover the workload.
- Many skeletal transforms with only a few bounds.
- Read back all transformed bounds in diagnostic mode and compare against independent matrix/AABB reference math.
- Require exact requested/processed counts and no stale-tail or out-of-range accesses.

## 2. P1 — Dirty propagation must follow lighting dependencies

### Confirmed defect

The Part K frame loop skips receiver groups not present in `frame_dirty_groups` (`astg_transport_engine.h:3690`). That tracks local object mutations, not all lighting dependencies. Moving an occluder can change lighting on a stationary receiver, which the current loop does not schedule.

### Required implementation

1. Distinguish transform/bounds dirtiness, receiver-data dirtiness, source-light dirtiness, and visibility/irradiance invalidation.
2. Use changed occluder bounds and light influence to schedule affected receivers on GPU. Include previous and current influence regions so vacated shadows are removed.
3. Preserve valid results for unaffected receivers in stable GPU-resident storage. Do not overwrite another group's results by treating a compact dirty batch as the complete persistent receiver array.
4. Light movement, enablement, color/intensity changes, occluder removal, and topology/layout changes must invalidate the appropriate dependencies even if no receiver moves.
5. If a conservative all-receiver update is temporarily needed for correctness, explicitly label it as a performance limitation; do not claim selective invalidation is finished until the hierarchy-based dependency scheduling is implemented and measured.

### Acceptance tests

- A stationary receiver, stationary light, and moving third-party occluder: unblocked -> blocked -> unblocked, with numeric irradiance/visibility expectations.
- Repeat with occluder disable, deletion, and teleport.
- Change light intensity/color while receiver transforms remain unchanged.
- Two receivers, only one affected: verify the affected result changes and the unaffected stored result remains valid.
- Assert affected receiver scheduling and unchanged-receiver preservation, not only mutation flags or dispatch counts.

## 3. P1 — Restore B1+ transport behavior in the default frame path

### Confirmed defect

`update_all_dynamic_occlusions()` returns through the new J/K frame path (`astg_transport_engine.h:4819`). The old per-group function still contains DAG-edge visibility and dependent-path updates (around 4677). The new frame path does not execute those stages simply because its selected mode is named `ANGULAR_B0_DAG_B1_PLUS`.

### Required implementation

1. Write down and enforce the stage contract for each supported occlusion mode.
2. Integrate the required B1+ candidate filtering, exact visibility, changed-edge application, and dependent-path invalidation/repair into the frame GPU workflow.
3. Do not reintroduce blocking per-group calls to the old function as the production solution.
4. Preserve aggregate blocker semantics: removing one of multiple blockers must not reactivate still-blocked transport.
5. Connect GPU transition/receiver outputs to actual downstream transport/render consumers. A correct private GPU buffer is insufficient if consumers still read stale host state.

### Acceptance tests

- Occluder intersects a B1+ segment but not B0: combined mode must update the dependent lighting; B0-only mode must follow its documented distinct behavior.
- Multiple overlapping blockers on a B1+ edge; remove them one at a time.
- Verify dependent path/energy results and the consuming lighting output, not just candidate counts.
- Compare supported modes against independent reference scenes over several frames.

## 4. P1 — Migrate persistent state before replacing its record layout

### Confirmed defect

The frame uploads the new static layout at `astg_transport_engine.h:3484`, then later submits removal pairs with old `record_offset` values (around 3626). These offsets no longer necessarily address the same logical records. Only dirty groups are processed, leaving other groups potentially holding obsolete offsets.

### Required implementation

Choose and document one correct GPU strategy:

- Retain old-layout resources until old memberships are removed against those resources, then install/rebuild the new layout; or
- Use stable record identities with an explicit GPU migration/remapping operation.

In either case:

1. Treat a flattened-layout generation change as a dependency change for all allocations referencing that layout, not only locally dirty groups.
2. Migrate or remove membership, aggregate blocker counts, visibility state, transport-node identity, and associated receiver/dependent references consistently.
3. Sequence removal, migration, and additions with explicit pass/barrier dependencies. Prepending work items to one parallel dispatch does not guarantee removal-before-addition execution.
4. Preserve the old published state on preparation failures. Do not free in-flight ranges or silently lose live state on capacity/clear/upload failures.
5. Reclaim retired allocations only after the relevant GPU fence, without a forced CPU wait in the normal frame submission path.

### Acceptance tests

- Reorder same-count records with deliberately different visibility and transport-node identities.
- Insert/remove a light that shifts other lights' flattened offsets.
- Use multiple groups, with at least one locally clean group retaining active memberships during the layout change.
- Compare every record's stable identity, membership, blocker count, visibility, transition, and downstream lighting against a clean reconstruction.
- Inject allocation, clear, and upload failures during preparation; assert the old complete GPU state remains valid and retryable.

## 5. P2 — Remove hidden frame-to-frame CPU waits

### Confirmed defect

Every dirty frame calls `rtx_complete_parts_jk_production()` (`astg_transport_engine.h:3476`), which invokes `WaitForGPU()` while a submission is pending (`rtx_raytracer.cpp:3637`). The no-I/O test explicitly completes the previous frame before resetting counters (`astg_parts_jk_acceptance_main.cpp:272`), so it excludes the synchronization that occurs during normal consecutive frames.

### Required implementation

1. Use fence-owned frame resource slots for command allocators, upload buffers, mutable constants, and other resources that cannot be overwritten while in flight.
2. Poll completed fences without waiting and retire resources only when safe.
3. Define visible behavior for exhausted frame capacity: explicit backpressure/deferred submission or a separately reported exceptional wait. Do not silently count it as no-wait production.
4. Keep waits/maps/readbacks in explicit diagnostics or documented synchronization boundaries.
5. Instrument actual API operations, including helper functions and completion paths—not just the entry point under test.

### Acceptance tests

- Submit several consecutive dirty frames without manual completion between calls.
- Measure wait/readback/map counters across the entire engine invocation, including preparation and retirement.
- Exercise frame-slot exhaustion deliberately and verify the documented behavior.
- Confirm multi-frame output correctness and no allocator/upload reuse before fence completion.
- Report CPU submission timing independently of GPU wait and diagnostic readback.

## 6. P1 — Replace proxy acceptance checks with actual lighting oracles

### Confirmed deficiencies

In `src/diagnostics/astg_parts_jk_acceptance_main.cpp`:

- Tests 1–6 select immediate mode, so they do not validate the default frame allocator's rollback behavior.
- Neighbor-range checking compares allocation metadata and `blocker_count >= 1`, not exact neighboring GPU state.
- Layout comparison checks one blocker count and a layout hash, not complete per-record state.
- Blocking/async equivalence compares positions and one blocker count, not irradiance or visibility.
- Mixed rigid/skeletal comparison uses simple translations and does not check the complete bound/normal/lighting result.
- The dense visibility test uses 64 probes and checks positions, finite nonnegative irradiance, and work conservation. It has no expected irradiance or shadow reference; an all-black lighting result can satisfy those lighting checks.

### Required implementation

1. Exercise the real default production entry points. Compatibility-mode legacy tests remain useful regressions, but must not substitute for production tests.
2. Build deterministic scenes with independently computed expected segment/AABB visibility and radiometric values for the supported light/material model.
3. Include explicitly lit and explicitly occluded probes and assert the expected classifications and numeric values within documented tolerances.
4. Compare blocking and async outputs for all relevant fields: stable IDs, bounds, positions, normals, memberships, blocker counts, transitions, visibility, and irradiance.
5. Test at least 256 probes, noncontiguous clusters, unequal transform/bound counts, nonuniform transforms, multiple lights, and multiple frames.
6. Use a deliberate negative control: force zero irradiance or skip the visibility stage in a test-only mutation and demonstrate that the reference test fails. Never retain that mutation in production.
7. Record requested/processed counts, maximum errors, mismatch counts, and representative expected/actual values. Do not export only PASS strings and descriptive text.

## Evidence, build identity, and reporting

- Preserve honest `UNCOMMITTED_WORKTREE` provenance until a clean source commit is authorized.
- Generate source-input and binary hashes from the final inputs/binaries used by each test. Include the acceptance source and its build script.
- Rebuild after the last relevant edit, then run acceptance, diagnostics, and E2E. Older binaries passing do not validate newer source.
- Keep debug-layer availability distinct from zero error count. If unavailable, report debug validation as unavailable—not cleanly validated.
- Export unmeasured fields as null/NOT_MEASURED or the documented sentinel with provenance. Do not emit plausible timing zeros.
- Do not change assertions to match observed broken output or weaken required scenarios to obtain green results.
- Preserve a requirement-to-test matrix with exact source locations, test names, final run identities, and observed results.
- Clean-commit sealing and pushing remain unauthorized. Explicitly report that remaining workflow step instead of pretending a dirty run is clean-commit evidence.

## Required execution order

1. Fix count correctness and dependency invalidation.
2. Fix layout migration and default-path B1+ integration.
3. Fix frame-resource ownership and actual consecutive-frame synchronization behavior.
4. Strengthen production-path tests and independent lighting references.
5. Rebuild all affected targets, run the complete hardware suite, and inspect failures.
6. Audit final source and evidence against every requirement above.

## Definition of done

The implementation is ready for another review only when the default production path handles the specified multi-object, multi-light, multi-frame scenarios correctly; no bounds are skipped because transform counts differ; stationary affected receivers refresh; B1+ mode semantics survive; layout changes preserve logical state; normal frame submission has verified resource-safe nonblocking behavior; and independent numeric tests would reject incorrect/all-black lighting.

The final report must list completed requirements with evidence, remaining defects or environmental limits, exact builds/tests run, and all uncommitted artifacts. A green legacy suite alone is not completion.
