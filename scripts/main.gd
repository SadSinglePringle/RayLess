class_name Main
extends Node3D

enum SceneType {
	CLASSROOM = 0,
	THREE_ROOM_LAB = 1
}

var current_scene_type: int = SceneType.CLASSROOM

var current_testbed: Node3D
var classroom_builder: ClassroomBuilder
var three_room_builder: TestbedBuilder

var astg_pipeline: ASTGPipeline
var ddgi_baseline: DDGIBaseline
var ground_truth: GroundTruthReference

var debug_renderer: ASTGDebugRenderer
var surface_interpolator: ASTGSurfaceInterpolator
var hud: HUDController
var benchmark_runner: BenchmarkRunner

var camera: Camera3D
var current_gi_mode: int = GIEnums.GIMode.ASTG
var current_debug_mode: int = GIEnums.DebugViewMode.NONE
var current_camera_preset: int = 1

var cam_rot: Vector2 = Vector2(-10.0, 140.0)
var cam_speed: float = 6.0
var is_right_mouse_down: bool = false

var surface_shader: Shader = preload("res://scripts/rendering/shaders/astg_gi.gdshader")

func _ready() -> void:
	DisplayServer.window_set_vsync_mode(DisplayServer.VSYNC_DISABLED)
	Engine.max_fps = 0
	
	print("==================================================")
	print("🚀 Initializing ASTG Persistent Transport Graph...")
	print("==================================================")
	
	_setup_camera()
	_load_scene(current_scene_type)
	_setup_rendering_and_debug()
	_setup_ui()
	
	set_camera_preset(1)
	print("[Main] ASTG Initialization complete! Ready for interactive evaluation.")

func _setup_camera() -> void:
	camera = Camera3D.new()
	camera.name = "MainCamera"
	camera.current = true
	add_child(camera)

func _load_scene(scene_type: int) -> void:
	current_scene_type = scene_type
	
	if is_instance_valid(current_testbed):
		current_testbed.queue_free()
		
	var world = get_world_3d()
	
	if scene_type == SceneType.CLASSROOM:
		classroom_builder = ClassroomBuilder.new()
		classroom_builder.name = "Classroom"
		add_child(classroom_builder)
		classroom_builder.build_classroom()
		classroom_builder.chunk_destroyed.connect(_on_chunk_destroyed)
		current_testbed = classroom_builder
		
		# Pipeline setup for Classroom
		astg_pipeline = ASTGPipeline.new()
		astg_pipeline.initialize(classroom_builder.static_mesh_nodes, classroom_builder.dynamic_lights, world)
		
		ddgi_baseline = DDGIBaseline.new()
		ddgi_baseline.initialize(classroom_builder.classroom_bounds, Vector3i(6, 3, 5), classroom_builder.dynamic_lights, world)
		
		ground_truth = GroundTruthReference.new()
		ground_truth.initialize(classroom_builder.dynamic_lights, world)
		ground_truth.compute_reference_solve(astg_pipeline.probes)
		
	else:
		three_room_builder = TestbedBuilder.new()
		three_room_builder.name = "ThreeRoomLab"
		add_child(three_room_builder)
		three_room_builder.build_testbed()
		three_room_builder.chunk_destroyed.connect(_on_chunk_destroyed)
		current_testbed = three_room_builder
		
		# Pipeline setup for 3-Room Lab
		astg_pipeline = ASTGPipeline.new()
		astg_pipeline.initialize(three_room_builder.static_mesh_nodes, three_room_builder.dynamic_lights, world)
		
		ddgi_baseline = DDGIBaseline.new()
		ddgi_baseline.initialize(three_room_builder.room_bounds, Vector3i(6, 3, 6), three_room_builder.dynamic_lights, world)
		
		ground_truth = GroundTruthReference.new()
		ground_truth.initialize(three_room_builder.dynamic_lights, world)
		ground_truth.compute_reference_solve(astg_pipeline.probes)
		
	if surface_interpolator != null:
		var meshes = classroom_builder.static_mesh_nodes if scene_type == SceneType.CLASSROOM else three_room_builder.static_mesh_nodes
		var bounds = classroom_builder.classroom_bounds if scene_type == SceneType.CLASSROOM else three_room_builder.room_bounds
		surface_interpolator.set_scene_bounds(bounds)
		surface_interpolator.register_scene_meshes(meshes, surface_shader)
		
	benchmark_runner = BenchmarkRunner.new()

