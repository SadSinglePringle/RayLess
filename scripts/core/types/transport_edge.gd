class_name TransportEdge
extends RefCounted

var id: int = -1
var source_id: int = -1          # Light ID (if is_light_edge) or Source Probe ID
var dest_probe_id: int = -1      # Destination Probe ID
var is_light_edge: bool = false
var transport_weight: float = 0.0 # Form factor / geometric transfer coefficient
var visibility: float = 1.0       # 1.0 = clear line of sight, 0.0 = blocked
var direction: Vector3 = Vector3.UP # Direction from source to dest
var distance: float = 1.0
var confidence: float = 1.0       # [0.0, 1.0]
var blocking_chunk_id: int = -1   # Destructible chunk ID currently occluding this edge (-1 if none/static)
var state: int = GIEnums.EdgeState.VALID
var last_repair_frame: int = 0

func get_effective_weight() -> float:
	if state != GIEnums.EdgeState.VALID and state != GIEnums.EdgeState.UNCERTAIN:
		return 0.0
	return transport_weight * visibility * confidence

func is_active() -> bool:
	return (state == GIEnums.EdgeState.VALID or state == GIEnums.EdgeState.UNCERTAIN) and visibility > 0.001
