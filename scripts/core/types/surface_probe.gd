class_name SurfaceProbe
extends RefCounted

var id: int = -1
var position: Vector3 = Vector3.ZERO
var normal: Vector3 = Vector3.UP
var albedo: Color = Color.WHITE
var area: float = 1.0
var mesh_id: int = -1
var primitive_id: int = -1
var chunk_id: int = -1          # -1 = static indestructible, >= 0 = destructible chunk
var destruction_chunk_id: int = -1
var surface_cluster_id: int = -1
var direct_radiance: Color = Color.BLACK
var indirect_radiance: Color = Color.BLACK
var confidence: float = 1.0     # [0.0, 1.0]
var incoming_edge_indices: PackedInt32Array = PackedInt32Array()
var outgoing_edge_indices: PackedInt32Array = PackedInt32Array()
var flags: int = GIEnums.ProbeFlags.VALID
var last_updated_frame: int = 0

func is_active() -> bool:
	return (flags & GIEnums.ProbeFlags.VALID) != 0

func get_total_radiance() -> Color:
	return direct_radiance + indirect_radiance

func reset_radiance() -> void:
	direct_radiance = Color.BLACK
	indirect_radiance = Color.BLACK

func duplicate_probe() -> SurfaceProbe:
	var p = SurfaceProbe.new()
	p.id = id
	p.position = position
	p.normal = normal
	p.albedo = albedo
	p.area = area
	p.mesh_id = mesh_id
	p.chunk_id = chunk_id
	p.direct_radiance = direct_radiance
	p.indirect_radiance = indirect_radiance
	p.confidence = confidence
	p.incoming_edge_indices = incoming_edge_indices.duplicate()
	p.outgoing_edge_indices = outgoing_edge_indices.duplicate()
	p.flags = flags
	p.last_updated_frame = last_updated_frame
	return p
