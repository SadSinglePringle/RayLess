class_name ProbeContributionTable
extends RefCounted

# ==============================================================================
# ASTG PROBE CONTRIBUTION TABLE (SPARSE CSR TRANSFER MATRIX)
# Stores persistent source-to-probe geometric/material transfer coefficients.
# Evaluates probe irradiance lazily on demand when sampled or visible.
# ==============================================================================

class Contribution:
	var light_id: int = 0
	var transfer_rgb: Color = Color.BLACK

	func _init(p_light_id: int, p_transfer: Color) -> void:
		light_id = p_light_id
		transfer_rgb = p_transfer

# Sparse per-probe contribution lists
var probe_contributions: Dictionary = {} # probe_id -> Array[Contribution]

# Cached evaluated state per probe
var cached_irradiances: Dictionary = {} # probe_id -> Color
var cached_signatures: Dictionary = {}   # probe_id -> int

# Statistics
var total_contributions: int = 0
var lazy_refreshes_count: int = 0
var cache_hits_count: int = 0
var last_eval_time_us: float = 0.0

func clear() -> void:
	probe_contributions.clear()
	cached_irradiances.clear()
	cached_signatures.clear()
	total_contributions = 0
	lazy_refreshes_count = 0
	cache_hits_count = 0

func add_contribution(probe_id: int, light_id: int, transfer_rgb: Color) -> void:
	if transfer_rgb.r <= 0.00001 and transfer_rgb.g <= 0.00001 and transfer_rgb.b <= 0.00001:
		return

	if not probe_contributions.has(probe_id):
		probe_contributions[probe_id] = []
		cached_irradiances[probe_id] = Color.BLACK
		cached_signatures[probe_id] = -1

	# Merge if existing contribution for this light exists
	var contrib_list: Array = probe_contributions[probe_id]
	var found = false
	for c in contrib_list:
		if c.light_id == light_id:
			c.transfer_rgb += transfer_rgb
			found = true
			break

	if not found:
		contrib_list.append(Contribution.new(light_id, transfer_rgb))
		total_contributions += 1

	cached_signatures[probe_id] = -1 # Invalidate cache

func clear_probe(probe_id: int) -> void:
	if probe_contributions.has(probe_id):
		total_contributions -= probe_contributions[probe_id].size()
		probe_contributions.erase(probe_id)
		cached_irradiances.erase(probe_id)
		cached_signatures.erase(probe_id)

func get_or_refresh_probe_irradiance(probe_id: int, light_mgr: RefCounted) -> Color:
	if not probe_contributions.has(probe_id):
		return Color.BLACK

	var contrib_list: Array = probe_contributions[probe_id]
	if contrib_list.is_empty():
		return Color.BLACK

	# 1. Compute current version signature across contributing lights
	var current_sig: int = 17
	for c in contrib_list:
		var gen = light_mgr.get_generation(c.light_id)
		current_sig = ((current_sig * 31) + gen) & 0x7FFFFFFF

	# 2. Check if cache is already up to date (O(1) fast path)
	if cached_signatures.get(probe_id, -1) == current_sig:
		cache_hits_count += 1
		return cached_irradiances.get(probe_id, Color.BLACK)

	# 3. Lazy evaluation on demand
	var t0 = Time.get_ticks_usec()
	var total_rad = Color.BLACK

	for c in contrib_list:
		var emission: Color = light_mgr.get_emission(c.light_id)
		if emission.r > 0.0001 or emission.g > 0.0001 or emission.b > 0.0001:
			total_rad.r += emission.r * c.transfer_rgb.r
			total_rad.g += emission.g * c.transfer_rgb.g
			total_rad.b += emission.b * c.transfer_rgb.b

	cached_irradiances[probe_id] = total_rad
	cached_signatures[probe_id] = current_sig
	lazy_refreshes_count += 1
	var t1 = Time.get_ticks_usec()
	last_eval_time_us = float(t1 - t0)

	return total_rad

func refresh_batch(requested_probe_ids: Array[int], light_mgr: RefCounted) -> Dictionary:
	var t0 = Time.get_ticks_usec()
	var refreshed_count = 0
	var hit_count = 0

	for p_id in requested_probe_ids:
		if not probe_contributions.has(p_id):
			continue

		var contrib_list: Array = probe_contributions[p_id]
		var current_sig: int = 17
		for c in contrib_list:
			var gen = light_mgr.get_generation(c.light_id)
			current_sig = ((current_sig * 31) + gen) & 0x7FFFFFFF

		if cached_signatures.get(p_id, -1) == current_sig:
			hit_count += 1
			continue

		var total_rad = Color.BLACK
		for c in contrib_list:
			var emission: Color = light_mgr.get_emission(c.light_id)
			if emission.r > 0.0001 or emission.g > 0.0001 or emission.b > 0.0001:
				total_rad.r += emission.r * c.transfer_rgb.r
				total_rad.g += emission.g * c.transfer_rgb.g
				total_rad.b += emission.b * c.transfer_rgb.b

		cached_irradiances[p_id] = total_rad
		cached_signatures[p_id] = current_sig
		refreshed_count += 1

	var t1 = Time.get_ticks_usec()
	var elapsed_ms = float(t1 - t0) / 1000.0

	return {
		"time_ms": elapsed_ms,
		"refreshed_probes": refreshed_count,
		"cached_probes": hit_count,
		"total_requested": requested_probe_ids.size()
	}

func get_contributing_light_count(probe_id: int) -> int:
	if probe_contributions.has(probe_id):
		return probe_contributions[probe_id].size()
	return 0

func get_all_probe_ids() -> Array:
	return probe_contributions.keys()
