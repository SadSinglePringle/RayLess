class_name ClassroomBuilder
extends Node3D

signal chunk_destroyed(chunk_id: int, bounds: AABB)

var classroom_model_root: Node3D
var destructible_shutters: DestructibleWall
var destructible_blackboard: DestructibleWall
var dynamic_lights: Array[Light3D] = []
var static_mesh_nodes: Array[Node3D] = []
var ceiling_lights: Array[OmniLight3D] = []

var classroom_bounds: AABB = AABB(Vector3(-4.5, 0.0, -4.5), Vector3(9.0, 3.8, 9.3))

func build_classroom() -> void:
	for child in get_children():
		child.queue_free()
	dynamic_lights.clear()
	static_mesh_nodes.clear()
	ceiling_lights.clear()
	
	print("[ClassroomBuilder] Loading Blender Classroom GLB model...")
	var glb_path = "res://assets/classroom/classroom.glb"
	var gltf_doc = GLTFDocument.new()
	var gltf_state = GLTFState.new()
	var err = gltf_doc.append_from_file(glb_path, gltf_state)
	
	if err == OK:
		classroom_model_root = gltf_doc.generate_scene(gltf_state)
		if classroom_model_root != null:
			classroom_model_root.name = "ClassroomModel"
			add_child(classroom_model_root)
			static_mesh_nodes.append(classroom_model_root)
			
			# Generate static collision bodies on meshes for physics ray casting
			_setup_mesh_collisions(classroom_model_root)
			print("[ClassroomBuilder] Successfully instantiated 838-mesh Blender Classroom with PBR materials!")
	else:
		printerr("[ClassroomBuilder] Failed to load classroom.glb (Error %d)" % err)
		
	# -------------------------------------------------------------
	# Destructible Elements for ASTG Event-Driven Testing
	# -------------------------------------------------------------
	# 1. Destructible Window Shutter Section (Aligned with Window Openings at +X = 3.42m)
	destructible_shutters = DestructibleWall.new()
	destructible_shutters.name = "DestructibleWindowShutters"
	destructible_shutters.wall_id = 1
	destructible_shutters.grid_width = 3
	destructible_shutters.grid_height = 2
	destructible_shutters.chunk_size = Vector3(0.12, 1.0, 0.8)
	destructible_shutters.wall_color = Color(0.25, 0.18, 0.12) # Dark wood shutter
	destructible_shutters.position = Vector3(3.42, 0.6, -0.67)
	destructible_shutters.chunk_destroyed.connect(func(c_id, bounds): chunk_destroyed.emit(c_id, bounds))
	add_child(destructible_shutters)
	static_mesh_nodes.append(destructible_shutters)
	
	# 2. Destructible Chalkboard Panel (Aligned with Blackboard Wall at Z = -3.25m)
	destructible_blackboard = DestructibleWall.new()
	destructible_blackboard.name = "DestructibleBlackboardPanel"
	destructible_blackboard.wall_id = 2
	destructible_blackboard.grid_width = 2
	destructible_blackboard.grid_height = 2
	destructible_blackboard.chunk_size = Vector3(0.9, 0.7, 0.08)
	destructible_blackboard.wall_color = Color(0.12, 0.22, 0.15) # Slate Green
	destructible_blackboard.position = Vector3(0.897, 0.85, -3.24)
	destructible_blackboard.chunk_destroyed.connect(func(c_id, bounds): chunk_destroyed.emit(c_id, bounds))
	add_child(destructible_blackboard)
	static_mesh_nodes.append(destructible_blackboard)

	# -------------------------------------------------------------
	# Classroom Lighting Rig (Aligned with Classroom Blender Model)
	# -------------------------------------------------------------
	var lights_root = Node3D.new()
	lights_root.name = "Lights"
	add_child(lights_root)
	
	# 1. Exterior Sunlight streaming through the side windows (+X) into the room (-X, -Z)
	var sun = DirectionalLight3D.new()
	sun.name = "ClassroomSun"
	sun.light_color = Color(1.0, 0.95, 0.86) # Warm morning sunlight
	sun.light_energy = 2.0
	sun.shadow_enabled = true
	# Angle: Shines from +X = 3.5m through the window frames towards desks and chalkboard
	sun.rotation_degrees = Vector3(-35.0, -110.0, 0.0)
	lights_root.add_child(sun)
	dynamic_lights.append(sun)
	
	# 2. Classroom Ceiling Pendant Lamps (6 fixtures aligned with 3D model lamps)
	var lamp_positions = [
		Vector3(-0.598, 2.68, -1.414),
		Vector3( 1.993, 2.68, -1.414),
		Vector3(-0.598, 2.68,  0.668),
		Vector3( 1.993, 2.68,  0.668),
		Vector3(-0.598, 2.68,  2.730),
		Vector3( 1.993, 2.68,  2.730)
	]
	
	for i in range(lamp_positions.size()):
		var lamp = OmniLight3D.new()
		lamp.name = "CeilingLamp_%d" % (i + 1)
		lamp.light_color = Color(0.96, 0.95, 1.0) # Clean neutral white
		lamp.light_energy = 0.6
		lamp.omni_range = 5.0
		lamp.omni_attenuation = 1.0
		lamp.shadow_enabled = true
		lamp.position = lamp_positions[i]
		lights_root.add_child(lamp)
		dynamic_lights.append(lamp)
		ceiling_lights.append(lamp)
		
	# 3. Blackboard Light (Spot/Omni over chalkboard)
	var bb_lamp = OmniLight3D.new()
	bb_lamp.name = "BlackboardLamp"
	bb_lamp.light_color = Color(1.0, 0.95, 0.9)
	bb_lamp.light_energy = 0.5
	bb_lamp.omni_range = 3.0
	bb_lamp.shadow_enabled = true
	bb_lamp.position = Vector3(0.898, 2.10, -3.12)
	lights_root.add_child(bb_lamp)
	dynamic_lights.append(bb_lamp)
	
	# 4. Teacher Desk Task Lamp
	var teacher_lamp = OmniLight3D.new()
	teacher_lamp.name = "TeacherDeskLamp"
	teacher_lamp.light_color = Color(1.0, 0.8, 0.4) # Warm task light
	teacher_lamp.light_energy = 0.7
	teacher_lamp.omni_range = 3.0
	teacher_lamp.shadow_enabled = true
	teacher_lamp.position = Vector3(1.904, 0.95, -1.293)
	lights_root.add_child(teacher_lamp)
	dynamic_lights.append(teacher_lamp)

func _setup_mesh_collisions(node: Node) -> void:
	var stack: Array[Node] = [node]
	var collider_count = 0
	
	while stack.size() > 0:
		var curr = stack.pop_back()
		if curr is MeshInstance3D:
			var mi: MeshInstance3D = curr
			if mi.mesh != null:
				var aabb = mi.get_aabb()
				# Skip tiny stationery/decor props to keep ray tracing ultra-fast
				if aabb.size.length() >= 0.2:
					var body = StaticBody3D.new()
					body.name = "Collider_" + mi.name
					var shape = mi.mesh.create_convex_shape(true, false)
					if shape != null:
						var cs = CollisionShape3D.new()
						cs.shape = shape
						body.add_child(cs)
						mi.add_child(body)
						collider_count += 1
					
		for child in curr.get_children():
			if not (child is StaticBody3D):
				stack.append(child)
				
	print("[ClassroomBuilder] Generated %d static colliders for physics ray tracing." % collider_count)

func toggle_ceiling_lights() -> void:
	var is_on = false
	if ceiling_lights.size() > 0:
		is_on = not ceiling_lights[0].visible
	for l in ceiling_lights:
		l.visible = is_on
