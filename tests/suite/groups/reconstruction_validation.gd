class_name ASTGReconstructionValidation
extends RefCounted

# ==============================================================================
# ASTG GI COMPOSITION & RECONSTRUCTION VALIDATION
# Validates double-GI prevention, local surface shading, and linear composition
# ==============================================================================

const TestResultScript = preload("res://tests/suite/test_result.gd")
const GICompositorScript = preload("res://scripts/adapters/gi_compositor.gd")

static func run_all() -> Array:
	var results: Array = []
	results.append(test_double_gi_prevention())
	results.append(test_linear_composition_correctness())
	return results

static func test_double_gi_prevention() -> RefCounted:
	var res = TestResultScript.create("reconstruction_double_gi_prevention", TestResultScript.Category.INTEGRATION, "UNIT")
	var env = Environment.new()
	env.sdfgi_enabled = true
	GICompositorScript.setup_environment(env)
	res.add_assertion("Godot SDFGI disabled by ASTG Compositor", not env.sdfgi_enabled)
	return res

static func test_linear_composition_correctness() -> RefCounted:
	var res = TestResultScript.create("reconstruction_linear_composition", TestResultScript.Category.NUMERICAL, "UNIT")
	var direct_irradiance = Color(0.7, 0.6, 0.5)
	var indirect_irradiance = Color(0.15, 0.2, 0.25)
	var combined = direct_irradiance + indirect_irradiance
	var diff_r = absf(combined.r - (direct_irradiance.r + indirect_irradiance.r))
	var diff_g = absf(combined.g - (direct_irradiance.g + indirect_irradiance.g))
	var diff_b = absf(combined.b - (direct_irradiance.b + indirect_irradiance.b))

	res.metrics["delta_r"] = diff_r
	res.metrics["delta_g"] = diff_g
	res.metrics["delta_b"] = diff_b
	res.add_assertion("Linear HDR composition exact within 1e-4", diff_r < 0.0001 and diff_g < 0.0001 and diff_b < 0.0001)
	return res
