class_name ProbeDepositor
extends RefCounted

const ProbeContributionTableScript = preload("res://scripts/core/runtime/probe_contribution_table.gd")

var ray_tracer: RefCounted
var clusters: Array[SurfaceCluster] = []
var min_branch_energy: float = 0.005
var diffuse_samples_per_node: int = 6
var contribution_table: RefCounted = null # ProbeContributionTable

func _init(p_ray_tracer: RefCounted = null) -> void:
	ray_tracer = p_ray_tracer

func deposit_direct_transport(
	nodes: Array[TransportNode],
	lights: Array[Light3D],
	probes: Array[SurfaceProbe]
) -> int:
	var total_deposits = 0

	# Reset direct irradiance on active probes
	for probe in probes:
		if probe.is_active():
			probe.direct_radiance = Color.BLACK

	for node in nodes:
		if not node.is_active() or node.source_id < 0 or node.source_id >= lights.size():
			continue

		var light = lights[node.source_id]
		var light_energy = light.light_color * light.light_energy if light.visible else Color.BLACK
		var effective_weight = node.get_effective_weight()
		var direct_radiance = light_energy * effective_weight
		node.direct_radiance = direct_radiance

		# Record persistent transfer coefficient into Contribution Table
		if contribution_table != null:
			var transfer_weight = Color(effective_weight, effective_weight, effective_weight)
			_record_transfer_to_probes(node.hit_position, node.hit_normal, node.surface_cluster_id, node.source_id, transfer_weight, probes)

		if direct_radiance.get_luminance() < min_branch_energy:
			continue

		# Surface-aware deposition into exact cluster & compatible neighbors
		var deposited = SurfaceAwareProbeLookup.deposit_to_surface_probes(
			node.hit_position,
			node.hit_normal,
			node.surface_cluster_id,
			direct_radiance,
			clusters,
			probes,
			true # is_direct
		)
		total_deposits += deposited

	if contribution_table != null and contribution_table.has_method("finalize_top_k_contributions"):
		contribution_table.finalize_top_k_contributions()

	return total_deposits

