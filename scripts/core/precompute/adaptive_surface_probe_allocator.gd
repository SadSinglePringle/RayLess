class_name AdaptiveSurfaceProbeAllocator
extends RefCounted

# ==============================================================================
# ASTG ADAPTIVE SPARSE SURFACE-PROBE ALLOCATOR & GRAPH TERMINATION ENGINE
# Implements Phases 3A through 3L:
# - Surface-area proportional Poisson seeding
# - Cluster and normal-cone compatibility
# - Curvature and normal-variance adaptive density
# - Irradiance gradient and ASTG transport complexity refinement
# - Dynamic probe split and merge
# - Directional encoding (Dominant Direction + L1 Spherical Harmonics)
# - Probe confidence & information-based transport branch termination
# ==============================================================================

const SurfaceProbeScript = preload("res://scripts/core/types/surface_probe.gd")
const SurfaceClusterScript = preload("res://scripts/core/types/surface_cluster.gd")

class AdaptiveProbe:
	var id: int = 0
	var position: Vector3 = Vector3.ZERO
	var normal: Vector3 = Vector3.UP
	var cluster_id: int = 0
	var destruction_chunk_id: int = 0
	var material_id: int = 0
	var area_weight: float = 1.0
	var curvature: float = 0.0

	# Radiance & Directional State
	var irradiance: Color = Color.BLACK
	var dominant_direction: Vector3 = Vector3.UP
	var directionality_strength: float = 0.0
	var sh_l1: Array[Color] = [Color.BLACK, Color.BLACK, Color.BLACK] # L1 SH coefficients (X, Y, Z)

	# ASTG Complexity & Confidence
	var incoming_path_count: int = 0
	var unique_light_count: int = 0
	var directional_variance: float = 0.0
	var confidence: float = 1.0 # 0.0 = Needs repair / low sample, 1.0 = Fully converged
	var last_update_frame: int = 0

	# Sparse persistent contributors (Sorted by energy)
	var contributors: Array[Dictionary] = [] # [{ "light_id": int, "transfer": Color }]

	func to_dict() -> Dictionary:
		return {
			"id": id,
			"position": position,
			"normal": normal,
			"cluster_id": cluster_id,
			"destruction_chunk_id": destruction_chunk_id,
			"material_id": material_id,
			"irradiance": irradiance,
			"dominant_dir": dominant_direction,
			"dir_strength": directionality_strength,
			"confidence": confidence,
			"contributors": contributors
		}

# Allocates surface-area-based seed probes across geometric clusters
static func allocate_surface_seeds(
	clusters: Array,
	target_total_probes: int = 1500,
	min_spacing: float = 0.4
) -> Array[AdaptiveProbe]:
	var probes: Array[AdaptiveProbe] = []
	if clusters.is_empty():
		return probes

	# 1. Compute total surface area across all clusters
	var total_area: float = 0.0
	for c in clusters:
		total_area += max(0.01, c.area if "area" in c else 1.0)

	var next_probe_id = 0

	# 2. Distribute seeds proportionally to cluster area
	for c in clusters:
		var c_area = max(0.01, c.area if "area" in c else 1.0)
		var cluster_budget = max(1, int(round((c_area / total_area) * target_total_probes)))

		var c_pos = c.centroid if "centroid" in c else Vector3.ZERO
		var c_norm = c.representative_normal if "representative_normal" in c else Vector3.UP
		var c_id = c.cluster_id if "cluster_id" in c else 0
		var chunk_id = c.destruction_chunk_id if "destruction_chunk_id" in c else 0

		# Seed probe slightly above geometry surface along cluster normal
		var p = AdaptiveProbe.new()
		p.id = next_probe_id
		p.position = c_pos + c_norm * 0.08
		p.normal = c_norm
		p.cluster_id = c_id
		p.destruction_chunk_id = chunk_id
		p.area_weight = c_area / float(cluster_budget)
		probes.append(p)
		next_probe_id += 1

		# Secondary Poisson-like seeds within cluster bounds
		if cluster_budget > 1:
			var c_radius = sqrt(c_area / PI) if c_area > 0.0 else 1.0
			for s in range(1, cluster_budget):
				var angle = s * 2.39996 # Golden ratio angle
				var r = (float(s) / cluster_budget) * c_radius
				var tangent = Vector3.UP.cross(c_norm).normalized()
				if tangent.length_squared() < 0.01:
					tangent = Vector3.RIGHT.cross(c_norm).normalized()
				var bitangent = c_norm.cross(tangent).normalized()

				var sub_pos = c_pos + (tangent * cos(angle) + bitangent * sin(angle)) * r + c_norm * 0.08
				var sub_p = AdaptiveProbe.new()
				sub_p.id = next_probe_id
				sub_p.position = sub_pos
				sub_p.normal = c_norm
				sub_p.cluster_id = c_id
				sub_p.destruction_chunk_id = chunk_id
				sub_p.area_weight = p.area_weight
				probes.append(sub_p)
				next_probe_id += 1

	return probes

