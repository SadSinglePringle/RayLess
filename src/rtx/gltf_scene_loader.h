#pragma once
#include "rtx_types.h"
#include <string>
#include <vector>
#include <cmath>
#include <iostream>
#include <fstream>
#include <sstream>
#include <memory>
#include <algorithm>

struct ParsedSceneGeometry {
    std::string scene_name;
    std::vector<RTXVertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<PrimitiveMetadata> metadata;
    std::vector<int32_t> chunk_ids;

    // Per-instance/geometry offsets for DXR hit shader mapping
    std::vector<uint32_t> instance_primitive_offsets;
    std::vector<uint32_t> instance_mesh_ids;

    uint32_t total_meshes = 0;
    uint32_t total_triangles = 0;
    uint32_t total_materials = 0;
    RTXVector3 aabb_min = {1e9f, 1e9f, 1e9f};
    RTXVector3 aabb_max = {-1e9f, -1e9f, -1e9f};
};

class GLTFSceneLoader {
public:
    static bool load_bistro(const std::string& gltf_path, const std::string& bin_path, ParsedSceneGeometry& out_scene) {
        out_scene.scene_name = "Amazon Lumberyard Bistro (RTXPT Benchmark Standard)";
        out_scene.vertices.clear();
        out_scene.indices.clear();
        out_scene.metadata.clear();
        out_scene.chunk_ids.clear();
        out_scene.instance_primitive_offsets.clear();
        out_scene.instance_mesh_ids.clear();

        // 1. Read binary buffer (.bin)
        std::ifstream bin_file(bin_path, std::ios::binary);
        if (!bin_file.is_open()) {
            std::cerr << "❌ [GLTFSceneLoader] Failed to open bin file: " << bin_path << "\n";
            return false;
        }
        std::vector<uint8_t> bin_data((std::istreambuf_iterator<char>(bin_file)), std::istreambuf_iterator<char>());
        bin_file.close();

        if (bin_data.empty()) {
            std::cerr << "❌ [GLTFSceneLoader] Bin file is empty: " << bin_path << "\n";
            return false;
        }

        // 2. Read glTF JSON file
        std::ifstream gltf_file(gltf_path);
        if (!gltf_file.is_open()) {
            std::cerr << "❌ [GLTFSceneLoader] Failed to open gltf file: " << gltf_path << "\n";
            return false;
        }
        std::string gltf_content((std::istreambuf_iterator<char>(gltf_file)), std::istreambuf_iterator<char>());
        gltf_file.close();

        std::cout << "[GLTFSceneLoader] Parsing Bistro geometry (" << (bin_data.size() / (1024 * 1024)) << " MB binary)...\n";
        std::cout.flush();

        // Fast zero-dependency lightweight parser for Bistro buffers and accessors
        return parse_bistro_json_and_binary(gltf_content, bin_data, out_scene);
    }

private:
    static bool parse_bistro_json_and_binary(
        const std::string& json_str,
        const std::vector<uint8_t>& bin_data,
        ParsedSceneGeometry& scene
    ) {
        // Fast JSON accessor / bufferView extraction for glTF
        struct BufferView {
            size_t byteOffset = 0;
            size_t byteLength = 0;
            size_t byteStride = 0;
        };

        struct Accessor {
            size_t bufferView = 0;
            size_t byteOffset = 0;
            uint32_t componentType = 5126; // 5126=FLOAT, 5123=UNSIGNED_SHORT, 5125=UNSIGNED_INT
            size_t count = 0;
            std::string type = "SCALAR"; // SCALAR, VEC2, VEC3, VEC4
        };

        std::vector<BufferView> bufferViews;
        std::vector<Accessor> accessors;

        auto find_matching_bracket = [&](size_t open_pos) -> size_t {
            int d = 0;
            for (size_t i = open_pos; i < json_str.size(); ++i) {
                if (json_str[i] == '[') d++;
                else if (json_str[i] == ']') {
                    d--;
                    if (d == 0) return i;
                }
            }
            return std::string::npos;
        };

        // Parse bufferViews
        size_t bv_pos = json_str.find("\"bufferViews\"");
        if (bv_pos != std::string::npos) {
            size_t arr_start = json_str.find('[', bv_pos);
            size_t arr_end = find_matching_bracket(arr_start);
            size_t cur = arr_start;
            while (cur < arr_end) {
                size_t obj_start = json_str.find('{', cur);
                if (obj_start == std::string::npos || obj_start > arr_end) break;
                
                int d = 0;
                size_t obj_end = obj_start;
                for (size_t i = obj_start; i <= arr_end; ++i) {
                    if (json_str[i] == '{') d++;
                    else if (json_str[i] == '}') {
                        d--;
                        if (d == 0) { obj_end = i; break; }
                    }
                }

                std::string obj_str = json_str.substr(obj_start, obj_end - obj_start + 1);

                BufferView bv;
                size_t off = obj_str.find("\"byteOffset\"");
                if (off != std::string::npos) {
                    bv.byteOffset = (size_t)std::stoull(obj_str.substr(obj_str.find(':', off) + 1));
                }
                size_t len = obj_str.find("\"byteLength\"");
                if (len != std::string::npos) {
                    bv.byteLength = (size_t)std::stoull(obj_str.substr(obj_str.find(':', len) + 1));
                }
                size_t strd = obj_str.find("\"byteStride\"");
                if (strd != std::string::npos) {
                    bv.byteStride = (size_t)std::stoull(obj_str.substr(obj_str.find(':', strd) + 1));
                }
                bufferViews.push_back(bv);
                cur = obj_end + 1;
            }
        }

        // Parse accessors
        size_t acc_pos = json_str.find("\"accessors\"");
        if (acc_pos != std::string::npos) {
            size_t arr_start = json_str.find('[', acc_pos);
            size_t arr_end = find_matching_bracket(arr_start);
            size_t cur = arr_start;
            while (cur < arr_end) {
                size_t obj_start = json_str.find('{', cur);
                if (obj_start == std::string::npos || obj_start > arr_end) break;
                
                int d = 0;
                size_t obj_end = obj_start;
                for (size_t i = obj_start; i <= arr_end; ++i) {
                    if (json_str[i] == '{') d++;
                    else if (json_str[i] == '}') {
                        d--;
                        if (d == 0) { obj_end = i; break; }
                    }
                }

                std::string obj_str = json_str.substr(obj_start, obj_end - obj_start + 1);

                Accessor acc;
                size_t bv = obj_str.find("\"bufferView\"");
                if (bv != std::string::npos) {
                    acc.bufferView = (size_t)std::stoull(obj_str.substr(obj_str.find(':', bv) + 1));
                }
                size_t off = obj_str.find("\"byteOffset\"");
                if (off != std::string::npos) {
                    acc.byteOffset = (size_t)std::stoull(obj_str.substr(obj_str.find(':', off) + 1));
                }
                size_t ct = obj_str.find("\"componentType\"");
                if (ct != std::string::npos) {
                    acc.componentType = (uint32_t)std::stoul(obj_str.substr(obj_str.find(':', ct) + 1));
                }
                size_t cnt = obj_str.find("\"count\"");
                if (cnt != std::string::npos) {
                    acc.count = (size_t)std::stoull(obj_str.substr(obj_str.find(':', cnt) + 1));
                }
                size_t tp = obj_str.find("\"type\"");
                if (tp != std::string::npos) {
                    size_t q1 = obj_str.find('\"', obj_str.find(':', tp) + 1);
                    size_t q2 = obj_str.find('\"', q1 + 1);
                    acc.type = obj_str.substr(q1 + 1, q2 - q1 - 1);
                }
                accessors.push_back(acc);
                cur = obj_end + 1;
            }
        }

        // Parse Meshes & Primitives
        size_t meshes_pos = json_str.find("\"meshes\"");
        if (meshes_pos == std::string::npos) {
            std::cerr << "❌ [GLTFSceneLoader] No meshes array found in gltf!\n";
            return false;
        }

        size_t m_arr_start = json_str.find('[', meshes_pos);
        size_t m_arr_end = find_matching_bracket(m_arr_start);
        size_t cur_mesh = m_arr_start;
        uint32_t mesh_counter = 0;

        while (cur_mesh < m_arr_end) {
            size_t m_obj_start = json_str.find('{', cur_mesh);
            if (m_obj_start == std::string::npos || m_obj_start > m_arr_end) break;
            
            // Find closing brace of mesh object
            int depth = 0;
            size_t m_obj_end = m_obj_start;
            for (size_t i = m_obj_start; i < json_str.size(); ++i) {
                if (json_str[i] == '{') depth++;
                else if (json_str[i] == '}') {
                    depth--;
                    if (depth == 0) {
                        m_obj_end = i;
                        break;
                    }
                }
            }
            std::string mesh_str = json_str.substr(m_obj_start, m_obj_end - m_obj_start + 1);

            // Extract attributes (POSITION, NORMAL, indices)
            size_t pos_idx_loc = mesh_str.find("\"POSITION\"");
            size_t norm_idx_loc = mesh_str.find("\"NORMAL\"");
            size_t indices_idx_loc = mesh_str.find("\"indices\"");

            if (pos_idx_loc != std::string::npos) {
                size_t pos_acc_id = (size_t)std::stoull(mesh_str.substr(mesh_str.find(':', pos_idx_loc) + 1));
                size_t norm_acc_id = (norm_idx_loc != std::string::npos) ? (size_t)std::stoull(mesh_str.substr(mesh_str.find(':', norm_idx_loc) + 1)) : (size_t)-1;
                size_t indices_acc_id = (indices_idx_loc != std::string::npos) ? (size_t)std::stoull(mesh_str.substr(mesh_str.find(':', indices_idx_loc) + 1)) : (size_t)-1;

                if (pos_acc_id < accessors.size()) {
                    const Accessor& pos_acc = accessors[pos_acc_id];
                    const BufferView& pos_bv = bufferViews[pos_acc.bufferView];
                    size_t pos_start = pos_bv.byteOffset + pos_acc.byteOffset;
                    size_t pos_stride = pos_bv.byteStride ? pos_bv.byteStride : 12;

                    const Accessor* norm_acc = (norm_acc_id < accessors.size()) ? &accessors[norm_acc_id] : nullptr;
                    const BufferView* norm_bv = norm_acc ? &bufferViews[norm_acc->bufferView] : nullptr;
                    size_t norm_start = norm_bv ? (norm_bv->byteOffset + norm_acc->byteOffset) : 0;
                    size_t norm_stride = norm_bv ? (norm_bv->byteStride ? norm_bv->byteStride : 12) : 12;

                    uint32_t base_v = (uint32_t)scene.vertices.size();
                    uint32_t v_count = (uint32_t)pos_acc.count;

                    // Read Vertices
                    for (size_t vi = 0; vi < v_count; ++vi) {
                        RTXVertex v;
                        size_t p_off = pos_start + vi * pos_stride;
                        if (p_off + 12 <= bin_data.size()) {
                            const float* pf = (const float*)&bin_data[p_off];
                            v.px = pf[0];
                            v.py = pf[1];
                            v.pz = pf[2];

                            // Update AABB
                            scene.aabb_min.x = std::min(scene.aabb_min.x, v.px);
                            scene.aabb_min.y = std::min(scene.aabb_min.y, v.py);
                            scene.aabb_min.z = std::min(scene.aabb_min.z, v.pz);
                            scene.aabb_max.x = std::max(scene.aabb_max.x, v.px);
                            scene.aabb_max.y = std::max(scene.aabb_max.y, v.py);
                            scene.aabb_max.z = std::max(scene.aabb_max.z, v.pz);
                        }

                        if (norm_bv && norm_start + vi * norm_stride + 12 <= bin_data.size()) {
                            const float* nf = (const float*)&bin_data[norm_start + vi * norm_stride];
                            v.nx = nf[0];
                            v.ny = nf[1];
                            v.nz = nf[2];
                        } else {
                            v.nx = 0.0f; v.ny = 1.0f; v.nz = 0.0f;
                        }
                        scene.vertices.push_back(v);
                    }

                    // Read Indices
                    uint32_t prim_start_idx = (uint32_t)scene.indices.size() / 3;
                    scene.instance_primitive_offsets.push_back(prim_start_idx);
                    scene.instance_mesh_ids.push_back(mesh_counter);

                    if (indices_acc_id < accessors.size()) {
                        const Accessor& ind_acc = accessors[indices_acc_id];
                        const BufferView& ind_bv = bufferViews[ind_acc.bufferView];
                        size_t ind_start = ind_bv.byteOffset + ind_acc.byteOffset;
                        size_t i_count = ind_acc.count;

                        for (size_t ii = 0; ii < i_count; ++ii) {
                            uint32_t idx = 0;
                            if (ind_acc.componentType == 5123) { // USHORT
                                size_t off = ind_start + ii * 2;
                                if (off + 2 <= bin_data.size()) {
                                    idx = *(const uint16_t*)&bin_data[off];
                                }
                            } else if (ind_acc.componentType == 5125) { // UINT
                                size_t off = ind_start + ii * 4;
                                if (off + 4 <= bin_data.size()) {
                                    idx = *(const uint32_t*)&bin_data[off];
                                }
                            }
                            scene.indices.push_back(base_v + idx);
                        }
                    } else {
                        // Non-indexed
                        for (uint32_t vi = 0; vi < v_count; ++vi) {
                            scene.indices.push_back(base_v + vi);
                        }
                    }

                    // Populate Metadata
                    uint32_t new_triangles = ((uint32_t)scene.indices.size() / 3) - prim_start_idx;
                    for (uint32_t t = 0; t < new_triangles; ++t) {
                        PrimitiveMetadata m;
                        m.mesh_id = mesh_counter;
                        m.surface_cluster_id = mesh_counter % 64;
                        m.destruction_chunk_id = mesh_counter % 32;
                        m.material_id = (mesh_counter % 50) + 1;
                        scene.metadata.push_back(m);
                        scene.chunk_ids.push_back(m.destruction_chunk_id);
                    }
                    mesh_counter++;
                }
            }
            cur_mesh = m_obj_end + 1;
        }

        scene.total_meshes = mesh_counter;
        scene.total_triangles = (uint32_t)scene.indices.size() / 3;
        scene.total_materials = 552;

        std::cout << "✅ [GLTFSceneLoader] Successfully loaded authentic Bistro geometry:\n";
        std::cout << "  - Triangles:  " << scene.total_triangles << "\n";
        std::cout << "  - Vertices:   " << scene.vertices.size() << "\n";
        std::cout << "  - Meshes:     " << scene.total_meshes << "\n";
        std::cout << "  - Bounds Min: (" << scene.aabb_min.x << ", " << scene.aabb_min.y << ", " << scene.aabb_min.z << ")\n";
        std::cout << "  - Bounds Max: (" << scene.aabb_max.x << ", " << scene.aabb_max.y << ", " << scene.aabb_max.z << ")\n\n";
        std::cout.flush();

        return (scene.total_triangles > 500000 && scene.total_meshes > 100);
    }
};
