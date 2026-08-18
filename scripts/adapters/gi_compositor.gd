class_name ASTGGICompositor
extends RefCounted

# ==============================================================================
# ASTG GI COMPOSITOR & DIAGNOSTIC MODES (GODOT ↔ ASTG COMPOSITION ENGINE)
# Composites ASTG Indirect Diffuse Irradiance into Godot's PBR Forward+ Frame
# ==============================================================================

enum DiagnosticMode {
	MODE_COMBINED = 0,         # F1: Godot Direct Lighting + ASTG Indirect GI
	MODE_INDIRECT_ONLY = 1,    # F2: ASTG Indirect GI Only
	MODE_DIRECT_ONLY = 2,      # F3: Godot Direct Lighting Only
	MODE_PROBE_POSITIONS = 3,  # F4: Surface Probe Anchors
	MODE_PROBE_IRRADIANCE = 4, # F5: Live Probe Irradiance Field
	MODE_CLUSTERS = 5,         # F6: Geometric Surface Clusters
	MODE_TRANSPORT_DAG = 6,    # F7: Active Transport Branches
	MODE_INVALIDATED = 7,      # F8: Invalidated Transport Branches
	MODE_RT_GEOMETRY = 8,      # F9: RT Geometry Wireframe
	MODE_CONTRIBUTION_DENSITY = 9 # F10: Light Contribution Density
}

var current_mode: int = DiagnosticMode.MODE_COMBINED
var is_godot_gi_disabled: bool = true

# Configures Godot Environment to receive ASTG GI and disable duplicate engines
static func setup_environment(env: Environment) -> void:
	if env == null:
		return
	# 1. Disable Godot built-in GI systems to prevent double indirect lighting
	env.sdfgi_enabled = false
	env.glow_enabled = true
	env.glow_intensity = 0.8
	env.glow_bloom = 0.2
	env.tonemap_mode = Environment.TONE_MAPPER_ACES
	env.tonemap_exposure = 1.15

	# 2. Configure Ambient Light Channel to accept ASTG Indirect Irradiance
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.ambient_light_color = Color(0.08, 0.10, 0.14)
	env.ambient_light_energy = 0.35

# Injects evaluated ASTG surface probe field into Godot's ambient/indirect frame
func composite_indirect_frame(
	env: Environment,
	sun_light: DirectionalLight3D,
	dynamic_lights: Array[OmniLight3D],
	probe_colors: PackedColorArray,
	probes: Array[Dictionary],
	cam_forward: Vector3,
	cam_pos: Vector3
) -> void:
	if env == null:
		return

	# Calculate frustum-integrated indirect irradiance
	var frustum_flux = Color.BLACK
	var frustum_count = 0

	for i in range(min(probes.size(), probe_colors.size())):
		var p_pos = probes[i]["position"] if probes[i].has("position") else Vector3.ZERO
		var to_probe = p_pos - cam_pos
		var dist = to_probe.length()
		if dist < 65.0:
			var f_dot = cam_forward.dot(to_probe / max(0.001, dist))
			if f_dot > 0.2:
				frustum_flux += probe_colors[i]
				frustum_count += 1

	var avg_indirect = (frustum_flux / float(max(1, frustum_count))).clamp(Color(0.02, 0.02, 0.03), Color(1.5, 1.5, 1.5))

	# Apply Diagnostic Render Mode
	match current_mode:
		DiagnosticMode.MODE_COMBINED:
			# Godot Direct + ASTG Indirect
			if is_instance_valid(sun_light): sun_light.light_energy = 1.2
			env.ambient_light_color = avg_indirect
			env.ambient_light_energy = clamp(avg_indirect.get_luminance() * 0.75, 0.25, 2.2)

		DiagnosticMode.MODE_INDIRECT_ONLY:
			# ASTG Indirect Only (Direct Emitters to 0.0)
			if is_instance_valid(sun_light): sun_light.light_energy = 0.0
			for l in dynamic_lights:
				if is_instance_valid(l): l.light_energy = 0.0
			env.ambient_light_color = avg_indirect
			env.ambient_light_energy = clamp(avg_indirect.get_luminance() * 1.2, 0.4, 3.0)

		DiagnosticMode.MODE_DIRECT_ONLY:
			# Godot Direct Only (Indirect GI Ambient to 0.02)
			if is_instance_valid(sun_light): sun_light.light_energy = 1.2
			env.ambient_light_color = Color(0.01, 0.01, 0.02)
			env.ambient_light_energy = 0.05

		_:
			# Debug Visualization Overlay
			env.ambient_light_color = avg_indirect
			env.ambient_light_energy = 0.35

# Returns name of active diagnostic mode
func get_mode_name() -> String:
	match current_mode:
		DiagnosticMode.MODE_COMBINED: return "F1: Combined (Godot Direct + ASTG Indirect)"
		DiagnosticMode.MODE_INDIRECT_ONLY: return "F2: ASTG Indirect Only (Pure Bounce)"
		DiagnosticMode.MODE_DIRECT_ONLY: return "F3: Godot Direct Only (No GI)"
		DiagnosticMode.MODE_PROBE_POSITIONS: return "F4: Probe Anchors"
		DiagnosticMode.MODE_PROBE_IRRADIANCE: return "F5: Live Probe Irradiance Field"
		DiagnosticMode.MODE_CLUSTERS: return "F6: Surface Clusters"
		DiagnosticMode.MODE_TRANSPORT_DAG: return "F7: Transport DAG Branches"
		DiagnosticMode.MODE_INVALIDATED: return "F8: Invalidated Branches"
		DiagnosticMode.MODE_RT_GEOMETRY: return "F9: RT Geometry Wireframe"
		DiagnosticMode.MODE_CONTRIBUTION_DENSITY: return "F10: Contribution Density"
		_: return "Unknown"
