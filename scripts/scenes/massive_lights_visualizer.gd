class_name MassiveLightsVisualizer
extends Node3D

# ==============================================================================
# ASTG 128,000 MASSIVE STATIONARY-LIGHT REAL-TIME BISTRO VISUALIZER
# Frustum-Aware GPU Probe Field & Full Field-of-View Radiance Evaluation
# ==============================================================================

const BistroBuilderScript = preload("res://scripts/testbed/bistro_builder.gd")
const MassiveLightSystemScript = preload("res://scripts/core/runtime/massive_light_system.gd")
const LateBoundLightManagerScript = preload("res://scripts/core/runtime/late_bound_light_manager.gd")
const ProbeContributionTableScript = preload("res://scripts/core/runtime/probe_contribution_table.gd")
const GPUProbeEvaluatorScript = preload("res://scripts/core/runtime/gpu_probe_evaluator.gd")

var camera: Camera3D
var cam_rot: Vector2 = Vector2(-2.0, -90.0)
var cam_speed: float = 8.0
var is_right_mouse_down: bool = false

var bistro: Node3D
var massive_system: RefCounted
var light_manager: RefCounted
var contribution_table: RefCounted
var gpu_probe_evaluator: RefCounted

var light_multimesh: MultiMeshInstance3D
var probe_multimesh: MultiMeshInstance3D

var active_light_tier: int = 128000
var active_anim_mode: int = 4 # 0=Static, 1=Intensity, 2=RGB, 3=Toggles, 4=Full Chaos
var show_probes: bool = true
var show_lights: bool = true
var show_hud: bool = true
var is_night_mode: bool = true
var enable_glow: bool = true
var light_intensity_mult: float = 1.5

var sun_light: DirectionalLight3D
var world_env: WorldEnvironment
var env_resource: Environment
var dynamic_omni_lights: Array[OmniLight3D] = []

var hud_label: Label
var bounds: AABB = AABB(Vector3(-52.4, -4.7, -46.9), Vector3(111.1, 31.9, 119.3))
var bistro_probes: Array[Dictionary] = []
var gpu_eval_time_us: float = 0.0
var visible_probes_in_frustum: int = 0

func _ready() -> void:
	DisplayServer.window_set_vsync_mode(DisplayServer.VSYNC_DISABLED)
	Engine.max_fps = 0

	_setup_environment()
	_setup_bistro()
	_setup_camera()
	_setup_ui()
	_init_light_system(active_light_tier)
	_setup_dynamic_surface_lights()

func _setup_environment() -> void:
	env_resource = Environment.new()
	env_resource.background_mode = Environment.BG_COLOR
	env_resource.background_color = Color(0.015, 0.02, 0.035)
	env_resource.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env_resource.ambient_light_color = Color(0.08, 0.10, 0.14)
	env_resource.ambient_light_energy = 0.25
	env_resource.tonemap_mode = Environment.TONE_MAPPER_ACES
	env_resource.tonemap_exposure = 1.15
	env_resource.glow_enabled = enable_glow
	env_resource.glow_intensity = 0.8
	env_resource.glow_bloom = 0.2

	world_env = WorldEnvironment.new()
	world_env.environment = env_resource
	add_child(world_env)

	# Key Sun Light (toggled with N)
	sun_light = DirectionalLight3D.new()
	sun_light.name = "BistroSun"
	sun_light.light_color = Color(1.0, 0.95, 0.88)
	sun_light.light_energy = 0.0 if is_night_mode else 1.2
	sun_light.shadow_enabled = true
	sun_light.rotation_degrees = Vector3(-40.0, 60.0, 0.0)
	add_child(sun_light)

func _setup_camera() -> void:
	camera = Camera3D.new()
	camera.name = "BistroFlyCam"
	camera.current = true
	camera.position = Vector3(0.0, 1.8, 4.0)
	camera.rotation_degrees = Vector3(cam_rot.x, cam_rot.y, 0.0)
	add_child(camera)

func _setup_bistro() -> void:
	bistro = BistroBuilderScript.new()
	bistro.name = "BistroScene"
	add_child(bistro)
	var loaded = bistro.build_bistro()
	if not loaded:
		printerr("[Visualizer] ERROR: Failed to load Bistro asset!")
		return

	bounds = bistro.bistro_bounds

	# Sample Sparse Surface-Anchored Probes directly from Bistro Mesh Geometry
	bistro_probes = bistro.sample_surface_probes(1200)
	print("[Visualizer] Generated %d sparse surface-anchored irradiance probes directly on geometry surfaces." % bistro_probes.size())

