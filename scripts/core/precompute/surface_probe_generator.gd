class_name SurfaceProbeGenerator
extends RefCounted

# Parameters
var target_probe_density: float = 1.2   # Probes per square meter
var normal_offset: float = 0.03         # Small epsilon to lift probe off surface
var min_probe_distance: float = 0.55    # Poisson-disk exclusion radius

func generate_probes_from_nodes(nodes: Array[Node3D]) -> Array[SurfaceProbe]:
	var probes: Array[SurfaceProbe] = []
	var probe_id_counter: int = 0
	
	var stack: Array[Node] = []
	for n in nodes:
		stack.append(n)
		
	while not stack.is_empty():
		var curr = stack.pop_back()
		if curr is MeshInstance3D and curr.visible and curr.mesh != null:
			var mi: MeshInstance3D = curr
			var aabb = mi.get_aabb()
			if aabb.size.length() >= 0.35:
				var chunk_id = -1
				if mi.has_meta("chunk_id"):
					chunk_id = int(mi.get_meta("chunk_id"))
				elif mi.get_parent() != null and mi.get_parent().has_meta("chunk_id"):
					chunk_id = int(mi.get_parent().get_meta("chunk_id"))
				
				var node_probes = _generate_probes_for_mesh_instance(mi, chunk_id, probe_id_counter)
				for p in node_probes:
					probes.append(p)
					probe_id_counter += 1
					
		for ch in curr.get_children():
			if not (ch is StaticBody3D):
				stack.append(ch)
				
	# Apply spatial thinning / Poisson-disk pruning to avoid redundant clustering
	probes = _prune_close_probes(probes, min_probe_distance)
	
	# Re-index contiguous IDs
	for i in range(probes.size()):
		probes[i].id = i
		
	return probes

func _generate_probes_for_mesh_instance(mesh_inst: MeshInstance3D, chunk_id: int, start_id: int) -> Array[SurfaceProbe]:
	var result: Array[SurfaceProbe] = []
	var mesh = mesh_inst.mesh
	if mesh == null:
		return result
	
	var xform = mesh_inst.global_transform if mesh_inst.is_inside_tree() else mesh_inst.transform
	var basis = xform.basis
	var material: Material = mesh_inst.get_active_material(0)
	var albedo: Color = Color(0.8, 0.8, 0.8)
	if material is StandardMaterial3D:
		albedo = material.albedo_color
	elif material is ORMMaterial3D:
		albedo = material.albedo_color
	
	# Extract triangles using MeshDataTool or ArrayMesh arrays
	for surf_idx in range(mesh.get_surface_count()):
		var arrays = mesh.surface_get_arrays(surf_idx)
		if arrays.is_empty():
			continue
		
		var vertices: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
		var normals: PackedVector3Array = arrays[Mesh.ARRAY_NORMAL]
		var indices: PackedInt32Array = arrays[Mesh.ARRAY_INDEX]
		
		var has_indices = indices.size() > 0
		var tri_count = indices.size() / 3 if has_indices else vertices.size() / 3
		
		for t in range(tri_count):
			var i0 = indices[t * 3 + 0] if has_indices else t * 3 + 0
			var i1 = indices[t * 3 + 1] if has_indices else t * 3 + 1
			var i2 = indices[t * 3 + 2] if has_indices else t * 3 + 2
			
			var v0_local = vertices[i0]
			var v1_local = vertices[i1]
			var v2_local = vertices[i2]
			
			var v0 = xform * v0_local
			var v1 = xform * v1_local
			var v2 = xform * v2_local
			
			var cross_prod = (v1 - v0).cross(v2 - v0)
			var tri_area = cross_prod.length() * 0.5
			if tri_area < 0.0001:
				continue
				
			var tri_normal = cross_prod.normalized()
			if normals.size() > i0:
				tri_normal = (basis * normals[i0]).normalized()
				
			var expected_probes = tri_area * target_probe_density
			var num_samples = int(expected_probes)
			if randf() < fmod(expected_probes, 1.0):
				num_samples += 1
			if num_samples <= 0:
				continue
				
			# Area-stratified sampling
			for s in range(num_samples):
				var r1 = sqrt(randf())
				var r2 = randf()
				var u = 1.0 - r1
				var v = r2 * r1
				var w = 1.0 - u - v
				
				var sample_pos = u * v0 + v * v1 + w * v2
				var probe = SurfaceProbe.new()
				probe.id = start_id + result.size()
				probe.position = sample_pos + tri_normal * normal_offset
				probe.normal = tri_normal
				probe.albedo = albedo
				probe.area = tri_area / float(num_samples)
				probe.chunk_id = chunk_id
				probe.confidence = 1.0
				probe.flags = GIEnums.ProbeFlags.VALID
				if chunk_id >= 0:
					probe.flags |= GIEnums.ProbeFlags.DESTRUCTIBLE
				
				result.append(probe)
				
	return result

func _prune_close_probes(in_probes: Array[SurfaceProbe], min_dist: float) -> Array[SurfaceProbe]:
	var out_probes: Array[SurfaceProbe] = []
	var min_dist_sq = min_dist * min_dist
	
	# Simple spatial grid for fast radius checking
	var cell_size = min_dist
	var grid: Dictionary = {}
	
	for p in in_probes:
		var cx = int(floor(p.position.x / cell_size))
		var cy = int(floor(p.position.y / cell_size))
		var cz = int(floor(p.position.z / cell_size))
		var key = Vector3i(cx, cy, cz)
		
		var too_close = false
		for dx in range(-1, 2):
			for dy in range(-1, 2):
				for dz in range(-1, 2):
					var neighbor_key = Vector3i(cx + dx, cy + dy, cz + dz)
					if grid.has(neighbor_key):
						var existing_probe: SurfaceProbe = grid[neighbor_key]
						if p.position.distance_squared_to(existing_probe.position) < min_dist_sq:
							# Check if coplanar
							if p.normal.dot(existing_probe.normal) > 0.7:
								too_close = true
								break
				if too_close:
					break
			if too_close:
				break
				
		if not too_close:
			grid[key] = p
			out_probes.append(p)
			
	return out_probes
