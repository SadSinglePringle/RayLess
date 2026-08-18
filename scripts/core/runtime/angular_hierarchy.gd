class_name AngularHierarchy
extends RefCounted

var light_id: int = -1
var light_node: Light3D
var ray_tracer: TransportRayTracer

var cells: Array[AngularCell] = []
var leaf_cell_indices: Array[int] = []
var transport_nodes: Array[TransportNode] = []

var max_subdivision_depth: int = 4 # Up to 16x16 = 256 angular cells for boundaries
var distance_variation_threshold: float = 1.2
var normal_variation_threshold: float = 0.45 # ~30 degrees

func build_hierarchy(p_light_id: int, p_light_node: Light3D, p_ray_tracer: TransportRayTracer) -> void:
	light_id = p_light_id
	light_node = p_light_node
	ray_tracer = p_ray_tracer
	
	cells.clear()
	leaf_cell_indices.clear()
	transport_nodes.clear()
	
	var root_cell = AngularCell.new()
	root_cell.id = 0
	root_cell.light_id = light_id
	root_cell.uv_min = Vector2.ZERO
	root_cell.uv_max = Vector2.ONE
	root_cell.parent_id = -1
	root_cell.depth = 0
	root_cell.compute_approx_solid_angle()
	cells.append(root_cell)
	
	_subdivide_cell(0)
	print("[AngularHierarchy] Light %d (%s): Built %d cells (%d leaf funnels, %d transport nodes)." % [
		light_id, light_node.name, cells.size(), leaf_cell_indices.size(), transport_nodes.size()
	])

func _subdivide_cell(cell_idx: int) -> void:
	var cell: AngularCell = cells[cell_idx]
	var samples = _evaluate_cell_samples(cell)
	
	var should_split = false
	if cell.depth < max_subdivision_depth:
		should_split = _should_subdivide(samples)
		
	if should_split:
		cell.state = GIEnums.CellState.SUBDIVIDED
		var u_mid = (cell.uv_min.x + cell.uv_max.x) * 0.5
		var v_mid = (cell.uv_min.y + cell.uv_max.y) * 0.5
		
		var quads = [
			[cell.uv_min, Vector2(u_mid, v_mid)],
			[Vector2(u_mid, cell.uv_min.y), Vector2(cell.uv_max.x, v_mid)],
			[Vector2(cell.uv_min.x, v_mid), Vector2(u_mid, cell.uv_max.y)],
			[Vector2(u_mid, v_mid), cell.uv_max]
		]
		
		for q in quads:
			var child = AngularCell.new()
			child.id = cells.size()
			child.light_id = light_id
			child.uv_min = q[0]
			child.uv_max = q[1]
			child.parent_id = cell.id
			child.depth = cell.depth + 1
			child.compute_approx_solid_angle()
			
			cell.child_indices.append(child.id)
			cells.append(child)
			_subdivide_cell(child.id)
	else:
		# Finalize leaf cell and generate Direct Transport Node (Bounce 0)
		cell.state = GIEnums.CellState.LEAF
		leaf_cell_indices.append(cell.id)
		_create_transport_node_for_cell(cell, samples)

func _evaluate_cell_samples(cell: AngularCell) -> Array[Dictionary]:
	var results: Array[Dictionary] = []
	var light_pos = light_node.global_position if light_node.is_inside_tree() else light_node.position
	var light_xform = light_node.global_transform if light_node.is_inside_tree() else light_node.transform
	
	if light_node is DirectionalLight3D:
		# For directional sun, trace parallel rays over the scene bounding footprint
		var light_dir = -light_xform.basis.z.normalized()
		var tangent = light_xform.basis.x.normalized()
		var bitangent = light_xform.basis.y.normalized()
		var scene_center = Vector3(0.0, 2.0, 0.0)
		var extent_x = 12.0
		var extent_y = 12.0
		
		var sample_uvs = [cell.get_center_uv()]
		for c_uv in cell.get_corner_uvs():
			sample_uvs.append(c_uv)
			
		for uv in sample_uvs:
			var u_off = (uv.x - 0.5) * 2.0 * extent_x
			var v_off = (uv.y - 0.5) * 2.0 * extent_y
			var ray_start = scene_center - light_dir * 20.0 + tangent * u_off + bitangent * v_off
			var ray_end = ray_start + light_dir * 45.0
			var hit = ray_tracer.trace_segment(ray_start, ray_end)
			results.append(hit)
			
		return results
		
	# Point / Omni / Spot light: sample center and 4 corners in spherical octahedral domain
	var test_dirs = [cell.get_center_direction()]
	for c_dir in cell.get_corner_directions():
		test_dirs.append(c_dir)
		
	for dir in test_dirs:
		var ray_start = light_pos
		var ray_end = light_pos + dir * (light_node.omni_range if light_node is OmniLight3D else 25.0)
		var hit = ray_tracer.trace_segment(ray_start, ray_end)
		results.append(hit)
		
	return results

