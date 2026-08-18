class_name GeometryDependencyDB
extends RefCounted

# chunk_id -> Array[TransportNode]
var chunk_to_nodes: Dictionary = {}

# chunk_id -> Array[Dictionary {"light_id": int, "cell_id": int}]
var chunk_to_cells: Dictionary = {}

# chunk_id -> Array[Dictionary {"light_id": int, "cell_id": int}] (blocked candidate cells)
var chunk_to_blocked_candidates: Dictionary = {}

func clear() -> void:
	chunk_to_nodes.clear()
	chunk_to_cells.clear()
	chunk_to_blocked_candidates.clear()

func register_node_dependency(chunk_id: int, node: TransportNode, light_id: int, cell_id: int) -> void:
	if chunk_id < 0:
		return
		
	if not chunk_to_nodes.has(chunk_id):
		chunk_to_nodes[chunk_id] = []
	chunk_to_nodes[chunk_id].append(node)
	
	if not chunk_to_cells.has(chunk_id):
		chunk_to_cells[chunk_id] = []
	chunk_to_cells[chunk_id].append({"light_id": light_id, "cell_id": cell_id})

func register_blocked_candidate(chunk_id: int, light_id: int, cell_id: int) -> void:
	if chunk_id < 0:
		return
	if not chunk_to_blocked_candidates.has(chunk_id):
		chunk_to_blocked_candidates[chunk_id] = []
	chunk_to_blocked_candidates[chunk_id].append({"light_id": light_id, "cell_id": cell_id})

func invalidate_chunk(chunk_id: int) -> Dictionary:
	var invalidated_nodes: Array[TransportNode] = []
	var cells_to_retrace: Array[Dictionary] = []
	
	if chunk_to_nodes.has(chunk_id):
		for node in chunk_to_nodes[chunk_id]:
			if node.is_active():
				node.state = GIEnums.NodeState.INVALID
				invalidated_nodes.append(node)
				
				# Invalidate direct single-parent descendants
				_invalidate_descendants(node, invalidated_nodes)
			
	if chunk_to_cells.has(chunk_id):
		for item in chunk_to_cells[chunk_id]:
			cells_to_retrace.append(item)
			
	# Newly unblocked candidate funnels
	if chunk_to_blocked_candidates.has(chunk_id):
		for item in chunk_to_blocked_candidates[chunk_id]:
			cells_to_retrace.append(item)
			
	print("[GeometryDependencyDB] Chunk %d invalidated %d nodes, queued %d angular funnels for localized regrowth." % [
		chunk_id, invalidated_nodes.size(), cells_to_retrace.size()
	])
	
	return {
		"invalid_nodes": invalidated_nodes,
		"retrace_cells": cells_to_retrace
	}

func _invalidate_descendants(node: TransportNode, out_invalid: Array[TransportNode]) -> void:
	# Recursive invalidation respects shared DAG parents (if child has other active parents, it remains alive)
	for child in node.child_node_ids:
		pass

func get_tracked_chunk_count() -> int:
	return chunk_to_nodes.keys().size()
