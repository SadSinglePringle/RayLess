class_name HUDController
extends CanvasLayer

signal gi_mode_changed(new_mode: int)
signal debug_view_changed(new_mode: int)
signal camera_preset_changed(preset: int)
signal toggle_probes_requested
signal cycle_light_colors_requested
signal toggle_rainbow_anim_requested
signal destroy_center_chunk_requested
signal destroy_all_chunks_requested
signal destroy_irrelevant_wall_requested
signal restore_wall_requested
signal toggle_light_requested
signal toggle_occluder_requested
signal run_benchmark_requested(scenario: int)

var telemetry_label: Label
var stats_panel: PanelContainer
var gi_mode_btn: OptionButton
var debug_mode_btn: OptionButton
var camera_preset_btn: OptionButton
var btn_toggle_probes: Button
var btn_cycle_colors: Button
var btn_rainbow_anim: Button

func _ready() -> void:
	_create_ui_layout()

func _create_ui_layout() -> void:
	var root_margin = MarginContainer.new()
	root_margin.set_anchors_preset(Control.PRESET_FULL_RECT)
	root_margin.add_theme_constant_override("margin_left", 14)
	root_margin.add_theme_constant_override("margin_top", 14)
	root_margin.add_theme_constant_override("margin_right", 14)
	root_margin.add_theme_constant_override("margin_bottom", 14)
	root_margin.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(root_margin)
	
	var h_split = HBoxContainer.new()
	h_split.mouse_filter = Control.MOUSE_FILTER_IGNORE
	root_margin.add_child(h_split)
	
	# Left: Telemetry & Controls Panel (Scrollable)
	var left_panel = PanelContainer.new()
	left_panel.custom_minimum_size = Vector2(410, 0)
	var scroll = ScrollContainer.new()
	scroll.custom_minimum_size = Vector2(410, 0)
	left_panel.add_child(scroll)
	
	var left_vbox = VBoxContainer.new()
	left_vbox.add_theme_constant_override("separation", 8)
	left_vbox.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	scroll.add_child(left_vbox)
	h_split.add_child(left_panel)
	
	# Title
	var title_lbl = Label.new()
	title_lbl.text = "🏫 ASTG Blender Classroom Benchmark"
	title_lbl.add_theme_font_size_override("font_size", 16)
	left_vbox.add_child(title_lbl)
	
	# Camera Preset Selector
	var cam_hbox = HBoxContainer.new()
	var cam_lbl = Label.new()
	cam_lbl.text = "Camera:"
	cam_hbox.add_child(cam_lbl)
	camera_preset_btn = OptionButton.new()
	camera_preset_btn.add_item("Cam 1: Iconic Benchmark View [1]", 1)
	camera_preset_btn.add_item("Cam 2: Front Chalkboard View [2]", 2)
	camera_preset_btn.add_item("Cam 3: Student Desk Close-Up [3]", 3)
	camera_preset_btn.add_item("Cam 4: Windows & Shutters [4]", 4)
	camera_preset_btn.add_item("Cam 5: Free Fly (WASD) [5]", 5)
	camera_preset_btn.item_selected.connect(func(idx): camera_preset_changed.emit(camera_preset_btn.get_item_id(idx)))
	cam_hbox.add_child(camera_preset_btn)
	left_vbox.add_child(cam_hbox)
	
	# GI Mode Selector
	var mode_hbox = HBoxContainer.new()
	var mode_lbl = Label.new()
	mode_lbl.text = "GI Mode:"
	mode_hbox.add_child(mode_lbl)
	gi_mode_btn = OptionButton.new()
	gi_mode_btn.add_item("ASTG (Proposed Graph)", GIEnums.GIMode.ASTG)
	gi_mode_btn.add_item("DDGI Baseline (Probe Grid)", GIEnums.GIMode.DDGI_BASELINE)
	gi_mode_btn.add_item("Ground Truth Reference", GIEnums.GIMode.GROUND_TRUTH)
	gi_mode_btn.add_item("Direct Lighting Only", GIEnums.GIMode.DIRECT_ONLY)
	gi_mode_btn.item_selected.connect(func(idx): gi_mode_changed.emit(gi_mode_btn.get_item_id(idx)))
	mode_hbox.add_child(gi_mode_btn)
	left_vbox.add_child(mode_hbox)
	
	# Probe Visualization Toggle Button
	btn_toggle_probes = Button.new()
	btn_toggle_probes.text = "🟢 Hide Sparse Surface Probes [P]"
	btn_toggle_probes.pressed.connect(func(): toggle_probes_requested.emit())
	left_vbox.add_child(btn_toggle_probes)
	
	# Debug View Mode
	var dbg_hbox = HBoxContainer.new()
	var dbg_lbl = Label.new()
	dbg_lbl.text = "Probe Coloring:"
	dbg_hbox.add_child(dbg_lbl)
	debug_mode_btn = OptionButton.new()
	debug_mode_btn.add_item("Total Irradiance (Live Shaded)", GIEnums.DebugViewMode.NONE)
	debug_mode_btn.add_item("Transport DAG Rays", GIEnums.DebugViewMode.TRANSPORT_GRAPH)
	debug_mode_btn.add_item("Angular Funnels (Octahedral)", GIEnums.DebugViewMode.ANGULAR_CELLS)
	debug_mode_btn.add_item("Direct Radiance Only", GIEnums.DebugViewMode.PROBES_DIRECT_ONLY)
	debug_mode_btn.add_item("Indirect Radiance Only", GIEnums.DebugViewMode.PROBES_INDIRECT_ONLY)
	debug_mode_btn.add_item("Confidence Heatmap", GIEnums.DebugViewMode.PROBES_CONFIDENCE)
	debug_mode_btn.add_item("Error Heatmap vs GT", GIEnums.DebugViewMode.PROBES_ERROR_HEATMAP)
	debug_mode_btn.item_selected.connect(func(idx): debug_view_changed.emit(debug_mode_btn.get_item_id(idx)))
	dbg_hbox.add_child(debug_mode_btn)
	left_vbox.add_child(dbg_hbox)
	
	left_vbox.add_child(HSeparator.new())
	
	# Light Color Cycling & Animation Controls
	var color_lbl = Label.new()
	color_lbl.text = "💡 Dynamic Lighting Controls:"
	color_lbl.add_theme_font_size_override("font_size", 14)
	left_vbox.add_child(color_lbl)
	
	btn_cycle_colors = Button.new()
	btn_cycle_colors.text = "🌈 Cycle Scene Light Colors [C]"
	btn_cycle_colors.pressed.connect(func(): cycle_light_colors_requested.emit())
	left_vbox.add_child(btn_cycle_colors)
	
	btn_rainbow_anim = Button.new()
	btn_rainbow_anim.text = "✨ Start Rainbow Color Cycling [R]"
	btn_rainbow_anim.pressed.connect(func(): toggle_rainbow_anim_requested.emit())
	left_vbox.add_child(btn_rainbow_anim)
	
	var btn_light = Button.new()
	btn_light.text = "💡 Toggle 6x Ceiling Pendant Lamps"
	btn_light.pressed.connect(func(): toggle_light_requested.emit())
	left_vbox.add_child(btn_light)
	
	var btn_teacher = Button.new()
	btn_teacher.text = "📖 Toggle Teacher Desk Lamp"
	btn_teacher.pressed.connect(func(): toggle_occluder_requested.emit())
	left_vbox.add_child(btn_teacher)
	
	left_vbox.add_child(HSeparator.new())
	
	# Interactive Destruction Actions
	var dest_lbl = Label.new()
	dest_lbl.text = "🔨 Interactive World Changes:"
	dest_lbl.add_theme_font_size_override("font_size", 14)
	left_vbox.add_child(dest_lbl)
	
	var btn_breach = Button.new()
	btn_breach.text = "🔨 Breach Window Shutter (Sunlight Burst)"
	btn_breach.pressed.connect(func(): destroy_center_chunk_requested.emit())
	left_vbox.add_child(btn_breach)
	
	var btn_explode = Button.new()
	btn_explode.text = "💥 Demolish All Window Shutters"
	btn_explode.pressed.connect(func(): destroy_all_chunks_requested.emit())
	left_vbox.add_child(btn_explode)
	
	var btn_blackboard = Button.new()
	btn_blackboard.text = "🧱 Destroy Chalkboard Panel"
	btn_blackboard.pressed.connect(func(): destroy_irrelevant_wall_requested.emit())
	left_vbox.add_child(btn_blackboard)
	
	var btn_restore = Button.new()
	btn_restore.text = "🔄 Restore All Classroom Elements"
	btn_restore.pressed.connect(func(): restore_wall_requested.emit())
	left_vbox.add_child(btn_restore)
	
	left_vbox.add_child(HSeparator.new())
	
	# Automated Benchmarks
	var bench_lbl = Label.new()
	bench_lbl.text = "📊 Benchmark Suite (Classroom Test):"
	bench_lbl.add_theme_font_size_override("font_size", 14)
	left_vbox.add_child(bench_lbl)
	
	var btn_full_timeline = Button.new()
	btn_full_timeline.text = "🎬 Run Full 85s Timeline Sequence"
	btn_full_timeline.pressed.connect(func(): run_benchmark_requested.emit(-1))
	left_vbox.add_child(btn_full_timeline)
	
	var btn_test_a = Button.new()
	btn_test_a.text = "Test 2/4: Ceiling Lamps Toggle (0-Ray Direct)"
	btn_test_a.pressed.connect(func(): run_benchmark_requested.emit(GIEnums.BenchmarkScenario.TEST_A_LIGHT_TOGGLE))
	left_vbox.add_child(btn_test_a)
	
	var btn_test_b = Button.new()
	btn_test_b.text = "Test 3: Color Shift & Diffuse Bounce (0-Ray)"
	btn_test_b.pressed.connect(func(): run_benchmark_requested.emit(GIEnums.BenchmarkScenario.TEST_B_COLOR_SHIFT))
	left_vbox.add_child(btn_test_b)
	
	var btn_test_c = Button.new()
	btn_test_c.text = "Test 5: Shutter Breach (T90 Latency & Repair)"
	btn_test_c.pressed.connect(func(): run_benchmark_requested.emit(GIEnums.BenchmarkScenario.TEST_C_WALL_BREACH))
	left_vbox.add_child(btn_test_c)
	
	left_vbox.add_child(HSeparator.new())
	
	# Telemetry Readout
	telemetry_label = Label.new()
	telemetry_label.text = "Initializing Classroom ASTG telemetry..."
	telemetry_label.add_theme_font_size_override("font_size", 12)
	left_vbox.add_child(telemetry_label)

