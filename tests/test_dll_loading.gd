extends SceneTree

func _init() -> void:
	print("==================================================")
	print("🧪 Testing ASTG RTX Hardware Ray Tracing DLL")
	print("==================================================")
	
	var dll_path = ProjectSettings.globalize_path("res://bin/astg_rtx.dll")
	print("DLL Path: %s" % dll_path)
	print("DLL Exists: %s" % FileAccess.file_exists("res://bin/astg_rtx.dll"))
	
	quit(0)
