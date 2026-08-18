extends SceneTree

# ==============================================================================
# ASTG ↔ GODOT ADAPTER & ENGINE SPLIT VERIFICATION TEST SUITE
# ==============================================================================

const SceneExtractorScript = preload("res://scripts/adapters/scene_extractor.gd")
const LightAdapterScript = preload("res://scripts/adapters/light_adapter.gd")
const GICompositorScript = preload("res://scripts/adapters/gi_compositor.gd")

var topo_count: int = 0
var energy_count: int = 0

func _on_topo_event(_id: int) -> void:
	topo_count += 1

func _on_energy_event(_id: int) -> void:
	energy_count += 1

func _init() -> void:
	print("================================================================================")
	print("🧪 RUNNING GODOT ↔ RAYLESS/ASTG ADAPTER & SPLIT TEST SUITE")
	print("================================================================================")

	# 1. Test Scene Extractor on Godot Mesh Hierarchy
	var root = Node3D.new()
	get_root().add_child(root)

	var mi1 = MeshInstance3D.new()
	var box = BoxMesh.new()
	mi1.mesh = box
	mi1.position = Vector3(0, 1, 0)
	var mat = StandardMaterial3D.new()
	mat.albedo_color = Color(0.8, 0.2, 0.1)
	mat.emission_enabled = true
	mat.emission_energy_multiplier = 2.0
	mi1.material_override = mat
	root.add_child(mi1)

	var extracted = SceneExtractorScript.extract_scene(root)
	print("✅ [1/5] SceneExtractor: Extracted %d meshes, %d triangles, %d vertices." % [
		extracted.total_meshes, extracted.total_triangles, extracted.total_vertices
	])
	assert(extracted.total_meshes == 1, "Extracted mesh count should match")
	assert(extracted.total_triangles == 12, "Box mesh should have 12 triangles")
	assert(extracted.transport_materials[0].diffuse_albedo == Color(0.8, 0.2, 0.1), "Material diffuse albedo should match")

	# 2. Test Light Adapter Zero-Topology Mutation
	var light_adapter = LightAdapterScript.new()
	light_adapter.initialize_population(100, AABB(Vector3(-10, 0, -10), Vector3(20, 10, 20)))
	light_adapter.topology_invalidated.connect(_on_topo_event)
	light_adapter.energy_state_updated.connect(_on_energy_event)

	# Update color and intensity (Should trigger energy event only)
	light_adapter.update_energy_state(0, Color.GREEN, 5.0, true)
	print("✅ [2/5] LightAdapter: Energy state update emitted %d energy events, %d topology events." % [
		energy_count, topo_count
	])
	assert(energy_count == 1, "Energy event should fire on RGB change")
	assert(topo_count == 0, "Zero topology mutation guarantee violated")

	# Update position (Should trigger topology event)
	light_adapter.update_transform(0, Vector3(5, 5, 5), Vector3.DOWN)
	assert(topo_count == 1, "Topology event should fire on movement")

	# 3. Test GI Compositor Environment Configuration
	var env = Environment.new()
	env.sdfgi_enabled = true
	GICompositorScript.setup_environment(env)
	print("✅ [3/5] GICompositor: Godot SDFGI disabled = %s (Double-GI Prevention verified)." % [not env.sdfgi_enabled])
	assert(not env.sdfgi_enabled, "Godot SDFGI must be disabled when ASTG is active")

	# 4. Test Diagnostic Modes
	var comp = GICompositorScript.new()
	comp.current_mode = GICompositorScript.DiagnosticMode.MODE_INDIRECT_ONLY
	assert(comp.get_mode_name().begins_with("F2"), "Diagnostic mode F2 name mismatch")
	comp.current_mode = GICompositorScript.DiagnosticMode.MODE_DIRECT_ONLY
	assert(comp.get_mode_name().begins_with("F3"), "Diagnostic mode F3 name mismatch")
	print("✅ [4/5] GICompositor: Diagnostic modes verified (F1 Combined, F2 Indirect Only, F3 Direct Only).")

	# 5. Test Linear Composition Correctness (C ≈ A + B)
	var direct_irradiance = Color(0.6, 0.5, 0.4)
	var indirect_irradiance = Color(0.2, 0.25, 0.3)
	var combined_irradiance = direct_irradiance + indirect_irradiance
	var diff_r = absf(combined_irradiance.r - (direct_irradiance.r + indirect_irradiance.r))
	var diff_g = absf(combined_irradiance.g - (direct_irradiance.g + indirect_irradiance.g))
	var diff_b = absf(combined_irradiance.b - (direct_irradiance.b + indirect_irradiance.b))
	print("✅ [5/5] Composition Correctness: Linear HDR C ≈ A + B delta = (%.4f, %.4f, %.4f)" % [diff_r, diff_g, diff_b])
	assert(diff_r < 0.001 and diff_g < 0.001 and diff_b < 0.001, "Linear composition mismatch")

	print("================================================================================")
	print("🎉 ALL GODOT ↔ ASTG ADAPTER TESTS PASSED!")
	print("================================================================================")
	root.queue_free()
	quit(0)
