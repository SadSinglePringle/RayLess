class_name ASTGDebugRenderer
extends Node3D

var current_view_mode: int = GIEnums.DebugViewMode.NONE
var show_probes: bool = true
var probe_sphere_radius: float = 0.055

var imm_mesh: ImmediateMesh
var mesh_inst: MeshInstance3D
var debug_mat: StandardMaterial3D

var probe_multimesh_inst: MultiMeshInstance3D
var probe_multimesh: MultiMesh
var probe_material: StandardMaterial3D

func _ready() -> void:
	# 1. Line Mesh Instance for DAG Rays & Normal Sticks
	imm_mesh = ImmediateMesh.new()
	mesh_inst = MeshInstance3D.new()
	mesh_inst.mesh = imm_mesh
	
	debug_mat = StandardMaterial3D.new()
	debug_mat.shading_mode = StandardMaterial3D.SHADING_MODE_UNSHADED
	debug_mat.vertex_color_use_as_albedo = true
	debug_mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	mesh_inst.material_override = debug_mat
	add_child(mesh_inst)

	# 2. MultiMesh Instance for 3D Sparse Surface Probe Spheres
	probe_multimesh_inst = MultiMeshInstance3D.new()
	probe_multimesh_inst.name = "ProbeSpheres"
	
	probe_multimesh = MultiMesh.new()
	probe_multimesh.transform_format = MultiMesh.TRANSFORM_3D
	probe_multimesh.use_colors = true
	
	var sphere = SphereMesh.new()
	sphere.radius = probe_sphere_radius
	sphere.height = probe_sphere_radius * 2.0
	sphere.radial_segments = 10
	sphere.rings = 5
	
	probe_material = StandardMaterial3D.new()
	probe_material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	probe_material.vertex_color_use_as_albedo = true
	sphere.material = probe_material
	
	probe_multimesh.mesh = sphere
	probe_multimesh_inst.multimesh = probe_multimesh
	add_child(probe_multimesh_inst)

func clear() -> void:
	if imm_mesh != null:
		imm_mesh.clear_surfaces()
	if probe_multimesh_inst != null:
		probe_multimesh_inst.visible = false

func update_debug_visuals(
	probes: Array[SurfaceProbe],
	bounce0_nodes: Array[TransportNode],
	bounce1_nodes: Array[TransportNode],
	lights: Array[Light3D],
	gt_radiance_map: Dictionary = {}
) -> void:
	imm_mesh.clear_surfaces()

	# 1. Update 3D Surface Probe Spheres
	if show_probes and probes.size() > 0:
		_render_probe_spheres(probes, gt_radiance_map)
		probe_multimesh_inst.visible = true
	else:
		if probe_multimesh_inst != null:
			probe_multimesh_inst.visible = false

	# 2. Render Lines (Transport DAG, Angular Funnels, or Normal Sticks)
	if current_view_mode == GIEnums.DebugViewMode.NONE and not show_probes:
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
		GIEnums.DebugViewMode.PROBES_ERROR_HEATMAP, \
		GIEnums.DebugViewMode.PROBE_LEAK_DEBUG:
			_render_probe_normals(probes)
			
	imm_mesh.surface_end()

func _render_probe_spheres(probes: Array[SurfaceProbe], gt_map: Dictionary) -> void:
	if probe_multimesh == null:
		return
		
	if probe_multimesh.instance_count != probes.size():
		probe_multimesh.instance_count = probes.size()

	for i in range(probes.size()):
		var p = probes[i]
		# Float slightly above the geometric surface along normal to avoid z-fighting
		var probe_pos = p.position + p.normal * (probe_sphere_radius * 0.4)
		var xform = Transform3D(Basis(), probe_pos)
		probe_multimesh.set_instance_transform(i, xform)

		var col = Color.WHITE
		match current_view_mode:
			GIEnums.DebugViewMode.NONE, GIEnums.DebugViewMode.PROBES_TOTAL_IRRADIANCE, GIEnums.DebugViewMode.TRANSPORT_GRAPH, GIEnums.DebugViewMode.ANGULAR_CELLS:
				var tot = p.get_total_radiance()
				# Scale radiance to vivid 0-1 display color
				var r = clamp(tot.r * 1.6, 0.05, 1.0)
				var g = clamp(tot.g * 1.6, 0.05, 1.0)
				var b = clamp(tot.b * 1.6, 0.05, 1.0)
				col = Color(r, g, b, 1.0)
			GIEnums.DebugViewMode.PROBES_DIRECT_ONLY:
				var d = p.direct_radiance
				col = Color(clamp(d.r * 1.6, 0.02, 1.0), clamp(d.g * 1.6, 0.02, 1.0), clamp(d.b * 1.6, 0.02, 1.0), 1.0)
			GIEnums.DebugViewMode.PROBES_INDIRECT_ONLY:
				var ind = p.indirect_radiance
				col = Color(clamp(ind.r * 3.5, 0.02, 1.0), clamp(ind.g * 3.5, 0.02, 1.0), clamp(ind.b * 3.5, 0.02, 1.0), 1.0)
			GIEnums.DebugViewMode.PROBES_CONFIDENCE:
				col = Color(1.0 - p.confidence, p.confidence, 0.1, 1.0)
			GIEnums.DebugViewMode.PROBES_ERROR_HEATMAP:
				if gt_map.has(p.id):
					var err = (p.get_total_radiance() - gt_map[p.id]).get_luminance()
					var heat = clamp(absf(err) * 4.0, 0.0, 1.0)
					col = Color(heat, 1.0 - heat, 0.05, 1.0)
				else:
					col = Color(0.1, 0.9, 0.2, 1.0)
			GIEnums.DebugViewMode.PROBE_LEAK_DEBUG:
				if not p.is_active():
					col = Color(1.0, 0.1, 0.1, 1.0) # Red: Inactive / Rejected
				elif p.surface_cluster_id >= 0:
					col = Color(0.1, 0.9, 0.2, 1.0) # Green: Exact surface cluster
				else:
					col = Color(0.2, 0.4, 1.0, 1.0) # Blue: Adjacent

		probe_multimesh.set_instance_color(i, col)

func _render_probe_normals(probes: Array[SurfaceProbe]) -> void:
	var line_len = 0.12
	for p in probes:
		imm_mesh.surface_set_color(Color(0.9, 0.9, 0.9, 0.6))
		imm_mesh.surface_add_vertex(p.position)
		imm_mesh.surface_add_vertex(p.position + p.normal * line_len)

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