func set_probes_visible_state(is_vis: bool) -> void:
	if btn_toggle_probes != null:
		btn_toggle_probes.text = "🟢 Hide Sparse Surface Probes [P]" if is_vis else "🔴 Show Sparse Surface Probes [P]"

func set_rainbow_anim_state(is_anim: bool) -> void:
	if btn_rainbow_anim != null:
		btn_rainbow_anim.text = "⏹️ Stop Rainbow Animation [R]" if is_anim else "✨ Start Rainbow Color Cycling [R]"

func set_debug_mode_index(idx: int) -> void:
	if debug_mode_btn != null:
		for i in range(debug_mode_btn.item_count):
			if debug_mode_btn.get_item_id(i) == idx:
				debug_mode_btn.selected = i
				break

func update_telemetry(astg_metrics: Dictionary, ddgi_metrics: Dictionary, gt_metrics: Dictionary, cur_mode: int) -> void:
	var mode_name = "ASTG"
	match cur_mode:
		GIEnums.GIMode.ASTG: mode_name = "ASTG (Proposed Graph)"
		GIEnums.GIMode.DDGI_BASELINE: mode_name = "DDGI Baseline"
		GIEnums.GIMode.GROUND_TRUTH: mode_name = "Ground Truth"
		GIEnums.GIMode.DIRECT_ONLY: mode_name = "Direct Only"
		
	var txt = """[ Benchmark: Blender Classroom ]
Mode: %s
Frame Time: %.2f ms (%.1f FPS)
Active Surface Probes: %d

[ ASTG Persistent Transport Graph ]
Transport Nodes: %d
Active DAG Edges: %d
Pending Repair Queue: %d
Repair Jobs Finished: %d
Newly Regrown Nodes: %d
Rays Traced This Frame: %d

[ Ground Truth Quality Metrics ]
MSE: %.6f
PSNR: %.2f dB
T90 Converged Ratio: %.1f%%

[ Shortcuts ]
[C] Cycle Colors   |  [R] Rainbow Mode
[P] Toggle Probes  |  [O] Cycle Colors Mode
[1-5] Camera Views |  [RMB] Mouselook""" % [
		mode_name,
		astg_metrics.get("time_ms", 16.6),
		1000.0 / max(0.1, astg_metrics.get("time_ms", 16.6)),
		astg_metrics.get("active_probes", 248),
		astg_metrics.get("active_nodes", 0),
		astg_metrics.get("active_nodes", 0),
		astg_metrics.get("remaining_repair_queue", 0),
		astg_metrics.get("repair_jobs_done", 0),
		astg_metrics.get("regrown_count", 0),
		astg_metrics.get("rays_traced", 0),
		gt_metrics.get("mse", 0.0),
		gt_metrics.get("psnr", 99.9),
		gt_metrics.get("t90_converged_ratio", 1.0) * 100.0
	]
	telemetry_label.text = txt