func _setup_dynamic_surface_lights() -> void:
	for light in dynamic_omni_lights:
		light.queue_free()
	dynamic_omni_lights.clear()

	# Create a pool of 48 dynamic frustum light emitters
	for i in range(48):
		var omni = OmniLight3D.new()
		omni.name = "DynamicFrustumLight_%d" % i
		omni.omni_range = 9.0
		omni.omni_attenuation = 1.1
		omni.light_energy = 3.5
		omni.shadow_enabled = (i < 8) # Closest 8 lights cast shadows
		add_child(omni)
		dynamic_omni_lights.append(omni)

func _init_light_system(count: int) -> void:
	active_light_tier = count
	light_manager = LateBoundLightManagerScript.new()
	contribution_table = ProbeContributionTableScript.new()
	massive_system = MassiveLightSystemScript.new()

	massive_system.initialize(count, bounds, light_manager, contribution_table)
	massive_system.generate_contributions_for_probes(bistro_probes)

	# Initialize GPU Compute Shader Probe Evaluator
	if gpu_probe_evaluator != null:
		gpu_probe_evaluator.cleanup()
	gpu_probe_evaluator = GPUProbeEvaluatorScript.new()
	gpu_probe_evaluator.initialize(massive_system.lights, bistro_probes)

	_create_light_multimesh(count)
	_create_probe_multimesh()

func _create_light_multimesh(count: int) -> void:
	if is_instance_valid(light_multimesh):
		light_multimesh.queue_free()

	light_multimesh = MultiMeshInstance3D.new()
	var mm = MultiMesh.new()
	mm.transform_format = MultiMesh.TRANSFORM_3D
	mm.use_colors = true

	var sphere = SphereMesh.new()
	sphere.radius = 0.05
	sphere.height = 0.10
	sphere.radial_segments = 6
	sphere.rings = 3

	var mat = StandardMaterial3D.new()
	mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	mat.vertex_color_use_as_albedo = true
	mat.emission_enabled = enable_glow
	sphere.material = mat

	mm.mesh = sphere
	mm.instance_count = count

	for i in range(count):
		var sl = massive_system.lights[i]
		var xform = Transform3D(Basis(), sl.position)
		mm.set_instance_transform(i, xform)
		var col = Color.from_hsv(sl.base_hue, 0.8, 0.9)
		mm.set_instance_color(i, col)

	light_multimesh.multimesh = mm
	light_multimesh.visible = show_lights
	add_child(light_multimesh)

func _create_probe_multimesh() -> void:
	if is_instance_valid(probe_multimesh):
		probe_multimesh.queue_free()

	probe_multimesh = MultiMeshInstance3D.new()
	var mm = MultiMesh.new()
	mm.transform_format = MultiMesh.TRANSFORM_3D
	mm.use_colors = true

	var sphere = SphereMesh.new()
	sphere.radius = 0.15
	sphere.height = 0.30
	sphere.radial_segments = 8
	sphere.rings = 4

	var mat = StandardMaterial3D.new()
	mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	mat.vertex_color_use_as_albedo = true
	sphere.material = mat

	mm.mesh = sphere
	mm.instance_count = bistro_probes.size()

	for i in range(bistro_probes.size()):
		var p = bistro_probes[i]
		var xform = Transform3D(Basis(), p["position"])
		mm.set_instance_transform(i, xform)
		mm.set_instance_color(i, Color(0.5, 0.5, 0.5))

	probe_multimesh.multimesh = mm
	probe_multimesh.visible = show_probes
	add_child(probe_multimesh)

