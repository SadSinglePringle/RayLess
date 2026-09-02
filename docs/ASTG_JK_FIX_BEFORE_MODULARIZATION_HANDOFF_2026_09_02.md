# ASTG Parts J/K — Correctness Fixes Before Modularization

## Execution instructions for Gemini Flash 3.7

This handoff is intended for Gemini Flash 3.7. Follow the constrained execution process below. It is based on observed failures in this repository, not assumptions about undocumented model capabilities.

### Start here — read before changing anything

Your task is to FIX and VERIFY the six numbered issues below. Do not modularize the project. Do not commit, push, reset, or revert. Do not change the task into a review-only report.

Before editing:

1. Read this entire handoff and applicable repository instructions.
2. Inspect current git status. Treat the existing uncommitted changes as the baseline; HEAD does not contain all prior work.
3. Create `docs/ASTG_JK_FIX_PROGRESS.md` with six rows, one per numbered issue. Initial status is `NOT_STARTED` for every row. Include columns for implementation locations, test names, latest build result, latest test result, and remaining failures.
4. Read the current implementation and its callers. Locate symbols by name because the provided line numbers may have shifted.

### Work in small verified stages

Execute the stages in this exact order:

| Stage | Scope | Required exit evidence |
|---|---|---|
| A | Issues 1–2: allocator selection, safe slot acquisition, ring ownership | Relevant build succeeds; ring reuse/exhaustion and generation-correctness tests pass |
| B | Issue 3: GPU-only B1+ execution | Small/large workload and unavailable-index tests prove GPU execution or explicit failure; no CPU reference execution |
| C | Issue 4: transactional layout migration | Multi-group layout change and injected-failure tests pass against complete reference state |
| D | Issue 5: real lighting negative controls | Actual validator accepts correct output and rejects every corrupted result; numeric errors exported |
| E | Issue 6: final rebuild and evidence | All affected targets rebuilt and tested after final edits; source/binary identities verified |

Do not implement all stages at once. At the end of each stage, update the progress document with observed results and give a brief progress report. Continue to the next stage without requesting approval unless new authority or an external dependency is genuinely required.

If an earlier stage is broken by a later edit, mark its evidence `STALE` and rerun its affected checks. Never retain a PASS solely because it passed before the relevant source changed.

### Editing safeguards — mandatory

- Make small, context-matched patches. Do not use global string replacements across the runtime.
- In particular, never replace every occurrence of `slot.command_allocator` or `bone_count`; each occurrence has different ownership/count semantics that must be inspected.
- Before changing a count, identify the element type, allocated capacity, uploaded count, shader constant, dispatch count, and consuming loop. Write those relationships into the progress note.
- Before changing a GPU resource or allocator, identify its owning slot, valid states, last-use fence, and all callers that reset/write it.
- After each logical patch, inspect its diff for unintended edits, self-assignment, ignored return values, renamed-but-unchanged behavior, or stale callers.
- Do not remove a wait until resource ownership proves it safe. Do not restore a hidden wait merely to make a GPU crash disappear and then claim the path is nonblocking.
- Do not fix a compile error by discarding an earlier uncommitted change. Repair the declaration, definition, and call sites consistently.
- Check API return values before advancing state. A failed upload, clear, reset, recording, or submission cannot be followed by success publication.

### Test safeguards — mandatory

- Add a regression scenario for the defect before or alongside its implementation fix. Where safe, show that the old behavior fails it. Never intentionally submit commands already known to violate GPU lifetime rules; detect those conditions with assertions/test hooks first.
- Tests must exercise the default production path, not only synchronous compatibility mode.
- A test name is not evidence. Identify the exact observed values and assertion that prove the requirement.
- An unavailable feature is `UNAVAILABLE` or `NOT_MEASURED`, not PASS.
- Skipped tests and early-exit/partial runs cannot produce a full-suite PASS artifact.
- Negative controls must call the same validator as the real lighting test. Comparing unrelated constants is prohibited.
- Do not lower scene complexity, increase tolerance, remove assertions, or hard-code output to turn failures green without explicitly documenting and justifying a requirements-preserving correction.
- If compilation fails, stop that test sequence. Do not execute an old executable and report its success as verification of the failed build.

### Failure handling and continuation

If a test fails, keep its failing evidence, isolate the smallest relevant scene, identify the cause, patch it, rebuild, and rerun. A failure is not permission to omit the requirement.

