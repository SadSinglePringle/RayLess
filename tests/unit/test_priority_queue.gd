class_name TestPriorityQueue
extends RefCounted

static func run() -> bool:
	print("  [TestPriorityQueue] Running tests...")
	var scheduler = PriorityRepairScheduler.new()
	scheduler.normal_ray_budget = 100
	scheduler.burst_budget = 500
	scheduler.current_budget = 100
	
	if scheduler.get_queue_size() != 0:
		printerr("FAIL: Queue should initially be empty")
		return false
		
	scheduler.queue_repair_job(0, 1, 2.5)
	scheduler.queue_repair_job(0, 2, 10.0)
	scheduler.queue_repair_job(0, 3, 5.0)
	
	if scheduler.get_queue_size() != 3:
		printerr("FAIL: Expected 3 items in queue, got %d" % scheduler.get_queue_size())
		return false
		
	# Check priority order
	if scheduler.repair_queue[0].cell_id != 2:
		printerr("FAIL: Item with highest priority (10.0) should be at front")
		return false
		
	scheduler.trigger_burst_budget()
	if scheduler.current_budget != 500:
		printerr("FAIL: Burst budget should be 500")
		return false
		
	print("  [TestPriorityQueue] PASSED!")
	return true
