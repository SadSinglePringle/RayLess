class_name MassiveLightSystem
extends RefCounted

# ==============================================================================
# ASTG MASSIVE STATIONARY LIGHT SYSTEM (1,000 to 128,000 LIGHTS)
# Manages massive stationary emitter populations purely in contiguous GPU buffers
# without scene-tree node overhead or Forward+ clustered light limits.
# ==============================================================================

const LateBoundLightManagerScript = preload("res://scripts/core/runtime/late_bound_light_manager.gd")
const ProbeContributionTableScript = preload("res://scripts/core/runtime/probe_contribution_table.gd")

class StationaryLight:
	var id: int = 0
	var position: Vector3 = Vector3.ZERO
	var range: float = 3.5
	var direction: Vector3 = Vector3.DOWN
	var type: int = 0 # 0=Omni, 1=Spot, 2=Directional
	var anim_phase: float = 0.0
	var anim_frequency: float = 1.0
	var base_hue: float = 0.0
	var base_intensity: float = 5.0
	var is_occluded: bool = false

var lights: Array[StationaryLight] = []
var active_light_count: int = 0
var light_manager: RefCounted = null
var contribution_table: RefCounted = null
var scene_bounds: AABB = AABB(Vector3(-5.0, 0.0, -5.0), Vector3(10.0, 4.0, 10.0))

func initialize(
	count: int,
	p_bounds: AABB,
	p_light_mgr: RefCounted,
	p_contrib_table: RefCounted
) -> void:
	active_light_count = count
	scene_bounds = p_bounds
	light_manager = p_light_mgr
	contribution_table = p_contrib_table
	lights.clear()

	var rng = RandomNumberGenerator.new()
	rng.seed = 42 # Fixed deterministic seed

	var min_p = scene_bounds.position
	var max_p = scene_bounds.end

	for i in range(count):
		var sl = StationaryLight.new()
		sl.id = i
		sl.anim_phase = rng.randf_range(0.0, TAU)
		sl.anim_frequency = rng.randf_range(0.5, 3.0)
		sl.base_hue = float(i) / float(max(1, count))
		sl.base_intensity = rng.randf_range(2.0, 8.0)

		# Distribution strategy across architectural surfaces:
		var zone = i % 4
		if zone == 0:
			# Ceiling fixtures
			sl.position = Vector3(
				rng.randf_range(min_p.x + 0.5, max_p.x - 0.5),
				max_p.y - rng.randf_range(0.1, 0.4),
				rng.randf_range(min_p.z + 0.5, max_p.z - 0.5)
			)
			sl.direction = Vector3.DOWN
			sl.range = rng.randf_range(3.0, 5.0)
		elif zone == 1:
			# Wall fixtures (Perimeter)
			var wall_side = i % 4
			var wy = rng.randf_range(min_p.y + 1.2, max_p.y - 0.8)
			if wall_side == 0:
				sl.position = Vector3(min_p.x + 0.1, wy, rng.randf_range(min_p.z + 0.5, max_p.z - 0.5))
				sl.direction = Vector3.RIGHT
			elif wall_side == 1:
				sl.position = Vector3(max_p.x - 0.1, wy, rng.randf_range(min_p.z + 0.5, max_p.z - 0.5))
				sl.direction = Vector3.LEFT
			elif wall_side == 2:
				sl.position = Vector3(rng.randf_range(min_p.x + 0.5, max_p.x - 0.5), wy, min_p.z + 0.1)
				sl.direction = Vector3.BACK
			else:
				sl.position = Vector3(rng.randf_range(min_p.x + 0.5, max_p.x - 0.5), wy, max_p.z - 0.1)
				sl.direction = Vector3.FORWARD
			sl.range = rng.randf_range(2.5, 4.0)
		elif zone == 2:
			# Desk / Task lights
			sl.position = Vector3(
				rng.randf_range(min_p.x + 1.0, max_p.x - 1.0),
				min_p.y + rng.randf_range(0.7, 0.95),
				rng.randf_range(min_p.z + 1.0, max_p.z - 1.0)
			)
			sl.direction = Vector3.DOWN
			sl.range = rng.randf_range(1.5, 3.0)
		else:
			# Corridors / Doorway portals
			sl.position = Vector3(
				rng.randf_range(min_p.x + 0.8, max_p.x - 0.8),
				min_p.y + rng.randf_range(2.0, 2.8),
				rng.randf_range(min_p.z + 0.8, max_p.z - 0.8)
			)
			sl.direction = Vector3.DOWN
			sl.range = rng.randf_range(2.5, 4.5)

		sl.is_occluded = (rng.randf() < 0.15) # 15% naturally occluded
		lights.append(sl)

		# Register initial state in LateBoundLightManager
		var init_color = Color.from_hsv(sl.base_hue, 0.7, 1.0)
		light_manager.register_light(sl.id, init_color, sl.base_intensity, true)

func generate_contributions_for_probes(probes: Array) -> void:
	contribution_table.clear()
	for sl in lights:
		if sl.is_occluded:
			continue
		var r_sq = sl.range * sl.range
		for p in probes:
			var p_pos = p.position if ("position" in p) else Vector3.ZERO
			var dist_sq = sl.position.distance_squared_to(p_pos)
			if dist_sq > r_sq:
				continue
			var dist = sqrt(dist_sq)
			var p_norm = p.normal if ("normal" in p) else Vector3.UP
			var p_id = p.id if ("id" in p) else 0

			var l_dir = (sl.position - p_pos) / max(0.001, dist)
			var ndotl = max(0.0, p_norm.dot(l_dir))
			if ndotl < 0.2:
				continue

			var atten = (1.0 - (dist / sl.range)) * ndotl / (dist_sq + 1.0)
			var w = atten * 0.15
			var importance = w * ndotl
			if contribution_table.has_method("add_candidate"):
				contribution_table.add_candidate(p_id, sl.id, Color(w, w, w), 0, importance)
			else:
				contribution_table.add_contribution(p_id, sl.id, Color(w, w, w))

	if contribution_table.has_method("finalize_top_k_contributions"):
		contribution_table.finalize_top_k_contributions()

