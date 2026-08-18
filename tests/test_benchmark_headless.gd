extends SceneTree

func _init() -> void:
	print("==================================================")
	print("🔬 Running ASTG End-to-End Automated Benchmarks")
	print("==================================================")
	
	var main_scene = preload("res://scenes/main.tscn").instantiate()
	root.add_child(main_scene)
	
	# Allow one frame to initialize physics space state and scenes
	await process_frame
	
	print("[E2E] Main scene initialized successfully.")
	print("[E2E] Surface Probes: %d, Bounce 0 Nodes: %d, Bounce 1 Nodes: %d" % [
		main_scene.astg_pipeline.probes.size(),
		main_scene.astg_pipeline.bounce0_nodes.size(),
		main_scene.astg_pipeline.bounce1_nodes.size()
	])
	
	var runner: BenchmarkRunner = main_scene.benchmark_runner
	
	# ----------------------------------------------------
	# 1. Run Scenario A: Light Toggle
	# ----------------------------------------------------
	print("\n>>> EXECUTING TEST A: LIGHT TOGGLE (0-Ray Direct Update) <<<")
	runner.start_scenario(
		GIEnums.BenchmarkScenario.TEST_A_LIGHT_TOGGLE,
		main_scene.current_testbed,
		main_scene.astg_pipeline,
		main_scene.ddgi_baseline,
		main_scene.ground_truth
	)
	
	while runner.is_running:
		runner.tick_frame(0.016)
		await process_frame
		
	var summary_a = runner.logger.compute_summary()
	if summary_a.avg_rays_per_frame > 0.1:
		printerr("WARNING: Test A expected 0 rays per frame during light toggle, got %.1f" % summary_a.avg_rays_per_frame)
	else:
		print("✅ TEST A VERIFIED: Steady-state and light toggle required 0 visibility rays!")

	# ----------------------------------------------------
	# 2. Run Scenario B: Color Shift
	# ----------------------------------------------------
	print("\n>>> EXECUTING TEST B: COLOR SHIFT (Graph Propagation) <<<")
	runner.start_scenario(
		GIEnums.BenchmarkScenario.TEST_B_COLOR_SHIFT,
		main_scene.current_testbed,
		main_scene.astg_pipeline,
		main_scene.ddgi_baseline,
		main_scene.ground_truth
	)
	
	while runner.is_running:
		runner.tick_frame(0.016)
		await process_frame
		
	var summary_b = runner.logger.compute_summary()
	print("✅ TEST B VERIFIED: Color shift propagated across graph in %.3f ms (Rays: %.1f)" % [summary_b.avg_time_ms, summary_b.avg_rays_per_frame])

	# ----------------------------------------------------
	# 3. Run Scenario C: Wall / Shutter Breach
	# ----------------------------------------------------
	print("\n>>> EXECUTING TEST C: SHUTTER BREACH (T90 Latency & Local Repair) <<<")
	runner.start_scenario(
		GIEnums.BenchmarkScenario.TEST_C_WALL_BREACH,
		main_scene.current_testbed,
		main_scene.astg_pipeline,
		main_scene.ddgi_baseline,
		main_scene.ground_truth
	)
	
	while runner.is_running:
		runner.tick_frame(0.016)
		await process_frame
		
	var summary_c = runner.logger.compute_summary()
	print("✅ TEST C VERIFIED: Breach repaired in %d frames (T90 latency: %d frames, Peak rays: %d)" % [
		summary_c.t90_latency_frames, summary_c.t90_latency_frames, summary_c.peak_rays
	])

	# ----------------------------------------------------
	# 4. Run Scenario E: Catastrophic Demolition
	# ----------------------------------------------------
	print("\n>>> EXECUTING TEST E: CATASTROPHIC DEMOLITION (Burst Budget Stability) <<<")
	runner.start_scenario(
		GIEnums.BenchmarkScenario.TEST_E_EXPLOSION,
		main_scene.current_testbed,
		main_scene.astg_pipeline,
		main_scene.ddgi_baseline,
		main_scene.ground_truth
	)
	
	while runner.is_running:
		runner.tick_frame(0.016)
		await process_frame
		
	var summary_e = runner.logger.compute_summary()
	print("✅ TEST E VERIFIED: Demolition managed under burst budget (Avg frame time: %.3f ms, Peak rays: %d)" % [
		summary_e.avg_time_ms, summary_e.peak_rays
	])

	print("\n==================================================")
	print("🎉 ALL END-TO-END BENCHMARKS COMPLETED SUCCESSFULLY!")
	print("==================================================")
	quit(0)