If context becomes limited, first update `docs/ASTG_JK_FIX_PROGRESS.md` with the exact current stage, source changes, last executed command/result, active process identifier if any, and next action. Resume from current files and live process state; do not restart or rerun completed work without reason.

If genuinely blocked, state the exact blocker, evidence, safe alternatives already tried, and what user action is needed. Mark unfinished requirements incomplete. Do not invent successful results.

### Final response contract

Return these sections:

1. `Implemented`: actual changes grouped by issue number.
2. `Verified`: build/test commands, exit results, test counts, source identity, and binary hashes.
3. `Requirement matrix`: each issue PASS, FAIL, or INCOMPLETE with its proving test/artifact.
4. `Limitations and remaining work`: include unavailable validation and unauthorized clean-commit sealing.
5. `Repository actions`: explicitly confirm whether commits/pushes occurred; neither is authorized here.

Use “all six issues fixed” only if every required implementation behavior and acceptance check is proven. Do not infer completion from legacy diagnostics or a summary generated earlier.

## Objective

Fix the current GPU resource-safety, B1+ execution, layout-migration, and validation defects. Establish trustworthy post-fix hardware evidence. Do not begin a broad modularization or rewrite in this pass; that is the next phase after these fixes are verified.

Work from the current uncommitted working tree in `C:\Users\Brand\Documents\hermes\Rayless`, not a fresh copy of HEAD. HEAD is `b7493e1`, but the current source contains substantial later work. Preserve existing implementation improvements and unrelated user files. Do not reset, revert, commit, or push without explicit authorization.

Read applicable repository instructions and inspect current source before editing. The line references below refer to the reviewed tree and may shift.

## Non-negotiable constraints

- Keep continuous light-relative angular selection for B0. Do not restore a coarse angular mask or B0 DAG-edge scanning.
- Keep production per-edge/per-probe visibility and lighting work on the GPU. CPU math is allowed as an independent test reference, not a silent runtime fallback.
- Do not weaken tests, change expected lighting to match broken output, bypass failing passes, or call legacy-suite success production verification.
- Do not claim that unavailable D3D12 debug validation passed merely because an error counter is zero.
- Make only the structural changes necessary to fix these defects. Defer general file/class/module extraction until the next phase.

## 1. P1 — Correct frame-slot command allocator ownership

### Confirmed defect

`sync_active_frame_slot_pointers()` in `src/rtx/rtx_raytracer.cpp:330` contains:

`g_rtx.command_allocator = g_rtx.command_allocator;`

This is a no-op. Upload buffers rotate to the selected slot, but the active allocator does not. The recorder can reset an allocator whose previous commands are still executing.

### Required fix

1. Select the allocator owned by the acquired frame slot, not the previously active allocator.
2. Record the owning slot and last submission fence for each allocator.
3. Reset an allocator only after its own fence is complete. Check and propagate HRESULT failures from allocator reset, command-list reset/close, and queue signal.
4. Audit every J/K helper that resets the shared allocator, including clear, migration, readback, telemetry, and compatibility paths. Fix any path that can reset a production allocator still in flight.
5. Ensure resource retirement and shutdown obey the same ownership rules; do not null/reset the wrong slot through shared aliases.

### Acceptance

- Record allocator identity, slot index, owning fence, and completed fence for each submission.
- Submit enough back-to-back frames to use and wrap every slot repeatedly.
- Require different slots to use their own allocator and assert no reset occurs before the allocator's owning fence completes.
- Exercise mixed diagnostic/production calls with pending work.
- Use debug-layer validation if available; otherwise report its absence and retain runtime ownership assertions plus hardware evidence.

## 2. P1 — Acquire a safe slot before any upload write

### Confirmed defect

The engine uploads J/K input buffers before invoking the recorder. The slot-fence check occurs inside `record_parts_jk_commands()` near `rtx_raytracer.cpp:3414`. On ring wraparound, upload memory can already be overwritten before that check waits for safety.

### Required fix

Implement this sequence explicitly:

1. Acquire a free frame slot using its completed fence.
2. If unavailable, report explicit deferred/backpressure status without overwriting anything. If a blocking compatibility policy is offered, make it separate, explicit, and measured.
3. Only after successful acquisition: select that slot's allocator, constants, and upload resources; write J/K inputs.
4. Record commands using exactly that slot's resources.
5. Submit and associate the signaled fence with the same slot.
6. Retain ownership until that fence completes.

