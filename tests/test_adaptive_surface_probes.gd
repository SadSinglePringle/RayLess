extends SceneTree

# ==============================================================================
# ASTG ADAPTIVE SPARSE SURFACE-PROBE & TERMINATION TEST SUITE
# ==============================================================================

const AdaptiveAllocatorScript = preload("res://scripts/core/precompute/adaptive_surface_probe_allocator.gd")

func _init() -> void:
	print("================================================================================")
	print("🧪 RUNNING ASTG ADAPTIVE SPARSE SURFACE-PROBE TEST SUITE")
	print("================================================================================")

	# 1. Test Surface-Area Seed Allocation
	var mock_clusters: Array[Dictionary] = [
		{ "cluster_id": 0, "area": 50.0, "centroid": Vector3(0, 0, 0), "representative_normal": Vector3.UP, "destruction_chunk_id": 1 },
		{ "cluster_id": 1, "area": 10.0, "centroid": Vector3(10, 2, 0), "representative_normal": Vector3.RIGHT, "destruction_chunk_id": 2 },
		{ "cluster_id": 2, "area": 100.0, "centroid": Vector3(0, 0, 10), "representative_normal": Vector3.UP, "destruction_chunk_id": 3 }
	]

	var seeds = AdaptiveAllocatorScript.allocate_surface_seeds(mock_clusters, 160)
	print("✅ [1/5] Surface-Area Seeding: Allocated %d probes proportionally across %d clusters." % [seeds.size(), mock_clusters.size()])
	assert(seeds.size() >= 140, "Seed count should be close to target")

	# 2. Test Complexity Scoring
	var p_complex = seeds[0]
	p_complex.incoming_path_count = 14
	p_complex.unique_light_count = 28
	p_complex.directional_variance = 0.85
	p_complex.curvature = 0.6
	var score = AdaptiveAllocatorScript.compute_probe_complexity_score(p_complex)
	print("✅ [2/5] Complexity Scoring: High-complexity doorway/corner probe score = %.3f" % score)
	assert(score > 0.65, "High-complexity score should exceed threshold")

	# 3. Test Adaptive Probe Split
	var refined = AdaptiveAllocatorScript.adaptively_refine_probes(seeds, 300, 0.65)
	print("✅ [3/5] Adaptive Probe Split: Refined from %d to %d probes on complex regions." % [seeds.size(), refined.size()])
	assert(refined.size() > seeds.size(), "Refinement should split complex probes")

	# 4. Test Redundant Probe Merge
	var redundant_probe = AdaptiveAllocatorScript.AdaptiveProbe.new()
	redundant_probe.id = 999
	redundant_probe.position = seeds[0].position + Vector3(0.05, 0, 0)
	redundant_probe.normal = seeds[0].normal
	redundant_probe.cluster_id = seeds[0].cluster_id
	redundant_probe.irradiance = seeds[0].irradiance
	refined.append(redundant_probe)

	var merged = AdaptiveAllocatorScript.merge_redundant_probes(refined, 0.3, 0.95, 0.08)
	print("✅ [4/5] Redundancy Merge: Collapsed %d redundant probes -> %d final compact probes." % [refined.size(), merged.size()])
	assert(merged.size() < refined.size(), "Merge should collapse redundant neighbor")

	# 5. Test Information-Based Transport Branch Termination
	var should_term = AdaptiveAllocatorScript.should_terminate_transport_branch(
		seeds[0].position,
		seeds[0].normal,
		seeds[0].cluster_id,
		merged,
		0.80
	)
	print("✅ [5/5] Information-Based Transport Termination: Termination Triggered = %s" % ("YES (Confidence Met)" if should_term else "NO"))
	assert(should_term, "High-confidence probe should terminate transport ray branch")

	print("================================================================================")
	print("🎉 ALL ADAPTIVE SURFACE-PROBE UNIT TESTS PASSED!")
	print("================================================================================\n")
	quit(0)