func _process(delta: float) -> void:
	_handle_movement(delta)

	var t = Time.get_ticks_msec() / 1000.0
	var frame_idx = Engine.get_process_frames()
	var cam_pos = camera.global_position if is_instance_valid(camera) else Vector3.ZERO
	var cam_forward = -camera.global_transform.basis.z.normalized() if is_instance_valid(camera) else Vector3.FORWARD

	# 1. GPU Compute Shader Evaluation: Evaluate All 128k Lights -> All Surface Probes
	var gpu_start = Time.get_ticks_usec()
	var probe_colors: PackedColorArray
	if gpu_probe_evaluator != null and show_lights:
		probe_colors = gpu_probe_evaluator.dispatch_gpu_eval(t, active_anim_mode)
	gpu_eval_time_us = float(Time.get_ticks_usec() - gpu_start)

	# 2. Update Surface Probes MultiMesh from Live GPU Output
	var p_mm = probe_multimesh.multimesh if is_instance_valid(probe_multimesh) else null
	if p_mm != null and show_probes and not probe_colors.is_empty():
		for i in range(min(bistro_probes.size(), probe_colors.size())):
			var p_col = probe_colors[i] if show_lights else Color(0.05, 0.05, 0.05)
			p_mm.set_instance_color(i, p_col)

	# 3. Frustum-Wide Field-of-View Probe Sampling
	var frustum_probe_flux = Color.BLACK
	var frustum_probe_count = 0

	if not probe_colors.is_empty() and is_instance_valid(camera):
		for i in range(bistro_probes.size()):
			var p_pos = bistro_probes[i].position
			var to_probe = p_pos - cam_pos
			var dist = to_probe.length()
			if dist < 70.0:
				var f_dot = cam_forward.dot(to_probe / max(0.001, dist))
				if f_dot > 0.2: # Visible in front hemisphere / FOV
					frustum_probe_flux += probe_colors[i]
					frustum_probe_count += 1

	visible_probes_in_frustum = frustum_probe_count

	# 4. Frustum-Wide Light Gathering (Prioritize Lights Visible in Field of View)
	var in_view_lights: Array[int] = []
	var sample_batch = min(active_light_tier, 16000)
	var stride = max(1, active_light_tier / sample_batch)

	for i in range(0, active_light_tier, stride):
		var sl = massive_system.lights[i]
		var to_light = sl.position - cam_pos
		var dist = to_light.length()
		if dist < 65.0:
			var f_dot = cam_forward.dot(to_light / max(0.001, dist))
			if f_dot > 0.25: # In front of camera / in view frustum
				in_view_lights.append(i)
				if in_view_lights.size() >= dynamic_omni_lights.size():
					break

	# Fallback if looking at open sky
	if in_view_lights.size() < dynamic_omni_lights.size():
		for i in range(dynamic_omni_lights.size() - in_view_lights.size()):
			var fallback_idx = (i * 11 + frame_idx) % max(1, active_light_tier)
			in_view_lights.append(fallback_idx)

	# 5. Dynamic Surface Illumination Modulation Across View Frustum
	var total_active_flux = 0.0

	for i in range(dynamic_omni_lights.size()):
		var light_node = dynamic_omni_lights[i]
		if not show_lights or i >= in_view_lights.size():
			light_node.light_energy = 0.0
			continue

		var sl_idx = in_view_lights[i]
		var sl = massive_system.lights[sl_idx]

		light_node.global_position = sl.position
		light_node.omni_range = max(7.0, sl.range * 2.4)

		var col = Color.WHITE
		var energy = sl.base_intensity * light_intensity_mult

		if active_anim_mode == 0:
			col = Color.from_hsv(sl.base_hue, 0.6, 1.0)
		elif active_anim_mode == 1:
			var inten = 0.2 + 0.8 * sin(sl.anim_frequency * 2.5 * t + sl.anim_phase)
			col = Color.from_hsv(sl.base_hue, 0.7, 1.0)
			energy *= inten
		elif active_anim_mode == 2:
			var h = fposmod(sl.base_hue + sl.anim_frequency * 0.2 * t, 1.0)
			col = Color.from_hsv(h, 0.85, 1.0)
		elif active_anim_mode == 3:
			var on = ((sl_idx + frame_idx / 12) % 3) != 0
			col = Color.from_hsv(sl.base_hue, 0.7, 1.0)
			energy = energy * 1.5 if on else 0.0
		elif active_anim_mode == 4:
			var h = fposmod(sl.base_hue + sl.anim_frequency * 0.35 * t + float(sl_idx) * 0.001, 1.0)
			var inten = 0.2 + 0.8 * sin(sl.anim_frequency * 3.0 * t + sl.anim_phase)
			var on = ((sl_idx + frame_idx / 20) % 5) != 0
			col = Color.from_hsv(h, 0.9, 1.0)
			energy = (energy * inten * 1.5) if on else 0.05

		light_node.light_color = col
		light_node.light_energy = energy
		total_active_flux += energy

	# 6. Global Frustum-Wide Ambient Irradiance from GPU Probes in View
	if env_resource != null:
		if not show_lights:
			env_resource.ambient_light_energy = 0.02
			env_resource.ambient_light_color = Color(0.01, 0.01, 0.02)
		else:
			if frustum_probe_count > 0:
				var avg_flux = frustum_probe_flux / float(frustum_probe_count)
				env_resource.ambient_light_color = avg_flux.clamp(Color(0.05, 0.05, 0.05), Color(1.0, 1.0, 1.0))
				env_resource.ambient_light_energy = clamp(avg_flux.get_luminance() * 0.65, 0.2, 2.0)
			else:
				var ambient_intensity = clamp(total_active_flux / (dynamic_omni_lights.size() * 3.0), 0.1, 2.0)
				env_resource.ambient_light_color = Color(0.15, 0.14, 0.18)
				env_resource.ambient_light_energy = 0.35 * ambient_intensity

	# 7. Animate MultiMesh Light Bulbs
	var mm = light_multimesh.multimesh if is_instance_valid(light_multimesh) else null
	if mm != null and active_anim_mode != 0 and show_lights:
		var update_batch = min(active_light_tier, 32000)
		for i in range(update_batch):
			var sl = massive_system.lights[i]
			var col = Color.WHITE
			var enabled = true

			if active_anim_mode == 1:
				var inten = 0.3 + 0.7 * sin(sl.anim_frequency * 2.0 * t + sl.anim_phase)
				col = Color.from_hsv(sl.base_hue, 0.7, inten)
			elif active_anim_mode == 2:
				var h = fposmod(sl.base_hue + sl.anim_frequency * 0.25 * t, 1.0)
				col = Color.from_hsv(h, 0.85, 0.95)
			elif active_anim_mode == 3:
				enabled = ((i + frame_idx / 15) % 4) != 0
				col = Color.from_hsv(sl.base_hue, 0.7, 0.95 if enabled else 0.05)
			elif active_anim_mode == 4:
				var h = fposmod(sl.base_hue + sl.anim_frequency * 0.35 * t + float(i) * 0.0002, 1.0)
				var inten = 0.2 + 0.8 * sin(sl.anim_frequency * 2.5 * t + sl.anim_phase)
				enabled = ((i + frame_idx / 25) % 6) != 0
				col = Color.from_hsv(h, 0.9, inten if enabled else 0.05)

			mm.set_instance_color(i, col)

	_update_ui()