func _setup_rendering_and_debug() -> void:
	surface_interpolator = ASTGSurfaceInterpolator.new()
	var meshes = classroom_builder.static_mesh_nodes if current_scene_type == SceneType.CLASSROOM else three_room_builder.static_mesh_nodes
	var bounds = classroom_builder.classroom_bounds if current_scene_type == SceneType.CLASSROOM else three_room_builder.room_bounds
	surface_interpolator.set_scene_bounds(bounds)
	surface_interpolator.register_scene_meshes(meshes, surface_shader)
	
	debug_renderer = ASTGDebugRenderer.new()
	debug_renderer.name = "DebugRenderer"
	add_child(debug_renderer)

func _setup_ui() -> void:
	hud = HUDController.new()
	add_child(hud)
	
	hud.gi_mode_changed.connect(_on_gi_mode_changed)
	hud.debug_view_changed.connect(_on_debug_view_changed)
	hud.camera_preset_changed.connect(set_camera_preset)
	
	hud.destroy_center_chunk_requested.connect(func():
		if current_scene_type == SceneType.CLASSROOM:
			classroom_builder.destructible_shutters.destroy_center_chunk()
		else:
			three_room_builder.destructible_wall.destroy_center_chunk()
	)
	hud.destroy_all_chunks_requested.connect(func():
		if current_scene_type == SceneType.CLASSROOM:
			classroom_builder.destructible_shutters.destroy_all_chunks()
		else:
			three_room_builder.destructible_wall.destroy_all_chunks()
	)
	hud.destroy_irrelevant_wall_requested.connect(func():
		if current_scene_type == SceneType.CLASSROOM:
			classroom_builder.destructible_blackboard.destroy_all_chunks()
		else:
			three_room_builder.irrelevant_wall.destroy_all_chunks()
	)
	hud.restore_wall_requested.connect(_on_restore_wall_requested)
	hud.toggle_light_requested.connect(_on_toggle_ceiling_lights_requested)
	hud.toggle_occluder_requested.connect(_on_toggle_teacher_lamp_requested)
	hud.run_benchmark_requested.connect(_on_run_benchmark_requested)

func set_camera_preset(preset: int) -> void:
	current_camera_preset = preset
	if current_scene_type == SceneType.CLASSROOM:
		match preset:
			1: # Iconic Benchmark View (Classic Blender Cycles View)
				camera.position = Vector3(2.58, 1.65, 3.80)
				camera.rotation_degrees = Vector3(-8.0, 160.0, 0.0)
			2: # Front Chalkboard View
				camera.position = Vector3(0.89, 1.50, -3.10)
				camera.rotation_degrees = Vector3(-6.0, 15.0, 0.0)
			3: # Student Desk Close-Up
				camera.position = Vector3(1.20, 1.25, 0.50)
				camera.rotation_degrees = Vector3(-12.0, 165.0, 0.0)
			4: # Side Windows & Sunlight Beams
				camera.position = Vector3(-0.5, 1.40, 0.0)
				camera.rotation_degrees = Vector3(0.0, -90.0, 0.0)
			5: # Free Fly
				pass
	else:
		match preset:
			1: # Cam A: Overview
				camera.position = Vector3(0.0, 8.5, 9.5)
				camera.rotation_degrees = Vector3(-45.0, 0.0, 0.0)
			2: # Cam B: Room C Focus
				camera.position = Vector3(4.0, 2.0, -6.5)
				camera.rotation_degrees = Vector3(-10.0, 0.0, 0.0)
			3: # Cam C: White Sphere Close-Up
				camera.position = Vector3(4.0, 1.2, -4.5)
				camera.rotation_degrees = Vector3(-5.0, 0.0, 0.0)
			4: # Cam D: Hallway Doorway
				camera.position = Vector3(-4.0, 1.8, 0.0)
				camera.rotation_degrees = Vector3(-5.0, 90.0, 0.0)
			5: # Free Fly
				pass
				
	cam_rot = Vector2(camera.rotation_degrees.x, camera.rotation_degrees.y)
	print("[Main] Switched to Camera Preset: %d" % preset)

