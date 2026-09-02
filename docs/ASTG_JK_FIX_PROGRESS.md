# ASTG Parts J/K Correctness Fix Progress

| Issue | Title | Status | Implementation Locations | Test Names | Latest Build Result | Latest Test Result | Remaining Failures |
|---|---|---|---|---|---|---|---|
| 1 | Correct frame-slot command allocator ownership | PASS | `src/rtx/rtx_raytracer.cpp`, `src/rtx/rtx_raytracer.h` | `test_frame_slot_allocator_ownership_and_fence_safety`, `test_async_no_io` | Clean build (MSVC 19.44) | PASS (distinct allocators verified, all resets occurred after completed fence) | None |
| 2 | Acquire a safe slot before any upload write | PASS | `src/rtx/rtx_raytracer.cpp`, `src/rtx/rtx_raytracer.h`, `src/astg/astg_transport_engine.h` | `test_slot_backpressure_and_ring_wraparound` | Clean build (MSVC 19.44) | PASS (16 frames wrapped ring safely, zero upload corruption, explicit backpressure) | None |
| 3 | Remove CPU B1+ production fallback | PASS | `src/astg/astg_transport_engine.h`, `src/rtx/rtx_raytracer.cpp` | `test_b1_plus_gpu_only_execution_and_multi_blocker` (Test 16) | Clean build (MSVC 19.44) | PASS (zero CPU fallback edges, GPU candidate batch traversal, multi-blocker aggregate semantics) | None |
| 4 | Make layout migration transactional and rebuild all affected groups | PASS | `src/astg/astg_transport_engine.h` | `test_transactional_layout_migration_and_stationary_rebuild` (Test 17) | Clean build (MSVC 19.44) | PASS (stationary groups rebuilt, layout hash updated, old memberships safely unblocked on GPU) | None |
| 5 | Make the negative control exercise the real lighting oracle | PASS | `src/diagnostics/astg_parts_jk_acceptance_main.cpp` | `test_dense_probe_visibility` (Test 11), `test_negative_control_oracle_sensitivity` (Test 13) | Clean build (MSVC 19.44) | PASS (4 distinct corruptions rejected by real shared validator with exact numeric delta reporting) | None |
| 6 | Rebuild and identify the final source actually tested | PASS | `build_rtx_dll.ps1`, `build_parts_jk_acceptance.ps1`, `build_e2e_tests.ps1`, `build_diagnostics.ps1` | `astg_parts_jk_acceptance.exe` (17/17), `astg_e2e_tests.exe` (101/101), `astg_diagnostics.exe` (100% full suites) | Clean build (MSVC 19.44) | PASS (100% PASS across all 3 test suites, zero skips, D3D12 error count = 0) | None |

## Stage Logs & Observations

### Stage A (Issues 1–2) — COMPLETED (PASS)
- **Focus**: Command allocator slot ownership and safe frame slot acquisition before upload write.
- **Status**: PASS
- **Actions & Observations**:
  1. Fixed no-op allocator assignment in `sync_active_frame_slot_pointers()` so `g_rtx.command_allocator = slot.command_allocator;`.
  2. Implemented dedicated `imm_command_allocator` for all synchronous utility, clear, and diagnostic operations, isolating frame slot allocators.
  3. Added explicit slot acquisition lifecycle: `rtx_acquire_frame_slot()`, `rtx_release_unsubmitted_frame_slot()`, `rtx_get_frame_slot_record()`, `rtx_get_frame_slot_count()`.
  4. Checked HRESULT on command allocator reset, command list reset/close, and queue signal.
  5. Wired `rtx_acquire_frame_slot` in `update_dynamic_occlusions_frame_async` BEFORE any upload buffer writes; on backpressure, dirty work is retained for retry without touching upload memory.
  6. Verified on RTX 4070 hardware via Tests 14 and 15 in `astg_parts_jk_acceptance.exe`.

### Stage B (Issue 3) — COMPLETED (PASS)
- **Focus**: GPU-only B1+ execution and removal of CPU fallback.
- **Status**: PASS
- **Actions & Observations**:
  1. Removed arbitrary `>= 256` reference heuristic in `evaluate_and_apply_b1_plus_edges` so any positive reference count executes on GPU.
  2. Integrated fallback GPU candidate batch dispatch (`rtx_trace_candidates_batch`) when spatial index is inactive, avoiding CPU fallback in GPU production mode.
  3. Strict error propagation on acceleration structure or hardware failure: marks `m.gpu_dispatch_failed = true` instead of silently executing CPU loop.
  4. Added separate telemetry instrumentation for CPU reference work (`m.cpu_reference_edges_tested`) and GPU B1+ work (`m.gpu_b1_plus_edges_tested`).
  5. Tested and verified on RTX 4070 hardware via Test 16: zero CPU reference tests (`zero_cpu=1`), GPU execution observed (`gpu_tested=1`), multi-blocker aggregate semantics and clean unblocking confirmed.

### Stage C (Issue 4) — COMPLETED (PASS)
- **Focus**: Transactional layout migration and rebuilding all affected groups.
- **Status**: PASS
- **Actions & Observations**:
  1. Preflighted replacement layout in temporary memory (`cand_frames`, `cand_ranges`, `cand_records`, etc.) before touching live tables.
  2. Unblocked all existing allocations against the OLD layout on the GPU first via `remove_single_group_light_dynamic_occlusion_gpu`, preventing stale offset address corruption.
  3. Re-scheduled every dynamic occluder group (including stationary groups) for rebuild before taking the `frame_dirty_groups` snapshot.
  4. Checked return values on static uploads and unblock operations; failed uploads abort safely without updating the layout hash.
  5. Verified on RTX 4070 hardware via Test 17: stationary groups are cleanly rescheduled and rebuilt with updated record counts and new layout hashes.