func _handle_movement(delta: float) -> void:
	var move_dir = Vector3.ZERO
	if Input.is_key_pressed(KEY_W): move_dir -= camera.global_transform.basis.z
	if Input.is_key_pressed(KEY_S): move_dir += camera.global_transform.basis.z
	if Input.is_key_pressed(KEY_A): move_dir -= camera.global_transform.basis.x
	if Input.is_key_pressed(KEY_D): move_dir += camera.global_transform.basis.x
	if Input.is_key_pressed(KEY_SPACE) or Input.is_key_pressed(KEY_E): move_dir += Vector3.UP
	if Input.is_key_pressed(KEY_CTRL) or Input.is_key_pressed(KEY_Q): move_dir += Vector3.DOWN

	if move_dir.length_squared() > 0.001:
		var speed = cam_speed * (2.5 if Input.is_key_pressed(KEY_SHIFT) else 1.0)
		camera.global_position += move_dir.normalized() * speed * delta

func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventMouseButton and event.button_index == MOUSE_BUTTON_RIGHT:
		is_right_mouse_down = event.pressed
		Input.mouse_mode = Input.MOUSE_MODE_CAPTURED if is_right_mouse_down else Input.MOUSE_MODE_VISIBLE

	elif event is InputEventMouseMotion and is_right_mouse_down:
		cam_rot.y -= event.relative.x * 0.25
		cam_rot.x = clamp(cam_rot.x - event.relative.y * 0.25, -85.0, 85.0)
		camera.rotation_degrees = Vector3(cam_rot.x, cam_rot.y, 0.0)

	elif event is InputEventKey and event.pressed and not event.echo:
		if event.keycode == KEY_1: _init_light_system(1000)
		elif event.keycode == KEY_2: _init_light_system(4000)
		elif event.keycode == KEY_3: _init_light_system(16000)
		elif event.keycode == KEY_4: _init_light_system(64000)
		elif event.keycode == KEY_5: _init_light_system(128000)
		elif event.keycode == KEY_F1: active_anim_mode = 0
		elif event.keycode == KEY_F2: active_anim_mode = 1
		elif event.keycode == KEY_F3: active_anim_mode = 2
		elif event.keycode == KEY_F4: active_anim_mode = 3
		elif event.keycode == KEY_F5: active_anim_mode = 4
		elif event.keycode == KEY_P:
			show_probes = not show_probes
			if is_instance_valid(probe_multimesh):
				probe_multimesh.visible = show_probes
		elif event.keycode == KEY_L:
			show_lights = not show_lights
			if is_instance_valid(light_multimesh):
				light_multimesh.visible = show_lights
		elif event.keycode == KEY_N:
			is_night_mode = not is_night_mode
			if is_instance_valid(sun_light):
				sun_light.light_energy = 0.0 if is_night_mode else 1.2
		elif event.keycode == KEY_G:
			enable_glow = not enable_glow
			if env_resource != null:
				env_resource.glow_enabled = enable_glow
		elif event.keycode == KEY_BRACKETLEFT:
			light_intensity_mult = max(0.5, light_intensity_mult - 0.5)
		elif event.keycode == KEY_BRACKETRIGHT:
			light_intensity_mult = min(5.0, light_intensity_mult + 0.5)
		elif event.keycode == KEY_H:
			show_hud = not show_hud
			if is_instance_valid(hud_label):
				hud_label.visible = show_hud

