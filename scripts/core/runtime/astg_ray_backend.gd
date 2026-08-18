class_name ASTGRayBackend
extends RefCounted

# ==============================================================================
# ASTG UNIFIED RAY TRACING BACKEND ABSTRACTION
# Dispatches geometric queries to either CPU Reference BVH or Hardware RT Cores.
# Graph logic, sampling, and probe deposition operate identically across backends.
# ==============================================================================

var backend_name: String = "ASTGRayBackend"
var total_triangles: int = 0
var is_initialized: bool = false

func initialize(mesh_nodes: Array[Node3D], world_3d: World3D) -> bool:
	return false

func trace_closest(ray: Dictionary) -> Dictionary:
	return {
		"hit": false,
		"distance": -1.0,
		"position": ray.get("origin", Vector3.ZERO) + ray.get("dir", Vector3.FORWARD) * ray.get("t_max", 1000.0),
		"normal": Vector3.UP,
		"chunk_id": -1,
		"primitive_id": -1,
		"mesh_id": -1,
		"surface_cluster_id": -1,
		"material_id": -1,
		"collider": null
	}

func trace_occlusion(ray: Dictionary) -> bool:
	var hit = trace_closest(ray)
	return hit.hit

func trace_batch(rays: Array[Dictionary]) -> Array[Dictionary]:
	var results: Array[Dictionary] = []
	for r in rays:
		results.append(trace_closest(r))
	return results

func destroy_chunk(chunk_id: int) -> void:
	pass

func restore_chunk(chunk_id: int) -> void:
	pass

func get_last_timings() -> Dictionary:
	return {
		"as_update_ms": 0.0,
		"ray_generation_ms": 0.0,
		"rt_traversal_ms": 0.0,
		"hit_processing_ms": 0.0,
		"total_gpu_ms": 0.0,
		"rays_traced": 0,
		"hits_recorded": 0
	}

func is_hardware() -> bool:
	return false


# ==============================================================================
# SOFTWARE BVH REFERENCE BACKEND (CPU Deterministic Oracle)
# ==============================================================================
class SoftwareBVHBackend extends RefCounted:
	var backend_name: String = "SoftwareBVH (CPU Reference)"
	var total_triangles: int = 0
	var is_initialized: bool = false
	var world_3d: World3D
	var _destroyed_chunks: Dictionary = {}

	func initialize(mesh_nodes: Array[Node3D], p_world_3d: World3D) -> bool:
		world_3d = p_world_3d
		_destroyed_chunks.clear()
		total_triangles = 0
		for node in mesh_nodes:
			_count_triangles(node)
		is_initialized = true
		return true

	func _count_triangles(node: Node) -> void:
		if node is MeshInstance3D and node.mesh != null:
			var mesh: Mesh = node.mesh
			for s in range(mesh.get_surface_count()):
				var arrays = mesh.surface_get_arrays(s)
				if not arrays.is_empty() and arrays[Mesh.ARRAY_INDEX] != null:
					total_triangles += arrays[Mesh.ARRAY_INDEX].size() / 3
		for child in node.get_children():
			_count_triangles(child)

	func trace_closest(ray: Dictionary) -> Dictionary:
		var origin: Vector3 = ray.get("origin", Vector3.ZERO)
		var dir: Vector3 = ray.get("dir", Vector3.FORWARD).normalized()
		var t_min: float = max(0.0001, ray.get("t_min", 0.01))
		var t_max: float = ray.get("t_max", 1000.0)

		var from_pos = origin + dir * t_min
		var to_pos = origin + dir * t_max

		if world_3d == null or world_3d.direct_space_state == null:
			return _miss_result(to_pos)

		var query = PhysicsRayQueryParameters3D.create(from_pos, to_pos)
		query.collide_with_areas = true
		query.collide_with_bodies = true

		var res = world_3d.direct_space_state.intersect_ray(query)
		if res.is_empty():
			return _miss_result(to_pos)

		var collider = res.get("collider")
		var hit_chunk_id = -1
		if collider != null:
			if collider.has_meta("chunk_id"):
				hit_chunk_id = int(collider.get_meta("chunk_id"))
			elif collider.get_parent() != null and collider.get_parent().has_meta("chunk_id"):
				hit_chunk_id = int(collider.get_parent().get_meta("chunk_id"))

		if hit_chunk_id >= 0 and _destroyed_chunks.has(hit_chunk_id):
			return _miss_result(to_pos)

		var hit_pos: Vector3 = res.get("position")
		var hit_norm: Vector3 = res.get("normal", Vector3.UP)
		var dist = origin.distance_to(hit_pos)

		return {
			"hit": true,
			"distance": dist,
			"position": hit_pos,
			"normal": hit_norm,
			"chunk_id": hit_chunk_id,
			"primitive_id": 0,
			"mesh_id": 0,
			"surface_cluster_id": 0,
			"material_id": 0,
			"collider": collider
		}

	func trace_occlusion(ray: Dictionary) -> bool:
		var hit = trace_closest(ray)
		return hit.hit

	func trace_batch(rays: Array[Dictionary]) -> Array[Dictionary]:
		var results: Array[Dictionary] = []
		for r in rays:
			results.append(trace_closest(r))
		return results

	func destroy_chunk(chunk_id: int) -> void:
		_destroyed_chunks[chunk_id] = true

	func restore_chunk(chunk_id: int) -> void:
		_destroyed_chunks.erase(chunk_id)

	func get_last_timings() -> Dictionary:
		return {
			"as_update_ms": 0.0,
			"ray_generation_ms": 0.0,
			"rt_traversal_ms": 0.0,
			"hit_processing_ms": 0.0,
			"total_gpu_ms": 0.0,
			"rays_traced": 0,
			"hits_recorded": 0
		}

	func _miss_result(end_pos: Vector3) -> Dictionary:
		return {
			"hit": false,
			"distance": -1.0,
			"position": end_pos,
			"normal": Vector3.UP,
			"chunk_id": -1,
			"primitive_id": -1,
			"mesh_id": -1,
			"surface_cluster_id": -1,
			"material_id": -1,
			"collider": null
		}

	func is_hardware() -> bool:
		return false


