class_name TelemetryLogger
extends RefCounted

var log_entries: Array[Dictionary] = []
var benchmark_name: String = "default"

func start_session(p_name: String) -> void:
	benchmark_name = p_name
	log_entries.clear()

func log_frame(entry: Dictionary) -> void:
	log_entries.append(entry)

func export_csv(file_path: String = "") -> String:
	if file_path.is_empty():
		DirAccess.make_dir_recursive_absolute("user://benchmark_results")
		file_path = "user://benchmark_results/benchmark_%s_%d.csv" % [benchmark_name, Time.get_unix_time_from_system()]
		
	var file = FileAccess.open(file_path, FileAccess.WRITE)
	if file == null:
		push_error("[Telemetry] Failed to open file for writing: %s" % file_path)
		return ""
		
	# CSV Headers
	var headers = [
		"frame", "mode", "time_ms", "rays_traced", "active_probes",
		"active_edges", "invalid_edges", "edges_repaired", "new_edges",
		"mse_vs_gt", "psnr_vs_gt", "t90_ratio"
	]
	file.store_line(",".join(headers))
	
	for entry in log_entries:
		var line_vals = [
			str(entry.get("frame", 0)),
			str(entry.get("mode", "ASTG")),
			"%.3f" % entry.get("time_ms", 0.0),
			str(entry.get("rays_traced", 0)),
			str(entry.get("active_probes", 0)),
			str(entry.get("active_edges", 0)),
			str(entry.get("invalid_edges", 0)),
			str(entry.get("edges_repaired", 0)),
			str(entry.get("new_edges", 0)),
			"%.6f" % entry.get("mse", 0.0),
			"%.2f" % entry.get("psnr", 0.0),
			"%.3f" % entry.get("t90_ratio", 1.0)
		]
		file.store_line(",".join(line_vals))
		
	file.close()
	print("[Telemetry] Exported %d frame records to %s" % [log_entries.size(), file_path])
	return file_path

func compute_summary() -> Dictionary:
	if log_entries.is_empty():
		return {}
		
	var total_time = 0.0
	var total_rays = 0
	var max_rays = 0
	var avg_mse = 0.0
	var t90_frames = -1
	
	for i in range(log_entries.size()):
		var e = log_entries[i]
		var t = e.get("time_ms", 0.0)
		var r = e.get("rays_traced", 0)
		var m = e.get("mse", 0.0)
		var t90 = e.get("t90_ratio", 0.0)
		
		total_time += t
		total_rays += r
		max_rays = max(max_rays, r)
		avg_mse += m
		
		if t90 >= 0.90 and t90_frames == -1 and i > 0:
			t90_frames = i
			
	var n = float(log_entries.size())
	return {
		"benchmark": benchmark_name,
		"total_frames": log_entries.size(),
		"avg_time_ms": total_time / n,
		"avg_rays_per_frame": total_rays / n,
		"peak_rays": max_rays,
		"avg_mse": avg_mse / n,
		"t90_latency_frames": t90_frames if t90_frames != -1 else log_entries.size()
	}