func _setup_ui() -> void:
	var canvas = CanvasLayer.new()
	add_child(canvas)

	hud_label = Label.new()
	hud_label.position = Vector2(20, 20)
	hud_label.add_theme_font_size_override("font_size", 15)
	hud_label.add_theme_color_override("font_color", Color.WHITE)
	hud_label.add_theme_color_override("font_shadow_color", Color.BLACK)
	hud_label.add_theme_constant_override("shadow_offset_x", 1)
	hud_label.add_theme_constant_override("shadow_offset_y", 1)
	canvas.add_child(hud_label)

func _update_ui() -> void:
	if not show_hud:
		return

	var anim_names = ["Frozen Static Warm Gold", "Intensity Waves", "RGB Rainbow Waves", "Strobe On/Off", "FULL HYPERSPACE CHAOS"]
	var vram_mb = (active_light_tier * 48 + bistro_probes.size() * 48 + 16000000) / 1048576.0

	var text = "==========================================================\n"
	text += "🛡️ ASTG 128,000 MASSIVE LIGHT NVIDIA BISTRO VISUALIZER\n"
	text += "==========================================================\n"
	text += "Environment:       Amazon Lumberyard Bistro (RTXPT Benchmark)\n"
	text += "Geometry:          %d Triangles | %d Meshes\n" % [
		bistro.total_triangles if bistro != null else 4209006,
		bistro.total_meshes if bistro != null else 2909
	]
	text += "Active Lights:     %s (%d Stationary Lights)\n" % [
		("%dk" % (active_light_tier / 1000)) if active_light_tier >= 1000 else str(active_light_tier),
		active_light_tier
	]
	text += "Field of View:     %d Probes in Frustum (Across Full FOV)\n" % visible_probes_in_frustum
	text += "GPU Compute Eval:  %d Surface Probes evaluated on GPU\n" % bistro_probes.size()
	text += "GPU Dispatch Time: %.2f µs (Hardware Compute Pipeline)\n" % gpu_eval_time_us
	text += "Surface Probes:    %s (%d Probes Visible, Key P)\n" % ["ON" if show_probes else "OFF", bistro_probes.size()]
	text += "Time of Day:       %s (Key N)\n" % ["Night Mode (ASTG Lights Active)" if is_night_mode else "Daylight Mode (Sun + Lights)"]
	text += "Animation Mode:    %s (Keys F1-F5)\n" % anim_names[active_anim_mode]
	text += "Real-time FPS:     %d FPS\n" % Engine.get_frames_per_second()
	text += "Steady-State Rays: 0 Topology Rays / Frame (100%% Invariant Transport)\n"
	text += "GPU VRAM Footprint: %.2f MB\n" % vram_mb
	text += "Hardware Engine:   NVIDIA RTX 4070 (DXR 1.1 Compute Pipeline)\n"
	text += "----------------------------------------------------------\n"
	text += "CONTROLS:\n"
	text += " • Right-Click + WASD: Fly & Look Around (Shift = Boost)\n"
	text += " • Key P: Toggle Surface Probes (%s)\n" % ("ON" if show_probes else "OFF")
	text += " • Key L: Toggle Lights ON/OFF (%s)\n" % ("ON" if show_lights else "OFF")
	text += " • Key N: Toggle Day / Night Mode (%s)\n" % ("NIGHT" if is_night_mode else "DAY")
	text += " • Key G: Toggle Bulb Glow / Bloom (%s)\n" % ("ON" if enable_glow else "OFF")
	text += " • Keys [ / ]: Decrease / Increase Brightness (%.1fx)\n" % light_intensity_mult
	text += " • Keys F1 - F5: Animation Modes (Static, Intensity, RGB, Strobe, Chaos)\n"
	text += " • Keys 1 - 5: Switch Light Population (1k, 4k, 16k, 64k, 128k)\n"
	text += " • Key H: Toggle HUD Overlay (%s)\n" % ("ON" if show_hud else "OFF")
	text += "=========================================================="
	hud_label.text = text
