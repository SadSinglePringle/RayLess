class_name GPUProbeEvaluator
extends RefCounted

# ==============================================================================
# ASTG GPU PROBE EVALUATOR (RenderingDevice GLSL Pipeline)
# Executes parallel compute dispatches across GPU hardware to evaluate
# all probes against all 128,000 stationary lights in real-time every frame.
# ==============================================================================

var rd: RenderingDevice
var shader_rid: RID
var pipeline_rid: RID
var uniform_set_rid: RID

var light_buffer_rid: RID
var probe_buffer_rid: RID
var output_buffer_rid: RID

var probe_count: int = 0
var total_lights: int = 0
var is_initialized: bool = false
var has_rd: bool = false

func initialize(lights: Array, probes: Array) -> bool:
	probe_count = probes.size()
	total_lights = lights.size()

	if probe_count == 0 or total_lights == 0:
		return false

	# Acquire RenderingDevice
	rd = RenderingServer.get_rendering_device()
	if rd == null:
		# Fallback if headless / compatibility renderer
		return false

	has_rd = true

	# 1. Load and compile GLSL Compute Shader
	var shader_file = load("res://shaders/compute/astg_probe_eval.glsl")
	if shader_file == null:
		printerr("[GPUProbeEvaluator] Failed to load compute shader: res://shaders/compute/astg_probe_eval.glsl")
		return false

	var spirv = shader_file.get_spirv()
	shader_rid = rd.shader_create_from_spirv(spirv)
	if not shader_rid.is_valid():
		printerr("[GPUProbeEvaluator] Failed to compile SPIR-V for compute shader!")
		return false

	pipeline_rid = rd.compute_pipeline_create(shader_rid)

	# 2. Build Light Descriptors Buffer (LightDesc: 48 bytes per light)
	var light_bytes = PackedByteArray()
	light_bytes.resize(total_lights * 48)

	for i in range(total_lights):
		var sl = lights[i]
		var offset = i * 48
		# pos_range (4 floats)
		light_bytes.encode_float(offset + 0, sl.position.x)
		light_bytes.encode_float(offset + 4, sl.position.y)
		light_bytes.encode_float(offset + 8, sl.position.z)
		light_bytes.encode_float(offset + 12, sl.range)
		# color_param (4 floats: base_r, base_g, base_b, anim_freq)
		var base_col = Color.from_hsv(sl.base_hue, 0.8, 1.0)
		light_bytes.encode_float(offset + 16, base_col.r)
		light_bytes.encode_float(offset + 20, base_col.g)
		light_bytes.encode_float(offset + 24, base_col.b)
		light_bytes.encode_float(offset + 28, sl.anim_frequency)
		# anim_data (4 floats: anim_phase, base_intensity, type, is_occluded)
		light_bytes.encode_float(offset + 32, sl.anim_phase)
		light_bytes.encode_float(offset + 36, sl.base_intensity)
		light_bytes.encode_float(offset + 40, float(sl.type))
		light_bytes.encode_float(offset + 44, 1.0 if sl.is_occluded else 0.0)

	light_buffer_rid = rd.storage_buffer_create(light_bytes.size(), light_bytes)

	# 3. Build Probe Descriptors Buffer (ProbeDesc: 32 bytes per probe)
	var probe_bytes = PackedByteArray()
	probe_bytes.resize(probe_count * 32)

	for i in range(probe_count):
		var p = probes[i]
		var offset = i * 32
		var p_pos = p.position if ("position" in p) else Vector3.ZERO
		var p_norm = p.normal if ("normal" in p) else Vector3.UP
		probe_bytes.encode_float(offset + 0, p_pos.x)
		probe_bytes.encode_float(offset + 4, p_pos.y)
		probe_bytes.encode_float(offset + 8, p_pos.z)
		probe_bytes.encode_float(offset + 12, 0.0)
		probe_bytes.encode_float(offset + 16, p_norm.x)
		probe_bytes.encode_float(offset + 20, p_norm.y)
		probe_bytes.encode_float(offset + 24, p_norm.z)
		probe_bytes.encode_float(offset + 28, 0.0)

	probe_buffer_rid = rd.storage_buffer_create(probe_bytes.size(), probe_bytes)

	# 4. Build Output Irradiance Buffer (16 bytes per probe: vec4 RGB + Magnitude)
	var out_bytes = PackedByteArray()
	out_bytes.resize(probe_count * 16)
	output_buffer_rid = rd.storage_buffer_create(out_bytes.size(), out_bytes)

	# 5. Create Uniform Set
	var u_light = RDUniform.new()
	u_light.uniform_type = RenderingDevice.UNIFORM_TYPE_STORAGE_BUFFER
	u_light.binding = 0
	u_light.add_id(light_buffer_rid)

	var u_probe = RDUniform.new()
	u_probe.uniform_type = RenderingDevice.UNIFORM_TYPE_STORAGE_BUFFER
	u_probe.binding = 1
	u_probe.add_id(probe_buffer_rid)

	var u_out = RDUniform.new()
	u_out.uniform_type = RenderingDevice.UNIFORM_TYPE_STORAGE_BUFFER
	u_out.binding = 2
	u_out.add_id(output_buffer_rid)

	uniform_set_rid = rd.uniform_set_create([u_light, u_probe, u_out], shader_rid, 0)
	is_initialized = true
	return true

