class_name PriorityRepairScheduler
extends RefCounted

var repair_queue: Array[Dictionary] = [] # Array of {"light_id": int, "cell_id": int, "priority": float}
var normal_ray_budget: int = 20000
var burst_budget: int = 80000
var current_budget: int = 20000
var burst_decay_rate: float = 0.5

func queue_repair_job(light_id: int, cell_id: int, priority: float = 1.0) -> void:
	# Avoid duplicate jobs (Deduplication)
	for job in repair_queue:
		if job.light_id == light_id and job.cell_id == cell_id:
			job.priority = max(job.priority, priority)
			return
			
	repair_queue.append({
		"light_id": light_id,
		"cell_id": cell_id,
		"priority": priority,
		"age_frames": 0
	})
	# Keep queue sorted by priority descending
	repair_queue.sort_custom(func(a, b): return (a.priority + a.age_frames * 0.1) > (b.priority + b.age_frames * 0.1))

func age_queue_jobs() -> void:
	for job in repair_queue:
		job.age_frames += 1
	# Re-sort to prevent starvation
	repair_queue.sort_custom(func(a, b): return (a.priority + a.age_frames * 0.1) > (b.priority + b.age_frames * 0.1))

func trigger_burst_budget() -> void:
	current_budget = burst_budget
	print("[PriorityRepairScheduler] Triggered burst repair budget: %d rays" % current_budget)

func process_repairs(
	hierarchies: Array[AngularHierarchy],
	depositor: ProbeDepositor,
	lights: Array[Light3D],
	probes: Array[SurfaceProbe],
	bounce0_nodes: Array[TransportNode],
	bounce1_nodes: Array[TransportNode]
) -> Dictionary:
	var rays_spent = 0
	var jobs_processed = 0
	var regrown_nodes: Array[TransportNode] = []
	
	while not repair_queue.is_empty() and rays_spent < current_budget:
		var job = repair_queue.pop_front()
		var l_id = job.light_id
		var c_id = job.cell_id
		
		if l_id >= 0 and l_id < hierarchies.size():
			var hier = hierarchies[l_id]
			var regrown_node = hier.retrace_leaf_cell(c_id)
			rays_spent += 5 # 5 rays per cell sample evaluation
			jobs_processed += 1
			
			if regrown_node != null:
				regrown_nodes.append(regrown_node)
				
	# Decay burst budget toward normal
	if current_budget > normal_ray_budget:
		current_budget = int(lerp(float(current_budget), float(normal_ray_budget), burst_decay_rate))
		
	# If any nodes were regrown, update probe deposition for direct and diffuse bounces
	if not regrown_nodes.is_empty():
		depositor.deposit_direct_transport(bounce0_nodes, lights, probes)
		depositor.trace_and_deposit_diffuse_bounces(bounce0_nodes, lights, probes, bounce1_nodes)
		
	return {
		"jobs_processed": jobs_processed,
		"rays_spent": rays_spent,
		"remaining_queue": repair_queue.size(),
		"regrown_count": regrown_nodes.size()
	}

func get_queue_size() -> int:
	return repair_queue.size()
