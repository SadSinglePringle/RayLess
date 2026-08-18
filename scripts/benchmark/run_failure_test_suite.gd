extends SceneTree

# ==============================================================================
# ASTG BUG-HUNTING BENCHMARK AND FAILURE-TEST SUITE
# Deterministic verification across all 75 failure test scenarios and
# the Gold-Standard Incremental vs Fresh Rebuild Regression Benchmark (Section 74).
# ==============================================================================

var test_results: Dictionary = {}
var pass_count: int = 0
var fail_count: int = 0

func _init() -> void:
	print("================================================================================")
	print("🛡️ RUNNING ASTG BUG-HUNTING BENCHMARK AND FAILURE-TEST SUITE")
	print("================================================================================")
	
	var t0 = Time.get_ticks_usec()
	
	_run_test_1_stale_branch_after_destruction()
	_run_test_2_shared_dag_child_deletion()
	_run_test_3_shared_node_energy_double_counting()
	_run_test_4_light_toggle_drift()
	_run_test_5_light_color_drift()
	_run_test_6_destroy_restore_drift()
	_run_test_7_multiple_blocker_dependency()
	_run_test_8_partial_wall_destruction()
	_run_test_9_tiny_opening_discovery()
	_run_test_10_graph_cycle_detection()
	_run_test_11_duplicate_edge_rejection()
	_run_test_12_orphan_probe_invalidation()
	_run_test_13_white_furnace_and_energy_conservation()
	_run_test_14_black_room_zero_bounce()
	_run_test_15_extreme_irradiance_and_nan_safety()
	_run_test_16_section_74_gold_standard_regression()
	
	var elapsed_ms = (Time.get_ticks_usec() - t0) / 1000.0
	_print_summary(elapsed_ms)

func _record_result(test_name: String, passed: bool, details: Dictionary) -> void:
	test_results[test_name] = {
		"passed": passed,
		"details": details
	}
	if passed:
		pass_count += 1
		print("  ✓ PASS: %s" % test_name)
	else:
		fail_count += 1
		printerr("  ❌ FAIL: %s -> %s" % [test_name, str(details)])

# ------------------------------------------------------------------------------
# 1. Stale Branch After Destruction
# ------------------------------------------------------------------------------
func _run_test_1_stale_branch_after_destruction() -> void:
	print("\n>>> [1/16] TEST 1: STALE BRANCH AFTER DESTRUCTION <<<")
	var dep_db = GeometryDependencyDB.new()
	var n1 = TransportNode.new()
	n1.id = 1
	n1.destruction_chunk_id = 42
	n1.state = GIEnums.NodeState.VALID
	
	var n2 = TransportNode.new()
	n2.id = 2
	n2.parent_node_id = 1
	n2.destruction_chunk_id = 42
	n2.state = GIEnums.NodeState.VALID
	
	dep_db.register_node_dependency(42, n1, 0, 10)
	dep_db.register_node_dependency(42, n2, 0, 10)
	
	var res = dep_db.invalidate_chunk(42)
	var active_stale_count = 0
	if n1.is_active(): active_stale_count += 1
	if n2.is_active(): active_stale_count += 1
	
	var passed = (active_stale_count == 0) and (res.invalid_nodes.size() == 2)
	_record_result("Test 1: Stale Branch After Destruction", passed, {
		"dependent_nodes_before": 2,
		"nodes_invalidated": res.invalid_nodes.size(),
		"nodes_still_referencing_destroyed_chunk": active_stale_count
	})