Do not rely on a global pointer change after submission as proof that the next slot is safe. Make acquired/reserved/submitted slot state explicit. On validation or recording failure, release only an unsubmitted reservation; do not clear dirty state or publish a successful frame.

### Acceptance

- Run substantially more consecutive dirty frames than the ring capacity, with distinct per-frame transforms and lighting inputs.
- Exercise slot exhaustion deterministically through a test hook or controlled GPU workload. Verify no writes occur to occupied slots.
- Verify the documented backpressure result and that dirty work is retained for retry.
- Compare completed frame outputs to their own input generations to detect cross-frame upload corruption.
- Count waits, maps, readbacks, and submissions across the entire engine invocation, not just the recorder.
- A two-frame test with a two-slot ring is insufficient: it never tests reuse.

## 3. P1 — Remove CPU B1+ production fallback

### Confirmed defect

`evaluate_and_apply_b1_plus_edges()` in `src/astg/astg_transport_engine.h` selects GPU visibility only when reference counts and discovery conditions satisfy its heuristic (near 3802). Otherwise it performs CPU spatial queries and per-edge segment/AABB loops (near 3873).

This contradicts the requested GPU-first production path, especially for small workloads below 256 references and when GPU discovery cannot be used.

### Required fix

1. In GPU production mode, all B1+ candidate filtering and exact visibility must execute on GPU for small and large workloads.
2. Small workloads may use a simpler GPU dispatch; do not send them to the CPU because dispatch overhead is inconvenient.
3. If an acceleration structure/index is unavailable, use an explicitly supported GPU alternative or return a visible failure/deferred status. Do not enter the CPU reference path.
4. Preserve multi-blocker semantics and dependent transport invalidation/restoration.
5. Integrate the B1+ work with safe frame resource ownership and GPU output consumers. Avoid a blocking per-group readback loop as the production implementation.
6. Instrument executed CPU reference work and GPU B1+ work separately. Configuration flags alone are not execution evidence.

### Acceptance

- Test fewer than 256 references, the threshold boundary, and a larger workload.
- Test index unavailable/invalid and allocation failure conditions; require supported GPU recovery or explicit failure, never CPU execution.
- Occlude a B1+ segment without occluding B0 and verify downstream lighting changes in combined mode.
- Use overlapping blockers and remove them one at a time; restoration occurs only when the last blocker leaves.
- Exercise both combined B0/B1+ and all-bounce edge modes with actual dependent-path/energy assertions.
- Require zero production CPU reference tests and observed GPU B1+ execution for every supported case.

## 4. P1 — Make layout migration transactional and rebuild all affected groups

### Confirmed defects

- `ensure_resident_gpu_static_layout()` ignores the result of `remove_single_group_light_dynamic_occlusion_gpu()` near `astg_transport_engine.h:3422`.
- `update_dynamic_occlusions_frame_async()` captures `frame_dirty_groups` before layout migration marks all dependent groups dirty (near 3499–3506).

Failure can be ignored while new layout resources are installed. Previously clean groups can be removed from the old layout without being rebuilt in the same frame.

### Required fix

1. Preflight replacement layout resources, capacities, uploads, and all affected allocations before destructive changes to active state.
2. Determine all layout-dependent groups before taking the immutable frame work snapshot.
3. Remove old memberships against the old layout or migrate them using stable record IDs. Do not interpret old offsets against new records.
4. Check every removal/clear/upload/recording result. Do not install or publish a new layout if a required step failed.
5. Avoid partial multi-group commits: use staged/double-buffered state or another explicit transaction mechanism that keeps the last complete layout valid until the replacement is ready.
6. Order GPU removal, migration, and additions with real pass dependencies. CPU ordering of work items in a parallel dispatch is not execution ordering.
7. Publish layout generation, allocation maps, dirty-state consumption, and persistent visibility consistently after successful submission under the chosen transaction contract.
8. Handle GPU/device failure distinctly from recoverable preparation failure; never report a failed frame as successful.

### Acceptance

- Multiple groups with active memberships, only one initially dirty; insert/remove a light and reorder same-count B0 records.
- Compare all affected groups to a clean reconstruction by stable record ID: memberships, blocker counts, visibility, transitions, and dependent lighting.
- Assert no frame exposes a partially removed/rebuilt layout.
- Inject failure on the first and a later group's removal, and on replacement allocation/clear/upload/recording.
- Verify unchanged published state for recoverable preparation failures, correct dirty-state retention, and successful retry.
- Verify failure outcomes and retired-range lifetime on both immediate compatibility and default frame paths.

