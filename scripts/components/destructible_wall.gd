class_name DestructibleWall
extends Node3D

signal chunk_destroyed(chunk_id: int, bounds: AABB)

@export var wall_id: int = 0
@export var grid_width: int = 3
@export var grid_height: int = 3
@export var chunk_size: Vector3 = Vector3(1.2, 1.2, 0.25)
@export var wall_color: Color = Color(0.75, 0.72, 0.68)

var chunks: Array[DestructibleChunk] = []

func _ready() -> void:
	_build_wall_chunks()

func _build_wall_chunks() -> void:
	# Clear existing children
	for child in get_children():
		child.queue_free()
	chunks.clear()
	
	var box_mesh = BoxMesh.new()
	box_mesh.size = chunk_size
	
	var mat = StandardMaterial3D.new()
	mat.albedo_color = wall_color
	mat.roughness = 0.8
	
	var box_shape = BoxShape3D.new()
	box_shape.size = chunk_size
	
	var total_w = grid_width * chunk_size.x
	var total_h = grid_height * chunk_size.y
	var start_x = -total_w * 0.5 + chunk_size.x * 0.5
	var start_y = chunk_size.y * 0.5
	
	var id_counter = wall_id * 100
	
	for y in range(grid_height):
		for x in range(grid_width):
			var chunk = DestructibleChunk.new()
			chunk.chunk_id = id_counter
			chunk.position = Vector3(start_x + x * chunk_size.x, start_y + y * chunk_size.y, 0)
			
			var mi = MeshInstance3D.new()
			mi.mesh = box_mesh
			mi.material_override = mat
			chunk.add_child(mi)
			
			var cs = CollisionShape3D.new()
			cs.shape = box_shape
			chunk.add_child(cs)
			
			add_child(chunk)
			chunks.append(chunk)
			
			chunk.chunk_destroyed.connect(func(c_id, bounds): chunk_destroyed.emit(c_id, bounds))
			id_counter += 1

func destroy_chunk_by_id(c_id: int) -> void:
	for chunk in chunks:
		if chunk.chunk_id == c_id:
			chunk.destroy()
			return

func destroy_center_chunk() -> void:
	var center_idx = chunks.size() / 2
	if center_idx < chunks.size():
		chunks[center_idx].destroy()

func destroy_all_chunks() -> void:
	for chunk in chunks:
		chunk.destroy()

func restore_all_chunks() -> void:
	for chunk in chunks:
		chunk.restore()
