class_name ASTGDebugRenderer
extends Node3D

var current_view_mode: int = GIEnums.DebugViewMode.NONE
var imm_mesh: ImmediateMesh
var mesh_inst: MeshInstance3D
var debug_mat: StandardMaterial3D

func _ready() -> void:
	imm_mesh = ImmediateMesh.new()
	mesh_inst = MeshInstance3D.new()
	mesh_inst.mesh = imm_mesh
	
	debug_mat = StandardMaterial3D.new()
	debug_mat.shading_mode = StandardMaterial3D.SHADING_MODE_UNSHADED
	debug_mat.vertex_color_use_as_albedo = true
	debug_mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	mesh_inst.material_override = debug_mat
	
	add_child(mesh_inst)

func clear() -> void:
	if imm_mesh != null:
		imm_mesh.clear_surfaces()

func update_debug_visuals(
	probes: Array[SurfaceProbe],
	bounce0_nodes: Array[TransportNode],
	bounce1_nodes: Array[TransportNode],
	lights: Array[Light3D],
	gt_radiance_map: Dictionary = {}
) -> void:
	imm_mesh.clear_surfaces()
	if current_view_mode == GIEnums.DebugViewMode.NONE:
		return
		
	imm_mesh.surface_begin(Mesh.PRIMITIVE_LINES)
	
	match current_view_mode:
		GIEnums.DebugViewMode.TRANSPORT_GRAPH:
			_render_transport_nodes(bounce0_nodes, bounce1_nodes, lights)
		GIEnums.DebugViewMode.ANGULAR_CELLS:
			_render_angular_funnels(bounce0_nodes, lights)
		GIEnums.DebugViewMode.PROBES_TOTAL_IRRADIANCE, \
		GIEnums.DebugViewMode.PROBES_DIRECT_ONLY, \
		GIEnums.DebugViewMode.PROBES_INDIRECT_ONLY, \
		GIEnums.DebugViewMode.PROBES_CONFIDENCE, \
		GIEnums.DebugViewMode.PROBES_ERROR_HEATMAP:
			_render_probes(probes, gt_radiance_map)
			
	imm_mesh.surface_end()

func _render_transport_nodes(
	bounce0: Array[TransportNode],
	bounce1: Array[TransportNode],
	lights: Array[Light3D]
) -> void:
	# Bounce 0 (Light -> Surface)
	for node in bounce0:
		if node.source_id < 0 or node.source_id >= lights.size():
			continue
		var light = lights[node.source_id]
		var col = _get_node_state_color(node.state)
		
		imm_mesh.surface_set_color(col)
		imm_mesh.surface_add_vertex(light.global_position)
		imm_mesh.surface_add_vertex(node.hit_position)
		
	# Bounce 1 (Surface -> Surface)
	for node in bounce1:
		if node.parent_node_id < 0 or node.parent_node_id >= bounce0.size():
			continue
		var parent = bounce0[node.parent_node_id]
		var col = _get_node_state_color(node.state)
		col.a = 0.6
		
		imm_mesh.surface_set_color(col)
		imm_mesh.surface_add_vertex(parent.hit_position)
		imm_mesh.surface_add_vertex(node.hit_position)

func _render_angular_funnels(bounce0: Array[TransportNode], lights: Array[Light3D]) -> void:
	for node in bounce0:
		if node.source_id < 0 or node.source_id >= lights.size():
			continue
		var light = lights[node.source_id]
		var col = Color(0.2, 0.8, 1.0, 0.4)
		if node.state == GIEnums.NodeState.REGROWN:
			col = Color(0.1, 0.5, 1.0, 0.9)
		elif node.state == GIEnums.NodeState.INVALID:
			col = Color(1.0, 0.2, 0.2, 0.9)
			
		imm_mesh.surface_set_color(col)
		imm_mesh.surface_add_vertex(light.global_position)
		imm_mesh.surface_add_vertex(node.hit_position)

func _render_probes(probes: Array[SurfaceProbe], gt_map: Dictionary) -> void:
	var line_len = 0.15
	for p in probes:
		var col = Color.WHITE
		match current_view_mode:
			GIEnums.DebugViewMode.PROBES_TOTAL_IRRADIANCE:
				var tot = p.get_total_radiance()
				col = Color(clamp(tot.r, 0.0, 1.0), clamp(tot.g, 0.0, 1.0), clamp(tot.b, 0.0, 1.0), 1.0)
			GIEnums.DebugViewMode.PROBES_DIRECT_ONLY:
				var d = p.direct_radiance
				col = Color(clamp(d.r, 0.0, 1.0), clamp(d.g, 0.0, 1.0), clamp(d.b, 0.0, 1.0), 1.0)
			GIEnums.DebugViewMode.PROBES_INDIRECT_ONLY:
				var ind = p.indirect_radiance
				col = Color(clamp(ind.r * 2.0, 0.0, 1.0), clamp(ind.g * 2.0, 0.0, 1.0), clamp(ind.b * 2.0, 0.0, 1.0), 1.0)
			GIEnums.DebugViewMode.PROBES_CONFIDENCE:
				col = Color(1.0 - p.confidence, p.confidence, 0.0, 1.0)
			GIEnums.DebugViewMode.PROBES_ERROR_HEATMAP:
				if gt_map.has(p.id):
					var err = (p.get_total_radiance() - gt_map[p.id]).get_luminance()
					var heat = clamp(absf(err) * 3.0, 0.0, 1.0)
					col = Color(heat, 1.0 - heat, 0.0, 1.0)
			GIEnums.DebugViewMode.PROBE_LEAK_DEBUG:
				if not p.is_active():
					col = Color(1.0, 0.1, 0.1, 0.8) # Red: Inactive / Rejected
				elif p.surface_cluster_id >= 0:
					col = Color(0.1, 0.9, 0.2, 0.9) # Green: Exact surface cluster
				else:
					col = Color(0.2, 0.4, 1.0, 0.8) # Blue: Adjacent
					
		imm_mesh.surface_set_color(col)
		imm_mesh.surface_add_vertex(p.position)
		imm_mesh.surface_add_vertex(p.position + p.normal * line_len)

func _get_node_state_color(state: int) -> Color:
	match state:
		GIEnums.NodeState.VALID:
			return Color(0.1, 0.9, 0.2, 0.7) # Green: Valid cached transport
		GIEnums.NodeState.INVALID:
			return Color(1.0, 0.1, 0.1, 0.9) # Red: Invalidated
		GIEnums.NodeState.QUEUED:
			return Color(1.0, 0.85, 0.0, 0.9) # Yellow: Queued
		GIEnums.NodeState.REGROWN:
			return Color(0.15, 0.5, 1.0, 0.9) # Blue: Newly regrown
		GIEnums.NodeState.BLOCKED:
			return Color(0.5, 0.5, 0.5, 0.4) # Gray: Blocked
	return Color.WHITE
