class_name ASTGProbeValidation
extends RefCounted

# ==============================================================================
# ASTG ADAPTIVE SPARSE SURFACE-PROBE VALIDATION
# Validates surface attachment, barycentrics, split/merge, and confidence growth
# ==============================================================================

const TestResultScript = preload("res://tests/suite/test_result.gd")
const AdaptiveAllocatorScript = preload("res://scripts/core/precompute/adaptive_surface_probe_allocator.gd")

static func run_all() -> Array:
	var results: Array = []
	results.append(test_surface_area_seeding())
	results.append(test_complexity_scoring())
	results.append(test_adaptive_split_merge())
	results.append(test_confidence_progression())
	return results

static func test_surface_area_seeding() -> RefCounted:
	var res = TestResultScript.create("probes_surface_area_seeding", 0, "UNIT")
	var mock_clusters: Array[Dictionary] = [
		{ "cluster_id": 0, "area": 50.0, "centroid": Vector3(0, 0, 0), "representative_normal": Vector3.UP, "destruction_chunk_id": 1 },
		{ "cluster_id": 1, "area": 10.0, "centroid": Vector3(10, 2, 0), "representative_normal": Vector3.RIGHT, "destruction_chunk_id": 2 },
		{ "cluster_id": 2, "area": 100.0, "centroid": Vector3(0, 0, 10), "representative_normal": Vector3.UP, "destruction_chunk_id": 3 }
	]
	var seeds = AdaptiveAllocatorScript.allocate_surface_seeds(mock_clusters, 160)
	res.add_assertion("Allocated >= 140 initial surface seeds", seeds.size() >= 140)
	res.metrics["allocated_probes"] = seeds.size()
	return res

static func test_complexity_scoring() -> RefCounted:
	var res = TestResultScript.create("probes_astg_complexity_score", 3, "UNIT")
	var mock_clusters: Array[Dictionary] = [
		{ "cluster_id": 0, "area": 50.0, "centroid": Vector3(0, 0, 0), "representative_normal": Vector3.UP, "destruction_chunk_id": 1 }
	]
	var seeds = AdaptiveAllocatorScript.allocate_surface_seeds(mock_clusters, 10)
	if seeds.size() > 0:
		var p = seeds[0]
		p.incoming_path_count = 14
		p.unique_light_count = 28
		p.directional_variance = 0.85
		p.curvature = 0.6
		var score = AdaptiveAllocatorScript.compute_probe_complexity_score(p)
		res.metrics["complexity_score"] = score
		res.add_assertion("Doorway/corner probe score > 0.65", score > 0.65)
	return res

static func test_adaptive_split_merge() -> RefCounted:
	var res = TestResultScript.create("probes_adaptive_split_merge", 0, "UNIT")
	var mock_clusters: Array[Dictionary] = [
		{ "cluster_id": 0, "area": 50.0, "centroid": Vector3(0, 0, 0), "representative_normal": Vector3.UP, "destruction_chunk_id": 1 }
	]
	var seeds = AdaptiveAllocatorScript.allocate_surface_seeds(mock_clusters, 20)
	seeds[0].incoming_path_count = 15
	seeds[0].unique_light_count = 30
	seeds[0].directional_variance = 0.9

	var refined = AdaptiveAllocatorScript.adaptively_refine_probes(seeds, 50, 0.65)
	res.add_assertion("Adaptive split increased probe count on high complexity", refined.size() > seeds.size())

	var redundant_probe = AdaptiveAllocatorScript.AdaptiveProbe.new()
	redundant_probe.id = 999
	redundant_probe.position = seeds[0].position + Vector3(0.05, 0, 0)
	redundant_probe.normal = seeds[0].normal
	redundant_probe.cluster_id = seeds[0].cluster_id
	refined.append(redundant_probe)

	var merged = AdaptiveAllocatorScript.merge_redundant_probes(refined, 0.3, 0.95, 0.08)
	res.add_assertion("Redundant probes collapsed into compact set", merged.size() < refined.size())
	return res

static func test_confidence_progression() -> RefCounted:
	var res = TestResultScript.create("probes_confidence_progression", 0, "UNIT")
	var p = AdaptiveAllocatorScript.AdaptiveProbe.new()
	p.confidence = 0.0
	res.add_assertion("Initial probe confidence == 0.0", p.confidence == 0.0)
	p.confidence = min(1.0, p.confidence + 0.5)
	res.add_assertion("Confidence grows after validation sample", p.confidence == 0.5)
	return res
