class_name GIEnums

enum NodeState {
	VALID = 0,      # Green: Valid cached transport
	INVALID = 1,    # Red: Invalidated by destruction
	QUEUED = 2,     # Yellow: Queued in repair scheduler
	REGROWN = 3,    # Blue: Newly regrown transport branch
	BLOCKED = 4     # Gray: Blocked candidate path
}

enum CellState {
	LEAF = 0,
	SUBDIVIDED = 1,
	INVALID = 2,
	QUEUED = 3,
	REGROWN = 4
}

enum ProbeFlags {
	NONE = 0,
	DIRTY = 1 << 0,
	UNCERTAIN = 1 << 1,
	VALID = 1 << 2,
	DESTRUCTIBLE = 1 << 3,
	ADAPTIVE_SUBDIVIDED = 1 << 4
}

enum GIMode {
	ASTG = 0,               # Persistent Light Transport Graph + Adaptive Probes (Proposed)
	DDGI_BASELINE = 1,      # World-space probe grid baseline
	GROUND_TRUTH = 2,       # High-sample reference solver
	DIRECT_ONLY = 3,        # Direct lighting only (Bounce 0 only)
	ALBEDO_ONLY = 4         # Unshaded albedo
}

enum ClusterAdjacencyType {
	COPLANAR = 0,             # Preferred adjacent cluster (weight = 0.8)
	SMOOTH_CONTINUATION = 1,  # Continuous curved surface (weight = 0.5)
	SHARP_EDGE = 2,           # Normal discontinuity >= 35 deg (weight = 0.0, reject)
	DISCONNECTED = 3          # Unrelated / distant mesh surface (reject)
}

enum ProbeLookupTier {
	EXACT_CLUSTER = 0,        # Level 1: Same surface cluster (weight = 1.0)
	ADJACENT_COMPATIBLE = 1,  # Level 2: Connected coplanar / smooth cluster
	WORLD_FALLBACK = 2,       # Level 4: Conservative world-space fallback
	REJECTED = 3              # Candidate rejected by normal or topology filter
}

enum DebugViewMode {
	NONE = 0,
	TRANSPORT_GRAPH = 1,    # Green=Valid, Red=Invalid, Yellow=Queued, Blue=Regrown
	ANGULAR_CELLS = 2,      # Octahedral angular funnels from lights
	PROBES_TOTAL_IRRADIANCE = 3,
	PROBES_DIRECT_ONLY = 4,
	PROBES_INDIRECT_ONLY = 5,
	PROBES_CONFIDENCE = 6,
	PROBES_DIRTY_STATE = 7,
	PROBES_ERROR_HEATMAP = 8,
	DAMAGE_BOUNDS = 9,
	PROBE_LEAK_DEBUG = 10   # Green=Exact, Blue=Adjacent, Yellow=Fallback, Red=Rejected
}

enum BenchmarkScenario {
	TEST_A_LIGHT_TOGGLE = 0,
	TEST_B_COLOR_SHIFT = 1,
	TEST_C_WALL_BREACH = 2,
	TEST_D_MULTI_ROOM = 3,
	TEST_E_EXPLOSION = 4
}