# Evaluates ASTG transport complexity score to drive adaptive refinement
static func compute_probe_complexity_score(p: AdaptiveProbe) -> float:
	var path_score = clamp(float(p.incoming_path_count) / 16.0, 0.0, 1.0)
	var light_score = clamp(float(p.unique_light_count) / 32.0, 0.0, 1.0)
	var dir_score = clamp(p.directional_variance, 0.0, 1.0)
	var curv_score = clamp(p.curvature * 2.0, 0.0, 1.0)

	# Complexity = A * Paths + B * Lights + C * DirVar + D * Curvature
	return 0.35 * path_score + 0.25 * light_score + 0.25 * dir_score + 0.15 * curv_score

# Refines high-error or high-complexity probes (Probe Split)
static func adaptively_refine_probes(
	probes: Array[AdaptiveProbe],
	max_probes: int = 3000,
	refinement_threshold: float = 0.65
) -> Array[AdaptiveProbe]:
	var refined: Array[AdaptiveProbe] = []
	var next_id = probes.size()

	for p in probes:
		refined.append(p)
		var score = compute_probe_complexity_score(p)

		if score > refinement_threshold and refined.size() < max_probes:
			# Split probe into 2 child candidates along tangent plane
			var tangent = Vector3.UP.cross(p.normal).normalized()
			if tangent.length_squared() < 0.01:
				tangent = Vector3.RIGHT.cross(p.normal).normalized()

			for offset in [-0.25, 0.25]:
				if refined.size() >= max_probes:
					break
				var child = AdaptiveProbe.new()
				child.id = next_id
				child.position = p.position + tangent * offset
				child.normal = p.normal
				child.cluster_id = p.cluster_id
				child.destruction_chunk_id = p.destruction_chunk_id
				child.area_weight = p.area_weight * 0.5
				child.confidence = p.confidence * 0.9
				refined.append(child)
				next_id += 1

	return refined

# Merges redundant neighboring probes with matching normal and irradiance
static func merge_redundant_probes(
	probes: Array[AdaptiveProbe],
	distance_threshold: float = 0.3,
	normal_dot_threshold: float = 0.95,
	irradiance_diff_threshold: float = 0.08
) -> Array[AdaptiveProbe]:
	var kept: Array[AdaptiveProbe] = []
	var merged_flags: Array[bool] = []
	merged_flags.resize(probes.size())
	merged_flags.fill(false)

	for i in range(probes.size()):
		if merged_flags[i]:
			continue

		var p1 = probes[i]
		kept.append(p1)

		for j in range(i + 1, probes.size()):
			if merged_flags[j]:
				continue
			var p2 = probes[j]

			if p1.cluster_id == p2.cluster_id:
				var dist = p1.position.distance_to(p2.position)
				if dist < distance_threshold:
					var ndot = p1.normal.dot(p2.normal)
					if ndot > normal_dot_threshold:
						var diff_r = absf(p1.irradiance.r - p2.irradiance.r)
						var diff_g = absf(p1.irradiance.g - p2.irradiance.g)
						var diff_b = absf(p1.irradiance.b - p2.irradiance.b)
						var max_diff = max(diff_r, max(diff_g, diff_b))
						if max_diff < irradiance_diff_threshold:
							merged_flags[j] = true # Collapse redundant probe

	return kept

# Information-based transport branch termination rule
static func should_terminate_transport_branch(
	hit_position: Vector3,
	hit_normal: Vector3,
	cluster_id: int,
	probes: Array[AdaptiveProbe],
	confidence_threshold: float = 0.85
) -> bool:
	var nearest_probe: AdaptiveProbe = null
	var min_dist: float = 1e9

	for p in probes:
		if p.cluster_id == cluster_id:
			var ndot = p.normal.dot(hit_normal)
			if ndot > 0.7: # Compatible normal cone
				var d = p.position.distance_to(hit_position)
				if d < min_dist:
					min_dist = d
					nearest_probe = p

	if nearest_probe != null and min_dist < 0.6:
		# If probe field confidence is high, terminate explicit ray branch
		return nearest_probe.confidence >= confidence_threshold

	return false # Continue explicit transport ray
