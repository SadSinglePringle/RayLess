class_name GroundTruthReference
extends RefCounted

var ray_tracer: TransportRayTracer
var lights: Array[Light3D] = []
var world_3d: World3D
var reference_probe_radiance: Dictionary = {} # probe_id -> Color
var samples_per_probe: int = 4

func initialize(light_nodes: Array[Light3D], p_world_3d: World3D) -> void:
	lights = light_nodes
	world_3d = p_world_3d
	ray_tracer = TransportRayTracer.new(world_3d)

func compute_reference_solve(probes: Array[SurfaceProbe]) -> Dictionary:
	var t_start = Time.get_ticks_usec()
	ray_tracer.reset_frame_counters()
	reference_probe_radiance.clear()
	
	for probe in probes:
		# 1. Direct Lighting
		var direct_rad = Color.BLACK
		for light in lights:
			if light.visible:
				var res = ray_tracer.trace_light_to_probe(light, probe)
				if res.visible:
					direct_rad += light.light_color * light.light_energy * res.form_factor
					
		# 2. Multi-sample indirect diffuse bounce
		var indirect_rad = Color.BLACK
		var u_norm = probe.normal
		var tangent = Vector3.UP.cross(u_norm)
		if tangent.length_squared() < 0.001:
			tangent = Vector3.RIGHT.cross(u_norm)
		tangent = tangent.normalized()
		var bitangent = u_norm.cross(tangent).normalized()
		
		for s in range(samples_per_probe):
			var r1 = randf()
			var r2 = randf()
			var theta = acos(sqrt(1.0 - r1))
			var phi = 2.0 * PI * r2
			
			var local_dir = Vector3(sin(theta) * cos(phi), cos(theta), sin(theta) * sin(phi))
			var world_dir = (tangent * local_dir.x + u_norm * local_dir.y + bitangent * local_dir.z).normalized()
			
			var ray_start = probe.position
			var ray_end = ray_start + world_dir * 20.0
			var hit_res = ray_tracer.trace_segment(ray_start, ray_end)
			
			if hit_res.hit:
				var hit_pos = hit_res.position
				var hit_norm = hit_res.normal
				var hit_albedo = Color(0.8, 0.8, 0.8)
				var collider = hit_res.get("collider")
				if collider != null:
					for ch in collider.get_children():
						if ch is MeshInstance3D and ch.material_override != null:
							var m = ch.material_override
							if m is StandardMaterial3D:
								hit_albedo = m.albedo_color
							elif m is ShaderMaterial and m.get_shader_parameter("albedo") != null:
								hit_albedo = m.get_shader_parameter("albedo")
								
				var hit_probe_dummy = SurfaceProbe.new()
				hit_probe_dummy.position = hit_pos + hit_norm * 0.02
				hit_probe_dummy.normal = hit_norm
				
				var bounce_direct = Color.BLACK
				for light in lights:
					if light.visible:
						var l_res = ray_tracer.trace_light_to_probe(light, hit_probe_dummy)
						if l_res.visible:
							bounce_direct += light.light_color * light.light_energy * l_res.form_factor
				indirect_rad += bounce_direct * hit_albedo * probe.albedo * (1.0 / float(samples_per_probe))
				
		reference_probe_radiance[probe.id] = direct_rad + indirect_rad
		
	var t_end = Time.get_ticks_usec()
	return {
		"time_ms": (t_end - t_start) / 1000.0,
		"rays_traced": ray_tracer.rays_traced_this_frame,
		"probes_solved": probes.size()
	}

func compute_metrics_vs_reference(probes: Array[SurfaceProbe]) -> Dictionary:
	if reference_probe_radiance.is_empty():
		return {"mse": 0.0, "psnr": 100.0, "t90_converged_ratio": 1.0}
		
	var mse = 0.0
	var count = 0
	var t90_converged = 0
	
	for probe in probes:
		if not reference_probe_radiance.has(probe.id):
			continue
		var ref_val: Color = reference_probe_radiance[probe.id]
		var cur_val: Color = probe.get_total_radiance()
		
		var diff_r = cur_val.r - ref_val.r
		var diff_g = cur_val.g - ref_val.g
		var diff_b = cur_val.b - ref_val.b
		mse += (diff_r * diff_r + diff_g * diff_g + diff_b * diff_b) / 3.0
		count += 1
		
		# Check T90 (within 10% of reference value)
		var ref_lum = ref_val.get_luminance()
		var cur_lum = cur_val.get_luminance()
		if ref_lum < 0.01:
			if cur_lum < 0.03:
				t90_converged += 1
		else:
			if cur_lum >= ref_lum * 0.9 and cur_lum <= ref_lum * 1.1:
				t90_converged += 1
				
	if count > 0:
		mse /= float(count)
		var psnr = 100.0 if mse < 0.000001 else 10.0 * (log(1.0 / mse) / log(10.0))
		var ratio = float(t90_converged) / float(count)
		return {"mse": mse, "psnr": psnr, "t90_converged_ratio": ratio}
		
	return {"mse": 0.0, "psnr": 100.0, "t90_converged_ratio": 1.0}
