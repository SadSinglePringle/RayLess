extends SceneTree

func _init() -> void:
	var glb_path = "res://assets/classroom/classroom.glb"
	var gltf_doc = GLTFDocument.new()
	var gltf_state = GLTFState.new()
	var err = gltf_doc.append_from_file(glb_path, gltf_state)
	if err != OK:
		printerr("Failed to append from glb: %d" % err)
		quit(1)
		return
		
	var inst: Node = gltf_doc.generate_scene(gltf_state)
	if inst == null:
		printerr("Failed to generate scene")
		quit(1)
		return
		
	var aabb = AABB()
	var mesh_count = 0
	var tri_count = 0
	
	var stack: Array[Node] = [inst]
	while stack.size() > 0:
		var node = stack.pop_back()
		if node is MeshInstance3D:
			mesh_count += 1
			var mi: MeshInstance3D = node
			if mi.mesh != null:
				for s in range(mi.mesh.get_surface_count()):
					var arr = mi.mesh.surface_get_arrays(s)
					if arr.size() > 0 and arr[Mesh.ARRAY_VERTEX] != null:
						tri_count += arr[Mesh.ARRAY_VERTEX].size() / 3
				var m_aabb = mi.global_transform * mi.get_aabb()
				if aabb.size == Vector3.ZERO:
					aabb = m_aabb
				else:
					aabb = aabb.merge(m_aabb)
		for child in node.get_children():
			stack.append(child)
			
	var report = """
==================================================
Classroom Meshes: %d
Classroom Triangles: %d
Classroom AABB Pos: %s
Classroom AABB Size: %s
Classroom Center: %s
==================================================
""" % [mesh_count, tri_count, str(aabb.position), str(aabb.size), str(aabb.get_center())]

	var f = FileAccess.open("res://inspect_results.txt", FileAccess.WRITE)
	f.store_string(report)
	f.close()
	print(report)
	quit(0)
