class_name ProbeMultiMesh
extends Node3D

var multimesh_inst: MultiMeshInstance3D
var sphere_mesh: SphereMesh
var probe_material: StandardMaterial3D

func _init() -> void:
	multimesh_inst = MultiMeshInstance3D.new()
	sphere_mesh = SphereMesh.new()
	sphere_mesh.radius = 0.08
	sphere_mesh.height = 0.16
	sphere_mesh.radial_segments = 8
	sphere_mesh.rings = 4
	
	probe_material = StandardMaterial3D.new()
	probe_material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	probe_material.vertex_color_use_as_albedo = true
	sphere_mesh.material = probe_material
	
	var mm = MultiMesh.new()
	mm.transform_format = MultiMesh.TRANSFORM_3D
	mm.use_colors = true
	multimesh_inst.multimesh = mm
	add_child(multimesh_inst)

func update_probes(probes: Array[SurfaceProbe], view_mode: int, ref_map: Dictionary = {}) -> void:
	var mm = multimesh_inst.multimesh
	if mm == null:
		return
		
	if mm.instance_count != probes.size():
		mm.instance_count = probes.size()
		
	for i in range(probes.size()):
		var p = probes[i]
		var xform = Transform3D(Basis(), p.position)
		mm.set_instance_transform(i, xform)
		
		var col = Color.WHITE
		match view_mode:
			GIEnums.DebugViewMode.PROBES_TOTAL_IRRADIANCE:
				col = p.get_total_radiance()
				col.a = 1.0
			GIEnums.DebugViewMode.PROBES_DIRECT_ONLY:
				col = p.direct_radiance
				col.a = 1.0
			GIEnums.DebugViewMode.PROBES_INDIRECT_ONLY:
				col = p.indirect_radiance * 2.0 # boosted for visibility
				col.a = 1.0
			GIEnums.DebugViewMode.PROBES_CONFIDENCE:
				# Green (1.0) to Red (0.0)
				col = Color(1.0 - p.confidence, p.confidence, 0.0, 1.0)
			GIEnums.DebugViewMode.PROBES_DIRTY_STATE:
				if (p.flags & GIEnums.ProbeFlags.DIRTY) != 0:
					col = Color.YELLOW
				elif (p.flags & GIEnums.ProbeFlags.UNCERTAIN) != 0:
					col = Color.MAGENTA
				else:
					col = Color(0.2, 0.8, 0.2)
			GIEnums.DebugViewMode.PROBES_ERROR_HEATMAP:
				if ref_map.has(p.id):
					var ref_c: Color = ref_map[p.id]
					var cur_c: Color = p.get_total_radiance()
					var err = (absf(cur_c.r - ref_c.r) + absf(cur_c.g - ref_c.g) + absf(cur_c.b - ref_c.b)) * 1.5
					col = Color(min(1.0, err), max(0.0, 1.0 - err), 0.0, 1.0)
				else:
					col = Color.GRAY
			_:
				col = p.get_total_radiance()
				
		mm.set_instance_color(i, col)