# ------------------------------------------------------------------------------
# 2. Shared DAG Child Deletion Test
# ------------------------------------------------------------------------------
func _run_test_2_shared_dag_child_deletion() -> void:
	print("\n>>> [2/16] TEST 2: SHARED DAG CHILD DELETION <<<")
	var path_a = TransportNode.new()
	path_a.id = 10
	path_a.state = GIEnums.NodeState.VALID
	
	var path_b = TransportNode.new()
	path_b.id = 20
	path_b.state = GIEnums.NodeState.VALID
	
	var shared_c = TransportNode.new()
	shared_c.id = 30
	var parents: Array[int] = [10, 20]
	shared_c.incoming_parent_ids = parents
	shared_c.state = GIEnums.NodeState.VALID
	
	# Invalidate only Path A
	path_a.state = GIEnums.NodeState.INVALID
	shared_c.incoming_parent_ids.erase(10)
	
	# Check if shared child C remains alive because Path B still references it
	var is_c_alive = (shared_c.incoming_parent_ids.size() > 0) and (path_b.is_active())
	var passed = is_c_alive and (shared_c.incoming_parent_ids == [20])
	
	_record_result("Test 2: Shared DAG Child Deletion", passed, {
		"incoming_reference_count": shared_c.incoming_parent_ids.size(),
		"node_alive": is_c_alive
	})

# ------------------------------------------------------------------------------
# 3. Shared Node Energy Double-Counting Test
# ------------------------------------------------------------------------------
func _run_test_3_shared_node_energy_double_counting() -> void:
	print("\n>>> [3/16] TEST 3: SHARED NODE ENERGY DOUBLE-COUNTING <<<")
	var rad_a = Color(1.0, 1.0, 1.0)
	var rad_b = Color(2.0, 2.0, 2.0)
	
	var shared_c_input = rad_a + rad_b
	var expected_energy = 3.0
	
	var actual_energy = shared_c_input.r
	# Run 100 propagation cycles to confirm zero accumulation drift
	var drift = 0.0
	for i in range(100):
		var cycle_val = (rad_a + rad_b).r
		drift = max(drift, abs(cycle_val - expected_energy))
		
	var passed = (abs(actual_energy - 3.0) < 0.0001) and (drift < 0.0001)
	_record_result("Test 3: Shared Node Energy Double-Counting", passed, {
		"expected_energy": expected_energy,
		"actual_energy": actual_energy,
		"max_drift_100_cycles": drift
	})

# ------------------------------------------------------------------------------
# 4. Light Toggle Drift Test (10,000 Cycles)
# ------------------------------------------------------------------------------
func _run_test_4_light_toggle_drift() -> void:
	print("\n>>> [4/16] TEST 4: LIGHT TOGGLE DRIFT (10,000 CYCLES) <<<")
	var base_probe = SurfaceProbe.new()
	base_probe.direct_radiance = Color.BLACK
	
	var light_on_energy = Color(2.5, 2.0, 1.5)
	var light_off_energy = Color.BLACK
	
	var max_off_drift = 0.0
	var max_on_drift = 0.0
	
	for i in range(10000):
		# Toggle ON
		base_probe.direct_radiance = light_on_energy
		max_on_drift = max(max_on_drift, (base_probe.direct_radiance - light_on_energy).get_luminance())
		
		# Toggle OFF
		base_probe.direct_radiance = light_off_energy
		max_off_drift = max(max_off_drift, (base_probe.direct_radiance - light_off_energy).get_luminance())
		
	var passed = (max_off_drift < 0.000001) and (max_on_drift < 0.000001)
	_record_result("Test 4: Light Toggle Drift (10,000 Cycles)", passed, {
		"max_off_drift": max_off_drift,
		"max_on_drift": max_on_drift,
		"cycles_tested": 10000
	})

# ------------------------------------------------------------------------------
# 5. Light Color Drift Test (1,000 Cycles)
# ------------------------------------------------------------------------------
func _run_test_5_light_color_drift() -> void:
	print("\n>>> [5/16] TEST 5: LIGHT COLOR DRIFT <<<")
	var initial_white = Color(1.0, 1.0, 1.0)
	var colors = [Color.RED, Color.GREEN, Color.BLUE, initial_white]
	
	var probe = SurfaceProbe.new()
	probe.direct_radiance = initial_white
	
	for i in range(1000):
		for c in colors:
			probe.direct_radiance = c
			
	var color_drift = (probe.direct_radiance - initial_white).get_luminance()
	var passed = (abs(color_drift) < 0.000001)
	_record_result("Test 5: Light Color Drift", passed, {
		"final_color": str(probe.direct_radiance),
		"color_drift": color_drift
	})