## 5. P1 — Make the negative control exercise the real lighting oracle

### Confirmed defect

`test_negative_control_oracle_sensitivity()` in `src/diagnostics/astg_parts_jk_acceptance_main.cpp:565` compares hard-coded `0.0` and `1.85`. That proves only that `nearf()` distinguishes two constants. It does not prove the scene's lighting oracle rejects broken GPU results.

The 256-probe test is a legitimate improvement and must be preserved. However, its `0.05 * expected` tolerance is 5% relative error, not evidence of agreement to six decimal places. It also skips the boundary band; describe that scope honestly.

### Required fix

1. Extract the actual scene-result validator into a shared test helper used by the real test and negative control.
2. Obtain actual GPU output for the deterministic lit/occluded scene.
3. Require the unmodified output to pass that validator.
4. Copy the output and deliberately corrupt it: all-black output, nonzero light on a known shadowed probe, and out-of-tolerance irradiance on a known lit probe.
5. Require each corrupted copy to fail the same validator. Report expected failure as negative-control success without contaminating the actual GPU results.
6. Keep all mutation hooks confined to diagnostic code; do not introduce a production bypass.
7. Export actual maximum absolute/relative errors, sample counts, mismatch counts, tolerances, and excluded boundary cases. Do not infer precision from printed decimal formatting.

### Acceptance

- At least 256 probes and noncontiguous clusters.
- Both known-lit and known-shadowed populations must be nonempty and numerically verified.
- The real validator rejects each deliberately corrupted result.
- A stationary receiver responds to a moving third-party blocker over multiple frames.
- Unequal transform/bound counts, including more than 64 bounds, remain covered.
- Independent oracle math must not call the production visibility implementation.

## 6. P1 — Rebuild and identify the final source actually tested

### Confirmed evidence gap

The reviewed engine header was modified at 09:21:27, while the acceptance and E2E executables were built at 09:17:43 and 09:18:30. Running those binaries again does not validate the newer header changes.

### Required workflow

1. Finish source and test edits.
2. Rebuild the RTX DLL/shaders, acceptance executable, diagnostics executable, and E2E executable using their checked-in build targets.
3. Ensure all build failures propagate and stop the run. Do not run a stale binary after a failed build.
4. Run the complete hardware acceptance suite, diagnostics, and E2E using the rebuilt binaries.
5. If any relevant source changes afterward, rebuild all affected consumers and rerun their tests.
6. Preserve deterministic source-input hashes and per-binary SHA-256 identities. Record `UNCOMMITTED_WORKTREE`, base commit, dirty state, GPU identity, and final test results honestly.
7. Source identity must describe the inputs used to build each binary, not merely files found at test runtime. Verify build-time and final input digests agree.
8. Report unavailable debug-layer validation separately from runtime/DRED observations.
9. Run `git diff --check` and JSON parsing checks after regeneration.

Do not clean-commit seal or push in this pass. Those operations remain unauthorized. Do not erase the uncommitted status to make the evidence look complete.

## Implementation order

1. Fix allocator selection and safe slot acquisition before upload.
2. Add ring-wraparound/resource-lifetime tests and prove them on hardware.
3. Fix GPU-only B1+ execution and layout transactions.
4. Strengthen lighting negative controls and numeric reporting.
5. Perform the final rebuild/test/provenance workflow.
6. Conduct a requirement-by-requirement review before reporting completion.

## Completion gate before modularization

Do not start the general modularization phase until all of the following are supported by inspected source and hardware evidence:

- Each frame slot owns the allocator and uploads actually used by its submission.
- No in-flight upload overwrite or allocator reset occurs during ring wraparound.
- Backpressure/failure preserves retryable dirty work and is reported explicitly.
- B1+ production never silently enters CPU reference execution.
- Layout changes do not publish partial or wrong-record state, including failures involving previously clean groups.
- The actual lighting validator rejects corrupted GPU-result copies.
- Precision/tolerance claims match recorded numeric errors.
- All affected executables were rebuilt after their final relevant source edits.

The final report must contain: changed files, exact build/test commands and outcomes, a requirement-to-test evidence matrix, measured limitations, remaining defects, and confirmation of no commit/push. Passing old tests or adding API names alone is not completion.

After this gate passes, hand the verified baseline back to the user. Plan modularization as a separate follow-up, not as a substitute for these fixes.
