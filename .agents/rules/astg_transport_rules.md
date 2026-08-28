---
trigger: always_on
description: Architectural rules for ASTG continuous B0 visibility, footprint projection, and dynamic receiver hierarchies.
---

# ASTG Continuous Transport & Receiver Rules

1. **Authoritative Continuous B0 State**:
   - Continuous light-relative source ray records (`ASTGB0DirectionRecordGPU`) exclusively drive direct B0 visibility, transport contributions, receiver dependencies, and regeneration requests.
   - Discrete octahedral cell masks (e.g. 64-cell masks) are strictly diagnostic/telemetry aids and must never split visibility authority with continuous records.

2. **Conservative Analytical Footprint Projection**:
   - Continuous angular footprints from a light source to a bounding volume must use exact analytical bounding-sphere cones:
     $$\mathbf{A} = \frac{\mathbf{C} - \mathbf{L}}{\|\mathbf{C} - \mathbf{L}\|}, \quad \theta = \arcsin\left(\frac{R}{\|\mathbf{C} - \mathbf{L}\|}\right)$$
   - If the light source intersects or lies inside the bounding sphere ($d \le R$), conservative full spherical coverage (`cos_half_angle = -1.0f`, `flags = 0x1`) must be assigned.
   - Do not use arbitrary heuristic epsilon constants.

3. **Separation of Static Membership and Dynamic Blocker State**:
   - Light B0 BVHs index all static source hit records regardless of transient blocker state.
   - Dynamic blocker state and reference counting must be stored in persistent auxiliary tables (`b0_record_blocker_counts`, `group_light_blocked_records`).

4. **Dynamic Slot Recycling**:
   - Persistent GPU tracking slots (e.g., bitmask state slots) must use modulo wrapping or recycling (`(next_slot++) % MAX_SLOTS`) to support indefinite execution and multi-suite testing without exhaustion.
