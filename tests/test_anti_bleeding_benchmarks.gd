extends SceneTree

# ==============================================================================
# ASTG ANTI-BLEEDING AND SURFACE-AWARE PROBE LOOKUP BENCHMARK SUITE
# Systematically evaluates:
#   Test A: Floor / Wall Corner (90-deg discontinuity rejection)
#   Test B: Thin Wall (Opposing normal & distinct cluster isolation)
#   Test C: Parallel Shelves (Identical normals separated by vertical gap)
#   Test D: Disconnected Coplanar Meshes (Unrelated cluster rejection)
#   Test E: Smooth Curved Surface (Continuous adjacent cluster interpolation)
# ==============================================================================

func _init() -> void:
	print("================================================================================")
	print("🧪 RUNNING ASTG ANTI-BLEEDING & SURFACE-AWARE PROBE LOOKUP BENCHMARKS")
	print("================================================================================")
	
	SurfaceAwareProbeLookup.reset_metrics()
	
	var test_a_passed = _test_a_floor_wall_corner()
	var test_b_passed = _test_b_thin_wall()
	var test_c_passed = _test_c_parallel_shelves()
	var test_d_passed = _test_d_disconnected_coplanar_meshes()
	var test_e_passed = _test_e_smooth_curved_surface()
	
	print("\n================================================================================")
	print("📊 ANTI-BLEEDING TELEMETRY & DIAGNOSTIC METRICS")
	print("================================================================================")
	print("  Exact Cluster Hits:           %d" % SurfaceAwareProbeLookup.exact_cluster_hits)
	print("  Adjacent Cluster Hits:        %d" % SurfaceAwareProbeLookup.adjacent_cluster_hits)
	print("  World-Space Fallback Hits:    %d" % SurfaceAwareProbeLookup.world_space_fallback_hits)
	print("  Normal Rejections:            %d" % SurfaceAwareProbeLookup.normal_rejections)
	print("  Topology Rejections:          %d" % SurfaceAwareProbeLookup.topology_rejections)
	
	var all_passed = test_a_passed and test_b_passed and test_c_passed and test_d_passed and test_e_passed
	if all_passed:
		print("\n🎉 ALL 5 ANTI-BLEEDING BENCHMARKS PASSED 100%!")
		quit(0)
	else:
		printerr("\n❌ SOME ANTI-BLEEDING BENCHMARKS FAILED!")
		quit(1)

# ------------------------------------------------------------------------------
# TEST A: Floor / Wall Corner (90-deg normal boundary)
# ------------------------------------------------------------------------------
func _test_a_floor_wall_corner() -> bool:
	print("\n>>> [1/5] TEST A: FLOOR / WALL CORNER ISOLATION <<<")
	
	var c_wall = SurfaceCluster.new()
	c_wall.id = 0
	c_wall.average_normal = Vector3.RIGHT # (1, 0, 0)
	c_wall.bounds = AABB(Vector3(0, 0, 0), Vector3(0.1, 3.0, 3.0))
	
	var c_floor = SurfaceCluster.new()
	c_floor.id = 1
	c_floor.average_normal = Vector3.UP # (0, 1, 0)
	c_floor.bounds = AABB(Vector3(0, 0, 0), Vector3(3.0, 0.1, 3.0))
	
	# Mark as sharp edge (90 deg)
	c_wall.add_adjacent_cluster(1, GIEnums.ClusterAdjacencyType.SHARP_EDGE)
	c_floor.add_adjacent_cluster(0, GIEnums.ClusterAdjacencyType.SHARP_EDGE)
	
	var clusters: Array[SurfaceCluster] = [c_wall, c_floor]
	var probes: Array[SurfaceProbe] = []
	
	# Wall probe (intensely illuminated)
	var p_wall = SurfaceProbe.new()
	p_wall.id = 0
	p_wall.position = Vector3(0.05, 0.5, 1.5)
	p_wall.normal = Vector3.RIGHT
	p_wall.surface_cluster_id = 0
	p_wall.indirect_radiance = Color(5.0, 5.0, 5.0)
	probes.append(p_wall)
	c_wall.probe_ids.append(0)
	
	# Floor probe (in shadow)
	var p_floor = SurfaceProbe.new()
	p_floor.id = 1
	p_floor.position = Vector3(0.5, 0.05, 1.5)
	p_floor.normal = Vector3.UP
	p_floor.surface_cluster_id = 1
	p_floor.indirect_radiance = Color.BLACK
	probes.append(p_floor)
	c_floor.probe_ids.append(1)
	
	# Sample floor surface right next to corner
	var floor_sample = SurfaceAwareProbeLookup.sample_surface_irradiance(
		Vector3(0.1, 0.0, 1.5), Vector3.UP, 1, clusters, probes
	)
	
	var leaked = floor_sample.get_luminance()
	print("  Floor Sample Irradiance near Wall: %.5f (Expected: 0.00000)" % leaked)
	if leaked < 0.0001:
		print("  ✓ PASSED: Floor/Wall corner zero-leakage verified.")
		return true
	else:
		printerr("  ✗ FAILED: Wall light bled onto floor!")
		return false

