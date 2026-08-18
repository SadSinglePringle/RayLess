class_name TransportRayTracer
extends RefCounted

# ==============================================================================
# ASTG TRANSPORT RAY TRACER
# Routes high-level transport queries (angular funnels, diffuse bounces, repair)
# through the active backend (Software BVH or Hardware RT).
# ==============================================================================

const HardwareRTBackendScript = preload("res://scripts/core/runtime/hardware_rt_backend.gd")
const SoftwareBVHBackendScript = preload("res://scripts/core/runtime/software_bvh_backend.gd")

var world_3d: World3D
var backend: RefCounted = null
var rays_traced_this_frame: int = 0
var total_rays_traced: int = 0

# Backward compatibility properties
var hardware_rt_bridge: RefCounted:
	get:
		if backend != null and backend.has_method("is_hardware") and backend.is_hardware():
			return backend.get("hardware_bridge")
		return null
	set(val):
		if val != null:
			var hw = HardwareRTBackendScript.new()
			hw.hardware_bridge = val
			backend = hw

var use_hardware_rt_cores: bool:
	get:
		return backend != null and backend.has_method("is_hardware") and backend.is_hardware()
	set(val):
		if val:
			if backend == null or not backend.is_hardware():
				backend = HardwareRTBackendScript.new()
		else:
			if backend == null or backend.is_hardware():
				var sw = SoftwareBVHBackendScript.new()
				sw.world_3d = world_3d
				backend = sw

func _init(p_world_3d: World3D = null, p_backend: RefCounted = null) -> void:
	world_3d = p_world_3d
	if p_backend != null:
		backend = p_backend
	else:
		backend = HardwareRTBackendScript.new()

func reset_frame_counters() -> void:
	rays_traced_this_frame = 0

func set_backend(p_backend: RefCounted) -> void:
	backend = p_backend

func trace_segment(from_pos: Vector3, to_pos: Vector3, collision_mask: int = 1, exclude_rids: Array[RID] = []) -> Dictionary:
	rays_traced_this_frame += 1
	total_rays_traced += 1

	var delta = to_pos - from_pos
	var dist = delta.length()
	if dist <= 0.0001:
		return {"hit": false, "position": to_pos, "normal": Vector3.UP, "chunk_id": -1, "collider": null}

	var dir = delta / dist
	var ray_dict = {
		"origin": from_pos,
		"dir": dir,
		"t_min": 0.001,
		"t_max": dist
	}

	if backend != null and backend.has_method("trace_closest"):
		var hit_res = backend.trace_closest(ray_dict)
		if hit_res.hit and hit_res.distance <= dist:
			return {
				"hit": true,
				"position": hit_res.position,
				"normal": hit_res.normal,
				"chunk_id": hit_res.chunk_id,
				"primitive_id": hit_res.get("primitive_id", -1),
				"collider": hit_res.get("collider", null)
			}
		else:
			return {"hit": false, "position": to_pos, "normal": Vector3.UP, "chunk_id": -1, "collider": null}

	# CPU fallback if no backend is attached
	if world_3d == null or world_3d.direct_space_state == null:
		return {"hit": false, "position": to_pos, "normal": Vector3.UP, "chunk_id": -1, "collider": null}

	var query = PhysicsRayQueryParameters3D.create(from_pos, to_pos, collision_mask, exclude_rids)
	query.collide_with_areas = true
	query.collide_with_bodies = true

	var result = world_3d.direct_space_state.intersect_ray(query)
	if result.is_empty():
		return {"hit": false, "position": to_pos, "normal": Vector3.UP, "chunk_id": -1, "collider": null}

	var collider = result.get("collider")
	var hit_chunk_id = -1
	if collider != null:
		if collider.has_meta("chunk_id"):
			hit_chunk_id = int(collider.get_meta("chunk_id"))
		elif collider.get_parent() != null and collider.get_parent().has_meta("chunk_id"):
			hit_chunk_id = int(collider.get_parent().get_meta("chunk_id"))

	return {
		"hit": true,
		"position": result.get("position"),
		"normal": result.get("normal"),
		"chunk_id": hit_chunk_id,
		"primitive_id": 0,
		"collider": collider
	}

func trace_batch(rays: Array[Dictionary]) -> Array[Dictionary]:
	if rays.is_empty():
		return []

	rays_traced_this_frame += rays.size()
	total_rays_traced += rays.size()

	if backend != null and backend.has_method("trace_batch"):
		return backend.trace_batch(rays)

	var results: Array[Dictionary] = []
	for r in rays:
		var from_pos = r.get("origin", Vector3.ZERO)
		var dir = r.get("dir", Vector3.FORWARD).normalized()
		var dist = r.get("t_max", 1000.0)
		results.append(trace_segment(from_pos, from_pos + dir * dist))
	return results

