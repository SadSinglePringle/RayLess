#pragma once

#include "sha256.h"

#include <algorithm>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#define NOMINMAX
#include <windows.h>

#ifndef ASTG_BUILD_COMMIT
#define ASTG_BUILD_COMMIT "unknown_commit"
#endif

#ifndef ASTG_BUILD_WORKTREE_DIRTY
#define ASTG_BUILD_WORKTREE_DIRTY 1
#endif

#define ASTG_PROV_STR_IMPL(x) #x
#define ASTG_PROV_STR(x) ASTG_PROV_STR_IMPL(x)

// Runtime provenance for evidence produced from a checkout.  The source tree
// identity is deliberately computed from the files used by the J/K build and
// its harness, rather than inferred from a Git commit label.  This keeps a
// dirty-worktree run auditable without pretending it came from a clean commit.
struct ASTGBuildProvenance {
    std::string base_commit;
    bool worktree_dirty = true;
    std::string source_commit_label;
    std::string source_tree_sha256;
    std::map<std::string, std::string> source_input_hashes;
    std::map<std::string, std::string> tested_binary_hashes;

    static std::string current_executable_path() {
        char path[MAX_PATH] = {};
        const DWORD length = GetModuleFileNameA(nullptr, path, MAX_PATH);
        return length > 0 ? std::string(path, length) : std::string();
    }

    static const std::vector<std::string>& relevant_source_inputs() {
        static const std::vector<std::string> paths = {
            "src/astg/astg_transport_engine.h",
            "src/diagnostics/astg_build_provenance.h",
            "src/diagnostics/astg_diagnostics_main.cpp",
            "src/diagnostics/astg_e2e_tests_main.cpp",
            "src/diagnostics/astg_transport_diagnostics.h",
            "src/diagnostics/astg_parts_jk_acceptance_main.cpp",
            "src/rtx/rtx_raytracer.cpp",
            "src/rtx/rtx_raytracer.h",
            "src/rtx/rtx_runner.cpp",
            "src/rtx/rtx_types.h",
            "src/rtx/rtx_b0_angular_runtime.hlsl",
            "src/rtx/rtx_dynamic_receiver_runtime.hlsl",
            "src/rtx/rtx_gpu_transport.hlsl",
            "src/rtx/rtx_lazy_probe_refresh.hlsl",
            "src/rtx/rtx_light_animator.hlsl",
            "src/rtx/rtx_raydesc_extractor.hlsl",
            "src/rtx/rtx_shader.hlsl",
            "src/rtx/rtx_b0_apply_deltas_cso.h",
            "src/rtx/rtx_b0_compact_transitions_cso.h",
            "src/rtx/rtx_b0_project_bounds_cso.h",
            "src/rtx/rtx_b0_traverse_bvh_cso.h",
            "src/rtx/rtx_gpu_transport_cso.h",
            "src/rtx/rtx_lazy_probe_refresh_cso.h",
            "src/rtx/rtx_light_animator_cso.h",
            "src/rtx/rtx_raydesc_extractor_cso.h",
            "src/rtx/rtx_rec_accum_irradiance_cso.h",
            "src/rtx/rtx_rec_build_args_cso.h",
            "src/rtx/rtx_rec_cull_hierarchy_cso.h",
            "src/rtx/rtx_rec_eval_visibility_cso.h",
            "src/rtx/rtx_rec_transform_clusters_cso.h",
            "src/rtx/rtx_rec_transform_bones_cso.h",
            "src/rtx/rtx_rec_transform_probes_cso.h",
            "src/rtx/rtx_shader_cso.h",
            "build_diagnostics.ps1",
            "build_e2e_tests.ps1",
            "build_parts_jk_acceptance.ps1",
            "build_rtx_dll.ps1",
            "build_rtx_dll.bat"
        };
        return paths;
    }

    static std::string hash_source_inputs(std::map<std::string, std::string>& out_hashes) {
        out_hashes.clear();
        std::string canonical;
        for (const auto& path : relevant_source_inputs()) {
            const std::string digest = SHA256::hash_file(path);
            out_hashes[path] = digest;
            canonical += path;
            canonical.push_back('\0');
            canonical += digest;
            canonical.push_back('\n');
        }
        return SHA256::hash_string(canonical);
    }

    static std::string hash_binary(const std::string& path) {
        if (path.empty() || !std::filesystem::exists(path)) return std::string();
        return SHA256::hash_file(path);
    }

    static ASTGBuildProvenance collect() {
        ASTGBuildProvenance result;
        result.base_commit = ASTG_PROV_STR(ASTG_BUILD_COMMIT);
        if (!result.base_commit.empty() && result.base_commit.front() == '"' && result.base_commit.back() == '"') {
            result.base_commit = result.base_commit.substr(1, result.base_commit.size() - 2);
        }
        result.worktree_dirty = (ASTG_BUILD_WORKTREE_DIRTY != 0);
        result.source_commit_label = result.worktree_dirty ? "UNCOMMITTED_WORKTREE" : result.base_commit;
        result.source_tree_sha256 = hash_source_inputs(result.source_input_hashes);

        const std::string executable = current_executable_path();
        if (!executable.empty()) result.tested_binary_hashes["current_executable"] = hash_binary(executable);
        result.tested_binary_hashes["astg_diagnostics.exe"] = hash_binary("bin/astg_diagnostics.exe");
        result.tested_binary_hashes["astg_parts_jk_acceptance.exe"] = hash_binary("bin/astg_parts_jk_acceptance.exe");
        result.tested_binary_hashes["astg_e2e_tests.exe"] = hash_binary("bin/astg_e2e_tests.exe");
        result.tested_binary_hashes["astg_rtx.dll"] = hash_binary("bin/astg_rtx.dll");
        result.tested_binary_hashes["astg_rtx_runner.exe"] = hash_binary("bin/astg_rtx_runner.exe");
        return result;
    }
};

// The diagnostics harness constructs some TestIdentity records directly and
// some through runtime_test_identity().  Keep both forms tied to the same
// provenance context so no sealed record can silently fall back to empty
// source or binary identity fields.
inline std::string ASTG_ACTIVE_SOURCE_COMMIT_LABEL;
inline std::string ASTG_ACTIVE_BINARY_HASH;
inline std::string ASTG_ACTIVE_BASE_COMMIT;
inline std::string ASTG_ACTIVE_SOURCE_TREE_SHA256;
inline bool ASTG_ACTIVE_WORKTREE_DIRTY = true;

inline void astg_set_active_provenance(const ASTGBuildProvenance& provenance, const std::string& binary_hash) {
    ASTG_ACTIVE_SOURCE_COMMIT_LABEL = provenance.source_commit_label;
    ASTG_ACTIVE_BINARY_HASH = binary_hash;
    ASTG_ACTIVE_BASE_COMMIT = provenance.base_commit;
    ASTG_ACTIVE_SOURCE_TREE_SHA256 = provenance.source_tree_sha256;
    ASTG_ACTIVE_WORKTREE_DIRTY = provenance.worktree_dirty;
}