func animate_lights_cpu(time_sec: float, anim_mode: int, frame_idx: int) -> int:
	var writes = 0
	for sl in lights:
		var col = Color.WHITE
		var intensity = sl.base_intensity
		var enabled = true

		if anim_mode == 0:
			# Intensity only
			intensity = sl.base_intensity * (0.5 + 0.5 * sin(sl.anim_frequency * time_sec + sl.anim_phase))
			col = Color.from_hsv(sl.base_hue, 0.7, 1.0)
		elif anim_mode == 1:
			# RGB only
			var current_hue = fposmod(sl.base_hue + sl.anim_frequency * 0.1 * time_sec, 1.0)
			col = Color.from_hsv(current_hue, 0.8, 1.0)
			intensity = sl.base_intensity
		elif anim_mode == 2:
			# Enable/disable toggling
			col = Color.from_hsv(sl.base_hue, 0.7, 1.0)
			intensity = sl.base_intensity
			enabled = ((sl.id + frame_idx / 30) % 5) != 0
		elif anim_mode == 3:
			# RGB + Intensity
			var current_hue = fposmod(sl.base_hue + sl.anim_frequency * 0.15 * time_sec, 1.0)
			col = Color.from_hsv(current_hue, 0.8, 1.0)
			intensity = sl.base_intensity * (0.5 + 0.5 * sin(sl.anim_frequency * time_sec + sl.anim_phase))
		elif anim_mode == 4:
			# Full Chaos
			var current_hue = fposmod(sl.base_hue + sl.anim_frequency * 0.2 * time_sec + float(sl.id) * 0.001, 1.0)
			col = Color.from_hsv(current_hue, 0.85, 1.0)
			intensity = 0.5 + sl.base_intensity * (0.5 + 0.5 * sin(sl.anim_frequency * 1.5 * time_sec + sl.anim_phase))
			enabled = ((sl.id + frame_idx / 20) % 10) != 0

		light_manager.set_light_color(sl.id, col)
		light_manager.set_light_energy(sl.id, intensity)
		light_manager.set_light_enabled(sl.id, enabled)
		writes += 3

	return writes

func compute_sparsity_statistics(probes_count: int) -> Dictionary:
	var total_records = contribution_table.total_contributions
	var fan_in_counts: Array[int] = []

	for p_id in contribution_table.get_all_probe_ids():
		fan_in_counts.append(contribution_table.get_contributing_light_count(p_id))

	if fan_in_counts.is_empty():
		return {
			"total_records": 0,
			"avg_lights_per_probe": 0.0,
			"median_lights_per_probe": 0,
			"p90_lights_per_probe": 0,
			"p95_lights_per_probe": 0,
			"p99_lights_per_probe": 0,
			"max_lights_per_probe": 0,
			"records_per_light": 0.0,
			"records_per_probe": 0.0
		}

	fan_in_counts.sort()
	var n = fan_in_counts.size()
	var avg = float(total_records) / max(1.0, float(probes_count))
	var med = fan_in_counts[int(n * 0.50)]
	var p90 = fan_in_counts[int(n * 0.90)]
	var p95 = fan_in_counts[int(n * 0.95)]
	var p99 = fan_in_counts[int(n * 0.99)]
	var max_v = fan_in_counts[n - 1]

	return {
		"total_records": total_records,
		"avg_lights_per_probe": avg,
		"median_lights_per_probe": med,
		"p90_lights_per_probe": p90,
		"p95_lights_per_probe": p95,
		"p99_lights_per_probe": p99,
		"max_lights_per_probe": max_v,
		"records_per_light": float(total_records) / max(1.0, float(active_light_count)),
		"records_per_probe": float(total_records) / max(1.0, float(probes_count))
	}

func calculate_vram_breakdown(probes_count: int, triangles_count: int) -> Dictionary:
	var static_bytes = active_light_count * 32
	var dynamic_bytes = active_light_count * 32
	var node_bytes = triangles_count * 64
	var edge_bytes = triangles_count * 16
	var contrib_bytes = contribution_table.total_contributions * 16
	var probe_bytes = probes_count * 16 + probes_count * 8
	var hierarchy_bytes = active_light_count * 256
	var rt_as_bytes = (triangles_count * 128) + 65536
	var total_bytes = static_bytes + dynamic_bytes + node_bytes + edge_bytes + contrib_bytes + probe_bytes + hierarchy_bytes + rt_as_bytes

	return {
		"light_static_mb": float(static_bytes) / 1048576.0,
		"light_dynamic_mb": float(dynamic_bytes) / 1048576.0,
		"transport_nodes_mb": float(node_bytes) / 1048576.0,
		"transport_edges_mb": float(edge_bytes) / 1048576.0,
		"contribution_records_mb": float(contrib_bytes) / 1048576.0,
		"probes_mb": float(probe_bytes) / 1048576.0,
		"angular_hierarchy_mb": float(hierarchy_bytes) / 1048576.0,
		"rt_as_mb": float(rt_as_bytes) / 1048576.0,
		"total_astg_vram_mb": float(total_bytes) / 1048576.0
	}