func trace_light_to_probe(light_node: Light3D, probe: SurfaceProbe, max_sun_dist: float = 100.0) -> Dictionary:
	var probe_pos = probe.position
	var probe_norm = probe.normal

	if light_node is DirectionalLight3D:
		var light_dir = -light_node.global_transform.basis.z.normalized()
		var dir_to_light = -light_dir
		var NdotL = max(0.0, probe_norm.dot(dir_to_light))
		if NdotL <= 0.0001:
			return {"visible": false, "form_factor": 0.0, "blocking_chunk_id": -1, "direction": dir_to_light, "distance": max_sun_dist}

		var ray_start = probe_pos
		var ray_end = probe_pos + dir_to_light * max_sun_dist
		var hit_res = trace_segment(ray_start, ray_end)

		if not hit_res.hit:
			var form_factor = NdotL
			return {"visible": true, "form_factor": form_factor, "blocking_chunk_id": -1, "direction": dir_to_light, "distance": max_sun_dist, "color": light_node.light_color}
		else:
			return {"visible": false, "form_factor": 0.0, "blocking_chunk_id": hit_res.chunk_id, "direction": dir_to_light, "distance": max_sun_dist}

	elif light_node is OmniLight3D:
		var light_pos = light_node.global_position
		var to_light = light_pos - probe_pos
		var dist = to_light.length()
		var omni_range = light_node.omni_range
		if dist > omni_range or dist < 0.001:
			return {"visible": false, "form_factor": 0.0, "blocking_chunk_id": -1, "direction": Vector3.UP, "distance": dist}

		var dir_to_light = to_light / dist
		var NdotL = max(0.0, probe_norm.dot(dir_to_light))
		if NdotL <= 0.0001:
			return {"visible": false, "form_factor": 0.0, "blocking_chunk_id": -1, "direction": dir_to_light, "distance": dist}

		var ray_start = probe_pos
		var ray_end = light_pos - dir_to_light * 0.05
		var hit_res = trace_segment(ray_start, ray_end)

		var norm_dist = dist / omni_range
		var atten = pow(clamp(1.0 - norm_dist * norm_dist, 0.0, 1.0), 2.0) / (1.0 + dist * dist)
		var form_factor = NdotL * atten

		if not hit_res.hit:
			return {"visible": true, "form_factor": form_factor, "blocking_chunk_id": -1, "direction": dir_to_light, "distance": dist, "color": light_node.light_color}
		else:
			return {"visible": false, "form_factor": 0.0, "blocking_chunk_id": hit_res.chunk_id, "direction": dir_to_light, "distance": dist}

	elif light_node is SpotLight3D:
		var light_pos = light_node.global_position
		var to_light = light_pos - probe_pos
		var dist = to_light.length()
		var spot_range = light_node.spot_range
		if dist > spot_range or dist < 0.001:
			return {"visible": false, "form_factor": 0.0, "blocking_chunk_id": -1, "direction": Vector3.UP, "distance": dist}

		var dir_to_light = to_light / dist
		var NdotL = max(0.0, probe_norm.dot(dir_to_light))
		if NdotL <= 0.0001:
			return {"visible": false, "form_factor": 0.0, "blocking_chunk_id": -1, "direction": dir_to_light, "distance": dist}

		var spot_forward = -light_node.global_transform.basis.z.normalized()
		var cos_angle = (-dir_to_light).dot(spot_forward)
		var spot_angle_rad = deg_to_rad(light_node.spot_angle)
		if cos_angle < cos(spot_angle_rad):
			return {"visible": false, "form_factor": 0.0, "blocking_chunk_id": -1, "direction": dir_to_light, "distance": dist}

		var ray_start = probe_pos
		var ray_end = light_pos - dir_to_light * 0.05
		var hit_res = trace_segment(ray_start, ray_end)

		var norm_dist = dist / spot_range
		var atten = pow(clamp(1.0 - norm_dist * norm_dist, 0.0, 1.0), 2.0) / (1.0 + dist * dist)
		var form_factor = NdotL * atten

		if not hit_res.hit:
			return {"visible": true, "form_factor": form_factor, "blocking_chunk_id": -1, "direction": dir_to_light, "distance": dist, "color": light_node.light_color}
		else:
			return {"visible": false, "form_factor": 0.0, "blocking_chunk_id": hit_res.chunk_id, "direction": dir_to_light, "distance": dist}

	return {"visible": false, "form_factor": 0.0, "blocking_chunk_id": -1, "direction": Vector3.UP, "distance": 1.0}

func trace_probe_to_probe(src_probe: SurfaceProbe, dst_probe: SurfaceProbe) -> Dictionary:
	var to_dst = dst_probe.position - src_probe.position
	var dist = to_dst.length()
	if dist < 0.05:
		return {"visible": false, "form_factor": 0.0, "blocking_chunk_id": -1, "distance": dist}

	var dir = to_dst / dist
	var cos_src = src_probe.normal.dot(dir)
	var cos_dst = dst_probe.normal.dot(-dir)

	if cos_src <= 0.01 or cos_dst <= 0.01:
		return {"visible": false, "form_factor": 0.0, "blocking_chunk_id": -1, "distance": dist}

	var geom_term = (cos_src * cos_dst) / (PI * (dist * dist + 0.2))
	var form_factor = geom_term * src_probe.area

	var ray_start = src_probe.position
	var ray_end = dst_probe.position
	var hit_res = trace_segment(ray_start, ray_end)

	var hit_dist = ray_start.distance_to(hit_res.position) if hit_res.hit else dist
	if hit_res.hit and hit_dist < dist - 0.1:
		return {"visible": false, "form_factor": 0.0, "blocking_chunk_id": hit_res.chunk_id, "direction": dir, "distance": dist}
	else:
		return {"visible": true, "form_factor": form_factor, "blocking_chunk_id": -1, "direction": dir, "distance": dist}