func trace_and_deposit_diffuse_bounces(
	bounce0_nodes: Array[TransportNode],
	lights: Array[Light3D],
	probes: Array[SurfaceProbe],
	out_diffuse_nodes: Array[TransportNode],
	max_diffuse_bounces: int = 4
) -> int:
	var total_diffuse_deposits = 0

	# Reset indirect irradiance on active probes
	for probe in probes:
		if probe.is_active():
			probe.indirect_radiance = Color.BLACK

	out_diffuse_nodes.clear()

	var current_parent_nodes: Array[TransportNode] = []
	for node in bounce0_nodes:
		if node.is_active() and node.source_id >= 0 and node.source_id < lights.size():
			current_parent_nodes.append(node)

	for bounce_idx in range(1, max_diffuse_bounces + 1):
		var next_bounce_nodes: Array[TransportNode] = []
		var samples_this_bounce = diffuse_samples_per_node if bounce_idx == 1 else max(2, int(diffuse_samples_per_node / 2))

		for node in current_parent_nodes:
			if not node.is_active() or node.source_id < 0 or node.source_id >= lights.size():
				continue

			var light = lights[node.source_id]
			var light_energy = light.light_color * light.light_energy if light.visible else Color.WHITE
			var node_weight = node.get_effective_weight()
			var incoming_rad = node.direct_radiance if node.bounce_depth == 0 else node.indirect_radiance
			var reflected_radiance = incoming_rad * node.albedo
			if reflected_radiance.get_luminance() < min_branch_energy * 0.1:
				continue

			var u_norm = node.hit_normal
			var tangent = Vector3.UP.cross(u_norm)
			if tangent.length_squared() < 0.001:
				tangent = Vector3.RIGHT.cross(u_norm)
			tangent = tangent.normalized()
			var bitangent = u_norm.cross(tangent).normalized()

			# Trace coarse hemisphere distribution
			for s in range(samples_this_bounce):
				var r1 = randf()
				var r2 = randf()
				var theta = acos(sqrt(1.0 - r1))
				var phi = 2.0 * PI * r2

				var local_dir = Vector3(sin(theta) * cos(phi), cos(theta), sin(theta) * sin(phi))
				var world_dir = (tangent * local_dir.x + u_norm * local_dir.y + bitangent * local_dir.z).normalized()

				var ray_start = node.hit_position + u_norm * 0.02
				var ray_end = ray_start + world_dir * 12.0
				var hit_res = ray_tracer.trace_segment(ray_start, ray_end)

				if hit_res.hit:
					var hit2_pos = hit_res.position
					var hit2_norm = hit_res.normal
					var dist = ray_start.distance_to(hit2_pos)
					var cos_dst = max(0.01, hit2_norm.dot(-world_dir))
					var geom_term = cos_dst / (dist * dist + 1.0)
					var bounce_factor = geom_term * (0.25 / (PI * float(samples_this_bounce)))
					var bounce_radiance = reflected_radiance * bounce_factor

					# Create Transport Node at current bounce depth
					var b_node = TransportNode.new()
					b_node.id = out_diffuse_nodes.size()
					b_node.source_id = node.source_id
					b_node.parent_node_id = node.id
					b_node.bounce_depth = bounce_idx
					b_node.hit_position = hit2_pos
					b_node.hit_normal = hit2_norm
					b_node.incoming_direction = world_dir
					b_node.destruction_chunk_id = hit_res.chunk_id
					b_node.surface_cluster_id = _find_closest_cluster(hit2_pos, hit2_norm)
					b_node.indirect_radiance = bounce_radiance
					b_node.albedo = node.albedo
					b_node.state = node.state
					out_diffuse_nodes.append(b_node)
					node.child_node_ids.append(b_node.id)
					next_bounce_nodes.append(b_node)

					# Record collapsed source->bounce->probe transfer in Contribution Table
					if contribution_table != null:
						var diffuse_transfer = Color(
							node_weight * node.albedo.r * bounce_factor,
							node_weight * node.albedo.g * bounce_factor,
							node_weight * node.albedo.b * bounce_factor
						)
						_record_transfer_to_probes(hit2_pos, hit2_norm, b_node.surface_cluster_id, node.source_id, diffuse_transfer, probes)

					# Surface-aware deposition into probe field
					var dep = SurfaceAwareProbeLookup.deposit_to_surface_probes(
						hit2_pos,
						hit2_norm,
						b_node.surface_cluster_id,
						bounce_radiance,
						clusters,
						probes,
						false # is_indirect
					)
					total_diffuse_deposits += dep

		current_parent_nodes = next_bounce_nodes
		if current_parent_nodes.is_empty():
			break

	if contribution_table != null and contribution_table.has_method("finalize_top_k_contributions"):
		contribution_table.finalize_top_k_contributions()

	return total_diffuse_deposits

func _record_transfer_to_probes(
	pos: Vector3,
	normal: Vector3,
	cluster_id: int,
	light_id: int,
	transfer_weight: Color,
	probes: Array[SurfaceProbe]
) -> void:
	if contribution_table == null:
		return

	for probe in probes:
		if not probe.is_active():
			continue
		var dist = probe.position.distance_to(pos)
		if dist > 2.5:
			continue
		var ndot = probe.normal.dot(normal)
		if ndot < 0.8: # Phase 6: Normal cone compatibility (approx 37 deg)
			continue

		var w = (1.0 - (dist / 2.5)) * ndot
		var final_transfer = transfer_weight * w
		var importance = final_transfer.get_luminance() * ndot
		if contribution_table.has_method("add_candidate"):
			contribution_table.add_candidate(probe.id, light_id, final_transfer, 0, importance)
		else:
			contribution_table.add_contribution(probe.id, light_id, final_transfer)

func _find_closest_cluster(pos: Vector3, normal: Vector3) -> int:
	var best_id = -1
	var best_score = -1e9
	for c in clusters:
		var d = c.centroid.distance_to(pos)
		if d > 4.0:
			continue
		var ndot = c.average_normal.dot(normal)
		if ndot < 0.2:
			continue
		var contains = 1.0 if c.contains_point(pos, 0.3) else 0.0
		var score = contains * 100.0 + ndot * 10.0 - d
		if score > best_score:
			best_score = score
			best_id = c.id
	return best_id
