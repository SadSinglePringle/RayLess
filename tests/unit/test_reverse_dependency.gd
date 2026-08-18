class_name TestReverseDependency
extends RefCounted

static func run() -> bool:
	print("  [TestReverseDependency] Running tests...")
	var db = GeometryDependencyDB.new()
	
	var n1 = TransportNode.new()
	n1.id = 1
	n1.state = GIEnums.NodeState.VALID
	
	var n2 = TransportNode.new()
	n2.id = 2
	n2.state = GIEnums.NodeState.VALID
	
	var n3 = TransportNode.new()
	n3.id = 3
	n3.state = GIEnums.NodeState.VALID
	
	db.register_node_dependency(10, n1, 0, 5)
	db.register_node_dependency(10, n2, 0, 6)
	db.register_node_dependency(20, n3, 1, 8)
	db.register_blocked_candidate(10, 0, 12)
	
	var res_10 = db.invalidate_chunk(10)
	if res_10.invalid_nodes.size() != 2:
		printerr("FAIL: Expected 2 invalid nodes for chunk 10, got %d" % res_10.invalid_nodes.size())
		return false
		
	if n1.state != GIEnums.NodeState.INVALID or n2.state != GIEnums.NodeState.INVALID:
		printerr("FAIL: Nodes were not marked INVALID")
		return false
		
	if res_10.retrace_cells.size() != 3: # 2 dependent cells + 1 blocked candidate
		printerr("FAIL: Expected 3 retrace cells (2 dependent + 1 blocked candidate), got %d" % res_10.retrace_cells.size())
		return false
		
	if n3.state != GIEnums.NodeState.VALID:
		printerr("FAIL: Chunk 20 node should remain VALID")
		return false
		
	print("  [TestReverseDependency] PASSED!")
	return true
