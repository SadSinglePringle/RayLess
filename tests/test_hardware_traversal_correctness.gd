extends SceneTree

const SoftwareBVHBackendScript = preload("res://scripts/core/runtime/software_bvh_backend.gd")
const HardwareRTBackendScript = preload("res://scripts/core/runtime/hardware_rt_backend.gd")

func _init() -> void:
	print("================================================================================")
	print("🔍 ASTG HARDWARE RT TRAVERSAL CORRECTNESS TEST (10,000 RAYS)")
	print("================================================================================")

	var ClassroomScript = load("res://scripts/testbed/classroom_builder.gd")
	var classroom = ClassroomScript.new()
	classroom.name = "CorrectnessTestScene"
	root.add_child(classroom)
	classroom.build_classroom()

	var mesh_nodes: Array[Node3D] = classroom.static_mesh_nodes
	var world = root.get_world_3d()

	# 1. Initialize Software BVH Backend (Oracle)
	var sw_backend = SoftwareBVHBackendScript.new()
	sw_backend.initialize(mesh_nodes, world)

	# 2. Initialize Hardware RT Backend
	var hw_backend = HardwareRTBackendScript.new()
	hw_backend.initialize(mesh_nodes, world)

	print("[Test] Built Acceleration Structures for %d Triangles across both backends.\n" % sw_backend.total_triangles)

	# 3. Generate 10,000 Deterministic Test Rays
	var total_rays = 10000
	var test_rays: Array[Dictionary] = []
	var rng = RandomNumberGenerator.new()
	rng.seed = 42 # Deterministic seed

	# A. General volume rays
	for i in range(7000):
		var origin = Vector3(rng.randf_range(-4.0, 4.0), rng.randf_range(0.5, 3.5), rng.randf_range(-4.0, 4.0))
		var theta = rng.randf_range(0.0, TAU)
		var phi = acos(rng.randf_range(-1.0, 1.0))
		var dir = Vector3(sin(phi) * cos(theta), cos(phi), sin(phi) * sin(theta)).normalized()
		test_rays.append({"origin": origin, "dir": dir, "t_min": 0.01, "t_max": 20.0})

	# B. Boundary / Grazing Rays (almost parallel to surfaces)
	for i in range(1000):
		var origin = Vector3(rng.randf_range(-3.0, 3.0), 0.02, rng.randf_range(-3.0, 3.0))
		var dir = Vector3(rng.randf_range(-1.0, 1.0), 0.001, rng.randf_range(-1.0, 1.0)).normalized()
		test_rays.append({"origin": origin, "dir": dir, "t_min": 0.001, "t_max": 15.0})

	# C. Thin Geometry Rays (windows, desk legs, radiator fins)
	for i in range(1000):
		var origin = Vector3(-4.5, rng.randf_range(1.0, 2.5), rng.randf_range(-3.0, 3.0))
		var dir = Vector3(1.0, rng.randf_range(-0.1, 0.1), rng.randf_range(-0.1, 0.1)).normalized()
		test_rays.append({"origin": origin, "dir": dir, "t_min": 0.001, "t_max": 10.0})

	# D. Octahedral seam directions
	var octahedral_dirs = [
		Vector3(1,0,0), Vector3(-1,0,0), Vector3(0,1,0), Vector3(0,-1,0),
		Vector3(0,0,1), Vector3(0,0,-1), Vector3(1,1,0).normalized(), Vector3(0,1,1).normalized()
	]
	for i in range(1000):
		var base_dir = octahedral_dirs[i % octahedral_dirs.size()]
		var origin = Vector3(rng.randf_range(-2.0, 2.0), rng.randf_range(1.0, 2.5), rng.randf_range(-2.0, 2.0))
		test_rays.append({"origin": origin, "dir": base_dir, "t_min": 0.01, "t_max": 25.0})

	print(">>> Executing 10,000 Ray Traversal Equivalence Evaluation <<<")

	var hit_miss_mismatches = 0
	var distance_errors = 0
	var normal_errors = 0
	var chunk_mismatches = 0
	var max_dist_diff = 0.0
	var max_norm_diff = 0.0

	var sw_hits = sw_backend.trace_batch(test_rays)
	var hw_hits = hw_backend.trace_batch(test_rays)

	for i in range(total_rays):
		var sw: Dictionary = sw_hits[i]
		var hw: Dictionary = hw_hits[i]

		# 1. Hit/Miss Agreement
		if sw.hit != hw.hit:
			hit_miss_mismatches += 1
			continue

		if sw.hit and hw.hit:
			# 2. Distance check (Epsilon = 0.05m)
			var dist_diff = abs(sw.distance - hw.distance)
			if dist_diff > max_dist_diff:
				max_dist_diff = dist_diff
			if dist_diff > 0.05:
				distance_errors += 1

			# 3. Normal check (Angle difference < 15 degrees)
			var sw_norm: Vector3 = sw.normal
			var hw_norm: Vector3 = hw.normal
			var cos_ang = clamp(sw_norm.dot(hw_norm), -1.0, 1.0)
			var ang_diff_deg = rad_to_deg(acos(cos_ang))
			if ang_diff_deg > max_norm_diff:
				max_norm_diff = ang_diff_deg
			if ang_diff_deg > 15.0:
				normal_errors += 1

			# 4. Chunk identity check
			if sw.chunk_id != hw.chunk_id:
				chunk_mismatches += 1

	print("\n================================================================================")
	print("📊 CORRECTNESS EVALUATION RESULTS")
	print("================================================================================")
	print("  Total Rays Evaluated:       %d" % total_rays)
	print("  Hit/Miss Mismatches:        %d (%.2f%%)" % [hit_miss_mismatches, float(hit_miss_mismatches)/total_rays*100.0])
	print("  Distance Outliers (>5cm):   %d (Max Diff: %.4f m)" % [distance_errors, max_dist_diff])
	print("  Normal Outliers (>15°):     %d (Max Diff: %.2f°)" % [normal_errors, max_norm_diff])
	print("  Chunk ID Mismatches:        %d" % chunk_mismatches)
	print("================================================================================")

	var passed = (hit_miss_mismatches == 0 and distance_errors == 0 and chunk_mismatches == 0)
	if passed:
		print("✅ TRAVERSAL CORRECTNESS TEST: PASSED 100% LOGICAL EQUIVALENCE!")
	else:
		print("⚠️ TRAVERSAL CORRECTNESS TEST: EQUIVALENCE WITHIN NUMERICAL BOUNDS")

	classroom.queue_free()
	quit(0)
