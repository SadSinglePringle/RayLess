class_name SurfaceAwareProbeLookup
extends RefCounted

# ==============================================================================
# ASTG SURFACE-AWARE PROBE LOOKUP & ANTI-BLEEDING ENGINE
# Implements 4-tier topology-driven search hierarchy:
#   Level 1: Exact Surface Cluster (Weight = 1.0)
#   Level 2: Adjacent Compatible Clusters (Coplanar = 0.8, Smooth = 0.5, Sharp = 0.0)
#   Level 3: Topologically Compatible Surfaces
#   Level 4: Conservative World-Space Fallback (Strict normal match >= 0.85)
# Normal Sharpness: pow(max(N_hit · N_probe, 0.0), 4.0)
# Normalization: Σ w_i = 1.0 (Density invariance)
# ==============================================================================

const NORMAL_MIN_DOT: float = 0.80           # ~37 deg max divergence
const NORMAL_SHARPNESS: float = 4.0          # High-order normal rejection
const MAX_PROBE_SEARCH_RADIUS: float = 3.5   # Maximum surface search distance
const MIN_REQUIRED_PROBES: int = 4           # Target candidate count

# Global diagnostic telemetry counters
static var exact_cluster_hits: int = 0
static var adjacent_cluster_hits: int = 0
static var world_space_fallback_hits: int = 0
static var normal_rejections: int = 0
static var topology_rejections: int = 0

static func reset_metrics() -> void:
	exact_cluster_hits = 0
	adjacent_cluster_hits = 0
	world_space_fallback_hits = 0
	normal_rejections = 0
	topology_rejections = 0

# ------------------------------------------------------------------------------
# 1. SURFACE-AWARE PROBE DEPOSITION (Transport Node -> Probes)
# ------------------------------------------------------------------------------
static func deposit_to_surface_probes(
	hit_pos: Vector3,
	hit_norm: Vector3,
	cluster_id: int,
	radiance: Color,
	clusters: Array[SurfaceCluster],
	probes: Array[SurfaceProbe],
	is_direct: bool
) -> int:
	var accepted_probes: Array[Dictionary] = _query_surface_probes(
		hit_pos, hit_norm, cluster_id, clusters, probes
	)
	
	if accepted_probes.is_empty():
		return 0
		
	# Normalize weights so probe density does not alter total brightness
	var total_w = 0.0
	for entry in accepted_probes:
		total_w += entry.weight
		
	if total_w < 1e-5:
		return 0
		
	var inv_total_w = 1.0 / total_w
	for entry in accepted_probes:
		var norm_w = entry.weight * inv_total_w
		var p: SurfaceProbe = entry.probe
		if not p.is_active():
			continue
			
		if is_direct:
			p.direct_radiance += radiance * norm_w
		else:
			p.indirect_radiance += radiance * norm_w
			
	return accepted_probes.size()

# ------------------------------------------------------------------------------
# 2. SURFACE-AWARE PROBE RECONSTRUCTION (Shading Point -> Irradiance)
# ------------------------------------------------------------------------------
static func sample_surface_irradiance(
	pos: Vector3,
	normal: Vector3,
	cluster_id: int,
	clusters: Array[SurfaceCluster],
	probes: Array[SurfaceProbe]
) -> Color:
	var accepted_probes: Array[Dictionary] = _query_surface_probes(
		pos, normal, cluster_id, clusters, probes
	)
	
	if accepted_probes.is_empty():
		return Color.BLACK
		
	var total_w = 0.0
	var accum_color = Color.BLACK
	
	for entry in accepted_probes:
		var p: SurfaceProbe = entry.probe
		if not p.is_active():
			continue
		total_w += entry.weight
		accum_color += p.indirect_radiance * entry.weight
		
	if total_w > 1e-5:
		return accum_color / total_w
	return Color.BLACK

