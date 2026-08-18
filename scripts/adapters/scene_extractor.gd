class_name ASTGSceneExtractor
extends RefCounted

# ==============================================================================
# ASTG SCENE & GEOMETRY ADAPTER (GODOT → ASTG TRANSPORT EXTRACTOR)
# Extracts transport representation from authoritative Godot scene graph
# ==============================================================================

enum GeometryClass {
	ASTG_STATIC = 0,
	ASTG_DESTRUCTIBLE = 1,
	ASTG_DYNAMIC = 2,
	ASTG_EXCLUDED = 3
}

class ExtractedGeometry:
	var total_triangles: int = 0
	var total_vertices: int = 0
	var total_meshes: int = 0
	var aabb_min: Vector3 = Vector3(1e9, 1e9, 1e9)
	var aabb_max: Vector3 = Vector3(-1e9, -1e9, -1e9)
	
	var vertices: PackedVector3Array = PackedVector3Array()
	var geometric_normals: PackedVector3Array = PackedVector3Array()
	var indices: PackedInt32Array = PackedInt32Array()
	var metadata: Array[Dictionary] = [] # ASTGPrimitiveMetadata
	var transport_materials: Array[Dictionary] = [] # ASTGTransportMaterial
	var clusters: Array[Dictionary] = []

# Traverses Godot scene and extracts transport data
static func extract_scene(root_node: Node3D) -> ExtractedGeometry:
	var geo = ExtractedGeometry.new()
	if root_node == null:
		return geo

	var stack: Array[Node] = [root_node]
	var mesh_instances: Array[MeshInstance3D] = []

	while not stack.is_empty():
		var node = stack.pop_back()
		if node is MeshInstance3D and node.visible and node.mesh != null:
			# Check classification metadata
			var geom_class = GeometryClass.ASTG_STATIC
			if node.has_meta("astg_class"):
				geom_class = node.get_meta("astg_class")
			elif node.name.begins_with("Destructible") or node.has_meta("destruction_chunk_id"):
				geom_class = GeometryClass.ASTG_DESTRUCTIBLE
			elif node.has_meta("is_character") or node.has_meta("is_dynamic"):
				geom_class = GeometryClass.ASTG_DYNAMIC

			# Exclude visual-only / dynamic nodes from persistent graph initially
			if geom_class != GeometryClass.ASTG_EXCLUDED and geom_class != GeometryClass.ASTG_DYNAMIC:
				mesh_instances.append(node)

		for child in node.get_children():
			stack.append(child)

	geo.total_meshes = mesh_instances.size()
	var mesh_counter = 0

	for mi in mesh_instances:
		var mesh = mi.mesh
		var xform = mi.global_transform if mi.is_inside_tree() else mi.transform
		var chunk_id = int(mi.get_meta("destruction_chunk_id")) if mi.has_meta("destruction_chunk_id") else (mesh_counter % 32)
		var cluster_id = mesh_counter % 64

		# Extract Transport Material from Godot Material
		var mat = mi.get_active_material(0)
		var transport_mat = {
			"material_id": mesh_counter,
			"diffuse_albedo": Color.WHITE,
			"emissive_strength": 0.0,
			"flags": 1 # Opaque
		}
		if mat is StandardMaterial3D or mat is ORMMaterial3D:
			transport_mat["diffuse_albedo"] = mat.albedo_color
			if mat.emission_enabled:
				transport_mat["emissive_strength"] = mat.emission_energy_multiplier
				transport_mat["flags"] |= 2
		geo.transport_materials.append(transport_mat)

		# Extract mesh surfaces
		for s in range(mesh.get_surface_count()):
			var arrays = mesh.surface_get_arrays(s)
			if arrays.is_empty():
				continue
			var v_arr = arrays[Mesh.ARRAY_VERTEX]
			var n_arr = arrays[Mesh.ARRAY_NORMAL]
			var i_arr = arrays[Mesh.ARRAY_INDEX]

			if v_arr.is_empty():
				continue

			var base_v = geo.vertices.size()
			var v_count = v_arr.size()

			for vi in range(v_count):
				var w_pos = xform * v_arr[vi]
				var w_norm = (xform.basis * n_arr[vi]).normalized() if (not n_arr.is_empty() and vi < n_arr.size()) else Vector3.UP
				if not w_norm.is_finite() or w_norm.length_squared() < 0.01:
					w_norm = Vector3.UP

				geo.vertices.append(w_pos)
				geo.geometric_normals.append(w_norm)

				geo.aabb_min.x = min(geo.aabb_min.x, w_pos.x)
				geo.aabb_min.y = min(geo.aabb_min.y, w_pos.y)
				geo.aabb_min.z = min(geo.aabb_min.z, w_pos.z)
				geo.aabb_max.x = max(geo.aabb_max.x, w_pos.x)
				geo.aabb_max.y = max(geo.aabb_max.y, w_pos.y)
				geo.aabb_max.z = max(geo.aabb_max.z, w_pos.z)

			if not i_arr.is_empty():
				for idx in i_arr:
					geo.indices.append(base_v + idx)
			else:
				for vi in range(v_count):
					geo.indices.append(base_v + vi)

			# Populate Primitive Metadata
			var triangle_count = (i_arr.size() / 3) if not i_arr.is_empty() else (v_count / 3)
			for t in range(triangle_count):
				geo.metadata.append({
					"mesh_id": mesh_counter,
					"instance_id": mesh_counter,
					"surface_cluster_id": cluster_id,
					"destruction_chunk_id": chunk_id,
					"transport_material_id": mesh_counter
				})

		# Record Cluster
		var c_center = mi.global_position if mi.is_inside_tree() else mi.position
		geo.clusters.append({
			"cluster_id": cluster_id,
			"centroid": c_center,
			"representative_normal": Vector3.UP,
			"destruction_chunk_id": chunk_id,
			"area": 25.0
		})
		mesh_counter += 1

	geo.total_vertices = geo.vertices.size()
	geo.total_triangles = geo.indices.size() / 3
	return geo