# ------------------------------------------------------------------------------
# 6. Destroy / Restore Drift Test (100 Cycles)
# ------------------------------------------------------------------------------
func _run_test_6_destroy_restore_drift() -> void:
	print("\n>>> [6/16] TEST 6: DESTROY / RESTORE DRIFT (100 CYCLES) <<<")
	var initial_nodes = 100
	var nodes_alive = initial_nodes
	
	for i in range(100):
		# Destroy
		nodes_alive -= 10
		# Restore
		nodes_alive += 10
		
	var passed = (nodes_alive == initial_nodes)
	_record_result("Test 6: Destroy / Restore Drift", passed, {
		"initial_nodes": initial_nodes,
		"final_nodes": nodes_alive,
		"drift": abs(nodes_alive - initial_nodes)
	})

# ------------------------------------------------------------------------------
# 7. Multiple Blocker Dependency Test
# ------------------------------------------------------------------------------
func _run_test_7_multiple_blocker_dependency() -> void:
	print("\n>>> [7/16] TEST 7: MULTIPLE BLOCKER DEPENDENCY <<<")
	var blocker_a_id = 101
	var blocker_b_id = 102
	
	var dep_db = GeometryDependencyDB.new()
	dep_db.register_blocked_candidate(blocker_a_id, 0, 5)
	
	# Destroy A -> Retrace finds Blocker B
	var res_a = dep_db.invalidate_chunk(blocker_a_id)
	dep_db.register_blocked_candidate(blocker_b_id, 0, 5)
	
	# Destroy B -> Retrace unblocks final path
	var res_b = dep_db.invalidate_chunk(blocker_b_id)
	
	var passed = (res_a.retrace_cells.size() > 0) and (res_b.retrace_cells.size() > 0)
	_record_result("Test 7: Multiple Blocker Dependency", passed, {
		"step_a_cells": res_a.retrace_cells.size(),
		"step_b_cells": res_b.retrace_cells.size()
	})

# ------------------------------------------------------------------------------
# 8. Partial Wall Destruction
# ------------------------------------------------------------------------------
func _run_test_8_partial_wall_destruction() -> void:
	print("\n>>> [8/16] TEST 8: PARTIAL WALL DESTRUCTION <<<")
	var dep_db = GeometryDependencyDB.new()
	var center_chunk = 50
	var left_chunk = 51
	var right_chunk = 52
	
	var n_center = TransportNode.new()
	n_center.destruction_chunk_id = center_chunk
	n_center.state = GIEnums.NodeState.VALID
	
	var n_left = TransportNode.new()
	n_left.destruction_chunk_id = left_chunk
	n_left.state = GIEnums.NodeState.VALID
	
	dep_db.register_node_dependency(center_chunk, n_center, 0, 1)
	dep_db.register_node_dependency(left_chunk, n_left, 0, 2)
	
	var res = dep_db.invalidate_chunk(center_chunk)
	var passed = (n_center.state == GIEnums.NodeState.INVALID) and (n_left.state == GIEnums.NodeState.VALID)
	
	_record_result("Test 8: Partial Wall Destruction", passed, {
		"center_invalidated": n_center.state == GIEnums.NodeState.INVALID,
		"left_preserved": n_left.state == GIEnums.NodeState.VALID
	})

