# RayLess

> *"What if you could bake raytracing, and still keep all the benefits?"*

RayLess is a next-generation real-time global illumination and transport engine designed to eliminate the per-frame ray-tracing bottleneck. By turning light transport from an ephemeral per-frame Monte Carlo calculation into a persistent **Adaptive Surface Transport Graph (ASTG)**, RayLess achieves real-time GI with **zero rays traced in steady state**, instant dynamic lighting animation, and ultra-high-throughput GPU-driven updates.

---

## ⚡ Five Numbers that Define RayLess Performance

Verified on **NVIDIA GeForce RTX 4070 Laptop GPU** (Amazon Bistro & Classroom scenes):

| Metric | Result | Why It Matters |
|---|---|---|
| **Steady-State Rays / Frame** | **0 rays** | Once transport is cached, static frames trace zero rays. Frametime is pure probe lookup (~0.02 ms / 40,000+ FPS). |
| **Light Modulation Cost** | **0 rays** | Pulsing, blinking, or moving light colors/intensities modifies only light buffers—graph topology is invariant to light spectrum. |
| **Dynamic Light Upscaling** | **$60\times$ to $100\times$** | Moving lights cast only 16–64 ingress rays; the ASTG highway upscales them into 4,000+ multi-bounce paths. |
| **128,000 Stationary Lights** | **0.78 ms** | Evaluated via GPU compute shaders with bounded probe fan-in (1,275+ FPS) without CPU intervention. |
| **Dynamic Destruction Invalidation** | **0.12%** | Geometry fracture or occlusion invalidates only localized subgraphs; repaired in 1 frame (T90 = 1 frame) with minimal rays. |
| **Total Transport VRAM** | **<600 KB** | Entire transport graph and probe representation for massive scenes fits in under a single megabyte of GPU memory. |

---

## 💡 The Core Idea: The Adaptive Surface Transport Graph (ASTG)

Traditional real-time ray tracing (DXR, ReSTIR, DDGI) casts rays per pixel every frame, suffering from noise, boiling, and heavy denoising lag.

