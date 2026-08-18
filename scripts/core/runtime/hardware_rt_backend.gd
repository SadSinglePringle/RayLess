class_name HardwareRTBackend
extends RefCounted

# ==============================================================================
# HARDWARE RT CORE BACKEND (NVIDIA RTX 4070 DXR 1.1)
# ==============================================================================

const BridgeScript = preload("res://scripts/core/runtime/hardware_rt_bridge.gd")

var backend_name: String = "HardwareRT (NVIDIA DXR 1.1 RT Cores)"
var total_triangles: int = 0
var is_initialized: bool = false
var hardware_bridge: RefCounted = null

func _init() -> void:
	hardware_bridge = BridgeScript.new()

func initialize(mesh_nodes: Array[Node3D], p_world_3d: World3D) -> bool:
	if hardware_bridge == null:
		hardware_bridge = BridgeScript.new()
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
