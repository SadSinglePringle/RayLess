class_name SurfaceClusterBuilder
extends RefCounted

func build_clusters_from_meshes(mesh_nodes: Array[Node3D], probes: Array[SurfaceProbe]) -> Array[SurfaceCluster]:
	var clusters: Array[SurfaceCluster] = []
	var cluster_id = 0
	
	var mesh_instances: Array[MeshInstance3D] = []
	for node in mesh_nodes:
		_gather_meshes(node, mesh_instances)
		
	var mesh_counter = 0
	for mi in mesh_instances:
		if not mi.visible or mi.mesh == null:
			continue
			
		var mi_id = mesh_counter
		mesh_counter += 1
		
		var chunk_id = -1
		if mi.has_meta("chunk_id"):
			chunk_id = int(mi.get_meta("chunk_id"))
		elif mi.get_parent() != null and mi.get_parent().has_meta("chunk_id"):
			chunk_id = int(mi.get_parent().get_meta("chunk_id"))
			
		var xform = mi.global_transform if mi.is_inside_tree() else mi.transform
		var mi_aabb = xform * mi.get_aabb()
		
		# Spatial subdivision of mesh into surface cluster tiles (max 3.0m tile size)
		var max_cluster_size = 3.0
		var num_x = max(1, int(ceil(mi_aabb.size.x / max_cluster_size)))
		var num_y = max(1, int(ceil(mi_aabb.size.y / max_cluster_size)))
		var num_z = max(1, int(ceil(mi_aabb.size.z / max_cluster_size)))
		
		var step_x = mi_aabb.size.x / float(num_x)
		var step_y = mi_aabb.size.y / float(num_y)
		var step_z = mi_aabb.size.z / float(num_z)
		
		var base_albedo = Color(0.8, 0.8, 0.8)
		var mat = mi.get_active_material(0)
		if mat is StandardMaterial3D:
			base_albedo = mat.albedo_color
		elif mat is ORMMaterial3D:
			base_albedo = mat.albedo_color
			
		for ix in range(num_x):
			for iy in range(num_y):
				for iz in range(num_z):
					var c_min = mi_aabb.position + Vector3(ix * step_x, iy * step_y, iz * step_z)
					var c_size = Vector3(step_x, step_y, step_z)
					
					var cluster = SurfaceCluster.new()
					cluster.id = cluster_id
					cluster.mesh_id = mi_id
					cluster.destruction_chunk_id = chunk_id
					cluster.bounds = AABB(c_min, c_size)
					cluster.centroid = cluster.bounds.get_center()
					cluster.average_normal = xform.basis.y.normalized()
					cluster.albedo = base_albedo
					clusters.append(cluster)
					cluster_id += 1
					
	# -------------------------------------------------------------
	# Build Cluster Adjacency Graph & Classify Topology
	# -------------------------------------------------------------
	for i in range(clusters.size()):
		var cA = clusters[i]
		for j in range(i + 1, clusters.size()):
			var cB = clusters[j]
			
			# Check bounding box proximity
			if cA.bounds.grow(0.15).intersects(cB.bounds):
				var normal_dot = cA.average_normal.dot(cB.average_normal)
				var adj_type = GIEnums.ClusterAdjacencyType.DISCONNECTED
				
				if normal_dot >= 0.90:
					# Coplanar adjacent surface
					adj_type = GIEnums.ClusterAdjacencyType.COPLANAR
				elif normal_dot >= 0.70:
					# Smooth curved continuation
					adj_type = GIEnums.ClusterAdjacencyType.SMOOTH_CONTINUATION
				else:
					# Sharp corner (e.g. wall to floor 90 deg boundary)
					adj_type = GIEnums.ClusterAdjacencyType.SHARP_EDGE
					
				cA.add_adjacent_cluster(cB.id, adj_type)
				cB.add_adjacent_cluster(cA.id, adj_type)
				
	# -------------------------------------------------------------
	# Map Probes to Exact Surface Clusters with Normal Filtering
	# -------------------------------------------------------------
	for probe in probes:
		var best_cluster_id = -1
		var best_score = -1e9
		
		for c in clusters:
			var d = c.centroid.distance_to(probe.position)
			if d > 4.0:
				continue
			var ndot = c.average_normal.dot(probe.normal)
			if ndot < 0.2:
				continue # Opposing normal
				
			var contains = 1.0 if c.contains_point(probe.position, 0.3) else 0.0
			var score = contains * 100.0 + ndot * 10.0 - d
			
			if score > best_score:
				best_score = score
				best_cluster_id = c.id
				
		if best_cluster_id >= 0 and best_cluster_id < clusters.size():
			probe.surface_cluster_id = best_cluster_id
			probe.destruction_chunk_id = clusters[best_cluster_id].destruction_chunk_id
			clusters[best_cluster_id].probe_ids.append(probe.id)
		else:
			probe.surface_cluster_id = -1
			
	print("[SurfaceClusterBuilder] Built %d surface clusters with topology adjacency for %d probes." % [
		clusters.size(), probes.size()
	])
	return clusters

func _gather_meshes(node: Node, out_meshes: Array[MeshInstance3D]) -> void:
	if node is MeshInstance3D and node.visible:
		out_meshes.append(node)
	for child in node.get_children():
		if not (child is StaticBody3D):
			_gather_meshes(child, out_meshes)
