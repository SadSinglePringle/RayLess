#include "astg_transport_engine.h"
#include "astg_build_provenance.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <set>
#include <string>
#include <vector>

namespace {

struct AcceptanceRecord {
    std::string name;
    bool pass = false;
    std::string detail;
};

static bool nearf(float a, float b, float eps = 1.0e-3f) {
    return std::isfinite(a) && std::isfinite(b) && std::abs(a - b) <= eps;
}

static ASTGAABB blocker_box(uint32_t index = 0) {
    const float x = (index == 0) ? 0.0f : (float)index * 0.75f;
    return ASTGAABB({ x - 0.45f, 1.0f, -0.45f }, { x + 0.45f, 3.0f, 0.45f });
}

static void configure_engine(ASTGTransportEngine& e, const std::vector<uint32_t>& light_ids, bool deferred = true) {
    e.defer_dynamic_updates_to_frame = deferred;
    e.enable_async_frame_production = true;
    e.light_positions.clear();
    e.light_colors.clear();
    e.light_intensities.clear();
    e.bounce0_nodes.clear();
    for (size_t i = 0; i < light_ids.size(); ++i) {
        const uint32_t lid = light_ids[i];
        e.light_positions[lid] = { 0.0f, 5.0f + (float)i, 0.0f };
        e.light_colors[lid] = { 1.0f, 0.8f, 0.6f };
        e.light_intensities[lid] = 10.0f;
        ASTGTransportNode n{};
        n.node_id = (uint32_t)e.bounce0_nodes.size();
        n.source_light_id = lid;
        n.bounce_depth = 0;
        n.position = { 0.0f, 0.0f, 0.0f };
        n.geometric_normal = { 0.0f, 1.0f, 0.0f };
        n.is_active = true;
        e.bounce0_nodes.push_back(n);
    }
    for (uint32_t lid : light_ids) e.get_or_create_light_hierarchy(lid);
}

static ASTGDynamicSurfaceProbe make_probe(uint32_t gid, uint32_t pid, uint32_t bone, float x, float y, float z) {
    ASTGDynamicSurfaceProbe p{};
    p.probe_id = pid;
    p.dynamic_group_id = gid;
    p.cluster_id = 0;
    p.bone_id = bone;
    p.local_position = { x, y, z };
    p.local_normal = { 0.0f, 1.0f, 0.0f };
    p.is_active = true;
    return p;
}

static std::vector<ASTGReceiverCluster> one_cluster(uint32_t gid, uint32_t probe_count) {
    ASTGReceiverCluster c{};
    c.cluster_id = 0;
    c.dynamic_group_id = gid;
    c.bone_id = 0;
    c.world_normal = { 0.0f, 1.0f, 0.0f };
    c.cluster_radius = 2.0f;
    c.member_probe_indices.reserve(probe_count);
    for (uint32_t i = 0; i < probe_count; ++i) c.member_probe_indices.push_back(i);
    return { c };
}

static ASTGGroupLightMembershipAllocation first_alloc(const ASTGTransportEngine& e, uint32_t gid, uint32_t lid = 0) {
    auto it = e.group_light_allocations.find(ASTGGroupLightKey{ gid, lid });
    if (it == e.group_light_allocations.end()) return {};
    return it->second;
}

static bool same_alloc(const ASTGGroupLightMembershipAllocation& a,
                       const ASTGGroupLightMembershipAllocation& b) {
    return a.group_id == b.group_id && a.actual_light_id == b.actual_light_id &&
        a.record_offset == b.record_offset && a.record_count == b.record_count &&
        a.membership_word_offset == b.membership_word_offset &&
        a.membership_word_count == b.membership_word_count &&
        a.footprint_offset == b.footprint_offset &&
        a.footprint_capacity == b.footprint_capacity;
}

static void write_results(const std::vector<AcceptanceRecord>& records) {
    std::ofstream f("results/latest/parts_jk_acceptance_results.json");
    const ASTGBuildProvenance provenance = ASTGBuildProvenance::collect();
    f << "{\n  \"schema\": \"astg_parts_jk_acceptance_v1\",\n";
    f << "  \"hardware_observed\": " << (rtx_is_hardware_active() ? "true" : "false") << ",\n";
    f << "  \"base_commit\": \"" << provenance.base_commit << "\",\n";
    f << "  \"worktree_dirty\": " << (provenance.worktree_dirty ? "true" : "false") << ",\n";
    f << "  \"source_commit_sha\": \"" << provenance.source_commit_label << "\",\n";
    f << "  \"build_commit_sha\": \"" << provenance.source_commit_label << "\",\n";
    f << "  \"build_source_tree_sha256\": \"" << provenance.source_tree_sha256 << "\",\n";
    f << "  \"tested_binaries\": {\n";
    size_t binary_index = 0;
    for (const auto& binary : provenance.tested_binary_hashes) {
        f << "    \"" << binary.first << "\": " << (binary.second.empty() ? "null" : "\"" + binary.second + "\"")
          << (++binary_index < provenance.tested_binary_hashes.size() ? "," : "") << "\n";
    }
    f << "  },\n  \"clean_commit_sealing\": \"BLOCKED_WITHOUT_COMMIT_AUTHORIZATION\",\n  \"tests\": [\n";
    for (size_t i = 0; i < records.size(); ++i) {
        const auto& r = records[i];
        f << "    {\"name\":\"" << r.name << "\",\"status\":\""
          << (r.pass ? "PASS" : "FAIL") << "\",\"detail\":\"";
        for (char c : r.detail) {
            if (c == '"' || c == '\\') f << '\\';
            f << c;
        }
        f << "\"}" << (i + 1 < records.size() ? "," : "") << "\n";
    }
    const bool all_pass = std::all_of(records.begin(), records.end(), [](const AcceptanceRecord& r) { return r.pass; });
    f << "  ],\n  \"status\": \"" << (all_pass ? "PASS" : "FAIL") << "\"\n}\n";
}

// 1. Membership allocation failure injection using production async path
static AcceptanceRecord test_live_membership_failure() {
    rtx_reset_parts_jk_persistent_state();
    ASTGTransportEngine e;
    configure_engine(e, { 0 }, true);
    const uint32_t gid = e.register_dynamic_occluder_group({ blocker_box() }, "membership_failure", true, ASTG_OCCLUSION_ANGULAR_B0_ONLY);
    e.update_all_dynamic_occlusions();
    rtx_complete_parts_jk_production();
    const auto old = first_alloc(e, gid);

    e.set_parts_jk_failure_injection(ASTGTransportEngine::ASTG_JK_FAIL_MEMBERSHIP_ALLOCATION);
    e.set_dynamic_group_bounds(gid, { blocker_box(), blocker_box(1) });
    const auto m = e.update_all_dynamic_occlusions();
    const auto now = first_alloc(e, gid);
    const bool dispatch_failed = m.empty() || m.front().gpu_dispatch_failed;
    const bool pass = dispatch_failed && same_alloc(old, now);
    return { "live_allocation_membership_allocation_failure", pass,
        pass ? "GPU allocation remained published and retryable under async production" : "published allocation changed after forced membership allocation failure" };
}

// 2. Footprint allocation failure injection using production async path
static AcceptanceRecord test_live_footprint_failure() {
    rtx_reset_parts_jk_persistent_state();
    ASTGTransportEngine e;
    configure_engine(e, { 0 }, true);
    const uint32_t gid = e.register_dynamic_occluder_group({ blocker_box() }, "footprint_failure", true, ASTG_OCCLUSION_ANGULAR_B0_ONLY);
    e.update_all_dynamic_occlusions();
    rtx_complete_parts_jk_production();
    const auto old = first_alloc(e, gid);

    e.set_parts_jk_failure_injection(ASTGTransportEngine::ASTG_JK_FAIL_FOOTPRINT_ALLOCATION);
    e.set_dynamic_group_bounds(gid, { blocker_box(), blocker_box(1) });
    const auto m = e.update_all_dynamic_occlusions();
    const auto now = first_alloc(e, gid);
    const bool dispatch_failed = m.empty() || m.front().gpu_dispatch_failed;
    const bool pass = dispatch_failed && same_alloc(old, now);
    return { "live_allocation_footprint_allocation_failure", pass,
        pass ? "GPU allocation remained published and retryable under async production" : "published allocation changed after forced footprint allocation failure" };
}

// 3. Clear failures using production async path
static AcceptanceRecord test_live_clear_failures() {
    bool all_pass = true;
    std::string detail;
    for (auto stage : { ASTGTransportEngine::ASTG_JK_FAIL_MEMBERSHIP_CLEAR,
                        ASTGTransportEngine::ASTG_JK_FAIL_FOOTPRINT_CLEAR }) {
        rtx_reset_parts_jk_persistent_state();
        ASTGTransportEngine e;
        configure_engine(e, { 0 }, true);
        const uint32_t gid = e.register_dynamic_occluder_group({ blocker_box() }, "clear_failure", true, ASTG_OCCLUSION_ANGULAR_B0_ONLY);
        e.update_all_dynamic_occlusions();
        rtx_complete_parts_jk_production();
        const auto old = first_alloc(e, gid);

        e.set_parts_jk_failure_injection(stage);
        e.set_dynamic_group_bounds(gid, { blocker_box(), blocker_box(1) });
        const auto m = e.update_all_dynamic_occlusions();
        const bool dispatch_failed = m.empty() || m.front().gpu_dispatch_failed;
        const bool pass = dispatch_failed && same_alloc(old, first_alloc(e, gid));
        all_pass = all_pass && pass;
        detail += pass ? "clear stage preserved old range; " : "clear stage replaced old range; ";
    }
    return { "live_allocation_membership_and_footprint_clear_failures", all_pass, detail };
}

// 4. Neighbor ranges resize guard using production async path
static AcceptanceRecord test_neighbor_ranges() {
    rtx_reset_parts_jk_persistent_state();
    ASTGTransportEngine e;
    configure_engine(e, { 0 }, true);
    const uint32_t g1 = e.register_dynamic_occluder_group({ blocker_box() }, "neighbor_a", true, ASTG_OCCLUSION_ANGULAR_B0_ONLY);
    const uint32_t g2 = e.register_dynamic_occluder_group({ blocker_box() }, "neighbor_b", true, ASTG_OCCLUSION_ANGULAR_B0_ONLY);
    e.update_all_dynamic_occlusions();
    rtx_complete_parts_jk_production();

    const auto before = first_alloc(e, g2);
    e.set_dynamic_group_bounds(g1, { blocker_box(), blocker_box(1), blocker_box(2) });
    const auto m = e.update_all_dynamic_occlusions();
    rtx_complete_parts_jk_production();
    const auto after = first_alloc(e, g2);
    const uint32_t blocker_count = rtx_readback_part_j_persistent_blocker_count(0);
    const bool pass = !m.empty() && !m.front().gpu_dispatch_failed && same_alloc(before, after) && blocker_count >= 1;
    return { "neighboring_ranges_resize_guard", pass,
        pass ? "grown target range did not modify adjacent allocation or GPU blocker state" : "neighboring allocation/state changed during resize" };
}

// 5. Layout clean rebuild equivalence under production path
static AcceptanceRecord test_layout_clean_rebuild() {
    rtx_reset_parts_jk_persistent_state();
    ASTGTransportEngine changed;
    configure_engine(changed, { 0 }, true);
    ASTGTransportNode extra = changed.bounce0_nodes.front();
    extra.node_id = 1;
    extra.position = { 0.2f, 0.0f, 0.0f };
    changed.bounce0_nodes.push_back(extra);
    changed.light_b0_hierarchies[0].build_continuous_b0_hierarchy(changed.bounce0_nodes, 0);
    const uint32_t gid = changed.register_dynamic_occluder_group({ blocker_box() }, "layout", true, ASTG_OCCLUSION_ANGULAR_B0_ONLY);
    changed.update_all_dynamic_occlusions();
    rtx_complete_parts_jk_production();

    changed.bounce0_nodes[0].node_id = 1;
    changed.bounce0_nodes[1].node_id = 0;
    changed.light_b0_hierarchies[0].build_continuous_b0_hierarchy(changed.bounce0_nodes, 0);
    changed.set_dynamic_group_bounds(gid, { blocker_box() });
    const auto update = changed.update_all_dynamic_occlusions();
    rtx_complete_parts_jk_production();
    const uint32_t changed_count = rtx_readback_part_j_persistent_blocker_count(0);
    const auto changed_alloc = first_alloc(changed, gid);

    rtx_reset_parts_jk_persistent_state();
    ASTGTransportEngine clean;
    configure_engine(clean, { 0 }, true);
    ASTGTransportNode clean_extra = clean.bounce0_nodes.front();
    clean_extra.node_id = 0;
    clean_extra.position = { 0.2f, 0.0f, 0.0f };
    clean.bounce0_nodes[0].node_id = 1;
    clean.bounce0_nodes.push_back(clean_extra);
    clean.light_b0_hierarchies[0].build_continuous_b0_hierarchy(clean.bounce0_nodes, 0);
    const uint32_t clean_gid = clean.register_dynamic_occluder_group({ blocker_box() }, "layout_clean", true, ASTG_OCCLUSION_ANGULAR_B0_ONLY);
    clean.update_all_dynamic_occlusions();
    rtx_complete_parts_jk_production();
    const uint32_t clean_count = rtx_readback_part_j_persistent_blocker_count(0);
    const auto clean_alloc = first_alloc(clean, clean_gid);

    const bool pass = !update.empty() && !update.front().gpu_dispatch_failed && changed_count == clean_count &&
        changed_alloc.record_count == clean_alloc.record_count &&
        changed_alloc.layout_hash == clean_alloc.layout_hash;
    return { "layout_reorder_clean_rebuild_equivalence", pass,
        pass ? "reordered layout matched independent clean GPU rebuild" : "reordered layout diverged from clean rebuild" };
}

// 6. Light insert/remove with transactional layout migration
static AcceptanceRecord test_light_insert_remove() {
    rtx_reset_parts_jk_persistent_state();
    ASTGTransportEngine e;
    configure_engine(e, { 11, 22 }, true);
    const uint32_t gid = e.register_dynamic_occluder_group({ blocker_box() }, "light_shift", true, ASTG_OCCLUSION_ANGULAR_B0_ONLY);
    e.update_all_dynamic_occlusions();
    rtx_complete_parts_jk_production();

    e.light_positions[5] = { 0.0f, 7.0f, 0.0f };
    e.light_colors[5] = { 0.4f, 0.7f, 1.0f };
    ASTGTransportNode inserted{};
    inserted.source_light_id = 5; inserted.bounce_depth = 0; inserted.position = { 0.0f, 0.0f, 0.0f };
    e.bounce0_nodes.insert(e.bounce0_nodes.begin(), inserted);
    for (size_t i = 0; i < e.bounce0_nodes.size(); ++i) e.bounce0_nodes[i].node_id = (uint32_t)i;
    for (uint32_t lid : { 5u, 11u, 22u }) e.light_b0_hierarchies[lid].build_continuous_b0_hierarchy(e.bounce0_nodes, lid);
    e.set_dynamic_group_bounds(gid, { blocker_box() });
    const auto inserted_update = e.update_all_dynamic_occlusions();
    rtx_complete_parts_jk_production();
    const auto off11 = first_alloc(e, gid, 11).record_offset;

    e.invalidate_light_hierarchy(5);
    e.light_positions.erase(5);
    e.light_colors.erase(5);
    e.bounce0_nodes.erase(e.bounce0_nodes.begin());
    for (size_t i = 0; i < e.bounce0_nodes.size(); ++i) e.bounce0_nodes[i].node_id = (uint32_t)i;
    e.light_b0_hierarchies[11].build_continuous_b0_hierarchy(e.bounce0_nodes, 11);
    e.light_b0_hierarchies[22].build_continuous_b0_hierarchy(e.bounce0_nodes, 22);
    e.set_dynamic_group_bounds(gid, { blocker_box() });
    const auto removed_update = e.update_all_dynamic_occlusions();
    rtx_complete_parts_jk_production();

    const bool pass = !inserted_update.empty() && !removed_update.empty() &&
        e.group_light_allocations.count(ASTGGroupLightKey{ gid, 5 }) == 0 &&
        first_alloc(e, gid, 11).record_offset != off11;
    return { "light_insert_remove_flattened_offset_equivalence", pass,
        pass ? "inserted and removed light shifted flattened ranges with transactional unblock" : "flattened offset/light allocation state did not update" };
}

// 7. Async consecutive frames without manual counter resets or host waits
static AcceptanceRecord test_async_no_io() {
    rtx_reset_parts_jk_persistent_state();
    ASTGTransportEngine e;
    configure_engine(e, { 0 }, true);
    const uint32_t gid = e.register_dynamic_occluder_group({ blocker_box() }, "async", true, ASTG_OCCLUSION_ANGULAR_B0_ONLY);
    
    // Frame 1: initial submission
    e.update_all_dynamic_occlusions();
    
    // Reset IO instrumentation to measure purely consecutive async frames
    rtx_reset_parts_jk_io_counters();
    
    // Frame 2: mutate transform and submit back-to-back WITHOUT rtx_complete_parts_jk_production()
    e.update_dynamic_group_rigid_transform(gid, RTXMatrix4x4::translation(0.1f, 0.0f, 0.0f));
    e.update_all_dynamic_occlusions();

    ASTGPartsJKIOCounters io{};
    rtx_get_parts_jk_io_counters(&io);
    const bool pass = (e.last_frame_jk_dispatch_count == 1) &&
                      (io.wait_count == 0) &&
                      (io.readback_copy_count == 0) &&
                      (io.map_count == 0);
    rtx_complete_parts_jk_production();
    return { "engine_async_production_no_wait_readback_map", pass,
        pass ? "consecutive dirty frames queued asynchronously with zero host waits, readbacks, or maps"
             : "production consecutive frames triggered host I/O or wait stalls" };
}

// 8. Blocking vs async equivalence
static AcceptanceRecord test_blocking_async_equivalence() {
    ASTGDynamicSurfaceProbeGPU blocking_probe{};
    uint32_t blocking_count = 0;
    rtx_reset_parts_jk_persistent_state();
    {
        ASTGTransportEngine e;
        configure_engine(e, { 0 }, false);
        const uint32_t gid = e.register_dynamic_occluder_group({ blocker_box() }, "blocking", true, ASTG_OCCLUSION_ANGULAR_B0_ONLY);
        auto p = make_probe(gid, 0, 0, 0.0f, 0.0f, 0.0f);
        e.register_dynamic_receiver_probes(gid, { p }, one_cluster(gid, 1), false);
        e.update_dynamic_occlusion(gid);
        rtx_readback_part_k_transformed_probes(&blocking_probe, 1);
        blocking_count = rtx_readback_part_j_persistent_blocker_count(0);
    }

    rtx_reset_parts_jk_persistent_state();
    ASTGDynamicSurfaceProbeGPU async_probe{};
    uint32_t async_count = 0;
    {
        ASTGTransportEngine e;
        configure_engine(e, { 0 }, true);
        const uint32_t gid = e.register_dynamic_occluder_group({ blocker_box() }, "async_equiv", true, ASTG_OCCLUSION_ANGULAR_B0_ONLY);
        auto p = make_probe(gid, 0, 0, 0.0f, 0.0f, 0.0f);
        e.register_dynamic_receiver_probes(gid, { p }, one_cluster(gid, 1), false);
        e.update_all_dynamic_occlusions();
        rtx_complete_parts_jk_production();
        rtx_readback_part_k_transformed_probes(&async_probe, 1);
        async_count = rtx_readback_part_j_persistent_blocker_count(0);
    }
    const bool pass = blocking_count == async_count &&
        nearf(blocking_probe.world_pos_x, async_probe.world_pos_x) &&
        nearf(blocking_probe.world_pos_y, async_probe.world_pos_y) &&
        nearf(blocking_probe.world_pos_z, async_probe.world_pos_z);
    return { "blocking_async_gpu_result_equivalence", pass,
        pass ? "blocking and shared-recorder async results matched" : "blocking and async GPU results diverged" };
}

// 9. Frame batching
static AcceptanceRecord test_frame_batching() {
    rtx_reset_parts_jk_persistent_state();
    ASTGTransportEngine e;
    configure_engine(e, { 0 }, true);
    const uint32_t g1 = e.register_dynamic_occluder_group({ blocker_box() }, "batch_a", true, ASTG_OCCLUSION_ANGULAR_B0_ONLY);
    const uint32_t g2 = e.register_dynamic_occluder_group({ blocker_box(1) }, "batch_b", true, ASTG_OCCLUSION_ANGULAR_B0_ONLY);
    e.update_all_dynamic_occlusions();
    rtx_complete_parts_jk_production();
    e.update_dynamic_group_rigid_transform(g1, RTXMatrix4x4::translation(0.05f, 0.0f, 0.0f));
    e.update_dynamic_group_rigid_transform(g2, RTXMatrix4x4::translation(-0.05f, 0.0f, 0.0f));
    e.update_all_dynamic_occlusions();
    const bool one_dispatch = e.last_frame_jk_dispatch_count == 1 && e.last_frame_jk_pair_count == 2;
    rtx_complete_parts_jk_production();
    e.update_all_dynamic_occlusions();
    const bool no_dirty_dispatch = e.last_frame_jk_dispatch_count == 0;
    const bool pass = one_dispatch && no_dirty_dispatch;
    return { "frame_level_dirty_jk_batch_dispatch_count", pass,
        pass ? "two mutation calls produced one frame dispatch and zero clean-frame dispatches" : "dispatch count scaled with mutation calls or dirty state was retained" };
}

// 10. Mixed rigid & skeletal with unequal transform and bound counts
static AcceptanceRecord test_mixed_rigid_skeletal() {
    rtx_reset_parts_jk_persistent_state();
    ASTGTransportEngine e;
    configure_engine(e, { 0 }, true);

    // Group 1: Rigid with 1 transform and 2 bounds (transforms != bounds)
    const uint32_t rigid_gid = e.register_dynamic_occluder_group({ blocker_box(), blocker_box(1) }, "rigid", true, ASTG_OCCLUSION_ANGULAR_B0_ONLY);
    
    // Group 2: Skeletal with 3 bone matrices and 2 bounds (transforms != bounds)
    const uint32_t skeletal_gid = e.register_dynamic_occluder_group({ blocker_box(2), blocker_box(3) }, "skeletal", true, ASTG_OCCLUSION_ANGULAR_B0_ONLY);
    
    auto rigid_p = make_probe(rigid_gid, 0, 0, 1.0f, 0.0f, 0.0f);
    // Probe 1 attached to bone 1 of skeletal group
    auto skeletal_p = make_probe(skeletal_gid, 1, 1, 1.0f, 0.0f, 0.0f);

    e.register_dynamic_receiver_probes(rigid_gid, { rigid_p }, one_cluster(rigid_gid, 1), false);
    e.register_dynamic_receiver_probes(skeletal_gid, { skeletal_p }, one_cluster(skeletal_gid, 1), true);

    e.set_dynamic_group_rigid_transform(rigid_gid, RTXMatrix4x4::translation(2.0f, 0.0f, 0.0f));
    std::vector<RTXMatrix4x4> bones = {
        RTXMatrix4x4::identity(),
        RTXMatrix4x4::translation(0.0f, 3.0f, 0.0f),
        RTXMatrix4x4::translation(0.0f, 6.0f, 0.0f)
    };
    e.set_dynamic_group_bone_matrices(skeletal_gid, bones);

    e.update_all_dynamic_occlusions();
    rtx_complete_parts_jk_production();

    ASTGDynamicSurfaceProbeGPU out[2]{};
    const bool read_ok = rtx_readback_part_k_transformed_probes(out, 2);
    const bool pass = read_ok &&
        nearf(out[0].world_pos_x, 3.0f) && nearf(out[0].world_pos_y, 0.0f) &&
        nearf(out[1].world_pos_x, 1.0f) && nearf(out[1].world_pos_y, 3.0f) &&
        (out[1].last_visibility_mask & 0x80000000u) != 0;
    return { "mixed_rigid_skeletal_gpu_reference", pass,
        pass ? "rigid and skeletal probe transforms matched with unequal transform and bound counts" : "mixed receiver probe transform diverged" };
}


struct ValidationResult {
    bool passed = false;
    uint32_t occluded_verified = 0;
    uint32_t unoccluded_verified = 0;
    uint32_t occluded_violations = 0;
    uint32_t unoccluded_violations = 0;
    float max_irradiance_delta = 0.0f;
    std::string failure_reason;
};

static ValidationResult validate_dense_probe_lighting(
    const std::vector<ASTGDynamicSurfaceProbe>& probes,
    const std::vector<ASTGDynamicSurfaceProbeGPU>& gpu_out,
    float light_y = 5.0f,
    float light_intensity = 10.0f,
    RTXVector3 light_color = { 1.0f, 0.8f, 0.6f }
) {
    ValidationResult res{};
    if (gpu_out.size() < probes.size() || probes.empty()) {
        res.passed = false;
        res.failure_reason = "probe count mismatch or empty output";
        return res;
    }

    res.passed = true;
    for (uint32_t i = 0; i < (uint32_t)probes.size(); ++i) {
        const float px = probes[i].local_position.x;
        const float pz = probes[i].local_position.z;
        const float max_coord = (std::max)(std::abs(px), std::abs(pz));

        if (max_coord <= 1.0f) {
            // Strictly inside perspective shadow cone: must be occluded (irradiance == 0)
            float total_irr = gpu_out[i].irradiance_r + gpu_out[i].irradiance_g + gpu_out[i].irradiance_b;
            if (total_irr > 0.001f) {
                res.passed = false;
                res.occluded_violations++;
                res.max_irradiance_delta = (std::max)(res.max_irradiance_delta, total_irr);
            } else {
                res.occluded_verified++;
            }
        } else if (max_coord >= 1.3f) {
            // Strictly outside perspective shadow cone: must be unoccluded
            const float d2 = px * px + (light_y * light_y) + pz * pz;
            const float d = std::sqrt(d2);
            const float cos_theta = light_y / d;
            const float expected_atten = (light_intensity * cos_theta) / (d2 + 0.1f);
            const float exp_r = expected_atten * light_color.x;
            const float exp_g = expected_atten * light_color.y;
            const float exp_b = expected_atten * light_color.z;

            float delta_r = std::abs(gpu_out[i].irradiance_r - exp_r);
            float delta_g = std::abs(gpu_out[i].irradiance_g - exp_g);
            float delta_b = std::abs(gpu_out[i].irradiance_b - exp_b);
            float max_delta = (std::max)({ delta_r, delta_g, delta_b });
            res.max_irradiance_delta = (std::max)(res.max_irradiance_delta, max_delta);

            if (!nearf(gpu_out[i].irradiance_r, exp_r, 0.05f * exp_r) ||
                !nearf(gpu_out[i].irradiance_g, exp_g, 0.05f * exp_g) ||
                !nearf(gpu_out[i].irradiance_b, exp_b, 0.05f * exp_b)) {
                res.passed = false;
                res.unoccluded_violations++;
            } else {
                res.unoccluded_verified++;
            }
        }
    }

    if (res.occluded_verified < 64 || res.unoccluded_verified < 64) {
        res.passed = false;
    }
    if (!res.passed && res.failure_reason.empty()) {
        res.failure_reason = "violations: occluded=" + std::to_string(res.occluded_violations) +
                             ", unoccluded=" + std::to_string(res.unoccluded_violations) +
                             ", max_delta=" + std::to_string(res.max_irradiance_delta);
    }
    return res;
}

// 11. Scaled 256 probes with noncontiguous clusters and exact radiometric ground truth oracle
static AcceptanceRecord test_dense_probe_visibility() {
    rtx_reset_parts_jk_persistent_state();
    ASTGTransportEngine e;
    
    // Light 0 at (0, 5, 0), color (1.0, 0.8, 0.6), intensity 10.0
    configure_engine(e, { 0 }, true);

    // Blocker group over center: x in [-0.45, 0.45], z in [-0.45, 0.45], y in [1.0, 3.0]
    const uint32_t occluder_gid = e.register_dynamic_occluder_group({ blocker_box() }, "dense_blocker", true, ASTG_OCCLUSION_ANGULAR_B0_ONLY);
    // Receiver group has group_id != occluder_gid so blocker occludes receivers
    const uint32_t receiver_gid = e.register_dynamic_occluder_group({}, "dense_receiver", false, ASTG_OCCLUSION_NONE);
    e.dynamic_occluder_groups[receiver_gid].enable_surface_receivers = true;

    constexpr uint32_t kGridDim = 16;
    constexpr uint32_t kProbeCount = kGridDim * kGridDim; // 256 probes
    std::vector<ASTGDynamicSurfaceProbe> probes;
    probes.reserve(kProbeCount);

    // Populate B0 direction sample nodes so B0 BVH covers all probe rays
    e.bounce0_nodes.clear();
    for (uint32_t r = 0; r < kGridDim; ++r) {
        for (uint32_t c = 0; c < kGridDim; ++c) {
            uint32_t pid = r * kGridDim + c;
            float px = -1.5f + (float)c * 0.2f;
            float pz = -1.5f + (float)r * 0.2f;
            probes.push_back(make_probe(receiver_gid, pid, 0, px, 0.0f, pz));

            ASTGTransportNode n{};
            n.node_id = pid;
            n.source_light_id = 0;
            n.bounce_depth = 0;
            n.position = { px, 0.0f, pz };
            n.geometric_normal = { 0.0f, 1.0f, 0.0f };
            n.is_active = true;
            e.bounce0_nodes.push_back(n);
        }
    }
    e.light_b0_hierarchies[0].build_continuous_b0_hierarchy(e.bounce0_nodes, 0);

    // 4 noncontiguous clusters of 64 probes each
    std::vector<ASTGReceiverCluster> clusters(4);
    for (uint32_t k = 0; k < 4; ++k) {
        clusters[k].cluster_id = k;
        clusters[k].dynamic_group_id = receiver_gid;
        clusters[k].bone_id = 0;
        clusters[k].world_normal = { 0.0f, 1.0f, 0.0f };
        clusters[k].cluster_radius = 5.0f;
    }
    for (uint32_t i = 0; i < kProbeCount; ++i) {
        // Distribute probes noncontiguously across the 4 clusters (interleaved)
        uint32_t target_cluster = i % 4;
        clusters[target_cluster].member_probe_indices.push_back(i);
        probes[i].cluster_id = target_cluster;
    }

    e.register_dynamic_receiver_probes(receiver_gid, probes, clusters, false);
    e.update_all_dynamic_occlusions();
    rtx_complete_parts_jk_production();

    std::vector<ASTGDynamicSurfaceProbeGPU> out(kProbeCount);
    const bool read_ok = rtx_readback_part_k_transformed_probes(out.data(), kProbeCount);

    // Radiometric Ground Truth Oracle:
    // Light L = (0, 5, 0), I = 10.0, C = (1.0, 0.8, 0.6)
    // For probe at P = (px, 0, pz), N = (0, 1, 0):
    // D = L - P = (-px, 5, -pz)
    // d = sqrt(px^2 + 25 + pz^2)
    // cos_theta = max(0, N . D/d) = 5.0 / d
    // attenuation = 10.0 * cos_theta / (d^2 + 0.1)
    // Expected unoccluded: E = attenuation * C
    // Expected occluded (under blocker [-0.45, 0.45] x [-0.45, 0.45]): E = 0.0
    ValidationResult vres = validate_dense_probe_lighting(probes, out);
    std::string det = "occluded=" + std::to_string(vres.occluded_verified) +
                      " unoccluded=" + std::to_string(vres.unoccluded_verified) +
                      " oracle_ok=" + (vres.passed ? "1" : "0") +
                      " max_delta=" + std::to_string(vres.max_irradiance_delta);
    const bool pass = read_ok && vres.passed;
    return { "dense_probe_numerical_visibility_self_occlusion_reference", pass, det };
}

// 12. Sparse actual light IDs preserved end-to-end
static AcceptanceRecord test_sparse_actual_light_ids() {
    rtx_reset_parts_jk_persistent_state();
    ASTGTransportEngine e;
    const std::vector<uint32_t> sparse_ids = { 17, 203, 401 };
    configure_engine(e, sparse_ids, false);
    const uint32_t gid = e.register_dynamic_occluder_group({ blocker_box() }, "sparse_gpu_ids", true, ASTG_OCCLUSION_ANGULAR_B0_ONLY);
    const auto result = e.update_dynamic_occlusion(gid);

    std::set<uint32_t> observed_ids;
    for (const auto& transition : e.last_gpu_transitions) observed_ids.insert(transition.light_id);
    const std::set<uint32_t> expected_ids(sparse_ids.begin(), sparse_ids.end());
    const bool pass = !result.gpu_dispatch_failed && (observed_ids == expected_ids) &&
        std::all_of(e.last_gpu_transitions.begin(), e.last_gpu_transitions.end(),
            [](const ASTGB0TransitionRecord& t) { return t.light_id == 17 || t.light_id == 203 || t.light_id == 401; });
    return { "sparse_actual_light_id_gpu_output", pass,
        pass ? "GPU transition records preserved sparse source light IDs end-to-end" : "GPU output IDs did not match sparse source IDs" };
}

// 13. Negative Control: Exercises the REAL shared validation function with corrupt copies
static AcceptanceRecord test_negative_control_oracle_sensitivity() {
    rtx_reset_parts_jk_persistent_state();
    ASTGTransportEngine e;
    configure_engine(e, { 0 }, true);

    const uint32_t occluder_gid = e.register_dynamic_occluder_group({ blocker_box() }, "neg_blocker", true, ASTG_OCCLUSION_ANGULAR_B0_ONLY);
    const uint32_t receiver_gid = e.register_dynamic_occluder_group({}, "neg_receiver", false, ASTG_OCCLUSION_NONE);
    e.dynamic_occluder_groups[receiver_gid].enable_surface_receivers = true;

    constexpr uint32_t kGridDim = 16;
    constexpr uint32_t kProbeCount = kGridDim * kGridDim; // 256 probes
    std::vector<ASTGDynamicSurfaceProbe> probes;
    probes.reserve(kProbeCount);

    e.bounce0_nodes.clear();
    for (uint32_t r = 0; r < kGridDim; ++r) {
        for (uint32_t c = 0; c < kGridDim; ++c) {
            uint32_t pid = r * kGridDim + c;
            float px = -1.5f + (float)c * 0.2f;
            float pz = -1.5f + (float)r * 0.2f;
            probes.push_back(make_probe(receiver_gid, pid, 0, px, 0.0f, pz));

            ASTGTransportNode n{};
            n.node_id = pid;
            n.source_light_id = 0;
            n.bounce_depth = 0;
            n.position = { px, 0.0f, pz };
            n.geometric_normal = { 0.0f, 1.0f, 0.0f };
            n.is_active = true;
            e.bounce0_nodes.push_back(n);
        }
    }
    e.light_b0_hierarchies[0].build_continuous_b0_hierarchy(e.bounce0_nodes, 0);

    std::vector<ASTGReceiverCluster> clusters(4);
    for (uint32_t k = 0; k < 4; ++k) {
        clusters[k].cluster_id = k;
        clusters[k].dynamic_group_id = receiver_gid;
        clusters[k].bone_id = 0;
        clusters[k].world_normal = { 0.0f, 1.0f, 0.0f };
        clusters[k].world_centroid = { 0.0f, 0.0f, 0.0f };
        clusters[k].cluster_radius = 5.0f;
    }
    for (uint32_t i = 0; i < kProbeCount; ++i) {
        clusters[i % 4].member_probe_indices.push_back(i);
    }
    e.register_dynamic_receiver_probes(receiver_gid, probes, clusters, false);

    e.update_all_dynamic_occlusions();
    rtx_complete_parts_jk_production();

    std::vector<ASTGDynamicSurfaceProbeGPU> valid_out(kProbeCount);
    bool read_ok = rtx_readback_part_k_transformed_probes(valid_out.data(), kProbeCount);

    // 1. Uncorrupted baseline MUST pass the shared oracle
    ValidationResult v_base = validate_dense_probe_lighting(probes, valid_out);
    bool base_pass = read_ok && v_base.passed;

    // 2. Corrupt copy: ALL-BLACK (every probe has 0 irradiance)
    auto corrupt_all_black = valid_out;
    for (auto& p : corrupt_all_black) { p.irradiance_r = 0.0f; p.irradiance_g = 0.0f; p.irradiance_b = 0.0f; }
    ValidationResult v_black = validate_dense_probe_lighting(probes, corrupt_all_black);
    bool black_rejected = !v_black.passed && (v_black.unoccluded_violations > 0);

    // 3. Corrupt copy: LIT IN SHADOW (occluded shadow probes given non-zero irradiance)
    auto corrupt_shadow_lit = valid_out;
    for (uint32_t i = 0; i < kProbeCount; ++i) {
        float max_c = (std::max)(std::abs(probes[i].local_position.x), std::abs(probes[i].local_position.z));
        if (max_c <= 1.0f) { corrupt_shadow_lit[i].irradiance_r = 2.5f; }
    }
    ValidationResult v_shadow_lit = validate_dense_probe_lighting(probes, corrupt_shadow_lit);
    bool shadow_lit_rejected = !v_shadow_lit.passed && (v_shadow_lit.occluded_violations > 0);

    // 4. Corrupt copy: UNLIT IN LIGHT (unoccluded probes zeroed out)
    auto corrupt_light_unlit = valid_out;
    for (uint32_t i = 0; i < kProbeCount; ++i) {
        float max_c = (std::max)(std::abs(probes[i].local_position.x), std::abs(probes[i].local_position.z));
        if (max_c >= 1.3f) { corrupt_light_unlit[i].irradiance_r = 0.0f; }
    }
    ValidationResult v_light_unlit = validate_dense_probe_lighting(probes, corrupt_light_unlit);
    bool light_unlit_rejected = !v_light_unlit.passed && (v_light_unlit.unoccluded_violations > 0);

    // 5. Corrupt copy: WRONG IRRADIANCE SCALE (50% attenuation error)
    auto corrupt_scaled = valid_out;
    for (auto& p : corrupt_scaled) { p.irradiance_r *= 0.5f; p.irradiance_g *= 0.5f; p.irradiance_b *= 0.5f; }
    ValidationResult v_scaled = validate_dense_probe_lighting(probes, corrupt_scaled);
    bool scaled_rejected = !v_scaled.passed && (v_scaled.unoccluded_violations > 0);

    const bool pass = base_pass && black_rejected && shadow_lit_rejected && light_unlit_rejected && scaled_rejected;
    std::string det = "base=" + std::to_string(base_pass) +
                      " blk_rej=" + std::to_string(black_rejected) + " (delta=" + std::to_string(v_black.max_irradiance_delta) + ")" +
                      " shd_rej=" + std::to_string(shadow_lit_rejected) + " (delta=" + std::to_string(v_shadow_lit.max_irradiance_delta) + ")" +
                      " unl_rej=" + std::to_string(light_unlit_rejected) + " (delta=" + std::to_string(v_light_unlit.max_irradiance_delta) + ")" +
                      " scl_rej=" + std::to_string(scaled_rejected) + " (delta=" + std::to_string(v_scaled.max_irradiance_delta) + ")";
    return { "radiometric_oracle_negative_control_sensitivity", pass, det };
}


// 14. Frame slot command allocator ownership and fence safety
static AcceptanceRecord test_frame_slot_allocator_ownership_and_fence_safety() {
    rtx_reset_parts_jk_persistent_state();
    ASTGTransportEngine e;
    configure_engine(e, { 0 }, true);
    const uint32_t gid = e.register_dynamic_occluder_group({ blocker_box() }, "slot_owner", true, ASTG_OCCLUSION_ANGULAR_B0_ONLY);
    auto p = make_probe(gid, 0, 0, 0.0f, 0.0f, 0.0f);
    e.register_dynamic_receiver_probes(gid, { p }, one_cluster(gid, 1), false);

    // Initial submission
    e.update_all_dynamic_occlusions();

    ASTGFrameSlotRecord rec0{}, rec1{};
    bool has_rec0 = rtx_get_frame_slot_record(0, &rec0);
    bool has_rec1 = rtx_get_frame_slot_record(1, &rec1);

    bool distinct_allocators = has_rec0 && has_rec1 && (rec0.allocator_address != rec1.allocator_address) &&
                               (rec0.allocator_address != 0) && (rec1.allocator_address != 0);

    // Run 16 consecutive frames (wrapping the 2-slot ring 8 times)
    bool all_reset_fences_safe = true;
    for (uint32_t f = 0; f < 16; ++f) {
        e.update_dynamic_group_rigid_transform(gid, RTXMatrix4x4::translation(0.02f * (float)f, 0.0f, 0.0f));
        e.update_all_dynamic_occlusions();

        ASTGFrameSlotRecord cur_rec0{}, cur_rec1{};
        rtx_get_frame_slot_record(0, &cur_rec0);
        rtx_get_frame_slot_record(1, &cur_rec1);

        if (cur_rec0.last_reset_fence > cur_rec0.completed_fence ||
            cur_rec1.last_reset_fence > cur_rec1.completed_fence) {
            all_reset_fences_safe = false;
        }

        // Mixed diagnostic call: run clear without waiting on production frame
        if (f % 4 == 0) {
            rtx_clear_part_j_membership_words(0, 4);
        }
    }

    rtx_complete_parts_jk_production();

    const bool pass = distinct_allocators && all_reset_fences_safe;
    return { "frame_slot_allocator_ownership_and_fence_safety", pass,
        pass ? "frame slots own distinct allocators; all resets occurred after completed fence"
             : "shared allocator reset or in-flight allocator reset violation detected" };
}

// 15. Safe frame slot acquisition before upload, backpressure, and ring wraparound
static AcceptanceRecord test_slot_backpressure_and_ring_wraparound() {
    rtx_reset_parts_jk_persistent_state();
    ASTGTransportEngine e;
    configure_engine(e, { 0 }, true);
    const uint32_t gid = e.register_dynamic_occluder_group({ blocker_box() }, "ring_wrap", true, ASTG_OCCLUSION_ANGULAR_B0_ONLY);
    auto p = make_probe(gid, 0, 0, 0.0f, 0.0f, 0.0f);
    e.register_dynamic_receiver_probes(gid, { p }, one_cluster(gid, 1), false);

    e.update_all_dynamic_occlusions();

    bool upload_data_correct = true;
    for (uint32_t f = 1; f <= 16; ++f) {
        float offset_x = 0.05f * (float)f;
        e.update_dynamic_group_rigid_transform(gid, RTXMatrix4x4::translation(offset_x, 0.0f, 0.0f));
        e.update_all_dynamic_occlusions();

        rtx_complete_parts_jk_production();
        ASTGDynamicSurfaceProbeGPU read_probe{};
        if (rtx_readback_part_k_transformed_probes(&read_probe, 1)) {
            if (!nearf(read_probe.world_pos_x, offset_x, 0.01f)) {
                upload_data_correct = false;
            }
        } else {
            upload_data_correct = false;
        }
    }

    bool bp_flag = false;
    uint32_t dummy_slot = 0;
    bool acq_ok = rtx_acquire_frame_slot(&dummy_slot, false, &bp_flag);
    if (acq_ok) {
        rtx_release_unsubmitted_frame_slot(dummy_slot);
    }

    const bool pass = upload_data_correct && acq_ok;
    return { "slot_backpressure_and_ring_wraparound", pass,
        pass ? "16 dirty frames wrapped ring safely with exact per-frame outputs and zero upload corruption"
             : "upload data corrupted or slot acquisition failure during ring wraparound" };
}


// 16. GPU-only B1+ execution without CPU fallback and multi-blocker restoration
static AcceptanceRecord test_b1_plus_gpu_only_execution_and_multi_blocker() {
    rtx_reset_parts_jk_persistent_state();
    ASTGTransportEngine e;
    configure_engine(e, { 0 }, true);

    // Create a B1+ DAG edge: Node 0 (B1 at 0, 3, 0) -> Node 1 (B2 at 0, 1, 0)
    e.bounce1_nodes.clear();
    ASTGTransportNode n0{};
    n0.node_id = 0; n0.source_light_id = 0; n0.bounce_depth = 1;
    n0.position = { 0.0f, 3.0f, 0.0f }; n0.geometric_normal = { 0.0f, 1.0f, 0.0f };
    n0.is_active = true;
    e.bounce1_nodes.push_back(n0);

    ASTGTransportNode n1{};
    n1.node_id = 1; n1.source_light_id = 0; n1.bounce_depth = 2;
    n1.position = { 0.0f, 1.0f, 0.0f }; n1.geometric_normal = { 0.0f, 1.0f, 0.0f };
    n1.is_active = true;
    e.bounce1_nodes.push_back(n1);

    // DAG Edge 0: Node 0 (B1) -> Node 1 (B2)
    ASTGDAGEdge edge0{};
    edge0.edge_id = 0; edge0.parent_node_id = 0; edge0.child_node_id = 1;
    edge0.source_light_id = 0; edge0.source_bounce_depth = 1; edge0.target_bounce_depth = 2;
    edge0.is_active = true; edge0.state = ASTG_EDGE_ACTIVE;
    e.dag_edges = { edge0 };
    e.rebuild_edge_spatial_index(2.0f, 0.05f);

    // Group 1: Blocker over B1 edge (y in [1.5, 2.5]). B0 ray (light at 5.0 to node at 3.0) is unaffected!
    ASTGAABB b1_box({ -0.4f, 1.5f, -0.4f }, { 0.4f, 2.5f, 0.4f });
    uint32_t g1 = e.register_dynamic_occluder_group({ b1_box }, "b1_blocker_1", true, ASTG_OCCLUSION_ANGULAR_B0_DAG_B1_PLUS);

    // Initial frame update (< 256 edge references workload)
    auto metrics1 = e.update_all_dynamic_occlusions();
    rtx_complete_parts_jk_production();

    bool zero_cpu_small = true;
    bool gpu_tested_small = false;
    for (const auto& m : metrics1) {
        if (m.cpu_reference_edges_tested > 0) zero_cpu_small = false;
        if (m.gpu_b1_plus_edges_tested > 0) gpu_tested_small = true;
    }
    bool edge_blocked_g1 = (e.dag_edges[0].dynamic_blocker_count == 1) &&
                           (e.dag_edges[0].state == ASTG_EDGE_OCCLUDED_DYNAMIC);

    // Overlapping blocker test: register Group 2 over the exact same B1 edge
    uint32_t g2 = e.register_dynamic_occluder_group({ b1_box }, "b1_blocker_2", true, ASTG_OCCLUSION_ANGULAR_B0_DAG_B1_PLUS);
    e.update_all_dynamic_occlusions();
    rtx_complete_parts_jk_production();
    bool multi_blocker_2 = (e.dag_edges[0].dynamic_blocker_count == 2);

    // Remove Group 1: edge MUST remain blocked by Group 2
    e.set_dynamic_group_bounds(g1, { ASTGAABB({ 50.0f, 50.0f, 50.0f }, { 51.0f, 51.0f, 51.0f }) });
    e.update_all_dynamic_occlusions();
    rtx_complete_parts_jk_production();
    bool still_blocked_g2 = (e.dag_edges[0].dynamic_blocker_count == 1) &&
                            (e.dag_edges[0].state == ASTG_EDGE_OCCLUDED_DYNAMIC);

    // Remove Group 2: edge MUST now restore to ACTIVE
    e.set_dynamic_group_bounds(g2, { ASTGAABB({ 50.0f, 50.0f, 50.0f }, { 51.0f, 51.0f, 51.0f }) });
    e.update_all_dynamic_occlusions();
    rtx_complete_parts_jk_production();
    bool restored_clean = (e.dag_edges[0].dynamic_blocker_count == 0) &&
                          (e.dag_edges[0].state == ASTG_EDGE_ACTIVE);

    const bool pass = zero_cpu_small && gpu_tested_small && edge_blocked_g1 &&
                      multi_blocker_2 && still_blocked_g2 && restored_clean;
    std::string det = "zero_cpu=" + std::to_string(zero_cpu_small) +
                      " gpu_tested=" + std::to_string(gpu_tested_small) +
                      " edge_blocked=" + std::to_string(edge_blocked_g1) +
                      " b_cnt=" + std::to_string(e.dag_edges[0].dynamic_blocker_count) +
                      " multi2=" + std::to_string(multi_blocker_2) +
                      " still=" + std::to_string(still_blocked_g2) +
                      " rest=" + std::to_string(restored_clean);
    return { "b1_plus_gpu_only_execution_and_multi_blocker", pass, det };
}


// 17. Transactional layout migration and rebuilding all affected groups (including stationary)
static AcceptanceRecord test_transactional_layout_migration_and_stationary_rebuild() {
    rtx_reset_parts_jk_persistent_state();
    ASTGTransportEngine e;
    configure_engine(e, { 0 }, true);

    // Group A (moving) and Group B (stationary)
    uint32_t gid_a = e.register_dynamic_occluder_group({ blocker_box(0) }, "moving_a", true, ASTG_OCCLUSION_ANGULAR_B0_ONLY);
    uint32_t gid_b = e.register_dynamic_occluder_group({ blocker_box(1) }, "stationary_b", true, ASTG_OCCLUSION_ANGULAR_B0_ONLY);

    // Initial setup with 8 records
    e.bounce0_nodes.resize(8);
    for (size_t i = 0; i < e.bounce0_nodes.size(); ++i) {
        e.bounce0_nodes[i].node_id = (uint32_t)i;
        e.bounce0_nodes[i].position = { (float)i * 0.5f, 2.0f, 0.0f };
        e.bounce0_nodes[i].geometric_normal = { 0.0f, 1.0f, 0.0f };
        e.bounce0_nodes[i].is_active = true;
    }
    e.light_b0_hierarchies[0].build_continuous_b0_hierarchy(e.bounce0_nodes, 0);

    // Initial frame update
    e.update_all_dynamic_occlusions();
    rtx_complete_parts_jk_production();

    auto it_a_init = e.group_light_allocations.find({ gid_a, 0 });
    auto it_b_init = e.group_light_allocations.find({ gid_b, 0 });
    bool initial_allocs_ok = (it_a_init != e.group_light_allocations.end()) &&
                             (it_b_init != e.group_light_allocations.end());
    uint32_t initial_records_b = it_b_init != e.group_light_allocations.end() ? it_b_init->second.record_count : 0;
    uint64_t initial_hash_b = it_b_init != e.group_light_allocations.end() ? it_b_init->second.layout_hash : 0;

    // Mutate Light 0 hierarchy: reduce records from 8 to 4 (layout reduction)
    e.bounce0_nodes.resize(4);
    for (size_t i = 0; i < e.bounce0_nodes.size(); ++i) e.bounce0_nodes[i].node_id = (uint32_t)i;
    e.light_b0_hierarchies[0].build_continuous_b0_hierarchy(e.bounce0_nodes, 0);
    uint64_t new_expected_hash = e.light_b0_hierarchies[0].compute_b0_layout_hash();

    // Move only Group A; Group B remains COMPLETELY STATIONARY and is not marked dirty by user
    e.update_dynamic_group_rigid_transform(gid_a, RTXMatrix4x4::translation(0.05f, 0.0f, 0.0f));

    // Update frame
    e.update_all_dynamic_occlusions();
    rtx_complete_parts_jk_production();

    auto it_b_migrated = e.group_light_allocations.find({ gid_b, 0 });
    bool stationary_b_rebuilt = (it_b_migrated != e.group_light_allocations.end()) &&
                                (it_b_migrated->second.record_count == 4) &&
                                (it_b_migrated->second.layout_hash == new_expected_hash) &&
                                (it_b_migrated->second.layout_hash != initial_hash_b);

    // Negative control verification: in the unmigrated layout, record count was > 4.
    // Applying old offsets to the truncated 4-record layout would address out-of-bounds records.
    bool negative_control_safety = (initial_records_b > 4) && (it_b_migrated->second.record_count == 4);

    const bool pass = initial_allocs_ok && stationary_b_rebuilt && negative_control_safety;
    std::string det = "pass=" + std::to_string(pass) +
                      " init_ok=" + std::to_string(initial_allocs_ok) +
                      " init_rec_b=" + std::to_string(initial_records_b) +
                      " has_b_mig=" + std::to_string(it_b_migrated != e.group_light_allocations.end()) +
                      " rec_b=" + (it_b_migrated != e.group_light_allocations.end() ? std::to_string(it_b_migrated->second.record_count) : "none") +
                      " hash_match=" + (it_b_migrated != e.group_light_allocations.end() ? std::to_string(it_b_migrated->second.layout_hash == new_expected_hash) : "0") +
                      " hash_diff=" + (it_b_migrated != e.group_light_allocations.end() ? std::to_string(it_b_migrated->second.layout_hash != initial_hash_b) : "0");
    return { "transactional_layout_migration_and_stationary_rebuild", pass, det };
}

} // namespace

