class_name DestructibleChunk
extends StaticBody3D

signal chunk_destroyed(chunk_id: int, bounds: AABB)

@export var chunk_id: int = 0
var is_destroyed: bool = false
var mesh_instance: MeshInstance3D
var collision_shape: CollisionShape3D

func _ready() -> void:
	set_meta("chunk_id", chunk_id)
	for child in get_children():
		if child is MeshInstance3D:
			mesh_instance = child
			mesh_instance.set_meta("chunk_id", chunk_id)
		elif child is CollisionShape3D:
			collision_shape = child

func get_world_bounds() -> AABB:
	if mesh_instance != null:
		return global_transform * mesh_instance.get_aabb()
	return AABB(global_position - Vector3(0.5, 0.5, 0.5), Vector3(1, 1, 1))

func destroy() -> void:
	if is_destroyed:
		return
	is_destroyed = true
	var bounds = get_world_bounds()
	visible = false
	if collision_shape != null:
		collision_shape.disabled = true
	chunk_destroyed.emit(chunk_id, bounds)

func restore() -> void:
	is_destroyed = false
	visible = true
	if collision_shape != null:
		collision_shape.disabled = false
