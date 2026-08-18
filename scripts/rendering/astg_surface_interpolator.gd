class_name ASTGSurfaceInterpolator
extends RefCounted

var registered_materials: Array[ShaderMaterial] = []
var volume_dims: Vector3i = Vector3i(16, 8, 16)
var volume_min: Vector3 = Vector3(-7.5, 0.0, -5.5)
var volume_size: Vector3 = Vector3(15.0, 4.0, 11.0)

var gi_texture_3d: ImageTexture3D
var voxel_images: Array[Image] = []

func _init() -> void:
	_init_volume_texture()

func _init_volume_texture() -> void:
	voxel_images.clear()
	for y in range(volume_dims.y):
		var img = Image.create(volume_dims.x, volume_dims.z, false, Image.FORMAT_RGBA8)
		img.fill(Color(0, 0, 0, 1))
		voxel_images.append(img)
		
	gi_texture_3d = ImageTexture3D.new()
	gi_texture_3d.create(Image.FORMAT_RGBA8, volume_dims.x, volume_dims.z, volume_dims.y, false, voxel_images)

func set_scene_bounds(bounds: AABB) -> void:
	volume_min = bounds.position
	volume_size = bounds.size
	for mat in registered_materials:
		mat.set_shader_parameter("volume_min", volume_min)
		mat.set_shader_parameter("volume_size", volume_size)

func register_scene_meshes(nodes: Array[Node3D], shader: Shader) -> void:
	registered_materials.clear()
	_collect_and_setup_meshes(nodes, shader)
	print("[ASTGSurfaceInterpolator] Successfully converted %d meshes with full PBR textures!" % registered_materials.size())

func _collect_and_setup_meshes(nodes: Array[Node3D], shader: Shader) -> void:
	for node in nodes:
		if node is MeshInstance3D and node.visible:
			var mi: MeshInstance3D = node
			if mi.mesh != null:
				var surf_count = mi.mesh.get_surface_count()
				for surf_idx in range(surf_count):
					var orig_mat = mi.get_active_material(surf_idx)
					if orig_mat == null:
						orig_mat = mi.mesh.surface_get_material(surf_idx)
						
					var sh_mat = ShaderMaterial.new()
					sh_mat.shader = shader
					sh_mat.set_shader_parameter("volume_min", volume_min)
					sh_mat.set_shader_parameter("volume_size", volume_size)
					sh_mat.set_shader_parameter("gi_intensity", 0.4)
					sh_mat.set_shader_parameter("gi_volume_texture", gi_texture_3d)
					sh_mat.set_shader_parameter("gi_enabled", true)
					
					if orig_mat is BaseMaterial3D:
						var bm: BaseMaterial3D = orig_mat
						sh_mat.set_shader_parameter("albedo", bm.albedo_color)
						if bm.albedo_texture != null:
							sh_mat.set_shader_parameter("texture_albedo", bm.albedo_texture)
							sh_mat.set_shader_parameter("use_texture_albedo", true)
							
						sh_mat.set_shader_parameter("roughness", bm.roughness)
						if bm.roughness_texture != null:
							sh_mat.set_shader_parameter("texture_roughness", bm.roughness_texture)
							sh_mat.set_shader_parameter("use_texture_roughness", true)
							
						sh_mat.set_shader_parameter("metallic", bm.metallic)
						if bm.metallic_texture != null:
							sh_mat.set_shader_parameter("texture_metallic", bm.metallic_texture)
							sh_mat.set_shader_parameter("use_texture_metallic", true)
							
						if bm.normal_texture != null:
							sh_mat.set_shader_parameter("texture_normal", bm.normal_texture)
							sh_mat.set_shader_parameter("use_texture_normal", true)
							sh_mat.set_shader_parameter("normal_scale", bm.normal_scale)
							
						sh_mat.set_shader_parameter("uv1_scale", bm.uv1_scale)
						sh_mat.set_shader_parameter("uv1_offset", bm.uv1_offset)
						
					elif orig_mat is ShaderMaterial:
						sh_mat = orig_mat
						
					mi.set_surface_override_material(surf_idx, sh_mat)
					registered_materials.append(sh_mat)
			
		for child in node.get_children():
			if child is Node3D and not (child is StaticBody3D):
				_collect_and_setup_meshes([child], shader)

var is_dirty: bool = true
var last_gi_mode: int = -1

func mark_dirty() -> void:
	is_dirty = true

func update_surfaces(gi_mode: int, astg: ASTGPipeline, ddgi: DDGIBaseline, gt: GroundTruthReference, force_update: bool = false) -> void:
	if not is_dirty and not force_update and gi_mode == last_gi_mode:
		return
	last_gi_mode = gi_mode
	is_dirty = false
	_update_gi_texture_3d(gi_mode, astg, ddgi, gt)

