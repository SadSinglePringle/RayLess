extends SceneTree

const TestReverseDependency = preload("res://tests/unit/test_reverse_dependency.gd")
const TestPriorityQueue = preload("res://tests/unit/test_priority_queue.gd")
const TestEnergyPropagation = preload("res://tests/unit/test_energy_propagation.gd")

func _init() -> void:
	print("==================================================")
	print("🧪 Running ASTG Headless Test & Verification Suite")
	print("==================================================")
	
	var all_passed = true
	
	# 1. Reverse Dependency Invalidation Test
	if not TestReverseDependency.run():
		all_passed = false
		
	# 2. Priority Queue & Burst Budget Test
	if not TestPriorityQueue.run():
		all_passed = false
		
	# 3. Energy Propagation & Dirty Queue Test
	if not TestEnergyPropagation.run():
		all_passed = false
		
	if all_passed:
		print("==================================================")
		print("✅ ALL UNIT TESTS PASSED SUCCESSFULLY!")
		print("==================================================")
		quit(0)
	else:
		print("==================================================")
		printerr("❌ SOME TESTS FAILED!")
		print("==================================================")
		quit(1)