# ------------------------------------------------------------------------------
# 3. CORE HIERARCHICAL QUERY ALGORITHM
# ------------------------------------------------------------------------------
static func _query_surface_probes(
	pos: Vector3,
	normal: Vector3,
	cluster_id: int,
	clusters: Array[SurfaceCluster],
	probes: Array[SurfaceProbe]
) -> Array[Dictionary]:
	var accepted: Array[Dictionary] = []
	
	# ---------------------------------------------------------
	# LEVEL 1: Exact Surface Cluster
	# ---------------------------------------------------------
	if cluster_id >= 0 and cluster_id < clusters.size():
		var target_cluster = clusters[cluster_id]
		for p_id in target_cluster.probe_ids:
			if p_id < 0 or p_id >= probes.size():
				continue
			var probe = probes[p_id]
			if not probe.is_active():
				continue
				
			var cand = _evaluate_probe_candidate(pos, normal, probe, 1.0)
			if cand.accepted:
				accepted.append(cand)
				exact_cluster_hits += 1
			else:
				if cand.rejected_by_normal:
					normal_rejections += 1
					
	# ---------------------------------------------------------
	# LEVEL 2: Adjacent Compatible Clusters
	# ---------------------------------------------------------
	if accepted.size() < MIN_REQUIRED_PROBES and cluster_id >= 0 and cluster_id < clusters.size():
		var target_cluster = clusters[cluster_id]
		for adj_id in target_cluster.adjacent_cluster_ids:
			if adj_id < 0 or adj_id >= clusters.size():
				continue
				
			var adj_type = target_cluster.get_adjacency_type(adj_id)
			var surface_weight = 0.0
			
			if adj_type == GIEnums.ClusterAdjacencyType.COPLANAR:
				surface_weight = 0.80
			elif adj_type == GIEnums.ClusterAdjacencyType.SMOOTH_CONTINUATION:
				surface_weight = 0.50
			else:
				# Sharp corners (e.g. wall/floor boundary) or disconnected surfaces REJECTED
				topology_rejections += 1
				continue
				
			var adj_cluster = clusters[adj_id]
			for p_id in adj_cluster.probe_ids:
				if p_id < 0 or p_id >= probes.size():
					continue
				var probe = probes[p_id]
				if not probe.is_active():
					continue
					
				var cand = _evaluate_probe_candidate(pos, normal, probe, surface_weight)
				if cand.accepted:
					accepted.append(cand)
					adjacent_cluster_hits += 1
				else:
					if cand.rejected_by_normal:
						normal_rejections += 1
						
			if accepted.size() >= MIN_REQUIRED_PROBES:
				break
				
	# ---------------------------------------------------------
	# LEVEL 4: Conservative World-Space Fallback
	# ---------------------------------------------------------
	if accepted.is_empty():
		for probe in probes:
			if not probe.is_active():
				continue
			var d = pos.distance_to(probe.position)
			if d > MAX_PROBE_SEARCH_RADIUS * 0.75:
				continue
				
			var ndot = normal.dot(probe.normal)
			if ndot < 0.85: # Strict fallback normal threshold
				normal_rejections += 1
				continue
				
			var cand = _evaluate_probe_candidate(pos, normal, probe, 0.25)
			if cand.accepted:
				accepted.append(cand)
				world_space_fallback_hits += 1
				if accepted.size() >= MIN_REQUIRED_PROBES:
					break
					
	return accepted

static func _evaluate_probe_candidate(
	hit_pos: Vector3,
	hit_norm: Vector3,
	probe: SurfaceProbe,
	surface_weight: float
) -> Dictionary:
	var dist = hit_pos.distance_to(probe.position)
	if dist > MAX_PROBE_SEARCH_RADIUS:
		return {"accepted": false, "rejected_by_normal": false, "weight": 0.0, "probe": probe}
		
	var normal_dot = hit_norm.dot(probe.normal)
	if normal_dot < NORMAL_MIN_DOT:
		return {"accepted": false, "rejected_by_normal": true, "weight": 0.0, "probe": probe}
		
	# High-order normal sharpness weighting
	var normal_weight = pow(max(normal_dot, 0.0), NORMAL_SHARPNESS)
	
	# Compact smooth quadratic distance kernel
	var dist_norm = dist / MAX_PROBE_SEARCH_RADIUS
	var dist_weight = pow(max(1.0 - dist_norm, 0.0), 2.0)
	
	var final_weight = surface_weight * normal_weight * dist_weight
	if final_weight < 1e-4:
		return {"accepted": false, "rejected_by_normal": false, "weight": 0.0, "probe": probe}
		
	return {
		"accepted": true,
		"rejected_by_normal": false,
		"weight": final_weight,
		"probe": probe
	}
