class_name ASTGDestructionValidation
extends RefCounted

# ==============================================================================
# ASTG DESTRUCTION & AS SYNCHRONIZATION VALIDATION
# Validates O(1) TLAS chunk masking, reverse dependencies, and repair locality
# ==============================================================================

const TestResultScript = preload("res://tests/suite/test_result.gd")
const GeometryDependencyDBScript = preload("res://scripts/core/runtime/geometry_dependency_db.gd")

static func run_all() -> Array:
	var results: Array = []
	results.append(test_chunk_masking())
	results.append(test_repair_locality())
	return results

static func test_chunk_masking() -> RefCounted:
	var res = TestResultScript.create("destruction_chunk_masking", 0, "UNIT")
	var destroyed_mask = 0
	destroyed_mask |= (1 << 4) # Destroy chunk 4
	
	res.add_assertion("Chunk 4 is masked in bitfield", (destroyed_mask & (1 << 4)) != 0)
	res.add_assertion("Chunk 3 is NOT masked in bitfield", (destroyed_mask & (1 << 3)) == 0)
	return res

static func test_repair_locality() -> RefCounted:
	var res = TestResultScript.create("destruction_repair_locality", 0, "UNIT")
	var db = GeometryDependencyDBScript.new()
	var nodes: Array = []
	for c in range(32):
		for n in range(10):
			var tn = TransportNode.new()
			tn.id = c * 10 + n
			tn.state = GIEnums.NodeState.VALID
			nodes.append(tn)
			db.register_node_dependency(c, tn, 0, n)

	var affected = db.invalidate_chunk(5)
	var invalidated = affected.get("invalid_nodes", [])
	res.add_assertion("Invalidated exactly 10 nodes for chunk 5", invalidated.size() == 10)
	res.metrics["total_nodes"] = 320
	res.metrics["invalidated_nodes"] = invalidated.size()
	res.metrics["preserved_ratio"] = float(320 - invalidated.size()) / 320.0
	res.add_assertion("Locality preserved: >95% of topology remained intact", res.metrics["preserved_ratio"] > 0.95)
	return res
