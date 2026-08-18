class_name DDGIBaseline
extends RefCounted

const TransportRayTracerScript = preload("res://scripts/core/runtime/transport_ray_tracer.gd")

class WorldProbe:
	var position: Vector3
	var irradiance: Color = Color.BLACK
	var prev_irradiance: Color = Color.BLACK
	var last_update_frame: int = 0

var probes: Array[WorldProbe] = []
var grid_bounds: AABB = AABB(Vector3(-10, 0, -10), Vector3(20, 8, 20))
var grid_dims: Vector3i = Vector3i(6, 4, 6)
var temporal_hysteresis: float = 0.85 # Standard DDGI temporal smoothing factor
var rays_per_probe_sample: int = 12
var ray_budget_per_frame: int = 240

var ray_tracer: RefCounted = null
var lights: Array[Light3D] = []
var world_3d: World3D
var frame_count: int = 0
var current_probe_cursor: int = 0
var rays_traced_this_frame: int = 0

func initialize(p_bounds: AABB, p_dims: Vector3i, light_nodes: Array[Light3D], p_world_3d: World3D) -> void:
	grid_bounds = p_bounds
	grid_dims = p_dims
	lights = light_nodes
	world_3d = p_world_3d
	ray_tracer = TransportRayTracerScript.new(world_3d)

	probes.clear()
	var step_x = grid_bounds.size.x / max(1, grid_dims.x - 1)
	var step_y = grid_bounds.size.y / max(1, grid_dims.y - 1)
	var step_z = grid_bounds.size.z / max(1, grid_dims.z - 1)

	for y in range(grid_dims.y):
		for z in range(grid_dims.z):
			for x in range(grid_dims.x):
				var p = WorldProbe.new()
				p.position = grid_bounds.position + Vector3(x * step_x, y * step_y, z * step_z)
				probes.append(p)

	print("[DDGI Baseline] Initialized %d world probes (%dx%dx%d grid)." % [probes.size(), grid_dims.x, grid_dims.y, grid_dims.z])

func process_frame(delta: float) -> Dictionary:
	var start_time = Time.get_ticks_usec()
	frame_count += 1
	rays_traced_this_frame = 0

	if probes.is_empty():
		return {"time_ms": 0.0, "rays_traced": 0, "probes_updated": 0}

	var probes_to_update = ray_budget_per_frame / rays_per_probe_sample
	var probes_updated_count = 0

	for _i in range(probes_to_update):
		var probe = probes[current_probe_cursor]
		_update_probe(probe)
		current_probe_cursor = (current_probe_cursor + 1) % probes.size()
		probes_updated_count += 1

	var elapsed_ms = (Time.get_ticks_usec() - start_time) / 1000.0
	return {
		"time_ms": elapsed_ms,
		"rays_traced": rays_traced_this_frame,
		"probes_updated": probes_updated_count,
		"total_probes": probes.size()
	}

func _update_probe(probe: WorldProbe) -> void:
	var accumulated_irradiance: Color = Color.BLACK

	# Fibonacci sphere ray sampling around probe
	var golden_ratio = (1.0 + sqrt(5.0)) / 2.0
	for r in range(rays_per_probe_sample):
		var theta = 2.0 * PI * float(r) / golden_ratio
		var phi = acos(1.0 - 2.0 * (float(r) + 0.5) / float(rays_per_probe_sample))
		var dir = Vector3(
			cos(theta) * sin(phi),
			sin(theta) * sin(phi),
			cos(phi)
		).normalized()

		rays_traced_this_frame += 1
		var hit_res = ray_tracer.trace_segment(probe.position, probe.position + dir * 15.0)

		if hit_res.hit:
			# Sample direct lights at hit location
			var hit_pos: Vector3 = hit_res.position
			var hit_norm: Vector3 = hit_res.normal
			for l in lights:
				if not l.visible or l.light_energy <= 0.001:
					continue

				if l is OmniLight3D:
					var to_l = l.global_position - hit_pos
					var d = to_l.length()
					if d < l.omni_range and d > 0.001:
						var l_dir = to_l / d
						var n_dot_l = max(0.0, hit_norm.dot(l_dir))
						if n_dot_l > 0.0:
							var shadow_hit = ray_tracer.trace_segment(hit_pos + hit_norm * 0.02, l.global_position)
							rays_traced_this_frame += 1
							if not shadow_hit.hit:
								var atten = pow(clamp(1.0 - d / l.omni_range, 0.0, 1.0), 2.0) / (1.0 + d * d)
								accumulated_irradiance += l.light_color * l.light_energy * n_dot_l * atten

				elif l is DirectionalLight3D:
					var l_dir = -l.global_transform.basis.z.normalized()
					var n_dot_l = max(0.0, hit_norm.dot(l_dir))
					if n_dot_l > 0.0:
						var shadow_hit = ray_tracer.trace_segment(hit_pos + hit_norm * 0.02, hit_pos + l_dir * 50.0)
						rays_traced_this_frame += 1
						if not shadow_hit.hit:
							accumulated_irradiance += l.light_color * l.light_energy * n_dot_l

	var new_irradiance = accumulated_irradiance / float(rays_per_probe_sample)

	# Exponential moving average / hysteresis
	probe.prev_irradiance = probe.irradiance
	probe.irradiance = probe.irradiance.lerp(new_irradiance, 1.0 - temporal_hysteresis)
	probe.last_update_frame = frame_count
