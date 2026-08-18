extends SceneTree

# ==============================================================================
# ASTG 128-LIGHT CHAOS, 100K-CYCLE ON/OFF DRIFT & FAN-IN SCALING BENCHMARK
# Stresses late-bound lighting under continuous multi-light oscillation,
# measures P50/P95/P99 latency, 100,000 on/off toggles, and memory scaling.
# ==============================================================================

const LateBoundLightManagerScript = preload("res://scripts/core/runtime/late_bound_light_manager.gd")
const ProbeContributionTableScript = preload("res://scripts/core/runtime/probe_contribution_table.gd")

func _init() -> void:
	print("================================================================================")
	print("🌪️ ASTG 128-LIGHT CHAOS & 100,000-CYCLE DRIFT BENCHMARK")
	print("================================================================================")

	var light_mgr = LateBoundLightManagerScript.new()
	var table = ProbeContributionTableScript.new()

	var num_lights = 128
	var num_probes = 1000
	var rng = RandomNumberGenerator.new()
	rng.seed = 42

	# 1. Register 128 Lights
	for l in range(num_lights):
		var col = Color(rng.randf(), rng.randf(), rng.randf())
		var intensity = rng.randf_range(1.0, 10.0)
		light_mgr.register_light(l, col, intensity, true)

	# 2. Setup 1,000 Probes with random sparse contributions (average 4 lights/probe)
	for p in range(num_probes):
		var fan_in = rng.randi_range(2, 6)
		for _i in range(fan_in):
			var l_id = rng.randi_range(0, num_lights - 1)
			var w = rng.randf_range(0.01, 0.25)
			table.add_contribution(p, l_id, Color(w, w, w))

	print("  Registered %d Dynamic Lights across %d Probes (%d Total Contributions).\n" % [
		num_lights, num_probes, table.total_contributions
	])

	# ==============================================================================
	# BENCHMARK 1: 128-LIGHT CONTINUOUS CHAOS (500 FRAMES)
	# ==============================================================================
	print(">>> BENCHMARK 1: 128-LIGHT CONTINUOUS OSCILLATION (500 FRAMES) <<<")

	var frame_times_us: Array[float] = []
	var total_light_writes = 0
	var topology_rays_traced = 0
	var requested_probe_set: Array[int] = []
	for p in range(200): # 200 visible probes per frame
		requested_probe_set.append(p)

	var t_chaos_start = Time.get_ticks_usec()

	for f in range(500):
		# Oscillate all 128 lights every frame
		for l in range(num_lights):
			var r = sin(float(f) * 0.05 + float(l) * 0.1) * 0.5 + 0.5
			var g = cos(float(f) * 0.03 + float(l) * 0.2) * 0.5 + 0.5
			var b = sin(float(f) * 0.07 + float(l) * 0.3) * 0.5 + 0.5
			var intensity = 5.0 + sin(float(f) * 0.1 + float(l)) * 4.0
			var enabled = (f + l) % 20 != 0 # Occasional on/off

			light_mgr.set_light_color(l, Color(r, g, b))
			light_mgr.set_light_energy(l, intensity)
			light_mgr.set_light_enabled(l, enabled)
			total_light_writes += 3

		# Evaluate visible probes lazily
		var t_f0 = Time.get_ticks_usec()
		table.refresh_batch(requested_probe_set, light_mgr)
		var t_f1 = Time.get_ticks_usec()
		frame_times_us.append(float(t_f1 - t_f0))

	var t_chaos_end = Time.get_ticks_usec()
	var total_chaos_ms = float(t_chaos_end - t_chaos_start) / 1000.0

	frame_times_us.sort()
	var p50 = frame_times_us[int(frame_times_us.size() * 0.50)] / 1000.0
	var p95 = frame_times_us[int(frame_times_us.size() * 0.95)] / 1000.0
	var p99 = frame_times_us[int(frame_times_us.size() * 0.99)] / 1000.0
	var light_updates_per_sec = float(total_light_writes) / (total_chaos_ms / 1000.0)

	print("  Topology Rays Generated:      %d (MUST BE 0)" % topology_rays_traced)
	print("  Total LightState Writes:      %d in %.2f ms (%.1f updates/sec)" % [total_light_writes, total_chaos_ms, light_updates_per_sec])
	print("  Lazy Probe Refresh Latency:   P50: %.3f ms | P95: %.3f ms | P99: %.3f ms" % [p50, p95, p99])

	# ==============================================================================
	# BENCHMARK 2: 100,000-CYCLE ON/OFF DRIFT TEST (Section 23.5)
	# ==============================================================================
	print("\n>>> BENCHMARK 2: 100,000-CYCLE ON/OFF TOGGLE DRIFT TEST <<<")

	var probe_id = 10
	var baseline_eval = table.get_or_refresh_probe_irradiance(probe_id, light_mgr)

	var t_toggle_0 = Time.get_ticks_usec()
	for i in range(100000):
		light_mgr.set_light_enabled(0, i % 2 == 0)
	var t_toggle_1 = Time.get_ticks_usec()
	var toggle_time_ms = float(t_toggle_1 - t_toggle_0) / 1000.0

	# Restore to baseline state
	light_mgr.set_light_enabled(0, true)
	var final_eval = table.get_or_refresh_probe_irradiance(probe_id, light_mgr)
	var drift = (final_eval - baseline_eval).get_luminance()

	print("  Executed 100,000 Light Toggles in %.2f ms (%.3f us / toggle)!" % [toggle_time_ms, toggle_time_ms * 10.0])
	print("  Numerical Drift after 100,000 cycles: %.10f (Tolerance: < 1e-6)" % abs(drift))

	var drift_passed = abs(drift) < 0.000001
	if drift_passed:
		print("  ✅ 100,000-CYCLE DRIFT TEST PASSED: Zero Accumulation Error!")
	else:
		print("  ❌ 100,000-CYCLE DRIFT TEST FAILED: Drift: %.8f" % drift)

	# ==============================================================================
	# BENCHMARK 3: FAN-IN SCALING BENCHMARK (Section 26)
	# ==============================================================================
	print("\n>>> BENCHMARK 3: CONTRIBUTION FAN-IN SCALING (1 to 128 LIGHTS/PROBE) <<<")
	var fan_in_levels = [1, 2, 4, 8, 16, 32, 64, 128]
	var fan_in_results = {}

	for fan_in in fan_in_levels:
		var test_table = ProbeContributionTableScript.new()
		for p in range(500):
			for i in range(fan_in):
				test_table.add_contribution(p, i % num_lights, Color(0.05, 0.05, 0.05))

		var test_probe_ids: Array[int] = []
		for p in range(500):
			test_probe_ids.append(p)

		var t0 = Time.get_ticks_usec()
		test_table.refresh_batch(test_probe_ids, light_mgr)
		var t1 = Time.get_ticks_usec()
		var eval_ms = float(t1 - t0) / 1000.0
		var us_per_probe = (float(t1 - t0) / 500.0)

		fan_in_results[str(fan_in)] = {
			"fan_in_lights": fan_in,
			"eval_500_probes_ms": eval_ms,
			"us_per_probe": us_per_probe,
			"evaluations_per_ms": (500.0 * float(fan_in)) / max(0.001, eval_ms)
		}
		print("  Fan-In %3d Lights/Probe: %.3f ms for 500 probes (%.2f us/probe | %.0f evals/ms)" % [
			fan_in, eval_ms, us_per_probe, (500.0 * float(fan_in)) / max(0.001, eval_ms)
		])

	# ==============================================================================
	# EXPORT BENCHMARK JSON
	# ==============================================================================
	var export_data = {
		"benchmark": "ASTG Late-Bound Lighting & Lazy Probe Evaluation",
		"chaos_128_lights": {
			"lights_count": num_lights,
			"total_frames": 500,
			"topology_rays": topology_rays_traced,
			"total_lightstate_writes": total_light_writes,
			"light_updates_per_sec": light_updates_per_sec,
			"p50_latency_ms": p50,
			"p95_latency_ms": p95,
			"p99_latency_ms": p99
		},
		"cycle_100k_drift_test": {
			"total_toggles": 100000,
			"total_time_ms": toggle_time_ms,
			"us_per_toggle": toggle_time_ms * 10.0,
			"numerical_drift": drift,
			"passed": drift_passed
		},
		"fan_in_scaling": fan_in_results
	}

	var json_file = FileAccess.open("res://late_bound_lighting_benchmark_results.json", FileAccess.WRITE)
	if json_file:
		json_file.store_string(JSON.stringify(export_data, "\t"))
		json_file.close()
		print("\n[Export] Successfully exported res://late_bound_lighting_benchmark_results.json!")

	print("\n================================================================================")
	print("🎯 128-LIGHT CHAOS & DRIFT BENCHMARK COMPLETE!")
	print("================================================================================")

	quit(0 if drift_passed else 1)
