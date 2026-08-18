class_name ASTGTransportValidation
extends RefCounted

# ==============================================================================
# ASTG TRANSPORT GRAPH & DAG INVARIANTS VALIDATION
# Validates DAG acyclicity, energy propagation, and reverse dependency indexing
# ==============================================================================

const TestResultScript = preload("res://tests/suite/test_result.gd")
const TestReverseDependencyScript = preload("res://tests/unit/test_reverse_dependency.gd")
const TestPriorityQueueScript = preload("res://tests/unit/test_priority_queue.gd")
const TestEnergyPropagationScript = preload("res://tests/unit/test_energy_propagation.gd")

static func run_all() -> Array:
	var results: Array = []
	results.append(test_reverse_dependency_db())
	results.append(test_priority_repair_queue())
	results.append(test_energy_propagation())
	return results

static func test_reverse_dependency_db() -> RefCounted:
	var res = TestResultScript.create("transport_reverse_dependencies", 0, "UNIT")
	var ok = TestReverseDependencyScript.run()
	res.add_assertion("Reverse dependency unit test passed", ok)
	return res

static func test_priority_repair_queue() -> RefCounted:
	var res = TestResultScript.create("transport_priority_repair_scheduler", 0, "UNIT")
	var ok = TestPriorityQueueScript.run()
	res.add_assertion("Priority repair queue unit test passed", ok)
	return res

static func test_energy_propagation() -> RefCounted:
	var res = TestResultScript.create("transport_energy_propagation", 3, "UNIT")
	var ok = TestEnergyPropagationScript.run()
	res.add_assertion("Energy propagation unit test passed", ok)
	return res
