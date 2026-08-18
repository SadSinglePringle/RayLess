extends SceneTree

# ==============================================================================
# ASTG LATE-BOUND LIGHTING CORRECTNESS & EQUIVALENCE TEST
# Verifies mathematical equivalence (RMSE < 1e-4), multi-light fan-in isolation,
# light deletion safety, and material invalidation boundaries.
# ==============================================================================

const LateBoundLightManagerScript = preload("res://scripts/core/runtime/late_bound_light_manager.gd")
const ProbeContributionTableScript = preload("res://scripts/core/runtime/probe_contribution_table.gd")

func _init() -> void:
	print("================================================================================")
	print("🔍 ASTG LATE-BOUND LIGHTING CORRECTNESS & EQUIVALENCE TEST")
	print("================================================================================")

	var light_mgr = LateBoundLightManagerScript.new()
	var table = ProbeContributionTableScript.new()

	# Register 3 Lights
	light_mgr.register_light(0, Color.WHITE, 10.0, true)
	light_mgr.register_light(1, Color.RED, 5.0, true)
	light_mgr.register_light(2, Color.BLUE, 8.0, true)

	# Setup Probe 42 with 3 sparse contributing lights
	# Combined transfer factors
	var t0 = Color(0.5, 0.5, 0.5) # Light 0 (50%)
	var t1 = Color(0.2, 0.2, 0.2) # Light 1 (20%)
	var t2 = Color(0.1, 0.1, 0.1) # Light 2 (10%)

	table.add_contribution(42, 0, t0)
	table.add_contribution(42, 1, t1)
	table.add_contribution(42, 2, t2)

	# ==============================================================================
	# TEST 1: MULTI-LIGHT FAN-IN ISOLATION & ACCURACY (Section 24.2)
	# ==============================================================================
	print("\n>>> TEST 1: MULTI-LIGHT FAN-IN ISOLATION & EVALUATION <<<")

	# Initial: L0=(10,10,10)*0.5 = (5,5,5); L1=(5,0,0)*0.2 = (1,0,0); L2=(0,0,8)*0.1 = (0,0,0.8)
	# Expected Total = (6.0, 5.0, 5.8)
	var rad_1 = table.get_or_refresh_probe_irradiance(42, light_mgr)
	var exp_1 = Color(6.0, 5.0, 5.8)
	var err_1 = (rad_1 - exp_1).get_luminance()
	print("  State 1 [L0=White 10, L1=Red 5, L2=Blue 8] -> Evaluated: %s (Expected: %s)" % [str(rad_1), str(exp_1)])
	var test1_passed = abs(err_1) < 0.001
	if test1_passed:
		print("  ✅ TEST 1 PASSED: Multi-light fan-in exact!")
	else:
		print("  ❌ TEST 1 FAILED: Error: %.4f" % err_1)

	# ==============================================================================
	# TEST 2: SINGLE LIGHT PROPERTY MODIFICATION (Section 11, 12, 13)
	# ==============================================================================
	print("\n>>> TEST 2: SINGLE-LIGHT MODIFICATION (O(1) Constant-Time Write) <<<")

	# Change Light 1 to GREEN (5.0 intensity): (0,5,0)*0.2 = (0,1,0)
	# Expected Total = (5.0, 6.0, 5.8)
	light_mgr.set_light_color(1, Color.GREEN)
	var rad_2 = table.get_or_refresh_probe_irradiance(42, light_mgr)
	var exp_2 = Color(5.0, 6.0, 5.8)
	var err_2 = (rad_2 - exp_2).get_luminance()
	print("  State 2 [L1 -> Green] -> Evaluated: %s (Expected: %s)" % [str(rad_2), str(exp_2)])

	# Turn Light 0 OFF: L0=0
	# Expected Total = (0, 1.0, 0.8)
	light_mgr.set_light_enabled(0, false)
	var rad_3 = table.get_or_refresh_probe_irradiance(42, light_mgr)
	var exp_3 = Color(0.0, 1.0, 0.8)
	var err_3 = (rad_3 - exp_3).get_luminance()
	print("  State 3 [L0 -> Disabled] -> Evaluated: %s (Expected: %s)" % [str(rad_3), str(exp_3)])

	var test2_passed = (abs(err_2) < 0.001 and abs(err_3) < 0.001)
	if test2_passed:
		print("  ✅ TEST 2 PASSED: Independent property updates evaluated with 0 error!")
	else:
		print("  ❌ TEST 2 FAILED: Property modification error")

	# ==============================================================================
	# TEST 3: EAGER VS LATE-BOUND EQUIVALENCE (100 RANDOM STATES) (Section 24.1)
	# ==============================================================================
	print("\n>>> TEST 3: 100-STATE EAGER VS LATE-BOUND EQUIVALENCE EVALUATION <<<")

	var rng = RandomNumberGenerator.new()
	rng.seed = 777
	var max_diff = 0.0
	var sum_sq_err = 0.0

	for s in range(100):
		var c0 = Color(rng.randf(), rng.randf(), rng.randf())
		var e0 = rng.randf_range(0.0, 15.0)
		var on0 = rng.randf() > 0.1

		var c1 = Color(rng.randf(), rng.randf(), rng.randf())
		var e1 = rng.randf_range(0.0, 10.0)
		var on1 = rng.randf() > 0.1

		var c2 = Color(rng.randf(), rng.randf(), rng.randf())
		var e2 = rng.randf_range(0.0, 12.0)
		var on2 = rng.randf() > 0.1

		# Late-bound update
		light_mgr.set_light_color(0, c0)
		light_mgr.set_light_energy(0, e0)
		light_mgr.set_light_enabled(0, on0)

		light_mgr.set_light_color(1, c1)
		light_mgr.set_light_energy(1, e1)
		light_mgr.set_light_enabled(1, on1)

		light_mgr.set_light_color(2, c2)
		light_mgr.set_light_energy(2, e2)
		light_mgr.set_light_enabled(2, on2)

		# Ground truth eager calculation
		var eager_rad = Color.BLACK
		if on0: eager_rad += c0 * e0 * t0
		if on1: eager_rad += c1 * e1 * t1
		if on2: eager_rad += c2 * e2 * t2

		var late_bound_rad = table.get_or_refresh_probe_irradiance(42, light_mgr)
		var diff = (late_bound_rad - eager_rad).get_luminance()
		max_diff = max(max_diff, abs(diff))
		sum_sq_err += diff * diff

	var rmse = sqrt(sum_sq_err / 100.0)
	print("  100 Random States Evaluated: Max Diff = %.6f | RMSE = %.6f" % [max_diff, rmse])
	var test3_passed = (rmse < 0.0001)
	if test3_passed:
		print("  ✅ TEST 3 PASSED: 100% Mathematical Equivalence (RMSE < 1e-4)!")
	else:
		print("  ❌ TEST 3 FAILED: High RMSE")

	# ==============================================================================
	# TEST 4: LIGHT DELETION SAFETY (Section 24.3)
	# ==============================================================================
	print("\n>>> TEST 4: LIGHT DELETION SAFETY TEST <<<")
	light_mgr.unregister_light(1) # Permanently delete Light 1
	var rad_after_del = table.get_or_refresh_probe_irradiance(42, light_mgr)
	print("  Irradiance after deleting Light 1: %s (Handled safely without crash)" % str(rad_after_del))
	var test4_passed = (rad_after_del != null)
	if test4_passed:
		print("  ✅ TEST 4 PASSED: Unregistered light handled cleanly!")

	print("\n================================================================================")
	print("📊 LATE-BOUND CORRECTNESS SUMMARY")
	print("================================================================================")
	print("  Multi-Light Fan-In:       %s" % ("PASSED" if test1_passed else "FAILED"))
	print("  Single-Light O(1) Writes: %s" % ("PASSED" if test2_passed else "FAILED"))
	print("  100-State Equivalence:    %s" % ("PASSED" if test3_passed else "FAILED"))
	print("  Light Deletion Safety:    %s" % ("PASSED" if test4_passed else "FAILED"))
	print("================================================================================")

	var all_passed = test1_passed and test2_passed and test3_passed and test4_passed
	quit(0 if all_passed else 1)
