extends SceneTree

func _init() -> void:
	print("Capturing screenshot of classroom...")
	var main_scene = preload("res://scenes/main.tscn").instantiate()
	root.add_child(main_scene)
	
	# Let scene load and GI settle
	for i in range(10):
		await process_frame
		
	var vp = root.get_viewport()
	var img = vp.get_texture().get_image()
	if img != null:
		var err = img.save_png("res://screenshot_classroom.png")
		print("Saved screenshot to screenshot_classroom.png (Error %d)" % err)
	quit(0)
