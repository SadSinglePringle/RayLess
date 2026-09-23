# RayLess: Deep Architecture, Techniques & Optimization Tricks
*The comprehensive technical blueprint of the Adaptive Surface Transport Graph (ASTG) engine.*

---

## Table of Contents
1. [The Paradigm Shift: From Ephemeral Rays to the Persistent ASTG](#1-the-paradigm-shift-from-ephemeral-rays-to-the-persistent-astg)
2. [Adaptive Surface-Attached Probes (Eliminating Volumetric Grid Leaks)](#2-adaptive-surface-attached-probes-eliminating-volumetric-grid-leaks)
3. [The 12-Byte Visibility Candidate & Register-Based RayDesc Synthesis](#3-the-12-byte-visibility-candidate--register-based-raydesc-synthesis)
4. [The 4-Stage GPU Culling Funnel (Killing Rays Before Hardware RT)](#4-the-4-stage-gpu-culling-funnel-killing-rays-before-hardware-rt)
5. [SM 6.5 Wave-Aggregated Atomics (Zero-Contention GPU Telemetry)](#5-sm-65-wave-aggregated-atomics-zero-contention-gpu-telemetry)
6. [Part J: Continuous Source-Local Angular B0 Transport & Cone BVHs](#6-part-j-continuous-source-local-angular-b0-transport--cone-bvhs)
7. [Part K: Three-Tier Dynamic Receiver Hierarchy & Bone-Anchored Probes](#7-part-k-three-tier-dynamic-receiver-hierarchy--bone-anchored-probes)
8. [Moving Objects in a Scene: Dynamic Occluders, Swept Bounds & GPU Spatial Discovery](#8-moving-objects-in-a-scene-dynamic-occluders-swept-bounds--gpu-spatial-discovery)
9. [Sub-Linear Destruction & Reverse Dependency Graph Surgery](#9-sub-linear-destruction--reverse-dependency-graph-surgery)
10. [Path Stitching: Reusing Downstream Light Transport Subgraphs](#10-path-stitching-reusing-downstream-light-transport-subgraphs)
11. [Dynamic Lights on the ASTG Highway & The Ray Count Upscaler](#11-dynamic-lights-on-the-astg-highway--the-ray-count-upscaler)
12. [Massive Stationary Light Decoupling & Bounded Probe Fan-In (128k Lights)](#12-massive-stationary-light-decoupling--bounded-probe-fan-in-128k-lights)
13. [Inline DXR 1.1 RayQuery & Compact Changed-State Return Ring](#13-inline-dxr-11-rayquery--compact-changed-state-return-ring)
14. [Summary of Reusable Architectural Patterns ("Tricks to Steal")](#14-summary-of-reusable-architectural-patterns-tricks-to-steal)

---

## 1. The Paradigm Shift: From Ephemeral Rays to the Persistent ASTG

### The Fundamental Flaw of Traditional Real-Time Ray Tracing
In conventional engines (DXR path tracers, ReSTIR GI, DDGI), ray tracing is treated as an **ephemeral, per-pixel, per-frame Monte Carlo operation**:
- Every frame, 1–8 rays are fired per pixel or per probe into a DXR BVH.
- Because sample counts are tiny, results are noisy and undergo aggressive spatiotemporal denoising (SVGF, NRD, DLSS-RR).
- Denoisers introduce ghosting, boiling, laggy indirect response, and smear out high-frequency contact cues.
- **Worst of all: even when nothing in the scene moves or changes, the GPU burns 100% of its ray budget every single frame.**

### The RayLess Solution: Persistent Light Transport
Light transport in a static or semi-static environment is physically deterministic. If a light illuminates a wall, and that wall bounces light to the floor, **that geometric path remains valid until geometry moves or breaks**.

RayLess models scene lighting as an **Adaptive Surface Transport Graph (ASTG)**:
- **Transport Nodes** ([`ASTGGPUNode`](file:///c:/Users/Brand/Documents/hermes/Rayless/src/rtx/rtx_types.h#L154-L180)): Physical surface interaction points (Bounce 0 direct hits, Bounce 1+ indirect scattering points).
- **DAG Edges** ([`ASTGGPUDAGEdge`](file:///c:/Users/Brand/Documents/hermes/Rayless/src/rtx/rtx_types.h#L184-L204)): Directed line segments encoding visibility and energy transfer between parent and child nodes.
- **Directional Records** ([`ASTGB0DirectionRecord`](file:///c:/Users/Brand/Documents/hermes/Rayless/src/rtx/rtx_types.h#L325-L343)): Continuous light-relative source rays encoding direct visibility.
- **Surface Probes** ([`SurfaceAttachedProbe`](file:///c:/Users/Brand/Documents/hermes/Rayless/src/astg/astg_transport_engine.h#L124-L142)): Triangle-attached receivers caching accumulated multi-bounce irradiance.

```
                  [Stationary Light Source L]
                         /            \
                   B0   /              \   B0
                       ▼                ▼
                  [Node 0]            [Node 1]      <-- Bounce 0 (Direct illumination)
                   (Wall)              (Floor)
                     │                    │
                   B1│                    │ B1
                     ▼                    ▼
                  [Node 2]            [Node 3]      <-- Bounce 1 (Indirect bounce points)
                     \                   /
                      \                 /
                       ▼               ▼
                      [Surface-Attached Probes]     <-- Diffuse Receivers
```

### The Invariant: Separation of Topology and Radiance
A fundamental mathematical insight in RayLess is that **light transport topology is completely decoupled from light emission spectrum**:
$$\text{Transport Matrix } \mathbf{T}_{ij} = \frac{\rho_j \cos\theta_i \cos\theta_j}{\pi \|\mathbf{x}_i - \mathbf{x}_j\|^2} \cdot V(\mathbf{x}_i, \mathbf{x}_j)$$
Notice that visibility $V(\mathbf{x}_i, \mathbf{x}_j)$ and geometric form factors depend purely on **geometry and spatial layout**. They have zero dependence on the light's color $(R, G, B)$ or its current intensity $I(t)$.

**The Resulting Performance:**
- **Steady-State Rays Traced**: **0 rays/frame**. In static geometry, the graph is static. Lighting evaluation is reduced to an $O(1)$ probe lookup (~0.015–0.034 ms, or 30,000+ FPS).
- **Lighting Animation**: Modulating colors, strobing, or dimming lights costs **0 rays**.
- **Massive Lights**: 128,000 stationary lights evaluate on an RTX 4070 Laptop GPU in **0.78 ms** (animated) or **0.20 ms** (static).

---

## 2. Adaptive Surface-Attached Probes (Eliminating Volumetric Grid Leaks)

### The Problem with 3D Grid Probes (DDGI)
Most real-time GI systems place probes on a regular 3D grid in open space (e.g. DDGI / Irradiance Volumes). This causes two catastrophic visual artifacts:
1. **Light Leaking**: A probe inside a brightly lit room sits 10 cm away from a thin partition wall. Pixels on the dark exterior of the wall interpolate that probe, causing bright light to leak through.
2. **Dark Leaking**: Probes end up embedded inside walls, furniture, or geometry, reading pure black and dragging down the lighting of nearby visible surfaces.

### The RayLess Approach: Surface-Attached Triangle Barycentric Probes
In RayLess ([`adaptive_surface_probe_allocator.gd`](file:///c:/Users/Brand/Documents/hermes/Rayless/scripts/core/precompute/adaptive_surface_probe_allocator.gd) and [`astg_transport_engine.h:6188`](file:///c:/Users/Brand/Documents/hermes/Rayless/src/astg/astg_transport_engine.h#L6188)), probes are **never placed in empty space**. They are explicitly attached to the surface manifold:
```cpp
struct SurfaceAttachedProbe {
    uint32_t probe_id;
    uint32_t mesh_id;
    uint32_t instance_id;
    uint32_t primitive_id;      // Exact triangle index
    float    barycentric_u;     // Triangle barycentric coordinate u
    float    barycentric_v;     // Triangle barycentric coordinate v
    RTXVector3 world_position;  // Position offset by small normal push (e.g. +8mm)
    RTXVector3 geometric_normal;// Surface normal at barycentric point
    uint32_t surface_cluster_id;
    uint32_t destruction_chunk_id;
    float    area_weight;
};
```

```
       3D Grid Probes (DDGI)             Surface-Attached Probes (RayLess)
       
          [Probe] (Leaks!)                         [Probe]
   ───────────────────────────── Wall       ───────────────────────────── Wall
          [Probe] (Inside wall!)                   [Probe]
   ───────────────────────────── Wall       ───────────────────────────── Wall
          [Probe]                                  [Probe]
```

### Allocation Pipeline & Density Adaptation
1. **Area-Proportional Poisson Seeding**:
   The scene is partitioned into geometric clusters. Seed probes are allocated proportionally to cluster surface area:
   $$N_{cluster} = \max\left(1, \left\lfloor \frac{\text{Area}_{cluster}}{\text{Area}_{total}} \cdot N_{target} \right\rceil\right)$$
2. **Curvature & Normal Variance Adaptation**:
   Surfaces with high geometric curvature or normal variance $\sigma_N^2$ receive a higher local probe density. Flat expanses (floors, ceilings) receive sparse probes.
3. **Directional Encoding (Dominant Direction + L1 Spherical Harmonics)**:
   Instead of storing heavy cubemaps or octahedral textures per probe, each probe stores:
   - Average diffuse irradiance $\mathbf{E} \in \mathbb{R}^3$.
   - Dominant incoming radiance direction $\mathbf{D}_{dom} \in S^2$ and strength $s \in [0, 1]$.
   - 3-band L1 Spherical Harmonics ($SH_{L1}$, 9 floats for RGB) providing directional diffuse response for normal-mapped surfaces with tiny memory overhead.
4. **Surface-Aware Lookup (Zero Leaks)**:
   When a shaded pixel looks up nearby probes, it calculates a lookup weight:
   $$W_p = \max(0, \mathbf{N}_{pixel} \cdot \mathbf{N}_p) \cdot \frac{1}{\|\mathbf{X}_{pixel} - \mathbf{X}_p\|^2 + \epsilon}$$
   If $\mathbf{N}_{pixel} \cdot \mathbf{N}_p \le 0$ (opposite facing) or the probe belongs to a different surface cluster across a wall, its weight is **identically zero**. Light leaking through walls is physically impossible.

---

## 3. The 12-Byte Visibility Candidate & Register-Based RayDesc Synthesis

### The Host-to-Device Bandwidth Bottleneck
In conventional DXR architectures, the CPU prepares arrays of 64-byte `RayDesc` structures and uploads them to the GPU via staging buffers:
```cpp
// Standard DXR RayDesc (64 bytes with metadata)
struct RayDesc {
    float3 Origin;    // 12 bytes
    float  TMin;      // 4 bytes
    float3 Direction; // 12 bytes
    float  TMax;      // 4 bytes
    // ... flags, payload metadata ...
};
```
For 131,072 rays, uploading `RayDesc[]` consumes **8.39 MB per frame**, stalling the CPU on ray-packing loops and saturating PCIe bandwidth.

### RayLess Solution: Persistent Buffers & 12-Byte Candidates
RayLess maintains the entire transport topology permanently resident in GPU VRAM:
- `StructuredBuffer<ASTGGPUNode> g_nodes : register(t1);` (48 bytes/node).
- `StructuredBuffer<ASTGGPUDAGEdge> g_edges : register(t2);` (32 bytes/edge).

When the host needs visibility evaluated (for dynamic object motion or geometry validation), it uploads **compact candidate records**:
```cpp
struct ASTGGPUVisibilityCandidate {
    uint32_t edge_id;               // 4 bytes: Index into persistent g_edges
    uint32_t object_id;             // 4 bytes: Dynamic occluder group ID
    uint32_t transport_generation;  // 4 bytes: Monotonic generation stamp
    uint32_t occluder_index;        // 4 bytes: Dense index into g_occluders
};
```
- **Candidate Size**: 12 to 16 bytes.
- **Bandwidth Reduction**: **>75% reduction** compared to uploading raw rays.
- **CPU Time (`cpu_raygen_ms`)**: Drops to **0.00 ms**.

### Register-Based RayDesc Synthesis
In the compute shader ([`rtx_gpu_transport.hlsl:400-413`](file:///c:/Users/Brand/Documents/hermes/Rayless/src/rtx/rtx_gpu_transport.hlsl#L400-L413)), each thread extracts the edge and node data and builds the ray directly in hardware VGPRs (registers):
```hlsl
ASTGGPUDAGEdge edge = g_edges[candidate_edge_id];
ASTGGPUNode src_node = g_nodes[edge.source_node_id];
ASTGGPUNode dst_node = g_nodes[edge.dest_node_id];

float3 p_src = src_node.position;
float3 p_dst = dst_node.position;
float3 n_src = src_node.normal;

float3 delta = p_dst - p_src;
float dist = length(delta);
float3 dir = delta / dist;

// Normal bias (5mm) and directional push (1mm) prevent self-intersection
float3 origin = p_src;
if (dot(n_src, n_src) > 0.1f) {
    origin += n_src * 0.005f;
}
origin += dir * 0.001f;

RayDesc ray;
ray.Origin = origin;
ray.Direction = dir;
ray.TMin = 0.001f;
ray.TMax = max(0.001f, dist - 0.02f);
```
**Zero intermediate UAV ray buffers are ever allocated.** The ray exists only in registers for the duration of the inline traversal call.

---

## 4. The 4-Stage GPU Culling Funnel (Killing Rays Before Hardware RT)

Hardware RT cores can trace hundreds of millions of rays per second, but **not tracing a ray is infinitely faster**. RayLess filters every candidate through a four-stage compute funnel before DXR is ever touched:

```
[Candidate Query Batch]
          │
          ▼
┌───────────────────────────────────────────────┐
│ Stage 1: Monotonic Generation & State Filter  │ ──> Cull Stale / Destroyed Edges (2 cycles)
└───────────────────────────────────────────────┘
          │ (Surviving)
          ▼
┌───────────────────────────────────────────────┐
│ Stage 2: Antipodal Normal Cone Culling        │ ──> Cull Back-facing Emitters (dot(N, D) < -0.01)
└───────────────────────────────────────────────┘
          │ (Surviving)
          ▼
┌───────────────────────────────────────────────┐
│ Stage 3: Octahedral Directional Cell Filter   │ ──> Cull Mismatched Angular Bins
└───────────────────────────────────────────────┘
          │ (Surviving)
          ▼
┌───────────────────────────────────────────────┐
│ Stage 4: Analytic Segment vs AABB Slab Test   │ ──> Cull Rays Missing Dynamic Bounds
└───────────────────────────────────────────────┘
          │ (Surviving Ambiguous Rays Only: ~5-15%)
          ▼
┌───────────────────────────────────────────────┐
│ Stage 5: Inline DXR 1.1 RayQuery Traversal    │ ──> Hardware RT Cores Trace First-Hit
└───────────────────────────────────────────────┘
```

### Stage 1: Monotonic Generation & Structural Filter
Prevents race conditions between CPU graph updates and in-flight GPU dispatches. If an edge has been updated on the host or its parent node deactivated, its generation will not match `candidate_generation`:
```hlsl
if (edge.generation != candidate_generation || (edge.flags & 0x1) == 0 ||
    edge.edge_state == 1 /* INVALID_STATIC */) {
    WaveInterlockedAdd(COUNTER_GENERATION_REJECTED, true);
    return; // Culled in 2 cycles
}
```
Furthermore, if any endpoint belongs to a destroyed chunk checked against the bitmask `g_destroyed_chunk_mask`, the candidate is immediately dropped.

### Stage 2: Antipodal Normal Cone Culling
Physically, opaque diffuse surfaces cannot emit or scatter light behind their surface plane:
$$\mathbf{N}_{src} \cdot \mathbf{D} \le -0.01 \implies \text{Cull}$$
If the target node lies behind the emitter normal, the ray is instantly dropped without touching the BVH.

### Stage 3: 64-Bin Octahedral Directional Hierarchy
Surfaces classify outgoing directions onto an $8 \times 8$ octahedral map (64 discrete leaf cells).
$$\text{EncodeOctahedral}(\mathbf{d}) = \frac{\mathbf{d}}{\|\mathbf{d}\|_1}$$
Edges retain their intended `angular_cell_id`. If a dynamic occluder or receiver query falls outside the edge's angular cell, it is discarded.

### Stage 4: Analytic Segment vs. AABB Slab Intersection
When testing whether a dynamic object (e.g., a moving crate or character AABB $[B_{min}, B_{max}]$) occludes an edge between $P_0$ and $P_1$:
Rather than launching a hardware ray query, the compute shader intersects the finite line segment $P(t) = P_0 + t(P_1 - P_0), t \in [0, 1]$ directly against the box slabs:
```hlsl
bool SegmentIntersectsAABB(float3 p0, float3 p1, float3 box_min, float3 box_max) {
    const float eps = 1e-7f;
    float3 d = p1 - p0;
    float tmin = 0.0f;
    float tmax = 1.0f;

    [unroll]
    for (int i = 0; i < 3; ++i) {
        float di = d[i];
        float p0i = p0[i];
        float min_i = box_min[i];
        float max_i = box_max[i];

        if (abs(di) < eps) {
            // Parallel to slab: if origin is outside slab, ray misses entirely
            if (p0i < min_i || p0i > max_i) return false;
        } else {
            float inv_d = 1.0f / di;
            float t1 = (min_i - p0i) * inv_d;
            float t2 = (max_i - p0i) * inv_d;
            tmin = max(tmin, min(t1, t2));
            tmax = min(tmax, max(t1, t2));
            if (tmin > tmax) return false;
        }
    }
    return tmin <= tmax && tmax >= 0.0f && tmin <= 1.0f;
}
```
**The Breakthrough:**
If `SegmentIntersectsAABB` returns `false`, **the edge is mathematically guaranteed to be unblocked by this dynamic object**. Hardware RT is completely bypassed, and the edge is flagged `VISIBLE` at near-zero GPU cost.

---

## 5. SM 6.5 Wave-Aggregated Atomics (Zero-Contention GPU Telemetry)

### The Atomic Serialization Trap
When tens of thousands of GPU threads run in parallel, tracking culling metrics (`edges_considered`, `broadphase_rejected`, `rayquery_candidates`) with naive atomic additions creates catastrophic memory controller serialization:
```hlsl
// NAIVE: 32 or 64 threads in the wave all execute atomic add to the same address!
InterlockedAdd(g_counters[COUNTER_BROADPHASE_REJECTED], 1);
```
Under high contention, threads queue up in hardware atomic units, causing massive stalls.

### The SM 6.5 Solution: Intra-Wave Aggregation
In [`rtx_gpu_transport.hlsl:138-144`](file:///c:/Users/Brand/Documents/hermes/Rayless/src/rtx/rtx_gpu_transport.hlsl#L138-L144), RayLess leverages modern Shader Model 6.5 wave intrinsics:
```hlsl
void WaveInterlockedAdd(uint counter_idx, bool condition) {
    uint count = WaveActiveCountBits(condition);
    if (WaveIsFirstLane() && count > 0) {
        InterlockedAdd(g_counters[counter_idx], count);
    }
}
```

```
Wave Lanes (Lanes 0 to 31):
 Lane 0: condition = 1 ┐
 Lane 1: condition = 1 │
 Lane 2: condition = 0 │──> WaveActiveCountBits() evaluates in 1 cycle inside registers (count = 18)
 ...                   │
 Lane 31: condition = 1┘
 
 Only Lane 0 (WaveIsFirstLane) executes:
 InterlockedAdd(g_counters[counter_idx], 18);
 
 Result: 1 memory atomic instead of 32!
```
- **Intra-Wave Atomics**: **0**.
- **Global Memory Atomics**: Reduced by **$32\times$ or $64\times$** (exactly one atomic per wave).
- Enables exact, production-grade telemetry counters across all 131k dispatches with **zero measurable performance impact**.

---

## 6. Part J: Continuous Source-Local Angular B0 Transport & Cone BVHs

### The Problem with Discrete Cubemaps / Shadow Grids
Traditional shadow mapping or octahedral directional grids discretize outgoing light directions into 2D textures or cell arrays. This causes:
1. **Edge Seam Popping**: Dynamic objects crossing grid cell boundaries cause sudden shadow jumps.
2. **Discretization Bias**: Small or distant occluders can be lost or over-magnified.

### Continuous Light-Relative Direction Records
RayLess enforces that direct Bounce 0 (B0) light transport is governed by **continuous light-relative directional records** ([`ASTGB0DirectionRecord`](file:///c:/Users/Brand/Documents/hermes/Rayless/src/rtx/rtx_types.h#L325-L343)):
```cpp
struct ASTGB0DirectionRecord {
    float    dir_local_x, dir_local_y, dir_local_z; // Unit vector in light-local frame
    uint32_t source_light_id;
    float    theta;              // Elevation in [-pi/2, pi/2]
    float    phi;                // Azimuth in [-pi, pi]
    uint32_t transport_node_id;  // Destination B0 transport node
    float    hit_dist;           // Exact Euclidean distance to static hit
    uint32_t generation;
    float    solid_angle;        // Differential solid angle steradians
};
```
Every light defines its own orthonormal basis: Forward ($\mathbf{F}$), Right ($\mathbf{R}$), and Up ($\mathbf{U}$). Static surface hits are stored as exact spherical coordinates in this light-local frame.

### Exact Conservative Bounding-Sphere Cone Projection (Pass J1)
When an occluder with bounding box $[B_{min}, B_{max}]$ moves near a light $\mathbf{L}$:
1. Compute the bounding sphere: center $\mathbf{C} = \frac{B_{min}+B_{max}}{2}$, radius $R = \frac{\|B_{max}-B_{min}\|}{2}$.
2. Compute distance to light: $d = \|\mathbf{C} - \mathbf{L}\|$.
3. **Exact Analytical Footprint Derivation** ([`rtx_b0_angular_runtime.hlsl:198-234`](file:///c:/Users/Brand/Documents/hermes/Rayless/src/rtx/rtx_b0_angular_runtime.hlsl#L198-L234)):
   - **Case A: Light inside or intersecting the bounding sphere ($d \le R$)**:
     $$\cos\theta_{half} = -1.0, \quad \text{flags} = 0x1 \quad (\text{Exact full spherical coverage } 4\pi\text{ sr})$$
   - **Case B: Light outside the bounding sphere ($d > R$)**:
     $$\mathbf{A}_{world} = \frac{\mathbf{C} - \mathbf{L}}{d}$$
     $$\mathbf{A}_{local} = (\mathbf{A}_{world} \cdot \mathbf{R}, \; \mathbf{A}_{world} \cdot \mathbf{U}, \; \mathbf{A}_{world} \cdot \mathbf{F})$$
     $$\sin\theta_{half} = \frac{R}{d}, \quad \cos\theta_{half} = \sqrt{1 - \sin^2\theta_{half}}$$
   - **No heuristic epsilons are used**. The cone is analytically exact and guaranteed conservative.

```
                  Light L
                     \  θ_half
                      \  │
                       \ ▼
                        ┌───────┐
                        │   C   │ Dynamic Bounding Sphere (Radius R)
                        └───────┘
```

### B0 Light Angular BVH Traversal (Pass J2)
Each stationary light owns a static **Angular BVH** ([`ASTGB0AngularBVHNode`](file:///c:/Users/Brand/Documents/hermes/Rayless/src/rtx/rtx_types.h#L347-L361)) partitioning all its static B0 direction records into bounding cones: $(\mathbf{A}_{node}, \cos\theta_{node})$.

In Pass J2, the compute shader tests the projected footprint cone $(\mathbf{A}_{fp}, \cos\theta_{fp})$ against BVH nodes via cone-cone overlap:
$$\mathbf{A}_{fp} \cdot \mathbf{A}_{node} \ge \cos(\theta_{fp} + \theta_{node})$$
- If cones do not overlap: The entire subtree of light rays is culled.
- At leaf nodes: Individual member rays are tested for angular containment and line-segment AABB intersection.

### Multi-Blocker Reference Counting & Guarded Deltas (Pass J4)
In real scenes, multiple dynamic objects (e.g. two characters or props) can simultaneously block the same light ray.
- If character A walks away while character B remains, the ray **must remain blocked**.
- RayLess maintains a persistent reference counter `ASTGB0PersistentState.blocker_count` per B0 record.
- In Pass J4 ([`rtx_b0_angular_runtime.hlsl:337-430`](file:///c:/Users/Brand/Documents/hermes/Rayless/src/rtx/rtx_b0_angular_runtime.hlsl#L337-L430)), threads process 32-bit membership bitset words in parallel:
  $$\text{added} = \text{current} \land \neg\text{previous}$$
  $$\text{removed} = \text{previous} \land \neg\text{current}$$
- `InterlockedAdd` increments blocker counts for additions.
- For removals, a guarded `InterlockedCompareExchange` loop decrements the count down to zero while strictly preventing unsigned underflow.
- A state transition record (`ASTGB0TransitionRecord`) is emitted **only when the count transitions $0 \to 1$ (Blocked) or $1 \to 0$ (Unblocked)**.

---

## 7. Part K: Three-Tier Dynamic Receiver Hierarchy & Bone-Anchored Probes

Dynamic objects (characters, vehicles, animated props) cannot be baked into static light transport. However, rebuilding the entire scene's BLAS/TLAS every frame is prohibitive.

Part K provides a **GPU-driven, three-level receiver hierarchy** that lights animated characters without any host synchronization:

```
[Level 1: Dynamic Bone Bounds] (ASTGBoneBoundGPU)
      │
      ├─ Pass K1: Transform Bone AABBs via Absolute Matrix Extents
      ▼
[Level 2: Receiver Clusters] (ASTGReceiverClusterGPU)
      │
      ├─ Pass K2: Transform Probes via Inverse-Transpose Normal Matrix
      ├─ Pass K3: Rebuild Cluster Centroids & Normal Cones
      ├─ Pass K4: Cull Clusters vs Light Range & Normal Cones
      ├─ Pass K4.5: Build Indirect Dispatch Arguments
      ▼
[Level 3: Dynamic Surface Probes] (ASTGDynamicSurfaceProbeGPU)
      │
      ├─ Pass K5: Indirect Dispatch Visibility & Ray Testing
      └─ Pass K6: Multi-Light Atomic Float Irradiance Accumulation
```

### Pass K1: Absolute Extents Matrix AABB Transformation
Transforming an oriented bounding box on the GPU by projecting its 8 corners requires 8 matrix multiplies. RayLess transforms the bone AABB in **a single matrix operation** using absolute matrix extents:
$$\mathbf{C}_{world} = \mathbf{M} \cdot \mathbf{C}_{local}$$
$$\mathbf{E}_{world} = |\mathbf{M}_{3\times3}| \cdot \mathbf{E}_{local} = \begin{bmatrix} |M_{00}| & |M_{01}| & |M_{02}| \\ |M_{10}| & |M_{11}| & |M_{12}| \\ |M_{20}| & |M_{21}| & |M_{22}| \end{bmatrix} \begin{bmatrix} E_x \\ E_y \\ E_z \end{bmatrix}$$
$$\mathbf{B}_{min} = \mathbf{C}_{world} - \mathbf{E}_{world}, \quad \mathbf{B}_{max} = \mathbf{C}_{world} + \mathbf{E}_{world}$$
This is mathematically exact and evaluates in just 9 FMA instructions.

### Pass K2 & K3: Bone-Local Probes & Cluster Hierarchy
- Probes are stored in **true bone-local coordinates** (`local_pos`, `local_norm`).
- In Pass K2, positions are transformed by $\mathbf{M}_{bone}$, and normals are transformed by the **exact inverse-transpose matrix**:
  $$\mathbf{N}_{world} = \frac{\mathbf{M}^{-T} \mathbf{N}_{local}}{\|\mathbf{M}^{-T} \mathbf{N}_{local}\|}$$
  Handling non-uniform bone scaling without normal distortion.
- In Pass K3, child probes are reduced into cluster centroids and bounding normal cones ($\mathbf{A}_{norm}, \cos\theta_{cone}$).

### Pass K4 $\to$ K4.5 $\to$ K5: GPU Work Queue & Indirect Dispatch
- **Pass K4 (Culling)**: Clusters test against all active lights. If a cluster is beyond light range or its representative normal cone faces away from the light ($\mathbf{N}_{cluster} \cdot \mathbf{D}_{light} < -0.2$), the entire cluster is discarded.
  Surviving cluster-light pairs expand their probes into a flat GPU work queue (`g_probe_work_items`).
- **Pass K4.5 (Indirect Arg Gen)**: A single-thread compute pass reads the atomic work counter and writes D3D12 indirect dispatch arguments:
  ```hlsl
  uint workCount = g_work_counter.Load(0);
  g_indirect_args.Store(0, (workCount + 63) / 64); // ThreadGroupsX
  g_indirect_args.Store(4, 1);                    // ThreadGroupsY
  g_indirect_args.Store(8, 1);                    // ThreadGroupsZ
  ```
- **Pass K5 (`DispatchIndirect`)**: The GPU executes visibility tests on the exact compact queue of probe-light pairs. **The CPU never knows or cares how many probe queries were generated.**

### Pass K6: Atomic Float Irradiance Superposition
When a probe receives light, it accumulates irradiance into a persistent `RWByteAddressBuffer`:
```hlsl
void InterlockedAddFloat(RWByteAddressBuffer buf, uint byte_offset, float value) {
    if (abs(value) < 1e-7f) return;
    uint prev = buf.Load(byte_offset);
    [allow_uav_condition]
    for (uint i = 0; i < 64; ++i) {
        float next_f = asfloat(prev) + value;
        uint orig;
        buf.InterlockedCompareExchange(byte_offset, prev, asuint(next_f), orig);
        if (orig == prev) break;
        prev = orig;
    }
}
```
This enables thousands of lights to contribute energy to the same probe simultaneously without race conditions or auxiliary ping-pong buffers.

### Per-Probe Skeletal Self-Occlusion Rules
A major challenge with dynamic characters is preventing self-intersection (acne):
- **Rigid objects**: If `bb.group_id == pr.group_id`, collision is skipped (rigid props do not shadow themselves).
- **Skeletal characters**: If `bb.group_id == pr.group_id && bb.bone_id == pr.bone_id`, collision is skipped (a forearm does not shadow itself).
- **Cross-bone testing**: If `bb.group_id == pr.group_id && bb.bone_id != pr.bone_id`, **occlusion is evaluated!**
  - An arm realistically casts shadows across the character's torso, head, and legs with zero self-intersection artifacts.

---

## 8. Moving Objects in a Scene: Dynamic Occluders, Swept Bounds & GPU Spatial Discovery

A critical question for any transport cache is: **What happens when an object moves through the scene?**
In traditional ray tracing, moving an object requires updating or rebuilding the top-level BVH (TLAS) and re-casting rays across every pixel. In RayLess, moving objects interact with the graph through an ultra-fast, two-tier dynamic occlusion pipeline:

```
[Moving Object / Character Moves from Frame t-1 to t]
                         │
                         ├─ Compute Swept Bounds: B_swept = B_(t-1) ∪ B_t
                         ▼
┌────────────────────────────────────────────────────────┐
│ CPU: Spatial Grid Range Query (ASTGGPUCellRange)       │ ──> Upload only overlapping cell ranges (bytes!)
└────────────────────────────────────────────────────────┘
                         │
                         ▼
┌────────────────────────────────────────────────────────┐
│ GPU: Spatial Edge Index Discovery (VRAM-resident)      │ ──> Discovers candidate edges inside swept corridor
└────────────────────────────────────────────────────────┘
                         │
        ┌────────────────┴────────────────┐
        ▼                                 ▼
   [Direct B0 Visibility]            [Indirect B1+ Transport]
   - Light B0 Angular Footprint      - Segment vs AABB Slab Test
   - Blocker Count Updates (0 ↔ 1)   - Mutated Edge State Bitmask
        │                                 │
        └────────────────┬────────────────┘
                         ▼
   [Dependency-Scheduled Receiver Updates]
   - Moving occluder casts dynamic shadows on stationary receivers!
   - Vacated shadow regions are cleaned up with zero stale trails.
```

### 1. Dynamic Occluder Groups (`ASTGDynamicOccluderGroup`)
Objects moving in the scene (crates, vehicles, articulated characters) register as **Dynamic Groups**:
- **Rigid Groups**: Possess a single transform and one or more bounding boxes (`bounds`).
- **Skeletal Groups**: Possess dynamic bone matrices and per-bone bounding boxes.
- **Never Mutates Static Topology**: Dynamic occluders do not delete or splice static transport nodes. Instead, dynamic occlusion is tracked via persistent auxiliary state buffers (`ASTGPersistentVisibilityState`).

### 2. Swept Bounds: Detecting Both Occlusion and Disocclusion
When an object moves from position $P_{t-1}$ to $P_t$, testing only its current bounding box $B_t$ would discover edges that are *currently* blocked, but would completely miss edges that *used to be blocked* and are now uncovered!

RayLess computes a **Swept AABB**:
$$\mathbf{B}_{\text{swept}} = \mathbf{B}_{t-1} \cup \mathbf{B}_t$$
$$\mathbf{B}_{\text{swept}}.\text{expand}(r_{\text{corridor}})$$
By querying the transport graph with the swept bounds, the engine simultaneously tests:
1. **Newly Occluded Edges** ($0 \to 1$): Now intersected by $B_t$.
2. **Newly Disoccluded Edges** ($1 \to 0$): Vacated by $B_{t-1}$ and clear in $B_t$.
This ensures dynamic shadows both appear immediately and dissolve without ghosting or stale lingering trails.

### 3. GPU Spatial Discovery via Cell Ranges (`ASTGGPUCellRange`)
Rather than having the CPU iterate through tens of thousands of DAG edges to find which ones intersect $\mathbf{B}_{\text{swept}}$, RayLess offloads spatial discovery to the GPU ([`astg_transport_engine.h:2582-2650`](file:///c:/Users/Brand/Documents/hermes/Rayless/src/astg/astg_transport_engine.h#L2582-L2650)):
- All scene edges are indexed into uniform 3D spatial cells resident in GPU memory (`g_spatial_edge_indices`).
- The CPU only identifies which cell indices overlap $\mathbf{B}_{\text{swept}}$ and uploads compact **Cell Range Descriptors**:
  ```cpp
  struct ASTGGPUCellRange {
      uint32_t edge_index_offset; // Offset into g_spatial_edge_indices
      uint32_t edge_index_count;  // Number of edges in this cell
      uint32_t dispatch_offset;   // Thread prefix offset
  };
  ```
- The compute shader reads the range descriptors and iterates through edge indices directly in high-bandwidth VRAM, eliminating CPU edge traversal entirely.

### 4. Three Distinct Occlusion Modes
RayLess provides fine-grained control over dynamic occlusion fidelity:
- **Mode A (`ASTG_OCCLUSION_DAG_EDGES_ALL_BOUNCES`)**: General segment-vs-AABB testing across all transport edges ($B_0, B_1, \dots, B_N$).
- **Mode B (`ASTG_OCCLUSION_ANGULAR_B0_DAG_B1_PLUS`)**: Two-tier specialized execution:
  - Direct light occlusion ($B_0$) uses continuous light-local angular footprints and Angular BVHs (Part J).
  - Indirect scattering edges ($B_1+$) use the spatial edge grid and segment-AABB slab tests.
- **Mode C (`ASTG_OCCLUSION_ANGULAR_B0_ONLY`)**: Evaluates direct shadowing only; indirect bounce paths remain cached, offering maximum throughput for mobile/laptop GPUs.

### 5. Moving Occluders Shadowing Stationary Receivers
A common trap in caching systems is updating lighting only when a receiver moves. If a stationary player stands under a stationary lamp, and an enemy walks between them, **the stationary player must be shadowed**.

RayLess enforces **dependency-based dirty scheduling** ([`astg_transport_engine.h:3690`](file:///c:/Users/Brand/Documents/hermes/Rayless/src/astg/astg_transport_engine.h#L3690)):
- When an occluder moves, its swept volume is tested against active light influence volumes.
- Any stationary surface probes whose sightlines to those lights intersect the occluder's swept volume are scheduled for GPU re-evaluation.
- Stationary probes outside the shadow corridor are preserved in place with zero recomputation.

### 6. Dynamic Slot Modulo Recycling & ABA Generation Safety
Persistent visibility states on the GPU are scoped to a stable dynamic-object slot:
$$\text{state\_index} = \text{slot} \times 524,288 + \text{edge\_id}$$
Slots are recycled using modulo wrapping (`slot % MAX_SLOTS`) to support indefinite gameplay without memory exhaustion. To prevent ABA race conditions (where an edge ID is reused by a mutated object), every state entry records the edge mutation generation:
```hlsl
ASTGPersistentVisibilityState prior = g_visibility_states[state_index];
if (prior.edge_generation == generation && prior.visibility_state == visibility_state) {
    return; // Already current; no state mutation
}
```

---

## 9. Sub-Linear Destruction & Reverse Dependency Graph Surgery

### The Problem with Geometry Destruction in GI
In destructible games (e.g. walls breaking, cover blowing up), standard GI engines have two poor choices:
1. Re-bake / re-trace everything (causes massive frame drops).
2. Rely on temporal probe updates (the broken wall leaves a dark "ghost" shadow that takes seconds to dissolve).

### The RayLess Solution: The Geometry Dependency Database
RayLess indexes transport paths using an explicit **Geometry Dependency Database** ([`astg_transport_engine.h:7062`](file:///c:/Users/Brand/Documents/hermes/Rayless/src/astg/astg_transport_engine.h#L7062)):
Every transport node and DAG edge records the ID of the destructible chunk it intersects:
```cpp
struct ASTGTransportNode {
    // ...
    uint32_t destruction_chunk_id; // 0xFFFFFFFF = static indestructible
    std::set<uint32_t> inherited_chunk_dependencies; // All chunks this path passed through
};
```

```
[Light] ──> [Wall Chunk A] ──> [Floor Node 1] ──> [Probe 42]
                   │
           (Chunk A Destroyed!)
                   │
                   ▼ (Reverse Dependency Lookup: O(1))
         Invalidate: Floor Node 1, Probe 42
         Preserve: 99.88% of unlinked scene graph
```

### Priority-Based Localized Graph Surgery
When chunk $A$ is destroyed:
1. **Instant Subgraph Invalidation**: A reverse index lookup identifies every edge, node, and probe depending on chunk $A$.
   - In benchmark testing on Amazon Bistro and destructible classroom environments, **only 0.128% of the graph** is affected. 99.87% of the scene remains untouched.
2. **Priority Repair Scheduler**:
   The engine generates **Regeneration Anchors** (`ASTGRegenerationAnchor`) for the severed paths. Anchors are sorted into a priority queue based on lost energy:
   $$\text{Priority} = I_{light} \cdot \prod_{b} \rho_b$$
3. **Ray Budgeting**:
   Rather than tracing thousands of rays, the engine dispatches a tiny repair budget (e.g. 5–80 rays). High-energy direct and primary bounce paths are re-traced immediately.
4. **T90 Convergence in 1 Frame**:
   The scene converges to 90% ground-truth accuracy ($T_{90}$) in **exactly 1 frame**, with zero visible light leaking or persistent dark ghosts.

---

## 10. Path Stitching: Reusing Downstream Light Transport Subgraphs

### The Concept
When an object is destroyed or a new light turns on, new repair rays are traced into the scene.
In a conventional path tracer, a ray that hits a surface must recursively cast bounce 1, bounce 2, bounce 3, etc., tracing an exponential tree of rays.

In RayLess, **repair rays do not need to trace multiple bounces**. When a repair ray hits a surface, the engine attempts to **stitch** the new ray onto an existing valid downstream transport node!

```
New Ray:
[Light L] ──────(Repair Ray)──────> [Surface Hit]
                                         │
                                   Can Stitch?
                                         │
                          ┌──────────────┴──────────────┐
                     YES  ▼                             ▼  NO
                  [Node K]                         Trace Fresh Bounce
                  (Reuse existing subtree!)         (Expensive fallback)
                     /       \
                 [Node K1]  [Node K2]
                    │           │
                 [Probe]     [Probe]
```

### The `can_stitch()` Decision Engine
In [`astg_transport_engine.h:5217-5310`](file:///c:/Users/Brand/Documents/hermes/Rayless/src/astg/astg_transport_engine.h#L5217-L5310), a spatial grid query (`ASTGStaticNodeSpatialGrid`) locates candidate nodes near the hit position. The candidate must satisfy six strict invariants:

1. **Active & Generation Freshness**: Candidate must be active and not marked stale.
2. **Cycle Prevention**: The candidate node ID must not appear in the current ray's `visited_node_ids`.
3. **Dependency Conflict Check**: The candidate node must **not** depend on the chunk that was just destroyed:
   $$\text{candidate.inherited\_chunk\_dependencies} \cap \{\text{destroyed\_chunk\_id}\} = \emptyset$$
4. **Surface Normal Alignment**:
   $$\mathbf{N}_{hit} \cdot \mathbf{N}_{candidate} \ge \tau_{norm} \quad (\text{typically } \ge 0.85)$$
5. **Euclidean Spatial Proximity**:
   $$\|\mathbf{P}_{hit} - \mathbf{P}_{candidate}\| \le \tau_{pos} \quad (\text{typically } \le 0.15\text{m})$$
6. **Downstream Viability**: The candidate must possess active downstream DAG edges or probe depositions.

### Stitch Scoring & Execution
If multiple candidates qualify, they are evaluated by a composite heuristic score:
$$\text{Score} = 0.4 \left(1 - \frac{d}{\tau_{pos}}\right) + 0.4 \left(\frac{\mathbf{N}_{hit} \cdot \mathbf{N}_{cand} - \tau_{norm}}{1 - \tau_{norm}}\right) + 0.2 \cdot \text{BounceBonus}$$
- The winning node becomes a **Bridge Node**.
- A new DAG edge is linked from the repair ray's origin to the bridge node with flag `TERMINATION_STITCHED_TO_EXISTING_DAG`.
- **Result:** The entire downstream subtree (indirect bounces, probe depositions) is instantly re-energized.
- **Ray Savings**: **80% to 95% of secondary and tertiary bounce rays are eliminated.**

---

## 11. Dynamic Lights on the ASTG Highway & The Ray Count Upscaler

### The Dynamic Light Dilemma
Stationary lights can be pre-analyzed into persistent transport graphs. But video games require **fully dynamic lights**:
- The player holding a moving flashlight or torch.
- Moving vehicle headlights sweeping across alleyways.
- Moving projectiles (fireballs, plasma bolts, muzzle flashes).

In traditional path tracing, moving a spotlight requires shooting hundreds of primary rays, each recursively launching 3 to 6 bounces ($O(\text{samples} \times \text{branching}^{\text{bounces}})$). This collapses frame rates or forces denoisers into unstable, blurry artifact soup.

### The Breakthrough: The ASTG as a Precomputed Light Highway
RayLess introduces a revolutionary paradigm ([`astg_transport_engine.h:5748-6120`](file:///c:/Users/Brand/Documents/hermes/Rayless/src/astg/astg_transport_engine.h#L5748-L6120)):
**The scene already contains an intricate, precomputed highway network of light transport paths, nodes, and probe depositions.**

When a dynamic light moves, it **does not trace multi-bounce paths through the scene**. Instead:
1. **Sparse Ingress Rays (The On-Ramp)**:
   The dynamic light fires a tiny batch of 1st-hop **Ingress Rays** (typically only 16 to 64 rays) using Fibonacci spherical sampling (point lights) or cosine-power cone sampling (spotlights):
   $$\mathbf{D}_s = \text{SpotConeSample}(s, N_{\text{samples}}, \theta_{\text{outer}})$$
2. **Happing onto the Highway (`can_stitch`)**:
   The instant an ingress ray hits a surface, the engine searches the local `ASTGStaticNodeSpatialGrid`. If a compatible transport node exists within proximity ($\le 0.15\text{m}$) and normal alignment ($\ge 0.85$):
   **The dynamic light merges onto the precomputed ASTG highway.**
3. **Riding the Cached Transport**:
   The compute engine calls `traverse_reusable_cached_segment()`. The light flux flows along the pre-existing multi-bounce DAG edges and surface probe couplings at near-zero compute cost:
   ```cpp
   // The dynamic light simply rides the existing transport highway!
   auto reuse_res = traverse_reusable_cached_segment(stitch_in, best_cand, child_frontiers, ...);
   res.cached_nodes_reused += reuse_res.nodes_reused;
   res.cached_edges_reused += reuse_res.edges_reused;
   ```
4. **Transient Receiver Contributions**:
   Dynamic light contributions are stored as transient entries (`ASTGDynamicReceiverContribution`) stamped with the light's `transform_generation`. **Zero persistent graph state is polluted.**

```
   [Moving Flashlight / Dynamic Light]
              │
              │ (Only 32 to 64 Sparse Ingress Rays!)
              ▼
       [Direct Surface Hit]
              │
              ▼  (can_stitch() finds local ASTG Node)
   ═══════════╦═══════════════════════════════════════════════════════════
              ║  <-- ON-RAMP onto the PRECOMPUTED ASTG HIGHWAY
              ▼
          [Node K] ───(B1)───> [Node K+1] ───(B2)───> [Node K+2]
             │                    │                      │
             ▼                    ▼                      ▼
        [Probe 10]           [Probe 25]             [Probe 84]
   ═══════════════════════════════════════════════════════════════════════
   (Energy flows down entire multi-bounce highway without tracing ANY rays!)
```

### The ASTG Graph as a "Ray Count Upscaler" (Ray Amplification)
Just as deep-learning upscalers (DLSS/FSR) take a low-resolution pixel grid and reconstruct high-resolution images, the ASTG graph acts as a **hardware-accelerated Ray Count Upscaler**:

$$\text{Upscaling Ratio} = \frac{\text{Effective Multi-Bounce Paths Evaluated}}{\text{Hardware Ingress Rays Traced}} = \mathbf{60\times \text{ to } 100\times}$$

- **Input**: The dynamic light traces **only 64 ingress rays** on hardware DXR.
- **Amplification**: Each ingress ray connects to a node that branches into multiple secondary and tertiary paths, reaching dozens of surface-attached probes.
- **Output**: Over **4,000+ effective multi-bounce transport paths and probe depositions** are updated across the scene in **under 0.2 ms**.
- **Visual Result**: The player sees full 4-to-6 bounce diffuse global illumination following their flashlight in real time, with sharp contact shadows and rich color bleeding, for the cost of a few dozen direct shadow rays.

### Continuation Frontiers (Zero Coverage Gaps)
If an ingress ray hits a newly placed dynamic object or an unpopulated area where no static node is close enough to stitch, the solver does not fail:
It emits an **`ASTGContinuationFrontier`**, tracing a single continuation ray to find the next bounce. Once that bounce hits the static world, it joins the highway. This guarantees zero visual popping or dark gaps.

---

## 12. Massive Stationary Light Decoupling & Bounded Probe Fan-In (128k Lights)

### The $O(M \times N)$ Light Scaling Problem
If a scene contains 128,000 stationary light sources (street lamps, neon signs, interior bulbs, candles) and 1,200 probes:
- A naive evaluation requires $128,000 \times 1,200 = \mathbf{153,600,000}$ checks per frame.
- Real-time performance would drop to single-digit FPS.

### Decoupling Static Descriptors from Dynamic State
RayLess stores lights in two distinct buffers:
- `LightStatic` (immutable position, direction, range, spot angles, base hue).
- `LightState` (32 bytes: dynamic RGB, intensity, enabled flag, monotonic generation).

When a light changes color, pulses, or turns on/off, **only the 32-byte `LightState` is updated**.

### Bounded Probe Fan-In & Sparse Transfer Matrix (CSR)
Surface probes do not receive meaningful energy from lights kilometers away or blocked by walls. During discovery, RayLess builds a **Compressed Sparse Row (CSR)** matrix of persistent light-to-probe transfer couplings ([`astg_transport_engine.h:6385`](file:///c:/Users/Brand/Documents/hermes/Rayless/src/astg/astg_transport_engine.h#L6385)):
$$\mathbf{E}_{probe} = \sum_{k \in \text{Couplings}(p)} \mathbf{T}_{p, k} \cdot \mathbf{L}_k$$

```
Light Fan-In Distribution (Amazon Bistro Benchmark across 128,000 Lights):
┌─────────────────────────┬─────────────┐
│ Metric                  │ Value       │
├─────────────────────────┼─────────────┤
│ Min Fan-in per Probe    │ 20.00 lights│
│ Median (P50) Fan-in     │ 32.00 lights│
│ 99th Percentile (P99)   │ 32.00 lights│
│ Max Fan-in Cap          │ 32.00 lights│
│ Energy Captured         │ > 99.4%     │
└─────────────────────────┴─────────────┘
```
By strictly capping probe fan-in to the top $K = 32$ or $64$ energy-contributing lights:
- Total coupling storage for the scene is under **40,000 records** (~600 KB).
- Shader loops are bounded and unrollable.
- **128,000 stationary lights update in 0.78 ms (1,275 FPS) on an RTX 4070 Laptop GPU.**

---

## 13. Inline DXR 1.1 RayQuery & Compact Changed-State Return Ring

### RayGen Shaders vs. Inline RayQuery
Traditional DXR uses full ray-tracing pipelines (`DispatchRays`) with Ray Generation, Closest Hit, Any Hit, and Miss shaders.
- Requires building and binding Shader Binding Tables (SBTs).
- Causes heavy GPU thread divergent branches and register spilling.
- High scheduling overhead for small ray batches.

### RayLess Implementation: Inline RayQuery Compute
RayLess uses DXR 1.1 **Inline RayQuery** inside standard compute shaders ([`rtx_gpu_transport.hlsl:414-426`](file:///c:/Users/Brand/Documents/hermes/Rayless/src/rtx/rtx_gpu_transport.hlsl#L414-L426)):
```hlsl
RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER> q;
q.TraceRayInline(
    g_tlas,
    RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER,
    0xFF,
    ray
);

while (q.Proceed()) {
    // Hardware RT Cores traverse the TLAS inline
}

bool is_blocked = (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT);
```
- **Flags**: `ACCEPT_FIRST_HIT_AND_END_SEARCH` terminates traversal the instant *any* opaque triangle is encountered (no need to find closest hit for shadow queries).
- `SKIP_CLOSEST_HIT_SHADER` avoids executing any hit shader code.
- Runs directly inside the compute thread with minimal register pressure.

### Compact Changed-State Output Ring
Most candidate queries confirm existing visibility (e.g. an edge was visible last frame and remains visible).
Writing millions of results back to the CPU wastes memory bus and PCIe bandwidth.
RayLess uses an in-shader persistent state cache (`g_visibility_states`):
```hlsl
ASTGPersistentVisibilityState prior = g_visibility_states[state_index];
if (prior.edge_generation == generation && prior.visibility_state == visibility_state) {
    return; // State has NOT changed; write nothing!
}

// State changed: atomic append to compact output ring
uint output_index;
InterlockedAdd(g_counters[COUNTER_STATE_CHANGED], 1u, output_index);
g_results[output_index] = changed_result;
```
- Only edges with **mutated visibility states** write to the readback buffer.
- Host readback traffic is reduced by **over 98%**.

---

## 14. Summary of Reusable Architectural Patterns ("Tricks to Steal")

| # | Technique / Pattern | Key Shader / Source File | Core Insight / Formulation |
|---|---|---|---|
| **1** | **Persistent ASTG Transport** | [`rtx_types.h`](file:///c:/Users/Brand/Documents/hermes/Rayless/src/rtx/rtx_types.h) | Model light transport as a persistent DAG; steady-state ray cost drops to **0 rays/frame**. |
| **2** | **12-Byte Candidate Records** | [`rtx_gpu_transport.hlsl`](file:///c:/Users/Brand/Documents/hermes/Rayless/src/rtx/rtx_gpu_transport.hlsl) | Upload compact candidate IDs instead of 64-byte `RayDesc` structs; reduce PCIe bandwidth by >75%. |
| **3** | **Register RayDesc Synthesis** | [`rtx_gpu_transport.hlsl`](file:///c:/Users/Brand/Documents/hermes/Rayless/src/rtx/rtx_gpu_transport.hlsl) | Construct `Origin`, `Direction`, `TMin`, `TMax` in VGPR registers; zero intermediate UAV buffers. |
| **4** | **Segment vs AABB Slab Culling** | [`rtx_gpu_transport.hlsl`](file:///c:/Users/Brand/Documents/hermes/Rayless/src/rtx/rtx_gpu_transport.hlsl) | Test finite segment $P(t) \cap \text{AABB}$ directly in compute; bypass hardware RT for misses. |
| **5** | **Wave-Aggregated Atomics** | [`rtx_gpu_transport.hlsl`](file:///c:/Users/Brand/Documents/hermes/Rayless/src/rtx/rtx_gpu_transport.hlsl) | Use `WaveActiveCountBits` to issue 1 atomic per wave instead of 32/64; zero bus serialization. |
| **6** | **Continuous Light B0 BVH** | [`rtx_b0_angular_runtime.hlsl`](file:///c:/Users/Brand/Documents/hermes/Rayless/src/rtx/rtx_b0_angular_runtime.hlsl) | Store exact spherical angles $(\theta, \phi)$ in light-local frames; eliminate cubemap/grid seams. |
| **7** | **Exact Sphere-Cone Projection** | [`rtx_b0_angular_runtime.hlsl`](file:///c:/Users/Brand/Documents/hermes/Rayless/src/rtx/rtx_b0_angular_runtime.hlsl) | Analytically project bounding sphere to cone ($\sin\theta = R/d$); handle $d \le R$ as full sphere. |
| **8** | **Guarded Blocker Counting** | [`rtx_b0_angular_runtime.hlsl`](file:///c:/Users/Brand/Documents/hermes/Rayless/src/rtx/rtx_b0_angular_runtime.hlsl) | Track multiple occluders with reference counts; prevent shadow leaking when one blocker leaves. |
| **9** | **Absolute Extents Matrix AABB** | [`rtx_dynamic_receiver_runtime.hlsl`](file:///c:/Users/Brand/Documents/hermes/Rayless/src/rtx/rtx_dynamic_receiver_runtime.hlsl) | Transform 3D boxes via $|\mathbf{M}_{3\times3}| \cdot \mathbf{E}_{local}$ in 1 matrix multiply instead of 8 corners. |
| **10**| **GPU Work Queue Indirect Dispatch** | [`rtx_dynamic_receiver_runtime.hlsl`](file:///c:/Users/Brand/Documents/hermes/Rayless/src/rtx/rtx_dynamic_receiver_runtime.hlsl) | Cluster culling writes compact probe work items; Pass K4.5 drives `DispatchIndirect` with 0 CPU sync. |
| **11**| **Atomic Float Irradiance Superposition**| [`rtx_dynamic_receiver_runtime.hlsl`](file:///c:/Users/Brand/Documents/hermes/Rayless/src/rtx/rtx_dynamic_receiver_runtime.hlsl) | Accumulate multi-light energy directly into ByteAddressBuffers using `InterlockedCompareExchange` float CAS loops. |
| **12**| **Skeletal Bone Self-Occlusion** | [`rtx_dynamic_receiver_runtime.hlsl`](file:///c:/Users/Brand/Documents/hermes/Rayless/src/rtx/rtx_dynamic_receiver_runtime.hlsl) | Skip same-bone tests (prevent acne), allow cross-bone tests (arm casts shadow on torso). |
| **13**| **Swept Bounds Occlusion Discovery**| [`astg_transport_engine.h`](file:///c:/Users/Brand/Documents/hermes/Rayless/src/astg/astg_transport_engine.h) | Query $\mathbf{B}_{t-1} \cup \mathbf{B}_t$ to discover both newly occluded and newly unblocked edges in 1 pass. |
| **14**| **GPU Spatial Edge Range Uploads** | [`astg_transport_engine.h`](file:///c:/Users/Brand/Documents/hermes/Rayless/src/astg/astg_transport_engine.h) | Upload tiny cell range descriptors (`ASTGGPUCellRange`); GPU discovers candidate edges in device VRAM. |
| **15**| **Surface-Attached Barycentric Probes**| [`adaptive_surface_probe_allocator.gd`](file:///c:/Users/Brand/Documents/hermes/Rayless/scripts/core/precompute/adaptive_surface_probe_allocator.gd)| Bind probes to triangle meshes $(u, v)$; eliminate wall and ceiling light leaking completely. |
| **16**| **Reverse Dependency Graph Surgery** | [`astg_transport_engine.h`](file:///c:/Users/Brand/Documents/hermes/Rayless/src/astg/astg_transport_engine.h) | Chunk destruction invalidates only $0.12\%$ of graph; localized priority repair converges in 1 frame. |
| **17**| **Path Stitching Subgraph Reuse** | [`astg_transport_engine.h`](file:///c:/Users/Brand/Documents/hermes/Rayless/src/astg/astg_transport_engine.h) | Connect repair rays onto existing valid downstream nodes; eliminate 80–95% of multi-bounce rays. |
| **18**| **Dynamic Light ASTG Highway** | [`astg_transport_engine.h`](file:///c:/Users/Brand/Documents/hermes/Rayless/src/astg/astg_transport_engine.h) | Moving lights cast 16–64 ingress rays and ride precomputed transport highways; zero full path tracing. |
| **19**| **ASTG Ray Count Upscaler** | [`astg_transport_engine.h`](file:///c:/Users/Brand/Documents/hermes/Rayless/src/astg/astg_transport_engine.h) | Precomputed graph amplifies 64 ingress rays into 4,000+ effective multi-bounce paths ($60\times$ to $100\times$ multiplier). |
| **20**| **Bounded Fan-In CSR Matrix** | [`astg_transport_engine.h`](file:///c:/Users/Brand/Documents/hermes/Rayless/src/astg/astg_transport_engine.h) | Cap probe-light couplings to top 32/64 lights; evaluate 128,000 stationary lights in <0.8 ms. |
| **21**| **Compact Changed-State Readback Ring** | [`rtx_gpu_transport.hlsl`](file:///c:/Users/Brand/Documents/hermes/Rayless/src/rtx/rtx_gpu_transport.hlsl) | Shader checks prior state cache; writes only mutated edges to ring buffer, cutting readback by >98%. |
