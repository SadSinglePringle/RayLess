class_name LightConnectivityBuilder
extends RefCounted

func build_light_connectivity(
	probes: Array[SurfaceProbe],
	lights: Array[Light3D],
	ray_tracer: TransportRayTracer,
	dep_db: GeometryDependencyDB,
	start_edge_id: int = 0
) -> Array[TransportEdge]:
	var edges: Array[TransportEdge] = []
	var edge_id_counter = start_edge_id
	
	for light_idx in range(lights.size()):
		var light_node = lights[light_idx]
		if not light_node.visible:
			continue
			
		for probe in probes:
			var res = ray_tracer.trace_light_to_probe(light_node, probe)
			
			# We store both visible edges and occluded edges that have a tracked blocking chunk
			if res.visible or res.blocking_chunk_id >= 0:
				var edge = TransportEdge.new()
				edge.id = edge_id_counter
				edge.source_id = light_idx
				edge.dest_probe_id = probe.id
				edge.is_light_edge = true
				edge.transport_weight = res.form_factor if res.visible else 0.0
				edge.visibility = 1.0 if res.visible else 0.0
				edge.direction = res.direction
				edge.distance = res.distance
				edge.confidence = 1.0
				edge.blocking_chunk_id = res.blocking_chunk_id
				edge.state = GIEnums.EdgeState.VALID
				
				if res.blocking_chunk_id >= 0:
					dep_db.register_dependency(res.blocking_chunk_id, edge.id)
					
				edges.append(edge)
				probe.incoming_edge_indices.append(edge.id)
				edge_id_counter += 1
				
	return edges