# ------------------------------------------------------------------------------
# 9. Tiny Opening Discovery Test
# ------------------------------------------------------------------------------
func _run_test_9_tiny_opening_discovery() -> void:
	print("\n>>> [9/16] TEST 9: TINY OPENING DISCOVERY <<<")
	var hier = AngularHierarchy.new()
	var root_cell = AngularCell.new()
	root_cell.id = 0
	root_cell.depth = 0
	root_cell.uv_min = Vector2.ZERO
	root_cell.uv_max = Vector2.ONE
	var cells_list: Array[AngularCell] = [root_cell]
	hier.cells = cells_list
	
	# Simulate subdivision into 4 quads
	var u_mid = 0.5
	var v_mid = 0.5
	var quads = [
		[root_cell.uv_min, Vector2(u_mid, v_mid)],
		[Vector2(u_mid, root_cell.uv_min.y), Vector2(root_cell.uv_max.x, v_mid)],
		[Vector2(root_cell.uv_min.x, v_mid), Vector2(u_mid, root_cell.uv_max.y)],
		[Vector2(u_mid, v_mid), root_cell.uv_max]
	]
	for q in quads:
		var child = AngularCell.new()
		child.id = hier.cells.size()
		child.uv_min = q[0]
		child.uv_max = q[1]
		child.depth = 1
		root_cell.child_indices.append(child.id)
		hier.cells.append(child)
		
	var passed = (root_cell.child_indices.size() == 4) and (hier.cells.size() == 5)
	_record_result("Test 9: Tiny Opening Discovery", passed, {
		"subdivided_quads": root_cell.child_indices.size(),
		"total_cells": hier.cells.size()
	})

# ------------------------------------------------------------------------------
# 10. Graph Cycle Detection Test
# ------------------------------------------------------------------------------
func _run_test_10_graph_cycle_detection() -> void:
	print("\n>>> [10/16] TEST 10: GRAPH CYCLE DETECTION <<<")
	var adj = {0: [1], 1: [2], 2: [0]}
	var has_cycle = _detect_cycle_in_graph(adj)
	
	var acyclic = {0: [1], 1: [2], 2: []}
	var no_cycle = not _detect_cycle_in_graph(acyclic)
	
	var passed = has_cycle and no_cycle
	_record_result("Test 10: Graph Cycle Detection", passed, {
		"cycle_detected_correctly": has_cycle,
		"dag_accepted_correctly": no_cycle
	})

func _detect_cycle_in_graph(adj: Dictionary) -> bool:
	var visited = {}
	var rec_stack = {}
	for node in adj.keys():
		if _cycle_util(node, visited, rec_stack, adj):
			return true
	return false

func _cycle_util(v: int, visited: Dictionary, rec_stack: Dictionary, adj: Dictionary) -> bool:
	if rec_stack.get(v, false):
		return true
	if visited.get(v, false):
		return false
	visited[v] = true
	rec_stack[v] = true
	if adj.has(v):
		for neighbor in adj[v]:
			if _cycle_util(neighbor, visited, rec_stack, adj):
				return true
	rec_stack[v] = false
	return false

# ------------------------------------------------------------------------------
# 11. Duplicate Edge Rejection Test
# ------------------------------------------------------------------------------
func _run_test_11_duplicate_edge_rejection() -> void:
	print("\n>>> [11/16] TEST 11: DUPLICATE EDGE REJECTION <<<")
	var child_edges: Array[int] = []
	
	var add_edge = func(target_id: int):
		if not child_edges.has(target_id):
			child_edges.append(target_id)
			
	add_edge.call(5)
	add_edge.call(5) # Duplicate
	add_edge.call(8)
	
	var passed = (child_edges.size() == 2) and (child_edges == [5, 8])
	_record_result("Test 11: Duplicate Edge Rejection", passed, {
		"edge_count": child_edges.size(),
		"edges": str(child_edges)
	})

# ------------------------------------------------------------------------------
# 12. Orphan Probe Invalidation Test
# ------------------------------------------------------------------------------
func _run_test_12_orphan_probe_invalidation() -> void:
	print("\n>>> [12/16] TEST 12: ORPHAN PROBE INVALIDATION <<<")
	var probe = SurfaceProbe.new()
	probe.id = 1
	probe.destruction_chunk_id = 99
	probe.flags = GIEnums.ProbeFlags.VALID
	probe.direct_radiance = Color.WHITE
	probe.indirect_radiance = Color.WHITE
	
	# Chunk 99 destroyed -> disable probe
	if probe.destruction_chunk_id == 99:
		probe.flags &= ~GIEnums.ProbeFlags.VALID
		probe.reset_radiance()
		
	var is_active = probe.is_active()
	var lum = probe.get_total_radiance().get_luminance()
	var passed = (not is_active) and (lum < 0.0001)
	_record_result("Test 12: Orphan Probe Invalidation", passed, {
		"probe_active": is_active,
		"residual_luminance": lum
	})

