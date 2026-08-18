class_name RadiancePropagator
extends RefCounted

var propagation_threshold: float = 0.005
var max_propagation_iterations: int = 3
var energy_damping: float = 0.92

func update_direct_lighting(
	probes: Array[SurfaceProbe],
	lights: Array[Light3D],
	edges: Array[TransportEdge]
) -> int:
	var updated_count = 0
	
	for probe in probes:
		var new_direct = Color.BLACK
		for edge_id in probe.incoming_edge_indices:
			if edge_id < 0 or edge_id >= edges.size():
				continue
			var edge = edges[edge_id]
			if not edge.is_light_edge or not edge.is_active():
				continue
				
			if edge.source_id >= 0 and edge.source_id < lights.size():
				var light = lights[edge.source_id]
				if light.visible:
					var light_color = light.light_color * light.light_energy
					var effective_weight = edge.get_effective_weight()
					new_direct += light_color * effective_weight
					
		# Check if direct lighting changed significantly
		var diff_r = absf(new_direct.r - probe.direct_radiance.r)
		var diff_g = absf(new_direct.g - probe.direct_radiance.g)
		var diff_b = absf(new_direct.b - probe.direct_radiance.b)
		var max_diff = max(diff_r, max(diff_g, diff_b))
		if max_diff > propagation_threshold:
			probe.flags |= GIEnums.ProbeFlags.DIRTY
			updated_count += 1
			
		probe.direct_radiance = new_direct
		
	return updated_count

func propagate_indirect_lighting(
	probes: Array[SurfaceProbe],
	edges: Array[TransportEdge],
	force_all: bool = false
) -> Dictionary:
	var dirty_probes: Array[SurfaceProbe] = []
	for probe in probes:
		if force_all or (probe.flags & GIEnums.ProbeFlags.DIRTY) != 0:
			dirty_probes.append(probe)
			
	var iterations_run = 0
	var total_propagated = 0
	
	for iter in range(max_propagation_iterations):
		if dirty_probes.is_empty():
			break
			
		iterations_run += 1
		var next_dirty_dict: Dictionary = {}
		
		# Compute next indirect radiance buffer
		for probe in dirty_probes:
			var new_indirect = Color.BLACK
			
			for edge_id in probe.incoming_edge_indices:
				if edge_id < 0 or edge_id >= edges.size():
					continue
				var edge = edges[edge_id]
				if edge.is_light_edge or not edge.is_active():
					continue
					
				if edge.source_id >= 0 and edge.source_id < probes.size():
					var src_probe = probes[edge.source_id]
					var outgoing_rad = (src_probe.direct_radiance + src_probe.indirect_radiance) * src_probe.albedo
					var weight = edge.get_effective_weight()
					new_indirect += outgoing_rad * weight * energy_damping
					
			var prev_indirect = probe.indirect_radiance
			probe.indirect_radiance = new_indirect
			probe.flags &= ~GIEnums.ProbeFlags.DIRTY
			probe.confidence = clamp(probe.confidence + 0.1, 0.0, 1.0)
			total_propagated += 1
			
			var diff_r = absf(new_indirect.r - prev_indirect.r)
			var diff_g = absf(new_indirect.g - prev_indirect.g)
			var diff_b = absf(new_indirect.b - prev_indirect.b)
			var max_diff = max(diff_r, max(diff_g, diff_b))
			
			if max_diff > propagation_threshold:
				# Queue outgoing neighbor probes
				for out_edge_id in probe.outgoing_edge_indices:
					if out_edge_id >= 0 and out_edge_id < edges.size():
						var out_edge = edges[out_edge_id]
						if out_edge.is_active() and out_edge.dest_probe_id >= 0 and out_edge.dest_probe_id < probes.size():
							var dst_p = probes[out_edge.dest_probe_id]
							next_dirty_dict[dst_p.id] = dst_p
							
		dirty_probes.clear()
		for p in next_dirty_dict.values():
			dirty_probes.append(p)
			
	return {
		"iterations": iterations_run,
		"propagated_probes": total_propagated,
		"remaining_dirty": dirty_probes.size()
	}
