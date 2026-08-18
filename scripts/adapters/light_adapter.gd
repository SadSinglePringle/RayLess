class_name ASTGLightAdapter
extends RefCounted

# ==============================================================================
# ASTG LIGHT ADAPTER (GODOT LIGHTS & GPU POPULATIONS → ASTG INTERFACE)
# Separates immutable spatial topology from live dynamic energy state
# ==============================================================================

signal topology_invalidated(light_id: int)
signal energy_state_updated(light_id: int)

enum LightClass {
	ASTG_STATIONARY = 0, # ASTG Transport + Godot Direct Light
	ASTG_DYNAMIC = 1,    # Godot Direct Light only (Moving lights)
	ASTG_EXCLUDED = 2
}

class ManagedLight:
	var id: int = 0
	var classification: int = LightClass.ASTG_STATIONARY
	var position: Vector3 = Vector3.ZERO
	var previous_position: Vector3 = Vector3.ZERO
	var direction: Vector3 = Vector3.DOWN
	var range_dist: float = 6.0
	var type: int = 0 # 0=Omni, 1=Spot, 2=Directional
	
	# Dynamic Live State
	var color: Color = Color.WHITE
	var intensity: float = 3.5
	var enabled: bool = true
	var generation: int = 1
	var anim_frequency: float = 1.0
	var anim_phase: float = 0.0
	var base_hue: float = 0.0

var lights: Array[ManagedLight] = []
var stationary_light_count: int = 0

# Initializes massive GPU light population from bounds
func initialize_population(count: int, bounds: AABB) -> void:
	lights.clear()
	stationary_light_count = count
	var min_p = bounds.position
	var max_p = bounds.end

	for i in range(count):
		var l = ManagedLight.new()
		l.id = i
		l.classification = LightClass.ASTG_STATIONARY

		var fx = float(i % 350) / 350.0
		var fz = float(i / 350) / 365.0
		l.position = Vector3(
			min_p.x + fx * (max_p.x - min_p.x),
			min_p.y + 2.5 + (i % 5) * 1.5,
			min_p.z + fz * (max_p.z - min_p.z)
		)
		l.previous_position = l.position
		l.range_dist = 4.0 + (i % 4)
		l.anim_frequency = 0.5 + (i % 10) * 0.25
		l.anim_phase = float(i % 100) * 0.0628
		l.base_hue = float(i) / max(1.0, float(count))
		l.color = Color.from_hsv(l.base_hue, 0.7, 1.0)
		l.intensity = 4.0
		l.enabled = true
		l.generation = 1
		lights.append(l)

# Updates light energy state (Zero-Topology-Mutation Guarantee)
func update_energy_state(light_id: int, col: Color, inten: float, is_on: bool) -> void:
	if light_id < 0 or light_id >= lights.size():
		return
	var l = lights[light_id]
	if l.color != col or l.intensity != inten or l.enabled != is_on:
		l.color = col
		l.intensity = inten
		l.enabled = is_on
		l.generation += 1
		energy_state_updated.emit(light_id)

# Updates light transform (Triggers Topology Event only on movement)
func update_transform(light_id: int, new_pos: Vector3, new_dir: Vector3) -> void:
	if light_id < 0 or light_id >= lights.size():
		return
	var l = lights[light_id]
	if l.position.distance_squared_to(new_pos) > 0.001 or l.direction.dot(new_dir) < 0.999:
		l.previous_position = l.position
		l.position = new_pos
		l.direction = new_dir
		topology_invalidated.emit(light_id)