func _should_subdivide(samples: Array[Dictionary]) -> bool:
	if samples.is_empty():
		return false
		
	var first_hit = samples[0].hit
	var first_chunk = samples[0].chunk_id
	var first_pos = samples[0].position
	var first_norm = samples[0].normal
	
	for i in range(1, samples.size()):
		var s = samples[i]
		# 1. Hit vs Miss disagreement (silhouette boundary)
		if s.hit != first_hit:
			return true
		if not s.hit:
			continue
			
		# 2. Destruction chunk disagreement (destructible boundary)
		if s.chunk_id != first_chunk:
			return true
			
		# 3. Large normal variation (corner boundary)
		if first_norm.dot(s.normal) < (1.0 - normal_variation_threshold):
			return true
			
		# 4. Large depth discontinuity
		if first_pos.distance_to(s.position) > distance_variation_threshold:
			return true
			
	return false

func _create_transport_node_for_cell(cell: AngularCell, samples: Array[Dictionary]) -> void:
	var rep_sample = samples[0]
	if not rep_sample.hit:
		return
		
	var node = TransportNode.new()
	node.id = transport_nodes.size()
	node.source_id = light_id
	node.parent_node_id = -1
	node.angular_cell_id = cell.id
	node.bounce_depth = 0 # Bounce 0: Direct Transport
	
	node.hit_position = rep_sample.position
	node.hit_normal = rep_sample.normal
	var dir_to_light = Vector3.UP
	var atten = 1.0
	
	if light_node is DirectionalLight3D:
		node.incoming_direction = -light_node.global_transform.basis.z.normalized()
		dir_to_light = -node.incoming_direction
		atten = 1.0
	else:
		node.incoming_direction = (node.hit_position - light_node.global_position).normalized()
		dir_to_light = -node.incoming_direction
		var dist = light_node.global_position.distance_to(node.hit_position)
		if light_node is OmniLight3D:
			var norm_dist = dist / light_node.omni_range
			atten = pow(clamp(1.0 - norm_dist * norm_dist, 0.0, 1.0), 2.0) / (1.0 + dist * dist)
		elif light_node is SpotLight3D:
			var norm_dist = dist / light_node.spot_range
			atten = pow(clamp(1.0 - norm_dist * norm_dist, 0.0, 1.0), 2.0) / (1.0 + dist * dist)
			
	var NdotL = max(0.0, node.hit_normal.dot(dir_to_light))
	node.destruction_chunk_id = rep_sample.chunk_id
	node.solid_angle = cell.solid_angle
	node.transport_weight = NdotL * atten * (cell.solid_angle if not (light_node is DirectionalLight3D) else 1.0)
	node.state = GIEnums.NodeState.VALID
	
	cell.transport_node_id = node.id
	transport_nodes.append(node)

func retrace_leaf_cell(cell_idx: int) -> TransportNode:
	if cell_idx < 0 or cell_idx >= cells.size():
		return null
	var cell = cells[cell_idx]
	var samples = _evaluate_cell_samples(cell)
	
	if cell.transport_node_id >= 0 and cell.transport_node_id < transport_nodes.size():
		var node = transport_nodes[cell.transport_node_id]
		var rep = samples[0]
		if rep.hit:
			node.hit_position = rep.position
			node.hit_normal = rep.normal
			node.destruction_chunk_id = rep.chunk_id
			
			var dir_to_light = Vector3.UP
			var atten = 1.0
			if light_node is DirectionalLight3D:
				node.incoming_direction = -light_node.global_transform.basis.z.normalized()
				dir_to_light = -node.incoming_direction
			else:
				node.incoming_direction = (node.hit_position - light_node.global_position).normalized()
				dir_to_light = -node.incoming_direction
				var dist = light_node.global_position.distance_to(node.hit_position)
				if light_node is OmniLight3D:
					var norm_dist = dist / light_node.omni_range
					atten = pow(clamp(1.0 - norm_dist * norm_dist, 0.0, 1.0), 2.0) / (1.0 + dist * dist)
				elif light_node is SpotLight3D:
					var norm_dist = dist / light_node.spot_range
					atten = pow(clamp(1.0 - norm_dist * norm_dist, 0.0, 1.0), 2.0) / (1.0 + dist * dist)
					
			var NdotL = max(0.0, node.hit_normal.dot(dir_to_light))
			node.transport_weight = NdotL * atten * (cell.solid_angle if not (light_node is DirectionalLight3D) else 1.0)
			node.state = GIEnums.NodeState.REGROWN
			cell.state = GIEnums.CellState.REGROWN
			return node
		else:
			node.state = GIEnums.NodeState.INVALID
			cell.state = GIEnums.CellState.INVALID
			return null
	else:
		_create_transport_node_for_cell(cell, samples)
		if cell.transport_node_id >= 0 and cell.transport_node_id < transport_nodes.size():
			var node = transport_nodes[cell.transport_node_id]
			node.state = GIEnums.NodeState.REGROWN
			cell.state = GIEnums.CellState.REGROWN
			return node
	return null
