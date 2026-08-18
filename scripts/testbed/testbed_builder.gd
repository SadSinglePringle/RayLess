class_name TestbedBuilder
extends Node3D

signal chunk_destroyed(chunk_id: int, bounds: AABB)

var destructible_wall: DestructibleWall           # Dividing wall between Room B and Room C
var irrelevant_wall: DestructibleWall             # Irrelevant corner wall for Test 7
var dynamic_lights: Array[Light3D] = []
var static_mesh_nodes: Array[Node3D] = []
var dynamic_occluder: AnimatableBody3D           # Moving block for Test 10
var occluder_moving: bool = false
var occluder_time: float = 0.0
var reference_sphere_probe_id: int = -1
var room_bounds: AABB = AABB(Vector3(-8.0, 0.0, -8.0), Vector3(16.0, 4.0, 16.0))

func build_testbed() -> void:
	build_environment()

func build_environment() -> void:
	for child in get_children():
		child.queue_free()
	dynamic_lights.clear()
	static_mesh_nodes.clear()
	
	# Saturated diffuse materials as specified in Section 3
	var mat_red = _create_mat(Color(0.88, 0.12, 0.12), "Mat_Red")       # Room A wall (Deep Red)
	var mat_green = _create_mat(Color(0.12, 0.85, 0.15), "Mat_Green")   # Room B wall (Deep Green)
	var mat_blue = _create_mat(Color(0.15, 0.25, 0.92), "Mat_Blue")     # Room C wall (Deep Blue)
	var mat_floor = _create_mat(Color(0.68, 0.68, 0.70), "Mat_Floor")   # Neutral Gray floor
	var mat_ceiling = _create_mat(Color(0.92, 0.92, 0.92), "Mat_Ceil")  # White ceiling
	var mat_white_ref = _create_mat(Color(0.95, 0.95, 0.95), "Mat_Ref")# Reference objects
	var mat_neutral_wall = _create_mat(Color(0.82, 0.82, 0.84), "Mat_Wall") # Light Gray partition
	var mat_occluder = _create_mat(Color(0.3, 0.3, 0.35), "Mat_Occluder") # Dynamic Occluder
	
	var static_root = Node3D.new()
	static_root.name = "StaticGeometry"
	add_child(static_root)
	
	# Layout Coordinates (Y: 0 to 4m height):
	# Room A (Top Left):    X: -8 to 0m,   Z:  0 to 8m  (8m x 8m)
	# Room B (Top Right):   X:  0 to 8m,   Z:  0 to 8m  (8m x 8m, facing SUN on Z=8)
	# Room C (Bottom Right):X:  0 to 8m,   Z: -8 to 0m  (8m x 8m, separated from B by destructible wall at Z=0)
	# Hallway (Bottom Left):X: -8 to 0m,   Z: -8 to 0m
	
	# -------------------------------------------------------------
	# Floors & Ceilings
	# -------------------------------------------------------------
	# Room A Floor & Ceiling
	_add_wall_box(static_root, Vector3(-4.0, -0.1, 4.0), Vector3(8.0, 0.2, 8.0), mat_floor)
	_add_wall_box(static_root, Vector3(-4.0, 4.1, 4.0), Vector3(8.0, 0.2, 8.0), mat_ceiling)
	# Room B Floor & Ceiling
	_add_wall_box(static_root, Vector3(4.0, -0.1, 4.0), Vector3(8.0, 0.2, 8.0), mat_floor)
	_add_wall_box(static_root, Vector3(4.0, 4.1, 4.0), Vector3(8.0, 0.2, 8.0), mat_ceiling)
	# Room C Floor & Ceiling
	_add_wall_box(static_root, Vector3(4.0, -0.1, -4.0), Vector3(8.0, 0.2, 8.0), mat_floor)
	_add_wall_box(static_root, Vector3(4.0, 4.1, -4.0), Vector3(8.0, 0.2, 8.0), mat_ceiling)
	# Hallway Floor & Ceiling
	_add_wall_box(static_root, Vector3(-4.0, -0.1, -4.0), Vector3(8.0, 0.2, 8.0), mat_floor)
	_add_wall_box(static_root, Vector3(-4.0, 4.1, -4.0), Vector3(8.0, 0.2, 8.0), mat_ceiling)
	
	# -------------------------------------------------------------
	# Room A (Deep Red Walls, X: -8 to 0, Z: 0 to 8)
	# -------------------------------------------------------------
	# Left outer wall (Deep Red)
	_add_wall_box(static_root, Vector3(-8.0, 2.0, 4.0), Vector3(0.2, 4.0, 8.0), mat_red)
	# Top outer wall
	_add_wall_box(static_root, Vector3(-4.0, 2.0, 8.0), Vector3(8.0, 4.0, 0.2), mat_neutral_wall)
	# Partition between Room A and Room B (X = 0, with 2.5m wide DOORWAY in the middle!)
	_add_wall_box(static_root, Vector3(0.0, 2.0, 1.25), Vector3(0.25, 4.0, 2.5), mat_red)
	_add_wall_box(static_root, Vector3(0.0, 2.0, 6.75), Vector3(0.25, 4.0, 2.5), mat_red)
	_add_wall_box(static_root, Vector3(0.0, 3.5, 4.0), Vector3(0.25, 1.0, 3.0), mat_red) # door header
	
	# -------------------------------------------------------------
	# Room B (Deep Green Walls, X: 0 to 8, Z: 0 to 8, SUNLIGHT WINDOW on Z = 8)
	# -------------------------------------------------------------
	# Right outer wall (Deep Green)
	_add_wall_box(static_root, Vector3(8.0, 2.0, 4.0), Vector3(0.2, 4.0, 8.0), mat_green)
	# Top wall with LARGE SUNLIGHT WINDOW at Z = 8
	_add_wall_box(static_root, Vector3(4.0, 0.6, 8.0), Vector3(8.0, 1.2, 0.2), mat_neutral_wall)  # lower sill
	_add_wall_box(static_root, Vector3(4.0, 3.5, 8.0), Vector3(8.0, 1.0, 0.2), mat_neutral_wall)  # upper lintel
	_add_wall_box(static_root, Vector3(0.8, 2.0, 8.0), Vector3(1.6, 2.0, 0.2), mat_neutral_wall)  # side post
	_add_wall_box(static_root, Vector3(7.2, 2.0, 8.0), Vector3(1.6, 2.0, 0.2), mat_neutral_wall)  # side post
	# (Window opening spans X: 1.6 to 6.4, Y: 1.2 to 3.0 allowing bright sunlight into Room B!)

	# -------------------------------------------------------------
	# BOUNDARY BETWEEN ROOM B AND ROOM C (at Z = 0, X: 0 to 8)
	# Fixed side sections + DESTRUCTIBLE DIVIDING WALL in the center!
	# -------------------------------------------------------------
	_add_wall_box(static_root, Vector3(1.0, 2.0, 0.0), Vector3(2.0, 4.0, 0.25), mat_neutral_wall)
	_add_wall_box(static_root, Vector3(7.0, 2.0, 0.0), Vector3(2.0, 4.0, 0.25), mat_neutral_wall)
	_add_wall_box(static_root, Vector3(4.0, 3.5, 0.0), Vector3(4.0, 1.0, 0.25), mat_neutral_wall)
	
	# Destructible Dividing Wall (Center section X: 2.0 to 6.0, Y: 0 to 3.0)
	destructible_wall = DestructibleWall.new()
	destructible_wall.name = "DestructibleDividingWall"
	destructible_wall.wall_id = 1
	destructible_wall.grid_width = 3
	destructible_wall.grid_height = 3
	destructible_wall.chunk_size = Vector3(1.3, 1.0, 0.25)
	destructible_wall.position = Vector3(4.0, 0.0, 0.0)
	destructible_wall.chunk_destroyed.connect(func(c_id, bounds): chunk_destroyed.emit(c_id, bounds))
	add_child(destructible_wall)

	# -------------------------------------------------------------
	# Room C (Deep Blue Wall, X: 0 to 8, Z: -8 to 0)
	# -------------------------------------------------------------
	# Right outer wall (Deep Blue)
	_add_wall_box(static_root, Vector3(8.0, 2.0, -4.0), Vector3(0.2, 4.0, 8.0), mat_blue)
	# Back outer wall
	_add_wall_box(static_root, Vector3(4.0, 2.0, -8.0), Vector3(8.0, 4.0, 0.2), mat_neutral_wall)
	# Boundary to Hallway (X = 0, Z: -8 to 0, with doorway)
	_add_wall_box(static_root, Vector3(0.0, 2.0, -6.75), Vector3(0.25, 4.0, 2.5), mat_neutral_wall)
	_add_wall_box(static_root, Vector3(0.0, 2.0, -1.25), Vector3(0.25, 4.0, 2.5), mat_neutral_wall)
	_add_wall_box(static_root, Vector3(0.0, 3.5, -4.0), Vector3(0.25, 1.0, 3.0), mat_neutral_wall)

	# Hallway outer walls
	_add_wall_box(static_root, Vector3(-8.0, 2.0, -4.0), Vector3(0.2, 4.0, 8.0), mat_neutral_wall)
	_add_wall_box(static_root, Vector3(-4.0, 2.0, -8.0), Vector3(8.0, 4.0, 0.2), mat_neutral_wall)
	
	# Irrelevant wall in closed hallway corner (for Test 7 verification)
	irrelevant_wall = DestructibleWall.new()
	irrelevant_wall.name = "IrrelevantCornerWall"
	irrelevant_wall.wall_id = 2
	irrelevant_wall.grid_width = 1
	irrelevant_wall.grid_height = 2
	irrelevant_wall.chunk_size = Vector3(1.2, 1.5, 0.2)
	irrelevant_wall.position = Vector3(-6.0, 0.0, -6.0)
	irrelevant_wall.chunk_destroyed.connect(func(c_id, bounds): chunk_destroyed.emit(c_id, bounds))
	add_child(irrelevant_wall)

	# -------------------------------------------------------------
	# Reference Objects as specified in Section 4
	# -------------------------------------------------------------
	# 1. WHITE SPHERE in Room C (Neutral lighting indicator right behind destructible wall!)
	_add_reference_sphere(static_root, Vector3(4.0, 1.2, -3.0), 1.2, mat_white_ref)
	# 2. WHITE CUBE in Room A
	_add_reference_cube(static_root, Vector3(-4.0, 0.9, 4.0), Vector3(1.8, 1.8, 1.8), mat_white_ref, Vector3(0, 25, 0))
	# 3. WHITE VERTICAL PILLAR in Room B
	_add_reference_cube(static_root, Vector3(5.5, 1.8, 4.0), Vector3(0.9, 3.6, 0.9), mat_white_ref)

	# -------------------------------------------------------------
	# Dynamic Occluder for Test 10 (Moving block between Lamp B and Wall)
	# -------------------------------------------------------------
	dynamic_occluder = AnimatableBody3D.new()
	dynamic_occluder.name = "DynamicOccluder"
	dynamic_occluder.position = Vector3(4.0, 2.0, 2.5)
	var occ_mi = MeshInstance3D.new()
	var occ_box = BoxMesh.new()
	occ_box.size = Vector3(1.8, 2.2, 0.6)
	occ_mi.mesh = occ_box
	occ_mi.material_override = mat_occluder
	dynamic_occluder.add_child(occ_mi)
	var occ_cs = CollisionShape3D.new()
	var occ_shape = BoxShape3D.new()
	occ_shape.size = Vector3(1.8, 2.2, 0.6)
	occ_cs.shape = occ_shape
	dynamic_occluder.add_child(occ_cs)
	add_child(dynamic_occluder)

	# -------------------------------------------------------------
	# Primary Lights as specified in Section 5 & Section 6
	# -------------------------------------------------------------
	var lights_root = Node3D.new()
	lights_root.name = "Lights"
	add_child(lights_root)
	
	# 1. Primary Exterior SUNLIGHT (DirectionalLight3D angled into Room B's window)
	var sun = DirectionalLight3D.new()
	sun.name = "SunLight"
	sun.light_color = Color(1.0, 0.96, 0.90)
	sun.light_energy = 3.2
	sun.shadow_enabled = true
	# Beam down from +Z (exterior) towards -Z and into Room B
	sun.rotation_degrees = Vector3(-35, 180, 0)
	lights_root.add_child(sun)
	dynamic_lights.append(sun)
	
	# 2. Lamp A in Room A (Warm Yellow, medium intensity)
	var lamp_a = OmniLight3D.new()
	lamp_a.name = "LampA"
	lamp_a.light_color = Color(1.0, 0.82, 0.45) # Warm Yellow
	lamp_a.light_energy = 2.0
	lamp_a.omni_range = 7.5
	lamp_a.omni_attenuation = 1.0
	lamp_a.shadow_enabled = true
	lamp_a.position = Vector3(-4.0, 3.2, 4.0)
	lights_root.add_child(lamp_a)
	dynamic_lights.append(lamp_a)
	
	# 3. Lamp B in Room B (Cool White, high intensity)
	var lamp_b = OmniLight3D.new()
	lamp_b.name = "LampB"
	lamp_b.light_color = Color(0.92, 0.96, 1.0) # Cool White
	lamp_b.light_energy = 2.8
	lamp_b.omni_range = 8.0
	lamp_b.omni_attenuation = 1.0
	lamp_b.shadow_enabled = true
	lamp_b.position = Vector3(4.0, 3.2, 4.0)
	lights_root.add_child(lamp_b)
	dynamic_lights.append(lamp_b)
	
	# 4. Lamp C in Room C (Blue, medium intensity)
	var lamp_c = OmniLight3D.new()
	lamp_c.name = "LampC"
	lamp_c.light_color = Color(0.25, 0.65, 1.0) # Blue
	lamp_c.light_energy = 1.8
	lamp_c.omni_range = 7.0
	lamp_c.omni_attenuation = 1.0
	lamp_c.shadow_enabled = true
	lamp_c.position = Vector3(4.0, 3.0, -4.0)
	lights_root.add_child(lamp_c)
	dynamic_lights.append(lamp_c)
	
	static_mesh_nodes.append(static_root)
	static_mesh_nodes.append(destructible_wall)
	static_mesh_nodes.append(irrelevant_wall)