func dispatch_gpu_eval(time_sec: float, anim_mode: int) -> PackedColorArray:
	if not is_initialized or not has_rd:
		return _cpu_eval_fallback(time_sec, anim_mode)

	# 1. Build Push Constants (16 bytes)
	var push_constants = PackedByteArray()
	push_constants.resize(16)
	push_constants.encode_u32(0, probe_count)
	push_constants.encode_u32(4, total_lights)
	push_constants.encode_float(8, time_sec)
	push_constants.encode_u32(12, anim_mode)

	# 2. Dispatch Compute List on GPU
	var compute_list = rd.compute_list_begin()
	rd.compute_list_bind_compute_pipeline(compute_list, pipeline_rid)
	rd.compute_list_bind_uniform_set(compute_list, uniform_set_rid, 0)
	rd.compute_list_set_push_constant(compute_list, push_constants, push_constants.size())

	var groups_x = int(ceil(float(probe_count) / 64.0))
	rd.compute_list_dispatch(compute_list, groups_x, 1, 1)
	rd.compute_list_end()

	# 3. Retrieve evaluated probe colors from GPU
	var out_data = rd.buffer_get_data(output_buffer_rid)
	var colors = PackedColorArray()
	colors.resize(probe_count)

	for i in range(probe_count):
		var offset = i * 16
		var r = out_data.decode_float(offset + 0)
		var g = out_data.decode_float(offset + 4)
		var b = out_data.decode_float(offset + 8)
		colors[i] = Color(r, g, b, 1.0)

	return colors

func _cpu_eval_fallback(time_sec: float, anim_mode: int) -> PackedColorArray:
	var colors = PackedColorArray()
	colors.resize(probe_count)
	for i in range(probe_count):
		var h = fposmod(float(i) * 0.05 + time_sec * 0.2, 1.0)
		colors[i] = Color.from_hsv(h, 0.8, 1.0)
	return colors

func cleanup() -> void:
	if has_rd and rd != null:
		if uniform_set_rid.is_valid(): rd.free_rid(uniform_set_rid)
		if output_buffer_rid.is_valid(): rd.free_rid(output_buffer_rid)
		if probe_buffer_rid.is_valid(): rd.free_rid(probe_buffer_rid)
		if light_buffer_rid.is_valid(): rd.free_rid(light_buffer_rid)
		if pipeline_rid.is_valid(): rd.free_rid(pipeline_rid)
		if shader_rid.is_valid(): rd.free_rid(shader_rid)
	is_initialized = false