# ==============================================================================
# HARDWARE RT CORE BACKEND (NVIDIA RTX 4070 DXR 1.1)
# ==============================================================================
class HardwareRTBackend extends RefCounted:
	var backend_name: String = "HardwareRT (NVIDIA DXR 1.1 RT Cores)"
	var total_triangles: int = 0
	var is_initialized: bool = false
	var hardware_bridge: HardwareRTBridge = null

	func _init() -> void:
		hardware_bridge = HardwareRTBridge.new()

	func initialize(mesh_nodes: Array[Node3D], p_world_3d: World3D) -> bool:
		if hardware_bridge == null:
			hardware_bridge = HardwareRTBridge.new()
		var success = hardware_bridge.build_scene_acceleration_structures(mesh_nodes, p_world_3d)
		total_triangles = hardware_bridge.total_triangles
		is_initialized = success
		return success

	func trace_closest(ray: Dictionary) -> Dictionary:
		var origin: Vector3 = ray.get("origin", Vector3.ZERO)
		var dir: Vector3 = ray.get("dir", Vector3.FORWARD).normalized()
		var t_min: float = ray.get("t_min", 0.01)
		var t_max: float = ray.get("t_max", 1000.0)
		return hardware_bridge.trace_single_ray(origin, dir, t_min, t_max)

	func trace_occlusion(ray: Dictionary) -> bool:
		var hit = trace_closest(ray)
		return hit.hit

	func trace_batch(rays: Array[Dictionary]) -> Array[Dictionary]:
		return hardware_bridge.trace_ray_batch(rays)

	func destroy_chunk(chunk_id: int) -> void:
		hardware_bridge.destroy_chunk(chunk_id)

	func restore_chunk(chunk_id: int) -> void:
		hardware_bridge.restore_chunk(chunk_id)

	func get_last_timings() -> Dictionary:
		return hardware_bridge.get_last_timings()

	func is_hardware() -> bool:
		return hardware_bridge != null and hardware_bridge.is_hardware_available