func _process(delta: float) -> void:
	if occluder_moving and is_instance_valid(dynamic_occluder):
		occluder_time += delta * 1.5
		var offset_x = sin(occluder_time) * 2.2
		dynamic_occluder.position = Vector3(4.0 + offset_x, 2.0, 2.5)

func _create_mat(col: Color, p_name: String = "") -> StandardMaterial3D:
	var mat = StandardMaterial3D.new()
	mat.resource_name = p_name
	mat.albedo_color = col
	mat.roughness = 0.75
	return mat

func _add_wall_box(parent: Node3D, pos: Vector3, size: Vector3, mat: Material, rot_deg: Vector3 = Vector3.ZERO) -> StaticBody3D:
	var body = StaticBody3D.new()
	body.position = pos
	body.rotation_degrees = rot_deg
	
	var mi = MeshInstance3D.new()
	var box = BoxMesh.new()
	box.size = size
	mi.mesh = box
	mi.material_override = mat
	body.add_child(mi)
	
	var cs = CollisionShape3D.new()
	var shape = BoxShape3D.new()
	shape.size = size
	cs.shape = shape
	body.add_child(cs)
	
	parent.add_child(body)
	return body

func _add_reference_sphere(parent: Node3D, pos: Vector3, radius: float, mat: Material) -> StaticBody3D:
	var body = StaticBody3D.new()
	body.position = pos
	
	var mi = MeshInstance3D.new()
	var sphere = SphereMesh.new()
	sphere.radius = radius * 0.5
	sphere.height = radius
	sphere.radial_segments = 16
	sphere.rings = 8
	mi.mesh = sphere
	mi.material_override = mat
	body.add_child(mi)
	
	var cs = CollisionShape3D.new()
	var shape = SphereShape3D.new()
	shape.radius = radius * 0.5
	cs.shape = shape
	body.add_child(cs)
	
	parent.add_child(body)
	return body

func _add_reference_cube(parent: Node3D, pos: Vector3, size: Vector3, mat: Material, rot_deg: Vector3 = Vector3.ZERO) -> StaticBody3D:
	return _add_wall_box(parent, pos, size, mat, rot_deg)
