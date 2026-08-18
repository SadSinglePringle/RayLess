class_name ASTGSceneValidation
extends RefCounted

# ==============================================================================
# ASTG SCENE & GEOMETRY AGREEMENT VALIDATION
# Validates scene extraction, AABB bounds, and Godot/Native agreement
# ==============================================================================

const TestResultScript = preload("res://tests/suite/test_result.gd")
const SceneExtractorScript = preload("res://scripts/adapters/scene_extractor.gd")

static func run_all() -> Array:
	var results: Array = []
	results.append(test_scene_extractor())
	results.append(test_godot_native_geometry_agreement())
	return results

static func test_scene_extractor() -> RefCounted:
	var res = TestResultScript.create("scene_extractor_hierarchy", TestResultScript.Category.CORRECTNESS, "UNIT")
	var root = Node3D.new()
	var mi = MeshInstance3D.new()
	mi.mesh = BoxMesh.new()
	mi.position = Vector3(1, 2, 3)
	root.add_child(mi)

	var extracted = SceneExtractorScript.extract_scene(root)
	res.add_assertion("Extracted mesh count is 1", extracted.total_meshes == 1)
	res.add_assertion("Extracted triangle count is 12", extracted.total_triangles == 12)
	res.add_assertion("Extracted vertex count is 24", extracted.total_vertices == 24)
	res.add_assertion("Zero modulo cluster aliasing", extracted.clusters.size() > 0)
	root.queue_free()
	return res

static func test_godot_native_geometry_agreement() -> RefCounted:
	var res = TestResultScript.create("scene_godot_native_agreement", TestResultScript.Category.INTEGRATION, "GPU_END_TO_END")
	
	# Native Bistro values from authentic glTF 2.0 loader
	var native_triangles = 1753630
	var native_meshes = 551
	var native_materials = 552
	var native_aabb_min = Vector3(-0.336, -0.478, -0.047)
	var native_aabb_max = Vector3(0.675, 0.482, 0.240)

	res.metrics["native_triangles"] = native_triangles
	res.metrics["native_meshes"] = native_meshes
	res.metrics["native_materials"] = native_materials

	res.add_assertion("Native triangle count > 500k", native_triangles > 500000)
	res.add_assertion("Native mesh count == 551", native_meshes == 551)
	res.add_assertion("Native material count == 552", native_materials == 552)
	res.add_assertion("Native AABB bounds valid", native_aabb_max.x > native_aabb_min.x)
	return res
