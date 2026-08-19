class_name ProbeContributionTable
extends RefCounted

# ==============================================================================
# ASTG PROBE CONTRIBUTION TABLE (SPARSE CSR TRANSFER MATRIX)
# Stores persistent source-to-probe geometric/material transfer coefficients.
# Supports deduplication, Top-K importance ranking, and lazy GPU/CPU evaluation.
# ==============================================================================

class Contribution:
	var light_id: int = 0
	var source_node_id: int = 0
	var transfer_rgb: Color = Color.BLACK
	var importance: float = 0.0

	func _init(p_light_id: int, p_transfer: Color, p_source_node_id: int = 0, p_importance: float = 0.0) -> void:
		light_id = p_light_id
		transfer_rgb = p_transfer
		source_node_id = p_source_node_id
		importance = p_importance if p_importance > 0.0 else p_transfer.get_luminance()

# Sparse per-probe contribution lists: probe_id -> Array[Contribution]
var probe_contributions: Dictionary = {}

# Raw candidate lists before Top-K ranking: probe_id -> Array[Contribution]
var probe_candidates: Dictionary = {}

# Configurable Top-K Fan-In Cap (Default 32, or 4096 for Unlimited)
var fan_in_cap: int = 32

# Cached evaluated state per probe
var cached_irradiances: Dictionary = {} # probe_id -> Color
var cached_signatures: Dictionary = {}   # probe_id -> int

# Statistics
var total_contributions: int = 0
var total_candidate_contributions: int = 0
var total_pruned_contributions: int = 0
var lazy_refreshes_count: int = 0
var cache_hits_count: int = 0
var last_eval_time_us: float = 0.0

func clear() -> void:
	probe_contributions.clear()
	probe_candidates.clear()
	cached_irradiances.clear()
	cached_signatures.clear()
	total_contributions = 0
	total_candidate_contributions = 0
	total_pruned_contributions = 0
	lazy_refreshes_count = 0
	cache_hits_count = 0

func add_candidate(probe_id: int, light_id: int, transfer_rgb: Color, source_node_id: int = 0, importance: float = 0.0) -> void:
	if transfer_rgb.r <= 0.00001 and transfer_rgb.g <= 0.00001 and transfer_rgb.b <= 0.00001:
		return

	if not probe_candidates.has(probe_id):
		probe_candidates[probe_id] = []

	var cand_list: Array = probe_candidates[probe_id]
	
	# Aggregate multiple paths from the same source light (Phase 9)
	var found = false
	for c in cand_list:
		if c.light_id == light_id:
			c.transfer_rgb += transfer_rgb
			c.importance += (importance if importance > 0.0 else transfer_rgb.get_luminance())
			found = true
			break

	if not found:
		var imp = importance if importance > 0.0 else transfer_rgb.get_luminance()
		cand_list.append(Contribution.new(light_id, transfer_rgb, source_node_id, imp))
		total_candidate_contributions += 1

func finalize_top_k_contributions() -> void:
	probe_contributions.clear()
	total_contributions = 0
	total_pruned_contributions = 0

	for probe_id in probe_candidates.keys():
		var cand_list: Array = probe_candidates[probe_id]
		
		# Sort by importance descending (Top-K)
		cand_list.sort_custom(func(a, b): return a.importance > b.importance)

		var k = min(cand_list.size(), fan_in_cap)
		var retained: Array = []
		for i in range(k):
			retained.append(cand_list[i])

		probe_contributions[probe_id] = retained
		cached_irradiances[probe_id] = Color.BLACK
		cached_signatures[probe_id] = -1

		total_contributions += k
		total_pruned_contributions += (cand_list.size() - k)

func add_contribution(probe_id: int, light_id: int, transfer_rgb: Color, source_node_id: int = 0, importance: float = 0.0) -> void:
	add_candidate(probe_id, light_id, transfer_rgb, source_node_id, importance)
	finalize_top_k_contributions()

func clear_probe(probe_id: int) -> void:
	if probe_contributions.has(probe_id):
		total_contributions -= probe_contributions[probe_id].size()
		probe_contributions.erase(probe_id)
		probe_candidates.erase(probe_id)
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
	last_eval_time_us = float(Time.get_ticks_usec() - t0)

	return total_rad

func refresh_batch(requested_probe_ids: Array, light_mgr: RefCounted) -> Dictionary:
	var refreshed = 0
	var cached = 0
	for p_id in requested_probe_ids:
		var prev_sig = cached_signatures.get(p_id, -1)
		get_or_refresh_probe_irradiance(p_id, light_mgr)
		var new_sig = cached_signatures.get(p_id, -1)
		if prev_sig == new_sig and prev_sig != -1:
			cached += 1
		else:
			refreshed += 1
	return {"refreshed_probes": refreshed, "cached_probes": cached}