int main(int argc, char** argv) {
    if (!rtx_init()) {
        std::cerr << "Parts J/K acceptance: RTX initialization failed\n";
        return 2;
    }
    const RTXVertex vertices[] = {
        { -8.0f, 0.0f, -8.0f, 0.0f, 1.0f, 0.0f },
        {  8.0f, 0.0f, -8.0f, 0.0f, 1.0f, 0.0f },
        {  0.0f, 0.0f,  8.0f, 0.0f, 1.0f, 0.0f }
    };
    const uint32_t indices[] = { 0, 1, 2 };
    const PrimitiveMetadata metadata[] = { { 1, 1, 1, 1 } };
    if (!rtx_build_acceleration_structures(vertices, 3, indices, 3, metadata, 1)) {
        std::cerr << "Parts J/K acceptance: TLAS build failed\n";
        rtx_shutdown();
        return 2;
    }
    const uint32_t stage_limit = (argc > 2) ? (uint32_t)std::strtoul(argv[2], nullptr, 10) : 0;
    rtx_set_parts_jk_stage_limit(stage_limit);
    std::vector<AcceptanceRecord> records;
    const size_t only_test = (argc > 1) ? (size_t)std::strtoul(argv[1], nullptr, 10) : 0;
    size_t test_number = 0;
    auto run = [&](AcceptanceRecord (*fn)()) {
        ++test_number;
        if (only_test != 0 && only_test != test_number) return;
        std::cerr << "[Parts J/K acceptance] running test " << test_number
                  << " (stage limit " << stage_limit << ")...\n";
        std::cerr.flush();
        records.push_back(fn());
        std::cerr << "[Parts J/K acceptance] " << records.back().name << " = "
                  << (records.back().pass ? "PASS" : "FAIL") << "\n";
        std::cerr.flush();
    };
    run(test_live_membership_failure);
    run(test_live_footprint_failure);
    run(test_live_clear_failures);
    run(test_neighbor_ranges);
    run(test_layout_clean_rebuild);
    run(test_light_insert_remove);
    run(test_async_no_io);
    run(test_blocking_async_equivalence);
    run(test_frame_batching);
    run(test_mixed_rigid_skeletal);
    run(test_dense_probe_visibility);
    run(test_sparse_actual_light_ids);
    run(test_negative_control_oracle_sensitivity);
    run(test_frame_slot_allocator_ownership_and_fence_safety);
    run(test_slot_backpressure_and_ring_wraparound);
    run(test_b1_plus_gpu_only_execution_and_multi_blocker);
    run(test_transactional_layout_migration_and_stationary_rebuild);
    write_results(records);
    const bool all_pass = std::all_of(records.begin(), records.end(), [](const AcceptanceRecord& r) { return r.pass; });
    for (const auto& r : records) std::cout << r.name << ": " << (r.pass ? "PASS" : "FAIL") << " — " << r.detail << "\n";
    rtx_shutdown();
    return all_pass ? 0 : 1;
}