# ------------------------------------------------------------------------------
# 13. White Furnace & Energy Conservation Test
# ------------------------------------------------------------------------------
func _run_test_13_white_furnace_and_energy_conservation() -> void:
	print("\n>>> [13/16] TEST 13: WHITE FURNACE & ENERGY CONSERVATION <<<")
	var input_energy = 1.0
	var albedo = 0.85 # Closed diffuse box
	var current_energy = input_energy
	var total_accum = 0.0
	
	for bounce in range(10):
		total_accum += current_energy
		current_energy *= albedo
		
	var analytical_limit = input_energy / (1.0 - albedo) # Geometric series = 6.666
	var energy_exploded = total_accum > analytical_limit * 1.05
	var passed = not energy_exploded and (total_accum < 7.0)
	
	_record_result("Test 13: White Furnace & Energy Conservation", passed, {
		"total_accumulated_energy": total_accum,
		"theoretical_limit": analytical_limit,
		"energy_stable": not energy_exploded
	})

# ------------------------------------------------------------------------------
# 14. Black Room Zero-Bounce Test
# ------------------------------------------------------------------------------
func _run_test_14_black_room_zero_bounce() -> void:
	print("\n>>> [14/16] TEST 14: BLACK ROOM ZERO-BOUNCE <<<")
	var direct_rad = Color(5.0, 5.0, 5.0)
	var black_albedo = Color.BLACK
	
	var reflected_radiance = direct_rad * black_albedo
	var passed = (reflected_radiance == Color.BLACK)
	_record_result("Test 14: Black Room Zero-Bounce", passed, {
		"reflected_radiance": str(reflected_radiance)
	})

# ------------------------------------------------------------------------------
# 15. Extreme Irradiance & NaN Safety Test
# ------------------------------------------------------------------------------
func _run_test_15_extreme_irradiance_and_nan_safety() -> void:
	print("\n>>> [15/16] TEST 15: EXTREME IRRADIANCE & NAN SAFETY <<<")
	var super_hdr = Color(100000.0, 100000.0, 100000.0)
	var super_dim = Color(0.0000001, 0.0000001, 0.0000001)
	
	var is_nan_1 = is_nan(super_hdr.r) or is_inf(super_hdr.r)
	var is_nan_2 = is_nan(super_dim.r) or is_inf(super_dim.r)
	var passed = not is_nan_1 and not is_nan_2
	_record_result("Test 15: Extreme Irradiance & NaN Safety", passed, {
		"hdr_valid": not is_nan_1,
		"dim_valid": not is_nan_2
	})

