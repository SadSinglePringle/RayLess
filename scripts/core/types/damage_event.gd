class_name DamageEvent
extends RefCounted

var chunk_id: int = -1
var mesh_id: int = -1
var bounds: AABB = AABB()
var center: Vector3 = Vector3.ZERO
var radius: float = 1.0
var timestamp_frame: int = 0
var event_type: String = "destroyed"

static func create_destruction(p_chunk_id: int, p_bounds: AABB, p_frame: int) -> DamageEvent:
	var evt = DamageEvent.new()
	evt.chunk_id = p_chunk_id
	evt.bounds = p_bounds
	evt.center = p_bounds.get_center()
	evt.radius = p_bounds.size.length() * 0.5
	evt.timestamp_frame = p_frame
	evt.event_type = "destroyed"
	return evt
