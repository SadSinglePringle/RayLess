class_name ASTGNativeBenchmarkBridge
extends RefCounted

# ==============================================================================
# ASTG NATIVE D3D12 / DXR RUNNER BRIDGE
# Bridges native DXR 1.1 C++ binary into the Godot diagnostic suite
# ==============================================================================

const TestResultScript = preload("res://tests/suite/test_result.gd")

static func run_native_bistro(light_count: int = 32) -> RefCounted:
	var res = TestResultScript.create("native_bistro_dxr_%d_lights" % light_count, TestResultScript.Category.PERFORMANCE, "GPU_END_TO_END")
	var runner_path = ProjectSettings.globalize_path("res://bin/astg_rtx_runner.exe")

	if not FileAccess.file_exists("res://bin/astg_rtx_runner.exe"):
		res.skip_test("Native executable not found at bin/astg_rtx_runner.exe")
		return res

	var output = []
	var start_t = Time.get_ticks_usec()
	var exit_code = OS.execute(runner_path, ["--benchmark", "bistro", "--lights", str(light_count)], output, true)
	res.duration_ms = float(Time.get_ticks_usec() - start_t) / 1000.0

	var out_str = "".join(output)

	if exit_code != 0:
		res.fail_test("Native runner exited with error code %d: %s" % [exit_code, out_str])
		return res

	# Read generated manifest
	if FileAccess.file_exists("res://benchmark_bistro_manifest.json"):
		var m_file = FileAccess.open("res://benchmark_bistro_manifest.json", FileAccess.READ)
		if m_file != null:
			var json_str = m_file.get_as_text()
			m_file.close()
			var json_data = JSON.parse_string(json_str)
			if json_data is Dictionary:
				res.metrics = json_data.get("metrics", {})
				var b_cat = json_data.get("category", "")
				if b_cat == "BENCHMARK_FULL_SCENE":
					res.metrics["benchmark_type"] = "FULL_SCENE"
					res.metrics["transport_node_count"] = json_data.get("transport_nodes", 0)
					res.metrics["transport_edge_count"] = json_data.get("transport_edges", 0)

	# Verify Invariants
	var has_valid_output = out_str.contains("ASTG AUTHENTIC BISTRO VALIDATION")
	var has_topology_zero = out_str.contains("Light RGB change topology rays:       0")
	var is_synthetic = out_str.contains("Synthetic contributions:         YES")

	res.add_assertion("Native Bistro executable completed with code 0", exit_code == 0)
	res.add_assertion("Authentic validation block generated", has_valid_output)
	res.add_assertion("Zero topology rays on light energy change", has_topology_zero)

	if is_synthetic:
		res.invalidate_test("Native benchmark executed with synthetic shortcuts!")

	return res
