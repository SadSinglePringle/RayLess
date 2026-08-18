class_name ASTGSuiteConfig
extends RefCounted

# ==============================================================================
# ASTG SUITE CONFIGURATION & CLI PARSER
# ==============================================================================

enum SuiteLevel {
	QUICK,     # < 30 seconds: sanity, unit tests, tiny DXR trace, zero-ray invariant
	STANDARD,  # 2-5 minutes: multi-scene, destruction cycles, hardware RT sweep
	FULL,      # 10-30 minutes: Bistro 32/128/512, fan-in sweep, quality PSNR/SSIM
	STRESS     # Exhaustive/nightly: 128k lights, millions of contributions, 100k cycles
}

var level: int = SuiteLevel.QUICK
var target_group: String = ""
var target_scene: String = "bistro"
var json_output_path: String = "results/latest/summary.json"
var fail_fast: bool = true
var is_headless: bool = true

static func parse_args() -> RefCounted:
	var cfg = (load("res://tests/suite/suite_config.gd") as GDScript).new()
	var args = OS.get_cmdline_user_args()
	if args.is_empty():
		args = OS.get_cmdline_args()

	var i = 0
	while i < args.size():
		var a = args[i]
		if (a == "--suite" or a == "--level") and i + 1 < args.size():
			var s = args[i + 1].to_lower()
			if s == "standard": cfg.level = SuiteLevel.STANDARD
			elif s == "full": cfg.level = SuiteLevel.FULL
			elif s == "stress": cfg.level = SuiteLevel.STRESS
			else: cfg.level = SuiteLevel.QUICK
			i += 1
		elif a == "--group" and i + 1 < args.size():
			cfg.target_group = args[i + 1].to_lower()
			i += 1
		elif a == "--scene" and i + 1 < args.size():
			cfg.target_scene = args[i + 1].to_lower()
			i += 1
		elif a == "--json" and i + 1 < args.size():
			cfg.json_output_path = args[i + 1]
			i += 1
		elif a == "--no-fail-fast":
			cfg.fail_fast = false
		i += 1

	return cfg

func get_level_string() -> String:
	match level:
		SuiteLevel.QUICK: return "QUICK"
		SuiteLevel.STANDARD: return "STANDARD"
		SuiteLevel.FULL: return "FULL"
		SuiteLevel.STRESS: return "STRESS"
		_: return "UNKNOWN"