RayLess represents scene lighting as a **persistent graph**:
- **Nodes** ([`ASTGGPUNode`](file:///c:/Users/Brand/Documents/hermes/Rayless/src/rtx/rtx_types.h#L154-L180)): Discrete surface interaction points (direct bounce B0 and indirect bounce B1+ hits).
- **Edges** ([`ASTGGPUDAGEdge`](file:///c:/Users/Brand/Documents/hermes/Rayless/src/rtx/rtx_types.h#L184-L204)): Physical lines of sight connecting lights, surfaces, and receivers.
- **Directional Records** ([`ASTGB0DirectionRecord`](file:///c:/Users/Brand/Documents/hermes/Rayless/src/rtx/rtx_types.h#L325-L343)): Continuous light-relative source rays encoding direct visibility.
- **Surface Probes** ([`SurfaceAttachedProbe`](file:///c:/Users/Brand/Documents/hermes/Rayless/src/astg/astg_transport_engine.h#L124-L142)): Triangle-attached receivers caching accumulated multi-bounce irradiance.

```
Host CPU (Topology & Scene Mutation)
  │
  ├─ 1. Topology Changes (Incremental) ──> [Persistent GPU Node & Edge StructuredBuffers]
  ├─ 2. Compact Visibility Candidates  ──> [ASTGGPUVisibilityCandidate[] (12 bytes/candidate)]
  │                                                      │
  │                                                      ▼
  │                                   [GPU Broadphase & Compaction CS]
  │                                     - Segment vs AABB Slab Intersection Test
  │                                     - 64-Bin Angular Octahedral Cell Filter
  │                                     - Monotonic Generation & Destruction Filter
  │                                     - SM 6.5 Wave Ballot Compaction
  │                                                      │
  │                                                      ▼ (Surviving Ambiguous Rays Only)
  │                                   [Inline DXR 1.1 RayQuery CS]
  │                                     - Register-Based RayDesc Synthesis
  │                                     - Inline RayQuery<ACCEPT_FIRST_HIT> (TLAS)
  │                                     - Compact Changed-State Return / Bitmask
  │                                                      │
  │                                                      ▼
  ├─ 3. Compact Changed-State Readback <── [Visibility Bitmask / Edge State Buffer]
  │
  ├─ 4. Dynamic Ingress Highway & Upscaler
  │     - 32-64 Ingress Rays merge onto precomputed ASTG Highway
  │     - Upscaled into 4,000+ multi-bounce paths & probe depositions
  │
  ├─ 5. Moving Objects & Swept-Bounds Discovery
  │     - Swept AABB corridors (B_(t-1) ∪ B_t) detect occlusions & disocclusions
  │     - GPU Spatial Cell Range uploads (bytes!) avoid full CPU edge traversal
  │     - Moving occluders dynamically shadow stationary receivers
  │
  └─ 6. Dynamic Surface Receiver Pipeline (Part K)
        - Bone Bounds Transform (Absolute Extents)
        - Cluster Culling & Normal Cone Filter
        - GPU Work Queue & DispatchIndirect
        - Atomic Float Irradiance Superposition
```

---

## 🛠️ Key Techniques & Tricks in RayLess

RayLess includes several novel GPU systems-engineering tricks designed to eliminate CPU overhead, PCIe bandwidth, and hardware RT bottlenecks:

1. **Adaptive Surface-Attached Probes (No Volumetric Leaks)**:
   - Probes are bound to triangle mesh barycentrics $(u, v)$ with normal offsets, rather than a 3D grid in open air.
   - Completely eliminates light leaking and dark leaking through thin walls, floors, and ceilings.
2. **Dynamic Lights on the ASTG Highway**:
   - Moving flashlights, torches, and headlights do not trace expensive multi-bounce paths.
   - Lights shoot a sparse batch of 1st-hop ingress rays (16–64 rays); on surface hit, they hop onto the pre-existing ASTG transport network and **ride the precomputed highway**.
3. **The ASTG Graph as a Ray Count Upscaler**:
   - Acts as a hardware ray multiplier: an input of **64 ingress rays** is upscaled across the graph into **4,000+ effective multi-bounce transport paths and probe depositions** ($60\times$ to $100\times$ amplification) in under 0.2 ms.
4. **Moving Objects & Swept-Bounds Discovery**:
   - Dynamic objects (characters, crates, vehicles) query swept corridors ($\mathbf{B}_{t-1} \cup \mathbf{B}_t$) on the GPU to simultaneously detect newly occluded and newly disoccluded edges in one pass.
   - The CPU uploads only compact cell range descriptors (`ASTGGPUCellRange`, bytes!), while candidate edges are discovered directly in device VRAM.
   - Moving occluders cast dynamic shadows on stationary receivers without rebuilding scene BLAS/TLAS.
5. **The 12-Byte Candidate & Register RayDesc Synthesis**:
   - Host CPU never packs or uploads full 64-byte `RayDesc` structs across PCIe.
   - Host submits tiny 12-byte candidate IDs (`edge_id`, `object_id`, `transport_generation`).
   - Compute shaders reconstruct `RayDesc` on-the-fly directly inside GPU registers from persistent node buffers. Bandwidth is reduced by **>75%** with **zero intermediate UAV ray buffers**.
6. **Multi-Stage GPU Rejection Funnel**:
   - Hardware RT Cores are fast, but **skipping them is infinitely faster**.
   - Rays pass through generation checks, antipodal back-face normal culling, octahedral angular filters, and analytic line-segment-vs-AABB slab tests before touching the BVH.
7. **SM 6.5 Wave-Aggregated Atomics**:
   - Rejection telemetry and compaction use `WaveActiveCountBits` and `WaveIsFirstLane`.
   - Results in **zero intra-wave atomics** and at most **one atomic per wave**, eliminating memory bus serialization.
8. **Part J: Continuous Light-Local Angular BVH**:
   - Avoids discrete shadow grid aliasing by storing exact spherical angles $(\theta, \phi)$ in light-local coordinate frames.
   - Exact analytical bounding-sphere cone projection ($\mathbf{A} = \frac{\mathbf{C}-\mathbf{L}}{\|\mathbf{C}-\mathbf{L}\|}, \theta = \arcsin\frac{R}{\|\mathbf{C}-\mathbf{L}\|}$) with no arbitrary epsilons.
   - Reference-counted blocker state ensures multi-occluder shadows do not leak when one blocker departs.
9. **Part K: Three-Tier Dynamic Receiver Hierarchy**:
   - Evaluates dynamic characters and props without rebuilding full-scene BLAS/TLAS.
   - Bone Bounds $\to$ Receiver Clusters $\to$ Surface Probes.
   - Single-matrix absolute extent transforms, cluster range/cone culling, and indirect GPU work queue expansion (`DispatchIndirect`).
   - Per-probe skeletal self-occlusion prevents self-intersection acne while allowing realistic cross-limb shadowing.
10. **Sub-Linear Destruction & Priority Graph Surgery**:
    - Reverse dependency index maps chunk IDs to dependent graph edges and probes.
    - Destroying geometry invalidates only ~0.12% of the graph, repaired in 1 frame ($T_{90}=1$) using a tiny ray budget (5–80 rays).
11. **Path Stitching (Downstream Subgraph Reuse)**:
    - When new repair rays hit a surface, `can_stitch()` finds existing valid transport nodes via a spatial hash grid.
    - Stitches new rays into existing downstream subtrees, eliminating 80–95% of multi-bounce continuation rays.
12. **128k Stationary Light Scaling via Bounded Fan-In**:
    - Probes bind sparsely to the top $K$ contributing lights ($K = 32$ or $64$).
    - Captures >99% emitted energy while maintaining a deterministic, constant-time evaluation envelope.
13. **Inline DXR 1.1 RayQuery & Changed-State Return Ring**:
    - Uses `RayQuery<ACCEPT_FIRST_HIT | SKIP_CLOSEST_HIT>` inside compute shaders, bypassing heavy RayGen shader binding tables.
    - Writes only mutated visibility states back to CPU, cutting readback bandwidth by >98%.

👉 **Read the full technical deep dive in [docs/TECHNIQUES_AND_TRICKS.md](file:///c:/Users/Brand/Documents/hermes/Rayless/docs/TECHNIQUES_AND_TRICKS.md).**

---

## 📂 Codebase Navigation

- [`src/rtx/rtx_types.h`](file:///c:/Users/Brand/Documents/hermes/Rayless/src/rtx/rtx_types.h): C/C++ and HLSL memory layouts, static asserts, and buffer definitions.
- [`src/rtx/rtx_gpu_transport.hlsl`](file:///c:/Users/Brand/Documents/hermes/Rayless/src/rtx/rtx_gpu_transport.hlsl): Compute kernels for candidate validation, 4-tier culling, wave compaction, and inline DXR 1.1 `RayQuery`.
- [`src/rtx/rtx_b0_angular_runtime.hlsl`](file:///c:/Users/Brand/Documents/hermes/Rayless/src/rtx/rtx_b0_angular_runtime.hlsl): Part J continuous B0 transport, angular BVH traversal, and multi-occluder delta updates.
- [`src/rtx/rtx_dynamic_receiver_runtime.hlsl`](file:///c:/Users/Brand/Documents/hermes/Rayless/src/rtx/rtx_dynamic_receiver_runtime.hlsl): Part K dynamic receiver pipeline: bone transforms, cluster culling, indirect dispatch args, and irradiance accumulation.
- [`src/rtx/rtx_raytracer.cpp`](file:///c:/Users/Brand/Documents/hermes/Rayless/src/rtx/rtx_raytracer.cpp): D3D12 device initialization, root signatures, persistent GPU buffers, and command dispatchers.
- [`src/astg/astg_transport_engine.h`](file:///c:/Users/Brand/Documents/hermes/Rayless/src/astg/astg_transport_engine.h): Host ASTG graph orchestration, dynamic ingress solver, topological mutation, destruction tracking, and test harnesses.
- [`scripts/core/precompute/adaptive_surface_probe_allocator.gd`](file:///c:/Users/Brand/Documents/hermes/Rayless/scripts/core/precompute/adaptive_surface_probe_allocator.gd): Adaptive surface probe generator and barycentric allocator.

---

## 🚀 Building & Running

### Requirements
- Windows 10/11 x64
- DXR 1.1-capable GPU (NVIDIA RTX 20/30/40 series or equivalent)
- Visual Studio 2022 (MSVC v14.40+) & Windows SDK (10.0.26100.0+)
- PowerShell 7+ or Windows PowerShell

### Build & Test Commands

```powershell
# 1. Compile D3D12 / DXR Shaders & Core RTX DLL
powershell -ExecutionPolicy Bypass -File .\build_rtx_dll.ps1

# 2. Build and Execute 101-Test Comprehensive E2E Test Suite (Tiers 1-4)
powershell -ExecutionPolicy Bypass -File .\build_e2e_tests.ps1
.\bin\astg_e2e_tests.exe

# 3. Build and Run Diagnostics & Scaling Benchmark Suite
powershell -ExecutionPolicy Bypass -File .\build_diagnostics.ps1
.\bin\astg_diagnostics.exe
```

---

## 📜 Documentation & Specifications

- [Techniques & Optimization Tricks Guide](file:///c:/Users/Brand/Documents/hermes/Rayless/docs/TECHNIQUES_AND_TRICKS.md) — Comprehensive deep dive into every GPU trick, data structure, and mathematical formulation.
- [Project Architecture & Milestones](file:///c:/Users/Brand/Documents/hermes/Rayless/PROJECT.md) — System specification and milestone breakdown.
- [E2E Test Infrastructure & Invariants](file:///c:/Users/Brand/Documents/hermes/Rayless/TEST_INFRA.md) — 4-tier testing methodology and validation rules.
- [Test Readiness Report](file:///c:/Users/Brand/Documents/hermes/Rayless/TEST_READY.md) — Verification report across 101 hardware DXR tests.