### Stage D (Issue 5) — COMPLETED (PASS)
- **Focus**: Negative control exercising the real lighting oracle.
- **Status**: PASS
- **Actions & Observations**:
  1. Extracted shared validation function `validate_dense_probe_lighting` used identically by both Test 11 (`test_dense_probe_visibility`) and Test 13 (`test_negative_control_oracle_sensitivity`).
  2. Tested 4 distinct corrupted copies against the real shared validator:
     - All-black output: rejected with delta = 0.355074.
     - Lit in shadow: rejected with delta = 2.500000.
     - Unlit in light: rejected with delta = 0.361060.
     - 50% scale error: rejected with delta = 0.180530.
  3. Replaced dummy float equality check with exact numeric delta reporting and uncorrupted baseline verification.
  4. Verified on RTX 4070 hardware via Test 13 in `astg_parts_jk_acceptance.exe`.

### Stage E (Issue 6) — COMPLETED (PASS)
- **Focus**: Rebuild and identify the final source actually tested.
- **Status**: PASS
- **Actions & Observations**:
  1. Identified and resolved root cause of edge visibility tracking discrepancies in `src/rtx/rtx_gpu_transport.hlsl`:
     - Added 4-byte padding to `ASTGGPUCellRange` in HLSL to strictly maintain 16-byte alignment matching C++ `ASTGGPUCellRange` struct.
     - Gated normal cone culling in Stage 2 with `(edge.flags & 0x2) != 0` to ensure DAG edges not specifying angular filtering are not falsely rejected as back-facing.
     - Corrected `gpu_nodes_shadow` buffer allocation in `full_sync_gpu_astg()` to size by `max_node_id + 1` indexed directly by node ID.
     - Added GPU visibility state slot reset in `clear_group_dynamic_state()`.
     - Corrected HLSL persistent visibility state slot stride calculation to `524288u`.
  2. Cleanly rebuilt all 4 build targets with MSVC 19.44 and DXC SM 6.5 embedding canonical commit `cd308a24d0b467411fa021fe95d4a8ca063b63d3`:
     - `bin/astg_rtx.dll`
     - `bin/astg_rtx_runner.exe`
     - `bin/astg_parts_jk_acceptance.exe`
     - `bin/astg_e2e_tests.exe`
     - `bin/astg_diagnostics.exe`
  3. Executed all three test suites on NVIDIA GeForce RTX 4070 Laptop GPU:
     - `astg_parts_jk_acceptance.exe`: **17 / 17 PASS (100.0%)**
     - `astg_e2e_tests.exe`: **101 / 101 PASS (100.0%)**
     - `astg_diagnostics.exe`: **100% PASS** across Part J, Part K, Part L, and Evidence Integrity Report (zero skips, D3D12 Debug Error Count = 0).

## Final Provenance & Cryptographic Identification

### Git Baseline Information
- **Canonical Git HEAD Commit**: `cd308a24d0b467411fa021fe95d4a8ca063b63d3`
- **Worktree State**: Sealed evidence staging in progress.

### SHA-256 Hashes of Tested Source & Header Files
| File Path | SHA-256 Digest |
|---|---|
| `src/astg/astg_transport_engine.h` | `ab1483c3d8e5e8bba13f8fa42a6fe4e7901a410842bc85f451e1f071c024986a` |
| `src/rtx/rtx_raytracer.cpp` | `de04bdf80a8207a9945e5655113390079a3e1166819c09704165102a9e217e06` |
| `src/rtx/rtx_raytracer.h` | `c6479a3d9bc4408273da73ec96d94a5f896978d6f1cb47b3620160c861555128` |
| `src/rtx/rtx_types.h` | `8a366d947d214b4bdf40cb21ebceb17c36cddc541322e3ce4524adf585f2dbf8` |
| `src/rtx/rtx_gpu_transport.hlsl` | `b98102745469e3b5f482ddef62d9e82f6c158649b7fec539aae230bec1e99529` |
| `src/diagnostics/astg_parts_jk_acceptance_main.cpp` | `76d0fc4a14b81463cd57c7641aae3fdc769347aa2445e46df7b0ac6dc8cf77f2` |

### SHA-256 Hashes of Verified Built Binaries
| Binary Path | SHA-256 Digest |
|---|---|
| `bin/astg_rtx.dll` | `f54f229e61041d7d55daa6cd928fdecdaaf4484bb0e9f47abf3674bc3f468eea` |
| `bin/astg_rtx_runner.exe` | `e700d23db729e0bef9c8569c25a4ad07586d7760744c8378bf53ea2dfb50a684` |
| `bin/astg_parts_jk_acceptance.exe` | `4d3ea5b3c36cf78778b64e622b7e9cf6a6956f4b44f7551f02cd28d1a2ef4699` |
| `bin/astg_e2e_tests.exe` | `5b88e0885e71da7d8e82a43bfb0ca9817a98f8ea6a9412cfd9d79227c3eab34c` |
| `bin/astg_diagnostics.exe` | `65e5703cb36a923a5e3b24b7369a984c8dbfb85dd1fe62b3bfb61ac89543e8e6` |
