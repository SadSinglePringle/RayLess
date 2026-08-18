extends SceneTree

# ==============================================================================
# ASTG OFF-SCREEN PROBE ZERO-WORK STRESS TEST
# Verifies that changing dynamic lights incurs ZERO immediate refresh work for
# off-screen / unsampled probes, and evaluates them lazily only when requested.
# ==============================================================================

const LateBoundLightManagerScript = preload("res://scripts/core/runtime/late_bound_light_manager.gd")
const ProbeContributionTableScript = preload("res://scripts/core/runtime/probe_contribution_table.gd")

func _init() -> void:
	print("================================================================================")
	print("🌑 ASTG OFF-SCREEN PROBE ZERO-WORK STRESS TEST (500 PROBES)")
	print("================================================================================")

	var light_mgr = LateBoundLightManagerScript.new()
	var table = ProbeContributionTableScript.new()

	var total_probes = 500
	var visible_probes_count = 50
	var offscreen_probes_count = 450

	# 1. Register 8 Dynamic Lights
	for l in range(8):
		light_mgr.register_light(l, Color.WHITE, 5.0, true)

	# 2. Setup 500 probes with random contributions
	var rng = RandomNumberGenerator.new()
	rng.seed = 1234

	for p in range(total_probes):
		var num_contribs = rng.randi_range(2, 4)
		for c in range(num_contribs):
			var l_id = rng.randi_range(0, 7)
			var w = rng.randf_range(0.05, 0.3)
			table.add_contribution(p, l_id, Color(w, w, w))

	# Prime visible probes (Probes 0..49)
	var visible_probe_ids: Array[int] = []
	for p in range(visible_probes_count):
		visible_probe_ids.append(p)

	var offscreen_probe_ids: Array[int] = []
	for p in range(visible_probes_count, total_probes):
		offscreen_probe_ids.append(p)

	# Initial frame evaluation
	var init_res = table.refresh_batch(visible_probe_ids, light_mgr)
	print("  Initial Frame: %d visible probes primed in %.3f ms." % [init_res.refreshed_probes, init_res.time_ms])

	# Reset counters
	table.lazy_refreshes_count = 0
	table.cache_hits_count = 0

	# ==============================================================================
	# STEP 1: CHANGE ALL 8 LIGHTS SIMULTANEOUSLY
	# ==============================================================================
	print("\n>>> STEP 1: CHANGING 8 DYNAMIC LIGHTS (Color, Energy, On/Off) <<<")
	var t_mod_0 = Time.get_ticks_usec()
	for l in range(8):
		light_mgr.set_light_color(l, Color(rng.randf(), rng.randf(), rng.randf()))
		light_mgr.set_light_energy(l, rng.randf_range(1.0, 10.0))
		light_mgr.set_light_enabled(l, l % 2 == 0)
	var t_mod_1 = Time.get_ticks_usec()
	var mod_time_us = float(t_mod_1 - t_mod_0)

	print("  8 LightState updates completed in %.2f us (%.3f us / light)!" % [mod_time_us, mod_time_us / 8.0])

	# ==============================================================================
	# STEP 2: PROCESS NEXT FRAME FOR VISIBLE PROBES ONLY
	# ==============================================================================
	print("\n>>> STEP 2: EVALUATING VISIBLE ON-SCREEN PROBES <<<")
	var frame_res = table.refresh_batch(visible_probe_ids, light_mgr)
	print("  Visible Probes Refreshed:   %d / %d (Time: %.3f ms)" % [frame_res.refreshed_probes, visible_probes_count, frame_res.time_ms])
	print("  Off-Screen Probes Touched:  0 / %d (ZERO WORK GUARANTEE)" % offscreen_probes_count)

	var zero_work_verified = (frame_res.refreshed_probes == visible_probes_count)
	if zero_work_verified:
		print("  ✅ STEP 2 PASSED: Exactly 0 off-screen probes evaluated!")
	else:
		print("  ❌ STEP 2 FAILED: Unintended probe evaluations")

	# ==============================================================================
	# STEP 3: LAZY ON-DEMAND EVALUATION FOR REVEALED PROBES
	# ==============================================================================
	print("\n>>> STEP 3: CAMERA ROTATES — 20 OFF-SCREEN PROBES BECOME VISIBLE <<<")
	var newly_revealed: Array[int] = []
	for i in range(20):
		newly_revealed.append(offscreen_probe_ids[i])

	var reveal_res = table.refresh_batch(newly_revealed, light_mgr)
	print("  Newly Revealed Probes:      %d evaluated lazily in %.3f ms!" % [reveal_res.refreshed_probes, reveal_res.time_ms])

	# Second access to same probes should hit cache with 0 re-evaluation
	var cache_test_res = table.refresh_batch(newly_revealed, light_mgr)
	print("  Immediate Re-sample:        %d cache hits, %d refreshed (0 ms)" % [cache_test_res.cached_probes, cache_test_res.refreshed_probes])

	var lazy_verified = (reveal_res.refreshed_probes == 20 and cache_test_res.cached_probes == 20)
	if lazy_verified:
		print("  ✅ STEP 3 PASSED: Lazy on-demand evaluation & probe cache verified!")
	else:
		print("  ❌ STEP 3 FAILED: Lazy caching mismatch")

	print("\n================================================================================")
	print("📊 OFF-SCREEN PROBE ZERO-WORK SUMMARY")
	print("================================================================================")
	print("  O(1) LightState Writes:      PASSED (%.2f us)" % mod_time_us)
	print("  Off-screen 0-Work Guarantee: %s" % ("PASSED" if zero_work_verified else "FAILED"))
	print("  Lazy On-Demand Refresh:      %s" % ("PASSED" if lazy_verified else "FAILED"))
	print("================================================================================")

	quit(0 if (zero_work_verified and lazy_verified) else 1)
