class_name RadianceEvaluator
extends RefCounted

func evaluate_energy(
	bounce0_nodes: Array[TransportNode],
	bounce1_nodes: Array[TransportNode],
	lights: Array[Light3D],
	probes: Array[SurfaceProbe],
	depositor: ProbeDepositor
) -> void:
	# 1. Zero-ray Direct lighting update along cached Bounce 0 nodes
	depositor.deposit_direct_transport(bounce0_nodes, lights, probes)
	
	# 2. Update Bounce 1 nodes energy and re-deposit into probes
	for b1 in bounce1_nodes:
		if not b1.is_active() or b1.parent_node_id < 0 or b1.parent_node_id >= bounce0_nodes.size():
			continue
			
		var parent = bounce0_nodes[b1.parent_node_id]
		if not parent.is_active():
			b1.indirect_radiance = Color.BLACK
			continue
			
		var parent_rad = parent.direct_radiance * parent.albedo
		var dist = parent.hit_position.distance_to(b1.hit_position)
		var cos_dst = max(0.01, b1.hit_normal.dot(-b1.incoming_direction))
		var geom_term = cos_dst / (dist * dist + 0.5)
		b1.indirect_radiance = parent_rad * geom_term * 0.2
		
		# Deposit updated indirect radiance into nearby probes
		SurfaceAwareProbeLookup.deposit_to_surface_probes(
			b1.hit_position,
			b1.hit_normal,
			b1.surface_cluster_id,
			b1.indirect_radiance,
			depositor.clusters,
			probes,
			false # is_indirect
		)
