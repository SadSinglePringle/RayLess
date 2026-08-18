class_name ASTGDiagnosisEngine
extends RefCounted

# ==============================================================================
# ASTG AUTOMATIC DIAGNOSIS ENGINE
# Identifies root causes and actionable remedies from test failure patterns
# ==============================================================================

static func diagnose(result: RefCounted) -> String:
	if result.status == 0: # Status.PASS
		return "OK: No issues detected."

	var name = result.name.to_lower()
	var metrics = result.metrics

	# Rule 1: Late-Bound Lighting Invariant Violation
	if name.contains("late_bound") or name.contains("light"):
		var topo_rays = metrics.get("topology_rays", 0)
		var lights_changed = metrics.get("lights_changed", 0)
		if lights_changed > 0 and topo_rays > 0:
			return "DIAGNOSIS: Late-bound separation violated! Modulating light energy state (RGB/Intensity/Enabled) generated %d topology rays. Verify zero-topology invariant in LightAdapter." % topo_rays

	# Rule 2: Full-Scene Authenticity Failure
	if metrics.get("benchmark_type", "") == "FULL_SCENE":
		var nodes = metrics.get("transport_node_count", 0)
		var edges = metrics.get("transport_edge_count", 0)
		if nodes == 0 or edges == 0:
			return "DIAGNOSIS: Workload did not execute genuine ASTG transport graph traversal (0 nodes/edges recorded). Benchmark is INVALID. Prohibit synthetic shortcuts."

	# Rule 3: Scene Geometry / AABB Mismatch
	if name.contains("scene") or name.contains("geometry") or name.contains("bistro"):
		var aabb_diff = metrics.get("aabb_delta", 0.0)
		if aabb_diff > 0.01:
			return "DIAGNOSIS: Native DXR AABB and Godot scene AABB mismatch (Delta: %.4f). Check glTF 2.0 node hierarchy matrix accumulation." % aabb_diff

	# Rule 4: Floating Surface Probes
	if name.contains("probe"):
		var floating_count = metrics.get("floating_probes", 0)
		if floating_count > 0:
			return "DIAGNOSIS: Detected %d floating surface probes detached from geometry surfaces! Replace tangent-plane offsets with triangle barycentric surface projection." % floating_count

	# Rule 5: Destruction Synchronization Failure
	if name.contains("destruction") or name.contains("as_sync"):
		var stale_hits = metrics.get("stale_hits", 0)
		if stale_hits > 0:
			return "DIAGNOSIS: Ray hit destroyed geometry after BLAS/TLAS update (%d stale hits)! GPU barrier/fence synchronization missing before repair ray dispatch." % stale_hits

	# Rule 6: Numerical Stability & NaNs
	if name.contains("numerical") or name.contains("nan"):
		var nan_count = metrics.get("nan_count", 0)
		if nan_count > 0:
			return "DIAGNOSIS: Detected %d NaN/Inf values in shader buffers! Check vertex normal normalization and zero-range light clamping." % nan_count

	# Rule 7: Global Ambient Fallback Detected
	if name.contains("composition") or name.contains("ambient"):
		var is_global_ambient = metrics.get("is_global_ambient_only", false)
		if is_global_ambient:
			return "DIAGNOSIS: Local surface GI reconstruction is inactive; frame is falling back to global averaged ambient! Wire local surface probe interpolator."

	# General Fallback
	if not result.errors.is_empty():
		return "DIAGNOSIS: " + result.errors[0]

	return "DIAGNOSIS: Unknown failure state."
