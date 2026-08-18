extends SceneTree

# ==============================================================================
# ASTG COMPREHENSIVE BENCHMARKING & PROFILING SUITE ORCHESTRATOR
# Single automated entry point for correctness, performance, and diagnosis
# ==============================================================================

const SuiteConfigScript = preload("res://tests/suite/suite_config.gd")
const TestResultScript = preload("res://tests/suite/test_result.gd")
const DiagnosisEngineScript = preload("res://tests/suite/diagnosis_engine.gd")
const ResultWriterScript = preload("res://tests/suite/result_writer.gd")
const NativeBridgeScript = preload("res://tests/suite/native/native_benchmark_bridge.gd")

const BootValidationScript = preload("res://tests/suite/groups/boot_validation.gd")
const SceneValidationScript = preload("res://tests/suite/groups/scene_validation.gd")
const TransportValidationScript = preload("res://tests/suite/groups/transport_validation.gd")
const ProbeValidationScript = preload("res://tests/suite/groups/probe_validation.gd")
const LateBoundValidationScript = preload("res://tests/suite/groups/late_bound_validation.gd")
const DestructionValidationScript = preload("res://tests/suite/groups/destruction_validation.gd")
const ReconstructionValidationScript = preload("res://tests/suite/groups/reconstruction_validation.gd")

func _init() -> void:
	var config = SuiteConfigScript.parse_args()
	_execute_suite(config)

func _execute_suite(config: RefCounted) -> void:
	var start_time_us = Time.get_ticks_usec()
	var all_results: Array[RefCounted] = []

	print("================================================================================")
	print("🛡️ RAYLESS / ASTG COMPREHENSIVE DIAGNOSTIC & BENCHMARK SUITE")
	print("================================================================================")
	print("Suite Level:       %s" % config.get_level_string())
	print("Target Scene:      %s" % config.target_scene)
	print("Fail Fast:         %s" % config.fail_fast)
	print("--------------------------------------------------------------------------------")

	var groups = [
		{"name": "Boot & Environment", "runner": BootValidationScript},
		{"name": "Scene & Geometry", "runner": SceneValidationScript},
		{"name": "Transport & Invariants", "runner": TransportValidationScript},
		{"name": "Probes & Surfaces", "runner": ProbeValidationScript},
		{"name": "Late-Bound Lighting", "runner": LateBoundValidationScript},
		{"name": "Destruction & Repair", "runner": DestructionValidationScript},
		{"name": "GI Reconstruction", "runner": ReconstructionValidationScript}
	]

	var has_failures = false
	var passed_count = 0
	var failed_count = 0
	var invalid_count = 0
	var skipped_count = 0

	for g in groups:
		if config.target_group != "" and not g["name"].to_lower().contains(config.target_group):
			continue

		print("\n▶ Running Group: %s..." % g["name"])
		var group_results = g["runner"].run_all()

		for r in group_results:
			all_results.append(r)
			r.diagnosis = DiagnosisEngineScript.diagnose(r)

			var tag = "[PASS]"
			if r.status == TestResultScript.Status.PASS:
				passed_count += 1
				print("  ✅ %s: %s (%.2f ms)" % [tag, r.name, r.duration_ms])
			elif r.status == TestResultScript.Status.FAIL:
				failed_count += 1
				has_failures = true
				print("  ❌ [FAIL]: %s - %s" % [r.name, r.errors[0] if not r.errors.is_empty() else "Failed"])
				print("     ↳ %s" % r.diagnosis)
			elif r.status == TestResultScript.Status.INVALID:
				invalid_count += 1
				has_failures = true
				print("  ⚠️ [INVALID]: %s - %s" % [r.name, r.diagnosis])
			elif r.status == TestResultScript.Status.SKIPPED:
				skipped_count += 1
				print("  ⏭️ [SKIPPED]: %s" % r.name)

			if has_failures and config.fail_fast:
				print("\n⚠️ FAIL-FAST: Terminating suite early due to critical failure.")
				break

		if has_failures and config.fail_fast:
			break

	# Run Native DXR Bistro Bridge if requested / enabled
	if not has_failures or not config.fail_fast:
		if config.level >= SuiteConfigScript.SuiteLevel.STANDARD:
			print("\n▶ Running Group: Native D3D12 / DXR Hardware Benchmarks...")
			var native_res = NativeBridgeScript.run_native_bistro(32)
			all_results.append(native_res)
			native_res.diagnosis = DiagnosisEngineScript.diagnose(native_res)
			if native_res.status == TestResultScript.Status.PASS:
				passed_count += 1
				print("  ✅ [PASS]: %s (%.2f ms)" % [native_res.name, native_res.duration_ms])
			else:
				failed_count += 1
				has_failures = true
				print("  ❌ [FAIL]: %s" % native_res.name)
				print("     ↳ %s" % native_res.diagnosis)

	var total_time_ms = float(Time.get_ticks_usec() - start_time_us) / 1000.0

	var summary = {
		"status": "FAIL" if has_failures else "PASS",
		"level": config.get_level_string(),
		"total_tests": all_results.size(),
		"passed": passed_count,
		"failed": failed_count,
		"invalid": invalid_count,
		"skipped": skipped_count,
		"duration_ms": total_time_ms,
		"timestamp": Time.get_datetime_string_from_system()
	}

	# Write structured artifacts to results/latest/
	ResultWriterScript.write_results(summary, all_results)

	print("\n================================================================================")
	print("📊 ASTG SUITE EXECUTION SUMMARY")
	print("================================================================================")
	print("Status:            %s" % ("❌ FAILED" if has_failures else "🎉 ALL TESTS PASSED"))
	print("Total Tests:       %d" % all_results.size())
	print("Passed:            %d" % passed_count)
	print("Failed:            %d" % failed_count)
	print("Invalid:           %d" % invalid_count)
	print("Skipped:           %d" % skipped_count)
	print("Total Duration:    %.2f ms (%.2f s)" % [total_time_ms, total_time_ms / 1000.0])
	print("Artifacts Written: results/latest/summary.json, tests.json, diagnostics.txt")
	print("================================================================================\n")

	quit(1 if has_failures else 0)
