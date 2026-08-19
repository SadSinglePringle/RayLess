class_name ASTGPipeline
extends RefCounted

# ==============================================================================
# ASTG GLOBAL ILLUMINATION PIPELINE
# Manages Persistent Light Transport Graph, Late-Bound Lighting & Lazy Probe Evaluation.
# ==============================================================================

const HardwareRTBackendScript = preload("res://scripts/core/runtime/hardware_rt_backend.gd")
const SoftwareBVHBackendScript = preload("res://scripts/core/runtime/software_bvh_backend.gd")
const TransportRayTracerScript = preload("res://scripts/core/runtime/transport_ray_tracer.gd")
const LateBoundLightManagerScript = preload("res://scripts/core/runtime/late_bound_light_manager.gd")
const ProbeContributionTableScript = preload("res://scripts/core/runtime/probe_contribution_table.gd")

var world_3d: World3D
var ray_tracer: RefCounted
var backend: RefCounted = null

var probes: Array[SurfaceProbe] = []
var clusters: Array[SurfaceCluster] = []
var lights: Array[Light3D] = []

var light_hierarchies: Array[AngularHierarchy] = []
var bounce0_nodes: Array[TransportNode] = []
var bounce1_nodes: Array[TransportNode] = []

var cluster_builder: SurfaceClusterBuilder = SurfaceClusterBuilder.new()
var probe_generator: SurfaceProbeGenerator = SurfaceProbeGenerator.new()
var probe_depositor: ProbeDepositor
var radiance_evaluator: RadianceEvaluator = RadianceEvaluator.new()
var dependency_db: GeometryDependencyDB = GeometryDependencyDB.new()
var repair_scheduler: PriorityRepairScheduler = PriorityRepairScheduler.new()

var target_probe_density: float = 3.5
var min_probe_distance: float = 0.28
var max_diffuse_bounces: int = 4

# Late-Bound Lighting & Lazy Probe Evaluation Infrastructure
var light_manager: RefCounted = null
var contribution_table: RefCounted = null
var last_light_states: Array[Dictionary] = []

func initialize(
	mesh_nodes: Array[Node3D],
	light_nodes: Array[Light3D],
	p_world_3d: World3D,
	use_hardware_rt: bool = true
) -> void:
	world_3d = p_world_3d
	lights = light_nodes

	# Initialize Late-Bound Managers
	light_manager = LateBoundLightManagerScript.new()
	contribution_table = ProbeContributionTableScript.new()

	for i in range(lights.size()):
		var l = lights[i]
		light_manager.register_light(i, l.light_color, l.light_energy, l.visible)

	# Select backend
	if use_hardware_rt:
		backend = HardwareRTBackendScript.new()
	else:
		backend = SoftwareBVHBackendScript.new()

	backend.initialize(mesh_nodes, world_3d)
	ray_tracer = TransportRayTracerScript.new(world_3d, backend)
	probe_depositor = ProbeDepositor.new(ray_tracer)
	probe_depositor.contribution_table = contribution_table

	print("==================================================")
	print("🚀 Initializing Persistent Light Transport Graph (%s)..." % backend.backend_name)
	print("==================================================")

	# 1. Generate surface probes (High-density quality settings: ~0.28m adaptive spacing)
	probe_generator.target_probe_density = target_probe_density
	probe_generator.min_probe_distance = min_probe_distance
	probes = probe_generator.generate_probes_from_nodes(mesh_nodes)
	print("[ASTG] Generated %d high-density surface probes (density: %.1f, spacing: %.2fm)." % [
		probes.size(), target_probe_density, min_probe_distance
	])

	# 2. Build surface clusters
	clusters = cluster_builder.build_clusters_from_meshes(mesh_nodes, probes)
	probe_depositor.clusters = clusters

	# 3. Build adaptive angular hierarchy and direct transport nodes for each light (Depth-4)
	light_hierarchies.clear()
	bounce0_nodes.clear()
	bounce1_nodes.clear()
	dependency_db.clear()

	for i in range(lights.size()):
		var hier = AngularHierarchy.new()
		hier.build_hierarchy(i, lights[i], ray_tracer)
		light_hierarchies.append(hier)

		for node in hier.transport_nodes:
			bounce0_nodes.append(node)
			if node.destruction_chunk_id >= 0:
				dependency_db.register_node_dependency(node.destruction_chunk_id, node, i, node.angular_cell_id)

	print("[ASTG] Built %d direct transport nodes across %d lights." % [bounce0_nodes.size(), lights.size()])

	# 4. Deposit direct transport and record sparse transfer coefficients
	var direct_deps = probe_depositor.deposit_direct_transport(bounce0_nodes, lights, probes)
	print("[ASTG] Deposited direct transport (%d probe interactions)." % direct_deps)

	# 5. Trace and deposit explicit diffuse bounces (4 bounces)
	var diffuse_deps = probe_depositor.trace_and_deposit_diffuse_bounces(
		bounce0_nodes, lights, probes, bounce1_nodes, max_diffuse_bounces
	)
	print("[ASTG] Generated %d diffuse transport nodes across %d bounces (%d diffuse probe deposits)." % [
		bounce1_nodes.size(), max_diffuse_bounces, diffuse_deps
	])
	print("[ASTG] Recorded %d sparse persistent transfer coefficients in Contribution Table." % contribution_table.total_contributions)

	_cache_light_states()
	print("[ASTG] Precomputation complete! Late-bound lighting active.")