var telemetry_frame_counter: int = 0
var last_gt_metrics: Dictionary = {}
var last_ddgi_metrics: Dictionary = {}

func _process(delta: float) -> void:
	if current_camera_preset == 5:
		_handle_camera_movement(delta)
	
	if benchmark_runner.is_running:
		benchmark_runner.tick_frame(delta)
	else:
		var astg_metrics = astg_pipeline.process_frame(delta, camera)
		if astg_metrics.regrown_count > 0 or astg_metrics.repair_jobs_done > 0:
			surface_interpolator.mark_dirty()
			
		var ddgi_metrics = last_ddgi_metrics
		if current_gi_mode == GIEnums.GIMode.DDGI_BASELINE:
			ddgi_metrics = ddgi_baseline.process_frame(delta)
			last_ddgi_metrics = ddgi_metrics
			surface_interpolator.mark_dirty()
			
		telemetry_frame_counter += 1
		var gt_metrics = last_gt_metrics
		if telemetry_frame_counter % 15 == 0 or current_debug_mode == GIEnums.DebugViewMode.PROBES_ERROR_HEATMAP:
			gt_metrics = ground_truth.compute_metrics_vs_reference(astg_pipeline.probes)
			last_gt_metrics = gt_metrics
		
		surface_interpolator.update_surfaces(current_gi_mode, astg_pipeline, ddgi_baseline, ground_truth)
		
		if current_debug_mode != GIEnums.DebugViewMode.NONE:
			debug_renderer.update_debug_visuals(
				astg_pipeline.probes,
				astg_pipeline.bounce0_nodes,
				astg_pipeline.bounce1_nodes,
				astg_pipeline.lights,
				ground_truth.reference_probe_radiance
			)
		else:
			debug_renderer.clear()
		
		hud.update_telemetry(astg_metrics, ddgi_metrics, gt_metrics, current_gi_mode)

func _handle_camera_movement(delta: float) -> void:
	if not is_instance_valid(camera):
		return
	var move_dir = Vector3.ZERO
	if Input.is_key_pressed(KEY_W): move_dir -= camera.global_transform.basis.z
	if Input.is_key_pressed(KEY_S): move_dir += camera.global_transform.basis.z
	if Input.is_key_pressed(KEY_A): move_dir -= camera.global_transform.basis.x
	if Input.is_key_pressed(KEY_D): move_dir += camera.global_transform.basis.x
	if Input.is_key_pressed(KEY_E) or Input.is_key_pressed(KEY_SPACE): move_dir += Vector3.UP
	if Input.is_key_pressed(KEY_Q) or Input.is_key_pressed(KEY_CTRL): move_dir += Vector3.DOWN
		
	if move_dir.length_squared() > 0.001:
		var speed_mult = 2.0 if Input.is_key_pressed(KEY_SHIFT) else 1.0
		camera.global_position += move_dir.normalized() * cam_speed * speed_mult * delta

