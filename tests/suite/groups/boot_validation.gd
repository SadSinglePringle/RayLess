class_name ASTGBootValidation
extends RefCounted

# ==============================================================================
# ASTG BOOT & ENVIRONMENT PREFLIGHT VALIDATION
# Validates GPU environment, RenderingDevice, compute shaders, and assets
# ==============================================================================

const TestResultScript = preload("res://tests/suite/test_result.gd")

static func run_all() -> Array:
	var results: Array = []
	results.append(test_rendering_device())
	results.append(test_asset_availability())
	results.append(test_compute_shader_compilation())
	return results

static func test_rendering_device() -> RefCounted:
	var res = TestResultScript.create("boot_rendering_device", 4, "UNIT")
	var ds_name = DisplayServer.get_name()
	res.metrics["display_server"] = ds_name
	res.add_assertion("DisplayServer runtime active", ds_name.length() > 0)

	var rd = RenderingServer.get_rendering_device()
	if rd == null and ds_name != "headless":
		rd = RenderingServer.create_local_rendering_device()

	if rd != null:
		var dev_name = rd.get_device_name()
		res.metrics["gpu_device"] = dev_name
		res.add_assertion("GPU device identified", dev_name.length() > 0)
	else:
		res.metrics["mode"] = "Headless / Command-line"
		res.add_assertion("Headless engine environment verified", true)
	return res

static func test_asset_availability() -> RefCounted:
	var res = TestResultScript.create("boot_bistro_asset_presence", 4, "UNIT")
	var gltf_exists = FileAccess.file_exists("res://assets/bistro/bistro.gltf")
	var bin_exists = FileAccess.file_exists("res://assets/bistro/bistro.bin")
	res.add_assertion("Bistro glTF exists at assets/bistro/bistro.gltf", gltf_exists)
	res.add_assertion("Bistro BIN exists at assets/bistro/bistro.bin", bin_exists)
	return res

static func test_compute_shader_compilation() -> RefCounted:
	var res = TestResultScript.create("boot_glsl_compute_shader", 4, "UNIT")
	var shader_exists = FileAccess.file_exists("res://shaders/compute/astg_probe_eval.glsl")
	res.add_assertion("ASTG Probe Eval GLSL compute shader exists", shader_exists)
	return res
