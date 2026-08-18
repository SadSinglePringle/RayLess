class_name ASTGTestResult
extends RefCounted

# ==============================================================================
# ASTG UNIVERSAL TEST RESULT DATA MODEL
# Standardized result reporting across Godot and Native D3D12/DXR benchmarks
# ==============================================================================

enum Status {
	PASS,
	FAIL,
	INVALID,
	SKIPPED
}

enum Category {
	CORRECTNESS,
	PERFORMANCE,
	QUALITY,
	NUMERICAL,
	INTEGRATION,
	MEMORY,
	HARNESS
}

var name: String = ""
var status: int = Status.PASS
var category: int = Category.CORRECTNESS
var duration_ms: float = 0.0

var metrics: Dictionary = {}
var assertions: Array[String] = []
var warnings: Array[String] = []
var errors: Array[String] = []

var evidence_level: String = "UNIT" # UNIT, INTEGRATION, GPU_END_TO_END, IMAGE_REFERENCE, STRESS
var diagnosis: String = ""

static func create(p_name: String, p_category: int = 0, p_evidence: String = "UNIT") -> RefCounted:
	var res = (load("res://tests/suite/test_result.gd") as GDScript).new()
	res.name = p_name
	res.category = p_category
	res.evidence_level = p_evidence
	return res

func pass_test() -> void:
	status = Status.PASS

func fail_test(err_msg: String) -> void:
	status = Status.FAIL
	errors.append(err_msg)

func invalidate_test(reason: String) -> void:
	status = Status.INVALID
	errors.append("[INVALID WORKLOAD] " + reason)

func skip_test(reason: String) -> void:
	status = Status.SKIPPED
	warnings.append("[SKIPPED] " + reason)

func add_assertion(desc: String, passed: bool) -> void:
	assertions.append("%s: %s" % ["PASS" if passed else "FAIL", desc])
	if not passed:
		fail_test("Assertion failed: " + desc)

func get_status_string() -> String:
	match status:
		Status.PASS: return "PASS"
		Status.FAIL: return "FAIL"
		Status.INVALID: return "INVALID"
		Status.SKIPPED: return "SKIPPED"
		_: return "UNKNOWN"

func get_category_string() -> String:
	match category:
		Category.CORRECTNESS: return "CORRECTNESS"
		Category.PERFORMANCE: return "PERFORMANCE"
		Category.QUALITY: return "QUALITY"
		Category.NUMERICAL: return "NUMERICAL"
		Category.INTEGRATION: return "INTEGRATION"
		Category.MEMORY: return "MEMORY"
		Category.HARNESS: return "HARNESS"
		_: return "UNKNOWN"

func to_dict() -> Dictionary:
	return {
		"name": name,
		"status": get_status_string(),
		"category": get_category_string(),
		"evidence_level": evidence_level,
		"duration_ms": duration_ms,
		"metrics": metrics,
		"assertions": assertions,
		"warnings": warnings,
		"errors": errors,
		"diagnosis": diagnosis
	}