func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventKey and event.pressed and not event.echo:
		if event.keycode == KEY_1: set_camera_preset(1)
		elif event.keycode == KEY_2: set_camera_preset(2)
		elif event.keycode == KEY_3: set_camera_preset(3)
		elif event.keycode == KEY_4: set_camera_preset(4)
		elif event.keycode == KEY_5: set_camera_preset(5)
		elif event.keycode == KEY_F1:
			_load_scene(SceneType.CLASSROOM if current_scene_type == SceneType.THREE_ROOM_LAB else SceneType.THREE_ROOM_LAB)
			set_camera_preset(1)

	if event is InputEventMouseButton:
		if event.button_index == MOUSE_BUTTON_RIGHT:
			is_right_mouse_down = event.pressed
			current_camera_preset = 5
			Input.mouse_mode = Input.MOUSE_MODE_CAPTURED if is_right_mouse_down else Input.MOUSE_MODE_VISIBLE
		elif event.button_index == MOUSE_BUTTON_LEFT and event.pressed:
			var mouse_pos = event.position
			var ray_origin = camera.project_ray_origin(mouse_pos)
			var ray_dir = camera.project_ray_normal(mouse_pos)
			var ray_end = ray_origin + ray_dir * 100.0
			
			var query = PhysicsRayQueryParameters3D.create(ray_origin, ray_end)
			var hit = get_world_3d().direct_space_state.intersect_ray(query)
			if not hit.is_empty():
				var collider = hit.collider
				if collider is DestructibleChunk:
					print("[Interactive] Clicked DestructibleChunk ID: %d" % collider.chunk_id)
					collider.destroy()
					
	elif event is InputEventMouseMotion and is_right_mouse_down:
		cam_rot.y -= event.relative.x * 0.3
		cam_rot.x = clamp(cam_rot.x - event.relative.y * 0.3, -85.0, 85.0)
		camera.rotation_degrees = Vector3(cam_rot.x, cam_rot.y, 0)

func _on_chunk_destroyed(chunk_id: int, bounds: AABB) -> void:
	astg_pipeline.notify_chunk_destroyed(chunk_id, bounds)
	if surface_interpolator != null:
		surface_interpolator.mark_dirty()

func _on_gi_mode_changed(new_mode: int) -> void:
	current_gi_mode = new_mode
	if surface_interpolator != null:
		surface_interpolator.mark_dirty()

func _on_debug_view_changed(new_mode: int) -> void:
	current_debug_mode = new_mode
	debug_renderer.current_view_mode = new_mode

func _on_restore_wall_requested() -> void:
	if current_scene_type == SceneType.CLASSROOM:
		classroom_builder.destructible_shutters.restore_all_chunks()
		classroom_builder.destructible_blackboard.restore_all_chunks()
	else:
		three_room_builder.destructible_wall.restore_all_chunks()
	if surface_interpolator != null:
		surface_interpolator.mark_dirty()
		three_room_builder.irrelevant_wall.restore_all_chunks()
		
	astg_pipeline.radiance_evaluator.evaluate_energy(
		astg_pipeline.bounce0_nodes,
		astg_pipeline.bounce1_nodes,
		astg_pipeline.lights,
		astg_pipeline.probes,
		astg_pipeline.probe_depositor
	)
	ground_truth.compute_reference_solve(astg_pipeline.probes)
	print("[Main] Restored all elements and re-evaluated energy.")

func _on_toggle_ceiling_lights_requested() -> void:
	if current_scene_type == SceneType.CLASSROOM:
		classroom_builder.toggle_ceiling_lights()
	else:
		three_room_builder.toggle_lamp_b()
	print("[Main] Toggled Lights (0-Ray Direct Update).")
	ground_truth.compute_reference_solve(astg_pipeline.probes)

func _on_toggle_teacher_lamp_requested() -> void:
	if current_scene_type == SceneType.CLASSROOM:
		for l in classroom_builder.dynamic_lights:
			if l.name == "TeacherDeskLamp":
				l.visible = not l.visible
				break
	else:
		three_room_builder.toggle_occluder_movement()
	ground_truth.compute_reference_solve(astg_pipeline.probes)

func _on_run_benchmark_requested(scenario: int) -> void:
	benchmark_runner.start_scenario(scenario, current_testbed, astg_pipeline, ddgi_baseline, ground_truth)
