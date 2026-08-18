class_name TransportNode
extends RefCounted

var id: int = -1
var generation: int = 0

var source_id: int = -1          # Light ID (for bounce 0) or parent Node ID (for bounce 1)
var parent_node_id: int = -1
var incoming_parent_ids: Array[int] = [] # Multiple incoming edges for DAG nodes
var angular_cell_id: int = -1

var surface_cluster_id: int = -1
var primitive_id: int = -1
var destruction_chunk_id: int = -1
var blocking_chunk_id: int = -1

var hit_position: Vector3 = Vector3.ZERO
var hit_normal: Vector3 = Vector3.UP
var incoming_direction: Vector3 = Vector3.UP

var solid_angle: float = 0.0
var transport_weight: float = 1.0

var child_node_ids: Array[int] = []
var bounce_depth: int = 0         # 0 = Direct transport, 1 = First diffuse bounce

var confidence: float = 1.0
var state: int = GIEnums.NodeState.VALID
var flags: int = 0

var albedo: Color = Color(0.8, 0.8, 0.8)
var direct_radiance: Color = Color.BLACK
var indirect_radiance: Color = Color.BLACK

func is_active() -> bool:
	return state == GIEnums.NodeState.VALID or state == GIEnums.NodeState.REGROWN

func get_total_radiance() -> Color:
	return direct_radiance + indirect_radiance

func get_effective_weight() -> float:
	return transport_weight * confidence if is_active() else 0.0
