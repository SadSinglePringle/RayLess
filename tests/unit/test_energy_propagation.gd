class_name TestEnergyPropagation
extends RefCounted

static func run() -> bool:
	print("  [TestEnergyPropagation] Running tests...")
	var depositor = ProbeDepositor.new()
	var evaluator = RadianceEvaluator.new()
	
	# Create 2 probes
	var p0 = SurfaceProbe.new()
	p0.id = 0
	p0.position = Vector3(0, 0, 0)
	p0.normal = Vector3.UP
	p0.albedo = Color(0.8, 0.8, 0.8)
	
	var p1 = SurfaceProbe.new()
	p1.id = 1
	p1.position = Vector3(1, 0, 0)
	p1.normal = Vector3.UP
	p1.albedo = Color(0.8, 0.8, 0.8)
	
	var probes: Array[SurfaceProbe] = [p0, p1]
	
	# Create cluster for surface lookup
	var cluster = SurfaceCluster.new()
	cluster.id = 0
	cluster.average_normal = Vector3.UP
	cluster.bounds = AABB(Vector3(-1, -1, -1), Vector3(3, 2, 2))
	var p_ids: Array[int] = [0, 1]
	cluster.probe_ids = p_ids
	var cluster_list: Array[SurfaceCluster] = [cluster]
	depositor.clusters = cluster_list
	p0.surface_cluster_id = 0
	p1.surface_cluster_id = 0
	
	# Create direct node
	var n0 = TransportNode.new()
	n0.id = 0
	n0.source_id = 0
	n0.hit_position = Vector3(0.1, 0, 0)
	n0.hit_normal = Vector3.UP
	n0.transport_weight = 1.0
	n0.state = GIEnums.NodeState.VALID
	n0.albedo = Color(0.8, 0.8, 0.8)
	
	# Create diffuse bounce node
	var n1 = TransportNode.new()
	n1.id = 1
	n1.source_id = 0
	n1.parent_node_id = 0
	n1.bounce_depth = 1
	n1.hit_position = Vector3(0.9, 0, 0)
	n1.hit_normal = Vector3.UP
	n1.incoming_direction = Vector3(1, 0, 0)
	n1.state = GIEnums.NodeState.VALID
	
	var b0_nodes: Array[TransportNode] = [n0]
	var b1_nodes: Array[TransportNode] = [n1]
	
	# Create simulated OmniLight
	var light = OmniLight3D.new()
	light.light_color = Color(1.0, 0.5, 0.2)
	light.light_energy = 2.0
	light.visible = true
	var lights: Array[Light3D] = [light]
	
	evaluator.evaluate_energy(b0_nodes, b1_nodes, lights, probes, depositor)
	
	if p0.direct_radiance.r <= 0.01:
		printerr("FAIL: P0 did not receive direct radiance (got %.3f)" % p0.direct_radiance.r)
		return false
		
	if p1.indirect_radiance.r <= 0.001:
		printerr("FAIL: P1 did not receive indirect radiance from diffuse bounce (got %.3f)" % p1.indirect_radiance.r)
		return false
		
	print("  [TestEnergyPropagation] PASSED! (P0 Direct: (%.3f, %.3f, %.3f), P1 Indirect: (%.3f, %.3f, %.3f))" % [
		p0.direct_radiance.r, p0.direct_radiance.g, p0.direct_radiance.b,
		p1.indirect_radiance.r, p1.indirect_radiance.g, p1.indirect_radiance.b
	])
	return true