func _update_gi_texture_3d(gi_mode: int, astg: ASTGPipeline, ddgi: DDGIBaseline, gt: GroundTruthReference) -> void:
	var total_voxels = volume_dims.x * volume_dims.y * volume_dims.z
	var accum_r = PackedFloat32Array()
	var accum_g = PackedFloat32Array()
	var accum_b = PackedFloat32Array()
	var accum_w = PackedFloat32Array()
	accum_r.resize(total_voxels)
	accum_g.resize(total_voxels)
	accum_b.resize(total_voxels)
	accum_w.resize(total_voxels)
	accum_r.fill(0.0)
	accum_g.fill(0.0)
	accum_b.fill(0.0)
	accum_w.fill(0.0)
	
	var step_x = volume_size.x / float(volume_dims.x)
	var step_y = volume_size.y / float(volume_dims.y)
	var step_z = volume_size.z / float(volume_dims.z)
	var inv_step_x = 1.0 / step_x
	var inv_step_y = 1.0 / step_y
	var inv_step_z = 1.0 / step_z
	
	if gi_mode == GIEnums.GIMode.ASTG and astg != null:
		# Fast probe-to-voxel splatting (O(N_probes * 27) instead of O(N_voxels * N_clusters))
		for probe in astg.probes:
			if not probe.is_active():
				continue
			var rad = probe.indirect_radiance
			if rad.r <= 0.0001 and rad.g <= 0.0001 and rad.b <= 0.0001:
				continue
				
			var p_pos = probe.position + probe.normal * 0.15
			var cx = int(floor((p_pos.x - volume_min.x) * inv_step_x))
			var cy = int(floor((p_pos.y - volume_min.y) * inv_step_y))
			var cz = int(floor((p_pos.z - volume_min.z) * inv_step_z))
			
			var min_x = maxi(0, cx - 1)
			var max_x = mini(volume_dims.x - 1, cx + 1)
			var min_y = maxi(0, cy - 1)
			var max_y = mini(volume_dims.y - 1, cy + 1)
			var min_z = maxi(0, cz - 1)
			var max_z = mini(volume_dims.z - 1, cz + 1)
			
			for vy in range(min_y, max_y + 1):
				var wy = volume_min.y + (float(vy) + 0.5) * step_y
				var dy = wy - p_pos.y
				for vz in range(min_z, max_z + 1):
					var wz = volume_min.z + (float(vz) + 0.5) * step_z
					var dz = wz - p_pos.z
					for vx in range(min_x, max_x + 1):
						var wx = volume_min.x + (float(vx) + 0.5) * step_x
						var dx = wx - p_pos.x
						
						var dist_sq = dx * dx + dy * dy + dz * dz
						var w = maxf(0.0, 1.0 - (dist_sq / 4.0))
						if w > 0.0001:
							var idx = vy * (volume_dims.x * volume_dims.z) + vz * volume_dims.x + vx
							accum_r[idx] += rad.r * w
							accum_g[idx] += rad.g * w
							accum_b[idx] += rad.b * w
							accum_w[idx] += w
							
	elif gi_mode == GIEnums.GIMode.DDGI_BASELINE and ddgi != null:
		for y in range(volume_dims.y):
			var world_y = volume_min.y + (float(y) + 0.5) * step_y
			for z in range(volume_dims.z):
				var world_z = volume_min.z + (float(z) + 0.5) * step_z
				for x in range(volume_dims.x):
					var world_x = volume_min.x + (float(x) + 0.5) * step_x
					var c = ddgi.interpolate_irradiance(Vector3(world_x, world_y, world_z), Vector3.UP)
					var idx = y * (volume_dims.x * volume_dims.z) + z * volume_dims.x + x
					accum_r[idx] = c.r
					accum_g[idx] = c.g
					accum_b[idx] = c.b
					accum_w[idx] = 1.0
					
	elif gi_mode == GIEnums.GIMode.GROUND_TRUTH and gt != null and astg != null:
		for probe in astg.probes:
			if not gt.reference_probe_radiance.has(probe.id):
				continue
			var rad = gt.reference_probe_radiance[probe.id]
			var p_pos = probe.position + probe.normal * 0.15
			var cx = int(floor((p_pos.x - volume_min.x) * inv_step_x))
			var cy = int(floor((p_pos.y - volume_min.y) * inv_step_y))
			var cz = int(floor((p_pos.z - volume_min.z) * inv_step_z))
			
			var min_x = maxi(0, cx - 1)
			var max_x = mini(volume_dims.x - 1, cx + 1)
			var min_y = maxi(0, cy - 1)
			var max_y = mini(volume_dims.y - 1, cy + 1)
			var min_z = maxi(0, cz - 1)
			var max_z = mini(volume_dims.z - 1, cz + 1)
			
			for vy in range(min_y, max_y + 1):
				var wy = volume_min.y + (float(vy) + 0.5) * step_y
				var dy = wy - p_pos.y
				for vz in range(min_z, max_z + 1):
					var wz = volume_min.z + (float(vz) + 0.5) * step_z
					var dz = wz - p_pos.z
					for vx in range(min_x, max_x + 1):
						var wx = volume_min.x + (float(vx) + 0.5) * step_x
						var dx = wx - p_pos.x
						var dist_sq = dx * dx + dy * dy + dz * dz
						var w = maxf(0.0, 1.0 - (dist_sq / 4.0))
						if w > 0.0001:
							var idx = vy * (volume_dims.x * volume_dims.z) + vz * volume_dims.x + vx
							accum_r[idx] += rad.r * w
							accum_g[idx] += rad.g * w
							accum_b[idx] += rad.b * w
							accum_w[idx] += w
							
	# Write out to Images
	for y in range(volume_dims.y):
		var img: Image = voxel_images[y]
		for z in range(volume_dims.z):
			for x in range(volume_dims.x):
				var idx = y * (volume_dims.x * volume_dims.z) + z * volume_dims.x + x
				var w = accum_w[idx]
				var r = clampf(accum_r[idx] / maxf(w, 1.0), 0.0, 1.0) if w > 0.0001 else 0.0
				var g = clampf(accum_g[idx] / maxf(w, 1.0), 0.0, 1.0) if w > 0.0001 else 0.0
				var b = clampf(accum_b[idx] / maxf(w, 1.0), 0.0, 1.0) if w > 0.0001 else 0.0
				img.set_pixel(x, z, Color(r, g, b, 1.0))
				
	gi_texture_3d.update(voxel_images)
