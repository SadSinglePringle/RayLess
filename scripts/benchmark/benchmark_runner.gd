class_name BenchmarkRunner
extends RefCounted

signal benchmark_completed(scenario_name: String, summary: Dictionary)

var is_running: bool = false
var current_scenario: int = GIEnums.BenchmarkScenario.TEST_A_LIGHT_TOGGLE
var current_frame: int = 0
var max_scenario_frames: int = 40
var is_full_timeline: bool = false

var scene_env: Node3D
var astg: ASTGPipeline
var ddgi: DDGIBaseline
var ground_truth: GroundTruthReference
var logger: TelemetryLogger = TelemetryLogger.new()

func start_full_timeline(
	p_env: Node3D,
	p_astg: ASTGPipeline,
	p_ddgi: DDGIBaseline,
	p_ground_truth: GroundTruthReference
) -> void:
	is_full_timeline = true
	max_scenario_frames = 120
	start_scenario(-1, p_env, p_astg, p_ddgi, p_ground_truth)

func start_scenario(
	p_scenario: int,
	p_env: Node3D,
	p_astg: ASTGPipeline,
	p_ddgi: DDGIBaseline,
	p_ground_truth: GroundTruthReference
) -> void:
	current_scenario = p_scenario
	scene_env = p_env
	astg = p_astg
	ddgi = p_ddgi
	ground_truth = p_ground_truth
	current_frame = 0
	is_running = true
	
	_restore_scene()
			
	var s_name = "Full_85s_Evaluation_Timeline" if is_full_timeline else _get_scenario_name(current_scenario)
	logger.start_session(s_name)
	print("[Benchmark] Starting %s in Classroom Benchmark..." % s_name)
	ground_truth.compute_reference_solve(astg.probes)

func tick_frame(delta: float) -> bool:
	if not is_running:
		return false
		
	current_frame += 1
	
	if is_full_timeline:
		_process_full_timeline_step(current_frame)
	else:
		_process_single_scenario_step(current_frame)
				
	var astg_metrics = astg.process_frame(delta)
	var ddgi_metrics = ddgi.process_frame(delta)
	var gt_metrics = ground_truth.compute_metrics_vs_reference(astg.probes)
	
	var log_entry = {
		"frame": current_frame,
		"mode": "ASTG",
		"time_ms": astg_metrics.get("time_ms", 0.0),
		"rays_traced": astg_metrics.get("rays_traced", 0),
		"active_probes": astg_metrics.get("active_probes", 0),
		"active_edges": astg_metrics.get("active_nodes", 0),
		"invalid_edges": astg_metrics.get("remaining_repair_queue", 0),
		"edges_repaired": astg_metrics.get("repair_jobs_done", 0),
		"new_edges": astg_metrics.get("regrown_count", 0),
		"mse": gt_metrics.get("mse", 0.0),
		"psnr": gt_metrics.get("psnr", 0.0),
		"t90_ratio": gt_metrics.get("t90_converged_ratio", 1.0)
	}
	logger.log_frame(log_entry)
	
	if current_frame >= max_scenario_frames:
		is_running = false
		is_full_timeline = false
		var summary = logger.compute_summary()
		var s_name = "Full_85s_Evaluation_Timeline" if is_full_timeline else _get_scenario_name(current_scenario)
		print("--------------------------------------------------")
		print("[Benchmark Completed: %s]" % s_name)
		print("  Total Frames: %d" % summary.total_frames)
		print("  Avg Time: %.3f ms" % summary.avg_time_ms)
		print("  Avg Rays/Frame: %.1f" % summary.avg_rays_per_frame)
		print("  Peak Rays: %d" % summary.peak_rays)
		print("  Avg MSE: %.6f" % summary.avg_mse)
		print("  T90 Convergence Latency: %d frames" % summary.t90_latency_frames)
		print("--------------------------------------------------")
		benchmark_completed.emit(s_name, summary)
		return false
		
	return true

func _restore_scene() -> void:
	if scene_env is ClassroomBuilder:
		var cb: ClassroomBuilder = scene_env
		cb.destructible_shutters.restore_all_chunks()
		cb.destructible_blackboard.restore_all_chunks()
		for l in cb.dynamic_lights:
			l.visible = true
	elif scene_env is TestbedBuilder:
		var tb: TestbedBuilder = scene_env
		tb.destructible_wall.restore_all_chunks()
		tb.irrelevant_wall.restore_all_chunks()
		tb.occluder_moving = false
		for l in tb.dynamic_lights:
			l.visible = true