# ------------------------------------------------------------------------------
# TEST B: Thin Wall (Opposing Normals & Disconnected Cluster)
# ------------------------------------------------------------------------------
func _test_b_thin_wall() -> bool:
	print("\n>>> [2/5] TEST B: THIN WALL ISOLATION (10cm WALL) <<<")
	
	var c_bright_room = SurfaceCluster.new()
	c_bright_room.id = 0
	c_bright_room.average_normal = Vector3.LEFT # Facing -X into bright room
	c_bright_room.bounds = AABB(Vector3(-2.0, 0, 0), Vector3(1.95, 3.0, 3.0))
	
	var c_dark_room = SurfaceCluster.new()
	c_dark_room.id = 1
	c_dark_room.average_normal = Vector3.RIGHT # Facing +X into dark room
	c_dark_room.bounds = AABB(Vector3(0.05, 0, 0), Vector3(2.0, 3.0, 3.0))
	
	var clusters: Array[SurfaceCluster] = [c_bright_room, c_dark_room]
	var probes: Array[SurfaceProbe] = []
	
	var p_bright = SurfaceProbe.new()
	p_bright.id = 0
	p_bright.position = Vector3(-0.05, 1.5, 1.5)
	p_bright.normal = Vector3.LEFT
	p_bright.surface_cluster_id = 0
	p_bright.indirect_radiance = Color(8.0, 8.0, 8.0)
	probes.append(p_bright)
	c_bright_room.probe_ids.append(0)
	
	var p_dark = SurfaceProbe.new()
	p_dark.id = 1
	p_dark.position = Vector3(0.05, 1.5, 1.5) # Only 10cm away!
	p_dark.normal = Vector3.RIGHT
	p_dark.surface_cluster_id = 1
	p_dark.indirect_radiance = Color.BLACK
	probes.append(p_dark)
	c_dark_room.probe_ids.append(1)
	
	var dark_sample = SurfaceAwareProbeLookup.sample_surface_irradiance(
		Vector3(0.05, 1.5, 1.5), Vector3.RIGHT, 1, clusters, probes
	)
	
	var leaked = dark_sample.get_luminance()
	print("  Dark Room Wall Irradiance: %.5f (Distance to bright probe = 0.10m)" % leaked)
	if leaked < 0.0001:
		print("  ✓ PASSED: Thin wall 100% light leak prevention verified.")
		return true
	else:
		printerr("  ✗ FAILED: Light penetrated thin wall!")
		return false

# ------------------------------------------------------------------------------
# TEST C: Parallel Shelves (Identical Normals, Vertical Gap)
# ------------------------------------------------------------------------------
func _test_c_parallel_shelves() -> bool:
	print("\n>>> [3/5] TEST C: PARALLEL SHELVES (5cm GAP, IDENTICAL UPWARD NORMALS) <<<")
	
	var c_upper = SurfaceCluster.new()
	c_upper.id = 0
	c_upper.average_normal = Vector3.UP
	c_upper.bounds = AABB(Vector3(-1.0, 1.05, -1.0), Vector3(2.0, 0.05, 2.0))
	
	var c_lower = SurfaceCluster.new()
	c_lower.id = 1
	c_lower.average_normal = Vector3.UP
	c_lower.bounds = AABB(Vector3(-1.0, 1.00, -1.0), Vector3(2.0, 0.05, 2.0))
	
	var clusters: Array[SurfaceCluster] = [c_upper, c_lower]
	var probes: Array[SurfaceProbe] = []
	
	var p_upper = SurfaceProbe.new()
	p_upper.id = 0
	p_upper.position = Vector3(0.0, 1.05, 0.0)
	p_upper.normal = Vector3.UP
	p_upper.surface_cluster_id = 0
	p_upper.indirect_radiance = Color(6.0, 6.0, 6.0)
	probes.append(p_upper)
	c_upper.probe_ids.append(0)
	
	var p_lower = SurfaceProbe.new()
	p_lower.id = 1
	p_lower.position = Vector3(0.0, 1.00, 0.0) # Only 5cm below with same normal!
	p_lower.normal = Vector3.UP
	p_lower.surface_cluster_id = 1
	p_lower.indirect_radiance = Color.BLACK
	probes.append(p_lower)
	c_lower.probe_ids.append(1)
	
	var lower_sample = SurfaceAwareProbeLookup.sample_surface_irradiance(
		Vector3(0.0, 1.00, 0.0), Vector3.UP, 1, clusters, probes
	)
	
	var leaked = lower_sample.get_luminance()
	print("  Lower Shelf Irradiance: %.5f (Gap = 0.05m)" % leaked)
	if leaked < 0.0001:
		print("  ✓ PASSED: Parallel shelf topology isolation verified (No vertical bleed).")
		return true
	else:
		printerr("  ✗ FAILED: Upper shelf bled onto lower shelf!")
		return false

