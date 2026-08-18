extends SceneTree

# ==============================================================================
# ASTG TEST HARNESS ENTRY POINT (Delegates to Unified Diagnostic Suite)
# ==============================================================================

const BenchmarkSuiteScript = preload("res://tests/suite/benchmark_suite.gd")

func _init() -> void:
	# Run the Comprehensive Benchmark & Diagnostic Suite in QUICK mode by default
	var suite = BenchmarkSuiteScript.new()
	suite._init()
