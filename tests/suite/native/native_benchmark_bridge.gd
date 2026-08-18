class_name ASTGNativeBenchmarkBridge
extends RefCounted

# ==============================================================================
# ASTG NATIVE D3D12 / DXR RUNNER BRIDGE
# Bridges native DXR 1.1 C++ binary into the Godot diagnostic suite
# ==============================================================================

const TestResultScript = preload("res://tests/suite/test_result.gd")

static func run_native_bistro(light_count: int = 32) -> RefCounted:
	var res = TestResultScript.create("native_bistro_dxr_%d_lights" % light_count, 1, "GPU_END_TO_END")
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
				var b_cat = json_data.get("manifest", {}).get("benchmark_category", "")
				if b_cat == "BENCHMARK_FULL_SCENE" or b_cat == "FULL_SCENE":
					res.metrics["benchmark_type"] = "FULL_SCENE"
					res.metrics["bounce0_nodes"] = json_data.get("metrics", {}).get("bounce0_nodes", 0)
					res.metrics["bounce1_nodes"] = json_data.get("metrics", {}).get("bounce1_nodes", 0)
					res.metrics["dag_edges"] = json_data.get("metrics", {}).get("dag_edges", 0)
					res.metrics["probe_deposition_links"] = json_data.get("metrics", {}).get("probe_deposition_links", 0)
					res.metrics["couplings"] = json_data.get("metrics", {}).get("couplings", 0)
					res.metrics["candidate_contributions"] = json_data.get("metrics", {}).get("candidate_contributions", 0)
					res.metrics["retained_contributions"] = json_data.get("metrics", {}).get("retained_contributions", 0)
					res.metrics["pruned_contributions"] = json_data.get("metrics", {}).get("pruned_contributions", 0)
					res.metrics["mean_candidate_fanin"] = json_data.get("metrics", {}).get("mean_candidate_fanin", 0.0)
					res.metrics["mean_retained_fanin"] = json_data.get("metrics", {}).get("mean_retained_fanin", 0.0)

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

static func run_native_kernel() -> RefCounted:
	var res = TestResultScript.create("native_kernel_dxr_microbenchmarks", 1, "GPU_END_TO_END")
	var runner_path = ProjectSettings.globalize_path("res://bin/astg_rtx_runner.exe")

	if not FileAccess.file_exists("res://bin/astg_rtx_runner.exe"):
		res.skip_test("Native executable not found at bin/astg_rtx_runner.exe")
		return res

	var output = []
	var start_t = Time.get_ticks_usec()
	var exit_code = OS.execute(runner_path, ["--benchmark", "kernel"], output, true)
	res.duration_ms = float(Time.get_ticks_usec() - start_t) / 1000.0

	var out_str = "".join(output)
	res.add_assertion("Native Kernel microbenchmarks completed with code 0", exit_code == 0)
	res.add_assertion("Kernel throughput reported", out_str.contains("Hardware Throughput"))
	return res

static func run_native_subsystem() -> RefCounted:
	var res = TestResultScript.create("native_subsystem_repair_and_invalidation", 1, "GPU_END_TO_END")
	var runner_path = ProjectSettings.globalize_path("res://bin/astg_rtx_runner.exe")

	if not FileAccess.file_exists("res://bin/astg_rtx_runner.exe"):
		res.skip_test("Native executable not found at bin/astg_rtx_runner.exe")
		return res

	var output = []
	var start_t = Time.get_ticks_usec()
	var exit_code = OS.execute(runner_path, ["--benchmark", "subsystem"], output, true)
	res.duration_ms = float(Time.get_ticks_usec() - start_t) / 1000.0

	var out_str = "".join(output)
	res.add_assertion("Native Subsystem testbed completed with code 0", exit_code == 0)
	res.add_assertion("Invalidation latency measured", out_str.contains("Invalidation latency"))
	return res