func process_frame(delta: float, camera: Camera3D = null, requested_probe_ids: Array[int] = []) -> Dictionary:
	var start_time = Time.get_ticks_usec()
	ray_tracer.reset_frame_counters()

	# 1. Late-Bound Light State Update: O(1) constant-cost writes, ZERO graph walks, ZERO eager probe rewrites
	var light_mod_count = 0
	if _have_lights_changed():
		for i in range(lights.size()):
			var l = lights[i]
			var prev = last_light_states[i]
			if l.visible != prev.visible:
				light_manager.set_light_enabled(i, l.visible)
				light_mod_count += 1
			if l.light_color != prev.color:
				light_manager.set_light_color(i, l.light_color)
				light_mod_count += 1
			if abs(l.light_energy - prev.energy) > 0.0001:
				light_manager.set_light_energy(i, l.light_energy)
				light_mod_count += 1
		_cache_light_states()

	# 2. Lazy Probe Evaluation on Demand (Only requested/visible probes refreshed)
	var lazy_refresh_stats = {}
	if not requested_probe_ids.is_empty():
		lazy_refresh_stats = contribution_table.refresh_batch(requested_probe_ids, light_manager)

	# 3. Process priority repair queue under frame ray budget for destruction events
	var repair_stats = repair_scheduler.process_repairs(
		light_hierarchies,
		probe_depositor,
		lights,
		probes,
		bounce0_nodes,
		bounce1_nodes
	)

	var elapsed_ms = (Time.get_ticks_usec() - start_time) / 1000.0

	return {
		"time_ms": elapsed_ms,
		"rays_traced": ray_tracer.rays_traced_this_frame,
		"active_probes": probes.size(),
		"active_nodes": bounce0_nodes.size() + bounce1_nodes.size(),
		"bounce0_nodes": bounce0_nodes.size(),
		"bounce1_nodes": bounce1_nodes.size(),
		"light_modifications": light_mod_count,
		"lazy_refreshes": lazy_refresh_stats.get("refreshed_probes", 0),
		"cache_hits": lazy_refresh_stats.get("cached_probes", 0),
		"repair_jobs_done": repair_stats.jobs_processed,
		"remaining_repair_queue": repair_stats.remaining_queue,
		"regrown_count": repair_stats.regrown_count
	}

func notify_chunk_destroyed(chunk_id: int, bounds: AABB = AABB()) -> void:
	print("[ASTG] Geometry Removed: Chunk ID %d" % chunk_id)
	repair_scheduler.trigger_burst_budget()

	if backend != null:
		backend.destroy_chunk(chunk_id)

	var disabled_probes = 0
	for p in probes:
		if p.destruction_chunk_id == chunk_id or p.chunk_id == chunk_id:
			p.flags &= ~GIEnums.ProbeFlags.VALID
			p.reset_radiance()
			if contribution_table != null:
				contribution_table.clear_probe(p.id)
			disabled_probes += 1
	if disabled_probes > 0:
		print("[ASTG] Disabled %d orphan probes attached to destroyed chunk %d." % [disabled_probes, chunk_id])

	var res = dependency_db.invalidate_chunk(chunk_id)
	var cells_to_retrace = res.retrace_cells

	for item in cells_to_retrace:
		var l_id = item.light_id
		var c_id = item.cell_id
		repair_scheduler.queue_repair_job(l_id, c_id, 2.0)

func notify_chunk_restored(chunk_id: int, bounds: AABB = AABB()) -> void:
	print("[ASTG] Geometry Restored: Chunk ID %d" % chunk_id)
	if backend != null:
		backend.restore_chunk(chunk_id)

	for p in probes:
		if p.destruction_chunk_id == chunk_id or p.chunk_id == chunk_id:
			p.flags |= GIEnums.ProbeFlags.VALID

func _cache_light_states() -> void:
	last_light_states.clear()
	for l in lights:
		last_light_states.append({
			"visible": l.visible,
			"color": l.light_color,
			"energy": l.light_energy
		})

func _have_lights_changed() -> bool:
	if last_light_states.size() != lights.size():
		return true
	for i in range(lights.size()):
		var l = lights[i]
		var prev = last_light_states[i]
		if l.visible != prev.visible or l.light_color != prev.color or abs(l.light_energy - prev.energy) > 0.0001:
			return true
	return false