# ------------------------------------------------------------------------------
# TEST D: Disconnected Coplanar Meshes
# ------------------------------------------------------------------------------
func _test_d_disconnected_coplanar_meshes() -> bool:
	print("\n>>> [4/5] TEST D: DISCONNECTED COPLANAR MESHES <<<")
	
	var c_mesh_a = SurfaceCluster.new()
	c_mesh_a.id = 0
	c_mesh_a.average_normal = Vector3.UP
	c_mesh_a.bounds = AABB(Vector3(-3.0, 0.0, -1.0), Vector3(2.0, 0.1, 2.0))
	
	var c_mesh_b = SurfaceCluster.new()
	c_mesh_b.id = 1
	c_mesh_b.average_normal = Vector3.UP
	c_mesh_b.bounds = AABB(Vector3(1.0, 0.0, -1.0), Vector3(2.0, 0.1, 2.0))
	
	var clusters: Array[SurfaceCluster] = [c_mesh_a, c_mesh_b]
	var probes: Array[SurfaceProbe] = []
	
	var p_a = SurfaceProbe.new()
	p_a.id = 0
	p_a.position = Vector3(-2.0, 0.05, 0.0)
	p_a.normal = Vector3.UP
	p_a.surface_cluster_id = 0
	p_a.indirect_radiance = Color(4.0, 0.0, 0.0)
	probes.append(p_a)
	c_mesh_a.probe_ids.append(0)
	
	var p_b = SurfaceProbe.new()
	p_b.id = 1
	p_b.position = Vector3(2.0, 0.05, 0.0)
	p_b.normal = Vector3.UP
	p_b.surface_cluster_id = 1
	p_b.indirect_radiance = Color.BLACK
	probes.append(p_b)
	c_mesh_b.probe_ids.append(1)
	
	var b_sample = SurfaceAwareProbeLookup.sample_surface_irradiance(
		Vector3(2.0, 0.05, 0.0), Vector3.UP, 1, clusters, probes
	)
	
	var leaked = b_sample.get_luminance()
	print("  Mesh B Irradiance: %.5f" % leaked)
	if leaked < 0.0001:
		print("  ✓ PASSED: Disconnected coplanar meshes remain completely isolated.")
		return true
	else:
		printerr("  ✗ FAILED: Cross-mesh contamination detected!")
		return false

# ------------------------------------------------------------------------------
# TEST E: Smooth Curved Surface (Continuous Cluster Interpolation)
# ------------------------------------------------------------------------------
func _test_e_smooth_curved_surface() -> bool:
	print("\n>>> [5/5] TEST E: SMOOTH CURVED SURFACE CONTINUOUS BLENDING <<<")
	
	var c_curve_1 = SurfaceCluster.new()
	c_curve_1.id = 0
	c_curve_1.average_normal = Vector3(0.0, 1.0, 0.0).normalized()
	c_curve_1.bounds = AABB(Vector3(-1.0, 0.0, 0.0), Vector3(1.0, 1.0, 1.0))
	
	var c_curve_2 = SurfaceCluster.new()
	c_curve_2.id = 1
	c_curve_2.average_normal = Vector3(0.25, 0.968, 0.0).normalized() # ~15 deg curved angle
	c_curve_2.bounds = AABB(Vector3(0.0, 0.0, 0.0), Vector3(1.0, 1.0, 1.0))
	
	# Mark as SMOOTH_CONTINUATION
	c_curve_1.add_adjacent_cluster(1, GIEnums.ClusterAdjacencyType.SMOOTH_CONTINUATION)
	c_curve_2.add_adjacent_cluster(0, GIEnums.ClusterAdjacencyType.SMOOTH_CONTINUATION)
	
	var clusters: Array[SurfaceCluster] = [c_curve_1, c_curve_2]
	var probes: Array[SurfaceProbe] = []
	
	var p1 = SurfaceProbe.new()
	p1.id = 0
	p1.position = Vector3(-0.5, 0.5, 0.5)
	p1.normal = Vector3(0.0, 1.0, 0.0)
	p1.surface_cluster_id = 0
	p1.indirect_radiance = Color(2.0, 2.0, 2.0)
	probes.append(p1)
	c_curve_1.probe_ids.append(0)
	
	# Sample on cluster 2 near the shared boundary
	var blend_sample = SurfaceAwareProbeLookup.sample_surface_irradiance(
		Vector3(0.1, 0.5, 0.5), Vector3(0.1, 0.99, 0.0).normalized(), 1, clusters, probes
	)
	
	var received = blend_sample.get_luminance()
	print("  Smooth Boundary Blended Irradiance: %.5f" % received)
	if received > 0.1:
		print("  ✓ PASSED: Smooth curved surface continuous interpolation confirmed.")
		return true
	else:
		printerr("  ✗ FAILED: Smooth surface was overly isolated!")
		return false
