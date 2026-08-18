extends SceneTree

const HardwareRTBackendScript = preload("res://scripts/core/runtime/hardware_rt_backend.gd")

func _init() -> void:
	print("================================================================================")
	print("💥 ASTG SAME-FRAME DESTRUCTION & BLOCKER REPLACEMENT TEST")
	print("================================================================================")

	var ClassroomScript = load("res://scripts/testbed/classroom_builder.gd")
	var classroom = ClassroomScript.new()
	classroom.name = "DestructionTestScene"
	root.add_child(classroom)
	classroom.build_classroom()

	var world = root.get_world_3d()
	var mesh_nodes: Array[Node3D] = classroom.static_mesh_nodes

	# Initialize Hardware RT Backend
	var hw_backend = HardwareRTBackendScript.new()
	hw_backend.initialize(mesh_nodes, world)

	# ==============================================================================
	# TEST 1: SAME-FRAME 1,000 RAY WALL DESTRUCTION TEST
	# ==============================================================================
	print("\n>>> TEST 1: SAME-FRAME 1,000 RAY WALL DESTRUCTION TEST <<<")

	var target_chunk = 1 # Front wall chunk
	var ray_count = 1000
	var rays_to_wall: Array[Dictionary] = []

	var rng = RandomNumberGenerator.new()
	rng.seed = 1337

	for i in range(ray_count):
		var origin = Vector3(rng.randf_range(-2.0, 2.0), rng.randf_range(1.0, 2.5), 0.0)
		var dir = Vector3(0.0, 0.0, -1.0).normalized() # Aim directly towards front wall
		rays_to_wall.append({"origin": origin, "dir": dir, "t_min": 0.01, "t_max": 20.0})

	# Step 1: Trace before destruction
	var hits_before = hw_backend.trace_batch(rays_to_wall)
	var wall_hits_before = 0
	for h in hits_before:
		if h.hit:
			wall_hits_before += 1

	print("  Step 1 (Before Destruction): %d / %d rays intersected geometry." % [wall_hits_before, ray_count])

	# Step 2: Invalidate chunk in Hardware TLAS in same frame
	var t0 = Time.get_ticks_usec()
	hw_backend.destroy_chunk(target_chunk)
	var t1 = Time.get_ticks_usec()
	var as_update_ms = (t1 - t0) / 1000.0
	print("  Step 2 (TLAS Invalidation): Chunk %d masked in %.3f ms." % [target_chunk, as_update_ms])

	# Step 3: Retrace same 1,000 rays immediately in the same frame
	var hits_after = hw_backend.trace_batch(rays_to_wall)
	var wall_hits_after = 0
	for h in hits_after:
		if h.hit and h.chunk_id == target_chunk:
			wall_hits_after += 1

	print("  Step 3 (Immediate Retrace): %d hits recorded on destroyed Chunk %d (Expected: 0)." % [wall_hits_after, target_chunk])
	var test1_passed = (wall_hits_after == 0)
	if test1_passed:
		print("  ✅ TEST 1 PASSED: 0 stale hits on destroyed chunk in same frame!")
	else:
		print("  ❌ TEST 1 FAILED: Detected stale hits on destroyed chunk!")

	# ==============================================================================
	# TEST 2: RESTORATION TEST
	# ==============================================================================
	print("\n>>> TEST 2: REVERSIBLE GEOMETRY RESTORATION TEST <<<")
	hw_backend.restore_chunk(target_chunk)
	var hits_restored = hw_backend.trace_batch(rays_to_wall)
	var wall_hits_restored = 0
	for h in hits_restored:
		if h.hit:
			wall_hits_restored += 1

	print("  Restored State: %d / %d rays intersected restored geometry." % [wall_hits_restored, ray_count])
	var test2_passed = (wall_hits_restored == wall_hits_before)
	if test2_passed:
		print("  ✅ TEST 2 PASSED: Geometry TLAS fully restored!")
	else:
		print("  ❌ TEST 2 FAILED: Restored count mismatch (%d vs %d)" % [wall_hits_restored, wall_hits_before])

	# ==============================================================================
	# TEST 3: MULTIPLE BLOCKER REPLACEMENT TEST
	# ==============================================================================
	print("\n>>> TEST 3: MULTIPLE BLOCKER REPLACEMENT (A -> B -> RECEIVER) <<<")
	var chunk_a = 10
	var chunk_b = 20

	var test_ray = {"origin": Vector3(0, 1.5, 5.0), "dir": Vector3(0, 0, -1.0), "t_min": 0.01, "t_max": 30.0}
	var hit_initial = hw_backend.trace_closest(test_ray)
	print("  Initial state: Ray blocked by Chunk %d (Dist: %.2f m)" % [hit_initial.chunk_id, hit_initial.distance])

	hw_backend.destroy_chunk(chunk_a)
	var hit_after_a = hw_backend.trace_closest(test_ray)
	print("  After removing Blocker A: Ray advances to Chunk %d (Dist: %.2f m)" % [hit_after_a.chunk_id, hit_after_a.distance])

	hw_backend.destroy_chunk(chunk_b)
	var hit_after_b = hw_backend.trace_closest(test_ray)
	print("  After removing Blocker B: Path opens to background (Dist: %.2f m)" % hit_after_b.distance)

	hw_backend.restore_chunk(chunk_b)
	var hit_restore_b = hw_backend.trace_closest(test_ray)
	print("  Restoring Blocker B: Path re-blocked by Chunk %d (Dist: %.2f m)" % [hit_restore_b.chunk_id, hit_restore_b.distance])

	hw_backend.restore_chunk(chunk_a)
	var hit_restore_a = hw_backend.trace_closest(test_ray)
	print("  Restoring Blocker A: Path re-blocked by Chunk %d (Dist: %.2f m)" % [hit_restore_a.chunk_id, hit_restore_a.distance])

	var test3_passed = (hit_after_b.distance >= hit_after_a.distance and hit_after_a.distance >= hit_initial.distance)
	if test3_passed:
		print("  ✅ TEST 3 PASSED: Blocker replacement hierarchy verified!")
	else:
		print("  ❌ TEST 3 FAILED: Blocker sequence error")

	print("\n================================================================================")
	print("📊 DESTRUCTION & RESTORATION TEST SUMMARY")
	print("================================================================================")
	print("  Same-Frame Destruction: %s" % ("PASSED" if test1_passed else "FAILED"))
	print("  Reversible Restoration: %s" % ("PASSED" if test2_passed else "FAILED"))
	print("  Blocker Replacement:    %s" % ("PASSED" if test3_passed else "FAILED"))
	print("================================================================================")

	classroom.queue_free()
	quit(0 if (test1_passed and test2_passed and test3_passed) else 1)
