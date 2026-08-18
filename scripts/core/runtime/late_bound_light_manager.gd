class_name LateBoundLightManager
extends RefCounted

# ==============================================================================
# ASTG LATE-BOUND LIGHT MANAGER
# Maintains compact GPU/CPU LightState records with generation version tracking.
# Turning light color, intensity, and on/off changes into O(1) constant-time writes.
# ==============================================================================

class LightEntry:
	var id: int = 0
	var color: Color = Color.WHITE
	var intensity: float = 1.0
	var enabled: bool = true
	var generation: int = 1
	var flags: int = 0

var lights: Dictionary = {} # light_id -> LightEntry
var total_modifications: int = 0
var last_update_time_us: float = 0.0

func register_light(light_id: int, p_color: Color = Color.WHITE, p_intensity: float = 1.0, p_enabled: bool = true) -> void:
	var entry = LightEntry.new()
	entry.id = light_id
	entry.color = p_color
	entry.intensity = p_intensity
	entry.enabled = p_enabled
	entry.generation = 1
	entry.flags = 0
	lights[light_id] = entry

func unregister_light(light_id: int) -> void:
	if lights.has(light_id):
		var entry: LightEntry = lights[light_id]
		entry.enabled = false
		entry.intensity = 0.0
		entry.generation += 1 # Generation bump invalidates dependents safely
		lights.erase(light_id)

func set_light_color(light_id: int, p_color: Color) -> void:
	var t0 = Time.get_ticks_usec()
	if lights.has(light_id):
		var entry: LightEntry = lights[light_id]
		if entry.color != p_color:
			entry.color = p_color
			entry.generation += 1
			total_modifications += 1
	var t1 = Time.get_ticks_usec()
	last_update_time_us = float(t1 - t0)

func set_light_energy(light_id: int, p_intensity: float) -> void:
	var t0 = Time.get_ticks_usec()
	if lights.has(light_id):
		var entry: LightEntry = lights[light_id]
		if abs(entry.intensity - p_intensity) > 0.0001:
			entry.intensity = p_intensity
			entry.generation += 1
			total_modifications += 1
	var t1 = Time.get_ticks_usec()
	last_update_time_us = float(t1 - t0)

func set_light_enabled(light_id: int, p_enabled: bool) -> void:
	var t0 = Time.get_ticks_usec()
	if lights.has(light_id):
		var entry: LightEntry = lights[light_id]
		if entry.enabled != p_enabled:
			entry.enabled = p_enabled
			entry.generation += 1
			total_modifications += 1
	var t1 = Time.get_ticks_usec()
	last_update_time_us = float(t1 - t0)

func get_emission(light_id: int) -> Color:
	if lights.has(light_id):
		var entry: LightEntry = lights[light_id]
		if entry.enabled and entry.intensity > 0.0001:
			return entry.color * entry.intensity
	return Color.BLACK

func get_generation(light_id: int) -> int:
	if lights.has(light_id):
		return lights[light_id].generation
	return 0

func get_light_entry(light_id: int) -> LightEntry:
	return lights.get(light_id, null)

func get_total_lights() -> int:
	return lights.size()
