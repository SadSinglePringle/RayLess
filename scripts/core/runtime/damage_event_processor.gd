class_name DamageEventProcessor
extends RefCounted

var expansion_radius: float = 2.5

func process_damage_event(
	event: DamageEvent,
	probes: Array[SurfaceProbe],
	lights: Array[Light3D],
	edges: Array[TransportEdge],
	dep_db: GeometryDependencyDB,
	scheduler: PriorityRepairScheduler,
	camera: Camera3D = null
) -> Dictionary:
	var invalidated_edges_count = 0
	var candidate_connections_queued = 0
	
	# 1. Reverse Dependency Invalidation
	var dependent_edges = dep_db.get_dependent_edges(event.chunk_id)
	for edge_id in dependent_edges:
		if edge_id >= 0 and edge_id < edges.size():
			var edge = edges[edge_id]
			edge.state = GIEnums.EdgeState.INVALID
			edge.visibility = 0.0
			
			var dst_probe_id = edge.dest_probe_id
			var prio = 5.0
			if dst_probe_id >= 0 and dst_probe_id < probes.size():
				var dst_probe = probes[dst_probe_id]
				dst_probe.flags |= GIEnums.ProbeFlags.UNCERTAIN
				dst_probe.confidence = max(0.2, dst_probe.confidence - 0.5)
				if camera != null:
					var cam_dist = camera.global_position.distance_to(dst_probe.position)
					prio += max(0.0, 20.0 - cam_dist)
					
			scheduler.enqueue_edge_repair(edge_id, prio)
			invalidated_edges_count += 1
			
	dep_db.clear_chunk(event.chunk_id)
	
	# 2. Candidate Discovery: Spatial query for probes around damage volume
	var expanded_bounds = event.bounds.grow(expansion_radius)
	var nearby_probes: Array[SurfaceProbe] = []
	
	for probe in probes:
		if expanded_bounds.has_point(probe.position):
			nearby_probes.append(probe)
			probe.confidence = max(0.3, probe.confidence - 0.4)
			probe.flags |= GIEnums.ProbeFlags.UNCERTAIN
			
	# Enqueue candidate light connections for nearby probes
	for probe in nearby_probes:
		for light_idx in range(lights.size()):
			var light = lights[light_idx]
			if not light.visible:
				continue
				
			# Check if edge already exists
			var already_connected = false
			for e_idx in probe.incoming_edge_indices:
				if e_idx >= 0 and e_idx < edges.size():
					var e = edges[e_idx]
					if e.is_light_edge and e.source_id == light_idx:
						already_connected = true
						break
			if not already_connected:
				var prio = 8.0
				if camera != null:
					prio += max(0.0, 15.0 - camera.global_position.distance_to(probe.position))
				scheduler.enqueue_candidate(light_idx, probe.id, true, prio)
				candidate_connections_queued += 1

	# Enqueue candidate probe-to-probe connections across the opened opening
	for i in range(nearby_probes.size()):
		var p_src = nearby_probes[i]
		for j in range(i + 1, nearby_probes.size()):
			var p_dst = nearby_probes[j]
			var dist = p_src.position.distance_to(p_dst.position)
			if dist < 0.5 or dist > 10.0:
				continue
			var dir = (p_dst.position - p_src.position).normalized()
			if p_src.normal.dot(dir) > 0.05 and p_dst.normal.dot(-dir) > 0.05:
				scheduler.enqueue_candidate(p_src.id, p_dst.id, false, 4.0)
				candidate_connections_queued += 1

	# Trigger burst budgeting for immediate responsive lighting update
	scheduler.trigger_burst(3)
	
	return {
		"invalidated_edges": invalidated_edges_count,
		"candidates_queued": candidate_connections_queued,
		"nearby_probes_affected": nearby_probes.size()
	}
