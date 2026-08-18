extends SceneTree

func _init() -> void:
	print("==================================================")
	print("⏱️ RUNNING PER-FRAME MICROSECOND PROFILER")
	print("==================================================")
	
	var main_scene: Main = preload("res://scenes/main.tscn").instantiate()
	root.add_child(main_scene)
	
	await process_frame
	await process_frame
	
	var astg = main_scene.astg_pipeline
	var ddgi = main_scene.ddgi_baseline
	var gt = main_scene.ground_truth
	var interp = main_scene.surface_interpolator
	var debug_rend = main_scene.debug_renderer
	var hud = main_scene.hud
	var cam = main_scene.camera
	var delta = 0.016
	
	print("\nProfiling individual components across 20 iterations...")
	
	var t_astg = 0.0
	var t_ddgi = 0.0
	var t_gt = 0.0
	var t_interp = 0.0
	var t_debug = 0.0
	var t_hud = 0.0
	var iters = 20
	
	for i in range(iters):
		var t0 = Time.get_ticks_usec()
		var astg_m = astg.process_frame(delta, cam)
		t_astg += (Time.get_ticks_usec() - t0) / 1000.0
		
		t0 = Time.get_ticks_usec()
		var ddgi_m = ddgi.process_frame(delta)
		t_ddgi += (Time.get_ticks_usec() - t0) / 1000.0
		
		t0 = Time.get_ticks_usec()
		var gt_m = gt.compute_metrics_vs_reference(astg.probes)
		t_gt += (Time.get_ticks_usec() - t0) / 1000.0
		
		t0 = Time.get_ticks_usec()
		interp.update_surfaces(main_scene.current_gi_mode, astg, ddgi, gt)
		t_interp += (Time.get_ticks_usec() - t0) / 1000.0
		
		t0 = Time.get_ticks_usec()
		debug_rend.update_debug_visuals(
			astg.probes, astg.bounce0_nodes, astg.bounce1_nodes, astg.lights, gt.reference_probe_radiance
		)
		t_debug += (Time.get_ticks_usec() - t0) / 1000.0
		
		t0 = Time.get_ticks_usec()
		hud.update_telemetry(astg_m, ddgi_m, gt_m, main_scene.current_gi_mode)
		t_hud += (Time.get_ticks_usec() - t0) / 1000.0
		
	var avg_astg = t_astg / float(iters)
	var avg_ddgi = t_ddgi / float(iters)
	var avg_gt = t_gt / float(iters)
	var avg_interp = t_interp / float(iters)
	var avg_debug = t_debug / float(iters)
	var avg_hud = t_hud / float(iters)
	var total_ms = avg_astg + avg_ddgi + avg_gt + avg_interp + avg_debug + avg_hud
	var fps = 1000.0 / total_ms
	
	print("\n==================================================")
	print("📊 PER-FRAME TIMING BREAKDOWN RESULTS")
	print("==================================================")
	print("1. ASTG Pipeline process_frame:     %8.3f ms (%5.1f%%)" % [avg_astg, (avg_astg/total_ms)*100.0])
	print("2. DDGI Baseline process_frame:     %8.3f ms (%5.1f%%)" % [avg_ddgi, (avg_ddgi/total_ms)*100.0])
	print("3. GroundTruth compute_metrics:     %8.3f ms (%5.1f%%)" % [avg_gt, (avg_gt/total_ms)*100.0])
	print("4. SurfaceInterpolator (3D Volume): %8.3f ms (%5.1f%%) 🔥 [BOTTLENECK!]" % [avg_interp, (avg_interp/total_ms)*100.0])
	print("5. DebugRenderer update_visuals:    %8.3f ms (%5.1f%%)" % [avg_debug, (avg_debug/total_ms)*100.0])
	print("6. HUD update_telemetry:            %8.3f ms (%5.1f%%)" % [avg_hud, (avg_hud/total_ms)*100.0])
	print("--------------------------------------------------")
	print("TOTAL PER-FRAME TIME:               %8.3f ms" % total_ms)
	print("ESTIMATED FRAMERATE:                %8.1f FPS" % fps)
	print("==================================================")
	quit(0)
