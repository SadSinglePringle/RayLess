class_name AngularCell
extends RefCounted

var id: int = -1
var light_id: int = -1

var uv_min: Vector2 = Vector2.ZERO
var uv_max: Vector2 = Vector2.ONE

var parent_id: int = -1
var child_indices: Array[int] = []

var transport_node_id: int = -1
var depth: int = 0
var solid_angle: float = 0.0

var state: int = GIEnums.CellState.LEAF
var flags: int = 0

func get_center_uv() -> Vector2:
	return (uv_min + uv_max) * 0.5

func get_center_direction() -> Vector3:
	return octahedral_to_direction(get_center_uv())

func get_corner_uvs() -> Array[Vector2]:
	return [
		uv_min,
		Vector2(uv_max.x, uv_min.y),
		uv_max,
		Vector2(uv_min.x, uv_max.y)
	]

func get_corner_directions() -> Array[Vector3]:
	var dirs: Array[Vector3] = []
	for uv in get_corner_uvs():
		dirs.append(octahedral_to_direction(uv))
	return dirs

func compute_approx_solid_angle() -> float:
	# Total sphere solid angle is 4 * PI.
	# Normalized 2D area (du * dv) maps proportionally to sphere solid angle.
	var du = uv_max.x - uv_min.x
	var dv = uv_max.y - uv_min.y
	solid_angle = du * dv * 4.0 * PI
	return solid_angle

# -------------------------------------------------------------
# Static Octahedral Projection Math (Sphere <-> [0, 1]^2)
# -------------------------------------------------------------
static func direction_to_octahedral(d: Vector3) -> Vector2:
	var norm_d = d.normalized()
	var l1 = absf(norm_d.x) + absf(norm_d.y) + absf(norm_d.z)
	if l1 < 0.0001:
		return Vector2(0.5, 0.5)
		
	var p = Vector2(norm_d.x / l1, norm_d.z / l1)
	if norm_d.y < 0.0:
		var sign_x = 1.0 if p.x >= 0.0 else -1.0
		var sign_z = 1.0 if p.y >= 0.0 else -1.0
		p = Vector2((1.0 - absf(p.y)) * sign_x, (1.0 - absf(p.x)) * sign_z)
		
	return (p + Vector2.ONE) * 0.5 # Remap from [-1, 1] to [0, 1]

static func octahedral_to_direction(uv: Vector2) -> Vector3:
	var p = uv * 2.0 - Vector2.ONE # Remap [0, 1] to [-1, 1]
	var d = Vector3(p.x, 1.0 - (absf(p.x) + absf(p.y)), p.y)
	
	if d.y < 0.0:
		var sign_x = 1.0 if d.x >= 0.0 else -1.0
		var sign_z = 1.0 if d.z >= 0.0 else -1.0
		d.x = (1.0 - absf(p.y)) * sign_x
		d.z = (1.0 - absf(p.x)) * sign_z
		
	return d.normalized()
