class_name ASTGLateBoundValidation
extends RefCounted

# ==============================================================================
# ASTG LATE-BOUND LIGHTING & ZERO-TOPOLOGY INVARIANT VALIDATION
# Validates that RGB/Intensity/Toggle changes mutate zero topology rays
# ==============================================================================

const TestResultScript = preload("res://tests/suite/test_result.gd")
const LightAdapterScript = preload("res://scripts/adapters/light_adapter.gd")

static func run_all() -> Array:
	var results: Array = []
	results.append(test_zero_topology_on_energy_change())
	results.append(test_movement_triggers_topology_event())
	return results

static func test_zero_topology_on_energy_change() -> RefCounted:
	var res = TestResultScript.create("late_bound_zero_topology_invariant", TestResultScript.Category.CORRECTNESS, "INTEGRATION")
	var adapter = LightAdapterScript.new()
	adapter.initialize_population(500, AABB(Vector3(-10, 0, -10), Vector3(20, 10, 20)))

	var topo_events = [0]
	var energy_events = [0]
	adapter.topology_invalidated.connect(func(_id): topo_events[0] += 1)
	adapter.energy_state_updated.connect(func(_id): energy_events[0] += 1)

	# 1. Change RGB
	adapter.update_energy_state(0, Color.BLUE, 5.0, true)
	# 2. Change Intensity
	adapter.update_energy_state(1, Color.YELLOW, 12.0, true)
	# 3. Toggle off
	adapter.update_energy_state(2, Color.YELLOW, 12.0, false)

	res.metrics["lights_changed"] = 3
	res.metrics["topology_rays"] = topo_events[0]
	res.metrics["energy_events"] = energy_events[0]

	res.add_assertion("3 Energy state update events emitted", energy_events[0] == 3)
	res.add_assertion("ZERO topology invalidation events emitted", topo_events[0] == 0)
	return res

static func test_movement_triggers_topology_event() -> RefCounted:
	var res = TestResultScript.create("late_bound_movement_topology_event", TestResultScript.Category.CORRECTNESS, "INTEGRATION")
	var adapter = LightAdapterScript.new()
	adapter.initialize_population(10, AABB(Vector3(-10, 0, -10), Vector3(20, 10, 20)))

	var topo_events = [0]
	adapter.topology_invalidated.connect(func(_id): topo_events[0] += 1)

	adapter.update_transform(0, Vector3(10, 10, 10), Vector3.DOWN)
	res.add_assertion("Spatial translation emitted topology invalidation event", topo_events[0] == 1)
	return res