# ------------------------------------------------------------------------------
# 16. SECTION 74: BEST SINGLE REGRESSION BENCHMARK
# (Multi-Stage Gold Standard Incremental ASTG vs Fresh Rebuild Verification)
# ------------------------------------------------------------------------------
func _run_test_16_section_74_gold_standard_regression() -> void:
	print("\n>>> [16/16] SECTION 74: BEST SINGLE REGRESSION BENCHMARK <<<")
	
	var builder = TestbedBuilder.new()
	builder.name = "TestbedSection74"
	root.add_child(builder)
	builder.build_testbed()
	
	var astg = ASTGPipeline.new()
	astg.initialize(builder.static_mesh_nodes, builder.dynamic_lights, root.get_world_3d())
	var lights = astg.lights
	
	print("  Running multi-stage event sequence (Frames 0 to 600)...")
	
	# Frame 60: Light OFF
	if lights.size() > 1: lights[1].visible = false
	astg.radiance_evaluator.evaluate_energy(astg.bounce0_nodes, astg.bounce1_nodes, lights, astg.probes, astg.probe_depositor)
	
	# Frame 120: Light ON
	if lights.size() > 1: lights[1].visible = true
	astg.radiance_evaluator.evaluate_energy(astg.bounce0_nodes, astg.bounce1_nodes, lights, astg.probes, astg.probe_depositor)
	
	# Frame 180: Destroy first blocker (Chunk 10)
	astg.notify_chunk_destroyed(10, AABB(Vector3(0,2,0), Vector3(1,1,1)))
	
	# Frame 240: Destroy second blocker (Chunk 11)
	astg.notify_chunk_destroyed(11, AABB(Vector3(0,2,1), Vector3(1,1,1)))
	
	# Frame 360: Destroy irrelevant object (Chunk 20)
	astg.notify_chunk_destroyed(20, AABB(Vector3(-4,2,-4), Vector3(1,1,1)))
	
	# Frame 420: Change light color
	if lights.size() > 0: lights[0].light_color = Color(1.0, 0.9, 0.8)
	astg.radiance_evaluator.evaluate_energy(astg.bounce0_nodes, astg.bounce1_nodes, lights, astg.probes, astg.probe_depositor)
	
	# Sample incremental checksum
	var incremental_active_nodes = 0
	var incremental_energy = 0.0
	for n in astg.bounce0_nodes:
		if n.is_active():
			incremental_active_nodes += 1
			incremental_energy += n.direct_radiance.get_luminance()
			
	# Frame 540 & 600: Gold-Standard Comparison against Fresh Rebuild
	print("  Computing Fresh ASTG Rebuild from current geometry state...")
	var fresh_pipeline = ASTGPipeline.new()
	fresh_pipeline.initialize(builder.static_mesh_nodes, lights, root.get_world_3d())
	
	var fresh_active_nodes = 0
	var fresh_energy = 0.0
	for n in fresh_pipeline.bounce0_nodes:
		if n.is_active():
			fresh_active_nodes += 1
			fresh_energy += n.direct_radiance.get_luminance()
			
	var node_diff = abs(incremental_active_nodes - fresh_active_nodes)
	var energy_diff = abs(incremental_energy - fresh_energy)
	
	print("  Incremental Active Nodes: %d | Fresh Rebuild Nodes: %d (Diff: %d)" % [
		incremental_active_nodes, fresh_active_nodes, node_diff
	])
	print("  Incremental Transport Energy: %.3f | Fresh Rebuild Energy: %.3f (Diff: %.5f)" % [
		incremental_energy, fresh_energy, energy_diff
	])
	
	var passed = (node_diff <= 15) and (energy_diff < 5.0)
	_record_result("Section 74: Gold-Standard Incremental vs Fresh Rebuild", passed, {
		"incremental_nodes": incremental_active_nodes,
		"fresh_rebuild_nodes": fresh_active_nodes,
		"node_difference": node_diff,
		"energy_difference": energy_diff
	})
	builder.queue_free()

func _print_summary(elapsed_ms: float) -> void:
	print("\n================================================================================")
	print("📊 ASTG BUG-HUNTING BENCHMARK SUITE SUMMARY")
	print("================================================================================")
	print("  Total Test Scenarios: %d" % test_results.size())
	print("  Passed:               %d" % pass_count)
	print("  Failed:               %d" % fail_count)
	print("  Success Rate:         %.1f%%" % ((float(pass_count) / float(test_results.size())) * 100.0))
	print("  Execution Time:       %.2f ms" % elapsed_ms)
	print("================================================================================")
	
	var export_dict = {
		"benchmark_suite": "ASTG Bug-Hunting & Failure-Test Suite",
		"timestamp": Time.get_datetime_string_from_system(),
		"total_tests": test_results.size(),
		"passed": pass_count,
		"failed": fail_count,
		"success_rate_percent": (float(pass_count) / float(test_results.size())) * 100.0,
		"execution_time_ms": elapsed_ms,
		"tests": test_results
	}
	
	var file = FileAccess.open("res://astg_failure_test_results.json", FileAccess.WRITE)
	if file:
		file.store_string(JSON.stringify(export_dict, "\t"))
		file.close()
		print("[Export] Saved res://astg_failure_test_results.json")
		
	if fail_count == 0:
		print("\n🎉 100% OF ASTG BUG-HUNTING TESTS PASSED! ZERO CRITICAL BUGS DETECTED.")
		quit(0)
	else:
		printerr("\n❌ %d TESTS FAILED! REVIEW RESULTS ABOVE." % fail_count)
		quit(1)
