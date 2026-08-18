class_name SurfaceTransportBuilder
extends RefCounted

var max_transport_distance: float = 12.0
var max_outgoing_edges_per_probe: int = 16
var min_form_factor: float = 0.001

func build_surface_transport(
	probes: Array[SurfaceProbe],
	ray_tracer: TransportRayTracer,
	dep_db: GeometryDependencyDB,
	start_edge_id: int = 0
) -> Array[TransportEdge]:
	var edges: Array[TransportEdge] = []
	var edge_id_counter = start_edge_id
	var probe_count = probes.size()
	
	for i in range(probe_count):
		var src_probe = probes[i]
		var candidates: Array[Dictionary] = []
		
		for j in range(probe_count):
			if i == j:
				continue
			var dst_probe = probes[j]
			var to_dst = dst_probe.position - src_probe.position
			var dist = to_dst.length()
			if dist > max_transport_distance or dist < 0.1:
				continue
				
			var dir = to_dst / dist
			var cos_src = src_probe.normal.dot(dir)
			var cos_dst = dst_probe.normal.dot(-dir)
			if cos_src <= 0.05 or cos_dst <= 0.05:
				continue
				
			var geom_term = (cos_src * cos_dst) / (PI * (dist * dist + 0.15))
			var approx_weight = geom_term * src_probe.area
			if approx_weight < min_form_factor:
				continue
				
			candidates.append({
				"dst_idx": j,
				"dst_probe": dst_probe,
				"approx_weight": approx_weight,
				"dir": dir,
				"dist": dist
			})
			
		# Sort candidates descending by weight and pick top K
		candidates.sort_custom(func(a, b): return a.approx_weight > b.approx_weight)
		var num_edges_to_trace = min(candidates.size(), max_outgoing_edges_per_probe)
		
		for k in range(num_edges_to_trace):
			var cand = candidates[k]
			var dst_probe: SurfaceProbe = cand.dst_probe
			var trace_res = ray_tracer.trace_probe_to_probe(src_probe, dst_probe)
			
			if trace_res.visible or trace_res.blocking_chunk_id >= 0:
				var edge = TransportEdge.new()
				edge.id = edge_id_counter
				edge.source_id = src_probe.id
				edge.dest_probe_id = dst_probe.id
				edge.is_light_edge = false
				edge.transport_weight = trace_res.form_factor if trace_res.visible else cand.approx_weight
				edge.visibility = 1.0 if trace_res.visible else 0.0
				edge.direction = cand.dir
				edge.distance = cand.dist
				edge.confidence = 1.0
				edge.blocking_chunk_id = trace_res.blocking_chunk_id
				edge.state = GIEnums.EdgeState.VALID
				
				if trace_res.blocking_chunk_id >= 0:
					dep_db.register_dependency(trace_res.blocking_chunk_id, edge.id)
					
				edges.append(edge)
				src_probe.outgoing_edge_indices.append(edge.id)
				dst_probe.incoming_edge_indices.append(edge.id)
				edge_id_counter += 1
				
	return edges
