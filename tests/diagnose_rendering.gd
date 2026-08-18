extends SceneTree

func _init() -> void:
	print("==================================================")
	print("🔬 DIAGNOSING RENDERING PIPELINE & PROBE RADIANCE")
	print("==================================================")
	
	var main_scene: Main = preload("res://scenes/main.tscn").instantiate()
	root.add_child(main_scene)
	
	await process_frame
	await process_frame
	
	var astg = main_scene.astg_pipeline
	var interp = main_scene.surface_interpolator
	var lights = main_scene.classroom_builder.dynamic_lights
	
	print("\n--- 1. LIGHT RIG ---")
	for l in lights:
		print("Light: %s | Color: %s | Energy: %.2f | Visible: %s" % [l.name, str(l.light_color), l.light_energy, str(l.visible)])
		
	print("\n--- 2. PROBE RADIANCES (Sample of 10 Probes) ---")
	for i in range(min(10, astg.probes.size())):
		var p: SurfaceProbe = astg.probes[i]
		print("Probe %d at %s | Direct: %s | Indirect: %s | Cluster: %d | Active: %s" % [
			p.id, str(p.position), str(p.direct_radiance), str(p.indirect_radiance), p.surface_cluster_id, str(p.is_active())
		])
		
	print("\n--- 3. MESH MATERIALS CHECK (Sample of 5 Meshes) ---")
	var count = 0
	for node in main_scene.classroom_builder.static_mesh_nodes:
		var stack = [node]
		while stack.size() > 0:
			var curr = stack.pop_back()
			if curr is MeshInstance3D and curr.visible:
				var mi: MeshInstance3D = curr
				var mat = mi.get_surface_override_material(0)
				if mat is ShaderMaterial:
					var alb = mat.get_shader_parameter("albedo")
					var use_tex = mat.get_shader_parameter("use_texture_albedo")
					var gi_en = mat.get_shader_parameter("gi_enabled")
					var gi_int = mat.get_shader_parameter("gi_intensity")
					print("Mesh: %s | OverrideMat0: ShaderMat | Albedo: %s | UseTex: %s | GI_en: %s | GI_int: %s" % [
						mi.name, str(alb), str(use_tex), str(gi_en), str(gi_int)
					])
					count += 1
					if count >= 8:
						break
			for child in curr.get_children():
				stack.append(child)
		if count >= 8:
			break
			
	print("\n--- 4. 3D GI VOLUME TEXTURE CHECK ---")
	var img: Image = interp.voxel_images[interp.volume_dims.y / 2]
	var min_c = Color(100, 100, 100)
	var max_c = Color(0, 0, 0)
	var avg_c = Color.BLACK
	var num_px = 0
	for z in range(img.get_height()):
		for x in range(img.get_width()):
			var px = img.get_pixel(x, z)
			min_c.r = min(min_c.r, px.r)
			min_c.g = min(min_c.g, px.g)
			min_c.b = min(min_c.b, px.b)
			max_c.r = max(max_c.r, px.r)
			max_c.g = max(max_c.g, px.g)
			max_c.b = max(max_c.b, px.b)
			avg_c += px
			num_px += 1
	avg_c /= float(num_px)
	print("GI Volume Middle Slice: Min: %s | Max: %s | Avg: %s" % [str(min_c), str(max_c), str(avg_c)])
	
	print("\n--- 5. CAMERA & VIEWPORT CHECK ---")
	print("Camera: pos=%s | rot=%s | near=%.2f | far=%.2f" % [
		str(main_scene.camera.global_position),
		str(main_scene.camera.rotation_degrees),
		main_scene.camera.near,
		main_scene.camera.far
	])
	
	quit(0)
