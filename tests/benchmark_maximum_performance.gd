extends SceneTree

const HardwareRTBackendScript = preload("res://scripts/core/runtime/hardware_rt_backend.gd")
const SoftwareBVHBackendScript = preload("res://scripts/core/runtime/software_bvh_backend.gd")
const DDGIBaselineScript = preload("res://scripts/baselines/ddgi_baseline.gd")

func _init() -> void:
	print("================================================================================")
	print("🚀 RUNNING ASTG MAXIMUM-PERFORMANCE HARDWARE RT BENCHMARK SUITE")
	print("================================================================================")

	var ClassroomScript = load("res://scripts/testbed/classroom_builder.gd")
	var ASTGPipelineScript = load("res://scripts/core/runtime/astg_pipeline.gd")

	var classroom = ClassroomScript.new()
	classroom.name = "ClassroomBenchmarkScene"
	root.add_child(classroom)
	classroom.build_classroom()

	var mesh_nodes: Array[Node3D] = classroom.static_mesh_nodes
	var light_nodes: Array[Light3D] = []
	for l in classroom.dynamic_lights:
		light_nodes.append(l)
	for l in classroom.ceiling_lights:
		light_nodes.append(l)

	if light_nodes.is_empty():
		var sun = DirectionalLight3D.new()
		sun.name = "SunLight"
		sun.light_color = Color(1.0, 0.95, 0.85)
		sun.light_energy = 2.0
		sun.rotation_degrees = Vector3(-45, 30, 0)
		classroom.add_child(sun)
		light_nodes.append(sun)

		var omni = OmniLight3D.new()
		omni.name = "CeilingOmni"
		omni.position = Vector3(0, 3.2, 0)
		omni.omni_range = 10.0
		omni.light_energy = 1.5
		classroom.add_child(omni)
		light_nodes.append(omni)

	var world = root.get_world_3d()

	# ==============================================================================
	# 1. BASELINE ISOLATIONS
	# ==============================================================================
	print("\n>>> 1. BASELINE ISOLATION MEASUREMENTS <<<")

	var hw_backend = HardwareRTBackendScript.new()
	hw_backend.initialize(mesh_nodes, world)

	# A. AS-Only Baseline (Section 49)
	var t_as_0 = Time.get_ticks_usec()
	hw_backend.destroy_chunk(1)
	hw_backend.restore_chunk(1)
	var t_as_1 = Time.get_ticks_usec()
	var as_maintenance_ms = (t_as_1 - t_as_0) / 1000.0 / 2.0
	print("  1. AS Maintenance Cost (Refit/Update):   %.3f ms" % as_maintenance_ms)

	# B. Traversal-Only Baseline (Section 48: 1,024 rays)
	var ray_count = 1024
	var test_rays: Array[Dictionary] = []
	var rng = RandomNumberGenerator.new()
	rng.seed = 999
	for i in range(ray_count):
		var origin = Vector3(rng.randf_range(-3.0, 3.0), rng.randf_range(1.0, 3.0), rng.randf_range(-3.0, 3.0))
		var dir = Vector3(rng.randf_range(-1.0, 1.0), rng.randf_range(-1.0, 1.0), rng.randf_range(-1.0, 1.0)).normalized()
		test_rays.append({"origin": origin, "dir": dir, "t_min": 0.01, "t_max": 25.0})

	var t_rt_0 = Time.get_ticks_usec()
	var raw_hits = hw_backend.trace_batch(test_rays)
	var t_rt_1 = Time.get_ticks_usec()
	var pure_rt_traversal_ms = (t_rt_1 - t_rt_0) / 1000.0
	var rt_mrays_sec = (float(ray_count) / (pure_rt_traversal_ms / 1000.0)) / 1000000.0
	print("  2. Pure RT Hardware Traversal (1,024):  %.3f ms (%.2f MRays/sec)" % [pure_rt_traversal_ms, rt_mrays_sec])

	# ==============================================================================
	# 2. FULL ASTG PIPELINE INITIALIZATION & PRECOMPUTATION
	# ==============================================================================
	print("\n>>> 2. ASTG HARDWARE RT PIPELINE INITIALIZATION <<<")

	var astg_hw = ASTGPipelineScript.new()
	var t_init_0 = Time.get_ticks_usec()
	astg_hw.initialize(mesh_nodes, light_nodes, world, true) # true = Hardware RT
	var t_init_1 = Time.get_ticks_usec()
	var init_hw_ms = (t_init_1 - t_init_0) / 1000.0

	var astg_sw = ASTGPipelineScript.new()
	var t_sw_init_0 = Time.get_ticks_usec()
	astg_sw.initialize(mesh_nodes, light_nodes, world, false) # false = Software BVH
	var t_sw_init_1 = Time.get_ticks_usec()
	var init_sw_ms = (t_sw_init_1 - t_sw_init_0) / 1000.0

	print("  Hardware RT ASTG Precompute Time: %.2f ms" % init_hw_ms)
	print("  Software BVH ASTG Precompute Time: %.2f ms" % init_sw_ms)
	print("  Active Surface Probes:             %d" % astg_hw.probes.size())
	print("  Direct Bounce-0 Nodes:             %d" % astg_hw.bounce0_nodes.size())
	print("  Diffuse Nodes (4 Bounces):         %d" % astg_hw.bounce1_nodes.size())

	# ==============================================================================
	# 3. SCENARIO A: STABLE SCENE FRAME PERFORMANCE (Section 71)
	# ==============================================================================
	print("\n>>> 3. SCENARIO A: STABLE SCENE FRAME (0 Topology Rays) <<<")

	var stable_stats_hw = astg_hw.process_frame(0.016)
	var stable_stats_sw = astg_sw.process_frame(0.016)

	print("  Hardware ASTG Stable Frame Time:  %.3f ms (Topology Rays: %d)" % [stable_stats_hw.time_ms, stable_stats_hw.rays_traced])
	print("  Software ASTG Stable Frame Time:  %.3f ms (Topology Rays: %d)" % [stable_stats_sw.time_ms, stable_stats_sw.rays_traced])

	# ==============================================================================
	# 4. SCENARIO B: LIGHT COLOR / INTENSITY CHANGE (Section 65, 69)
	# ==============================================================================
	print("\n>>> 4. SCENARIO B: ZERO-RAY LIGHT COLOR / INTENSITY CHANGE <<<")

	for l in light_nodes:
		l.light_color = Color(1.0, 0.4, 0.2)
		l.light_energy = 2.5

	var t_light_0 = Time.get_ticks_usec()
	var light_stats_hw = astg_hw.process_frame(0.016)
	var t_light_1 = Time.get_ticks_usec()
	var light_hw_ms = (t_light_1 - t_light_0) / 1000.0

	print("  Hardware ASTG Light Update Time:  %.3f ms (Topology Rays: %d)" % [light_hw_ms, light_stats_hw.rays_traced])
	print("  Nodes Re-evaluated:               %d" % (astg_hw.bounce0_nodes.size() + astg_hw.bounce1_nodes.size()))

	# ==============================================================================
	# 5. SCENARIO C: LOCALIZED DESTRUCTION REPAIR (Section 53)
	# ==============================================================================
	print("\n>>> 5. SCENARIO C: LOCALIZED DESTRUCTION EVENT & REPAIR <<<")

	var target_chunk = 1
	var t_dest_hw_0 = Time.get_ticks_usec()
	astg_hw.notify_chunk_destroyed(target_chunk)
	var dest_stats_hw = astg_hw.process_frame(0.016)
	var t_dest_hw_1 = Time.get_ticks_usec()
	var dest_hw_ms = (t_dest_hw_1 - t_dest_hw_0) / 1000.0

	var t_dest_sw_0 = Time.get_ticks_usec()
	astg_sw.notify_chunk_destroyed(target_chunk)
	var dest_stats_sw = astg_sw.process_frame(0.016)
	var t_dest_sw_1 = Time.get_ticks_usec()
	var dest_sw_ms = (t_dest_sw_1 - t_dest_sw_0) / 1000.0

	print("  Hardware ASTG Destruction Event:  %.3f ms (Repair Rays: %d, Jobs: %d)" % [
		dest_hw_ms, dest_stats_hw.rays_traced, dest_stats_hw.repair_jobs_done
	])
	print("  Software ASTG Destruction Event:  %.3f ms (Repair Rays: %d, Jobs: %d)" % [
		dest_sw_ms, dest_stats_sw.rays_traced, dest_stats_sw.repair_jobs_done
	])
	var dest_speedup = dest_sw_ms / max(0.001, dest_hw_ms)
	print("  ⚡ Destruction Repair Speedup:     %.2fx" % dest_speedup)

	# ==============================================================================
	# 6. SCENARIO D: HEAD-TO-HEAD COMPARISON: ASTG VS DDGI BASELINE
	# ==============================================================================
	print("\n>>> 6. SCENARIO D: HEAD-TO-HEAD COMPARISON (ASTG VS DDGI BASELINE) <<<")

	var ddgi = DDGIBaselineScript.new()
	ddgi.initialize(classroom.classroom_bounds, Vector3i(8, 4, 8), light_nodes, world)

	var t_ddgi_0 = Time.get_ticks_usec()
	var ddgi_frame_res = ddgi.process_frame(0.016)
	var t_ddgi_1 = Time.get_ticks_usec()
	var ddgi_ms = (t_ddgi_1 - t_ddgi_0) / 1000.0

	print("  DDGI Baseline (240 Rays/frame):   %.2f ms (~%.1f FPS | %d Rays/frame)" % [
		ddgi_ms, 1000.0 / max(0.001, ddgi_ms), ddgi_frame_res.rays_traced
	])
	print("  ASTG Hardware RT (Stable Frame):  %.3f ms (~%.1f FPS | %d Rays/frame)" % [
		stable_stats_hw.time_ms, 1000.0 / max(0.001, stable_stats_hw.time_ms), stable_stats_hw.rays_traced
	])
	print("  ASTG Hardware RT (Repair Frame):  %.3f ms (~%.1f FPS | %d Rays/frame)" % [
		dest_hw_ms, 1000.0 / max(0.001, dest_hw_ms), dest_stats_hw.rays_traced
	])

	# ==============================================================================
	# 7. FULL COST STACK REPORTING & JSON EXPORT
	# ==============================================================================
	var export_data = {
		"scene": "ASTG Photorealistic Classroom",
		"backend": "HardwareRT (NVIDIA DXR 1.1 RT Cores)",
		"scene_triangles": hw_backend.total_triangles,
		"cost_stack_breakdown": {
			"as_maintenance_ms": as_maintenance_ms,
			"pure_rt_traversal_ms": pure_rt_traversal_ms,
			"rt_mrays_sec": rt_mrays_sec,
			"stable_frame_total_ms": stable_stats_hw.time_ms,
			"light_update_ms": light_hw_ms,
			"destruction_event_total_ms": dest_hw_ms,
			"ddgi_baseline_frame_ms": ddgi_ms
		},
		"graph_metrics": {
			"active_probes": astg_hw.probes.size(),
			"bounce0_nodes": astg_hw.bounce0_nodes.size(),
			"diffuse_nodes_4_bounces": astg_hw.bounce1_nodes.size(),
			"destruction_repair_rays": dest_stats_hw.rays_traced
		},
		"comparison": {
			"software_destruction_ms": dest_sw_ms,
			"hardware_destruction_ms": dest_hw_ms,
			"destruction_speedup_factor": dest_speedup,
			"ddgi_speedup_factor": ddgi_ms / max(0.001, dest_hw_ms)
		}
	}

	var json_file = FileAccess.open("res://hardware_rt_maximum_performance_results.json", FileAccess.WRITE)
	if json_file:
		json_file.store_string(JSON.stringify(export_data, "\t"))
		json_file.close()
		print("\n[Export] Successfully exported res://hardware_rt_maximum_performance_results.json!")

	print("\n================================================================================")
	print("🎯 ASTG MAXIMUM-PERFORMANCE HARDWARE RT BENCHMARK COMPLETE!")
	print("================================================================================")

	classroom.queue_free()
	quit(0)
