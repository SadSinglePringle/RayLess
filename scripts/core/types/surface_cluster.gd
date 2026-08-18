class_name SurfaceCluster
extends RefCounted

var id: int = -1
var mesh_id: int = -1
var destruction_chunk_id: int = -1
var material_id: int = -1

var centroid: Vector3 = Vector3.ZERO
var average_normal: Vector3 = Vector3.UP
var bounds: AABB = AABB()

var probe_ids: Array[int] = []
var adjacent_cluster_ids: Array[int] = []
var adjacency_types: Dictionary = {} # int (cluster_id) -> GIEnums.ClusterAdjacencyType
var albedo: Color = Color(0.8, 0.8, 0.8)

func contains_point(p: Vector3, tolerance: float = 0.5) -> bool:
	return bounds.grow(tolerance).has_point(p)

func add_adjacent_cluster(other_id: int, adj_type: int) -> void:
	if not adjacent_cluster_ids.has(other_id):
		adjacent_cluster_ids.append(other_id)
	adjacency_types[other_id] = adj_type

func get_adjacency_type(other_id: int) -> int:
	if adjacency_types.has(other_id):
		return adjacency_types[other_id]
	return GIEnums.ClusterAdjacencyType.DISCONNECTED