func _process_single_scenario_step(frame: int) -> void:
	match current_scenario:
		GIEnums.BenchmarkScenario.TEST_A_LIGHT_TOGGLE:
			if frame == 10:
				print("[Benchmark] Frame 10: Toggling Classroom Ceiling Lamps OFF")
				_toggle_lights(false)
				ground_truth.compute_reference_solve(astg.probes)
			elif frame == 25:
				print("[Benchmark] Frame 25: Toggling Classroom Ceiling Lamps ON")
				_toggle_lights(true)
				ground_truth.compute_reference_solve(astg.probes)
				
		GIEnums.BenchmarkScenario.TEST_B_COLOR_SHIFT:
			if frame == 10:
				print("[Benchmark] Frame 10: Shifting Ceiling Lamps to Warm RED")
				_set_light_color(Color(1.0, 0.15, 0.15))
				ground_truth.compute_reference_solve(astg.probes)
			elif frame == 25:
				print("[Benchmark] Frame 25: Shifting Ceiling Lamps to Cool BLUE")
				_set_light_color(Color(0.2, 0.5, 1.0))
				ground_truth.compute_reference_solve(astg.probes)
				
		GIEnums.BenchmarkScenario.TEST_C_WALL_BREACH:
			if frame == 10:
				print("[Benchmark] Frame 10: Breaching Window Shutter Section")
				if scene_env is ClassroomBuilder:
					scene_env.destructible_shutters.destroy_center_chunk()
				ground_truth.compute_reference_solve(astg.probes)
				
		GIEnums.BenchmarkScenario.TEST_E_EXPLOSION:
			if frame == 10:
				print("[Benchmark] Frame 10: Demolishing all window shutters!")
				if scene_env is ClassroomBuilder:
					scene_env.destructible_shutters.destroy_all_chunks()
				ground_truth.compute_reference_solve(astg.probes)

func _process_full_timeline_step(frame: int) -> void:
	match frame:
		10:
			print("[Full Timeline] Frame 10: Ceiling lights intensity 100% -> 25%")
			_set_ceiling_energy(0.4)
			ground_truth.compute_reference_solve(astg.probes)
		20:
			print("[Full Timeline] Frame 20: Ceiling lights intensity -> 100%")
			_set_ceiling_energy(1.6)
			ground_truth.compute_reference_solve(astg.probes)
		30:
			print("[Full Timeline] Frame 30: Ceiling lights White -> RED")
			_set_light_color(Color(1.0, 0.15, 0.15))
			ground_truth.compute_reference_solve(astg.probes)
		40:
			print("[Full Timeline] Frame 40: Ceiling lights Red -> BLUE")
			_set_light_color(Color(0.2, 0.5, 1.0))
			ground_truth.compute_reference_solve(astg.probes)
		50:
			print("[Full Timeline] Frame 50: Ceiling lights OFF")
			_toggle_lights(false)
			ground_truth.compute_reference_solve(astg.probes)
		60:
			print("[Full Timeline] Frame 60: Ceiling lights ON")
			_toggle_lights(true)
			_set_light_color(Color(0.96, 0.95, 1.0))
			ground_truth.compute_reference_solve(astg.probes)
		70:
			print("[Full Timeline] Frame 70: Destroy SMALL Window Shutter Section")
			if scene_env is ClassroomBuilder:
				scene_env.destructible_shutters.destroy_center_chunk()
			ground_truth.compute_reference_solve(astg.probes)
		85:
			print("[Full Timeline] Frame 85: Destroy ALL Window Shutters")
			if scene_env is ClassroomBuilder:
				scene_env.destructible_shutters.destroy_all_chunks()
			ground_truth.compute_reference_solve(astg.probes)
		95:
			print("[Full Timeline] Frame 95: Sunlight intensity down (3.5 -> 0.5)")
			_set_sun_energy(0.5)
			ground_truth.compute_reference_solve(astg.probes)
		105:
			print("[Full Timeline] Frame 105: Sunlight intensity restored")
			_set_sun_energy(3.5)
			ground_truth.compute_reference_solve(astg.probes)
		112:
			print("[Full Timeline] Frame 112: Destroy Blackboard Panel (Test 7: Localized impact)")
			if scene_env is ClassroomBuilder:
				scene_env.destructible_blackboard.destroy_all_chunks()

func _toggle_lights(is_vis: bool) -> void:
	if scene_env is ClassroomBuilder:
		for l in scene_env.ceiling_lights:
			l.visible = is_vis

func _set_light_color(col: Color) -> void:
	if scene_env is ClassroomBuilder:
		scene_env.set_ceiling_lights_color(col)

func _set_ceiling_energy(energy: float) -> void:
	if scene_env is ClassroomBuilder:
		scene_env.set_ceiling_lights_energy(energy)

func _set_sun_energy(energy: float) -> void:
	if scene_env is ClassroomBuilder:
		for l in scene_env.dynamic_lights:
			if l is DirectionalLight3D:
				l.light_energy = energy

func _get_scenario_name(s: int) -> String:
	match s:
		GIEnums.BenchmarkScenario.TEST_A_LIGHT_TOGGLE: return "Test_A_Light_Toggle"
		GIEnums.BenchmarkScenario.TEST_B_COLOR_SHIFT: return "Test_B_Color_Shift"
		GIEnums.BenchmarkScenario.TEST_C_WALL_BREACH: return "Test_C_Window_Breach"
		GIEnums.BenchmarkScenario.TEST_D_MULTI_ROOM: return "Test_D_Classroom_Transport"
		GIEnums.BenchmarkScenario.TEST_E_EXPLOSION: return "Test_E_Shutters_Demolition"
	return "Scenario_%d" % s
