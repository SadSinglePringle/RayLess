class_name ASTGResultWriter
extends RefCounted

# ==============================================================================
# ASTG TEST & BENCHMARK RESULT WRITER
# Serializes structured JSON, CSV, and Diagnostic text reports
# ==============================================================================

static func write_results(
	summary_dict: Dictionary,
	results: Array,
	output_dir: String = "res://results/latest"
) -> void:
	var da = DirAccess.open("res://")
	if da != null:
		da.make_dir_recursive(output_dir)
		da.make_dir_recursive("res://results/history")

	# 1. Write summary.json
	var sum_file = FileAccess.open(output_dir + "/summary.json", FileAccess.WRITE)
	if sum_file != null:
		sum_file.store_string(JSON.stringify(summary_dict, "  "))
		sum_file.close()

	# 2. Write tests.json
	var test_list = []
	for r in results:
		test_list.append(r.to_dict())

	var tests_file = FileAccess.open(output_dir + "/tests.json", FileAccess.WRITE)
	if tests_file != null:
		tests_file.store_string(JSON.stringify(test_list, "  "))
		tests_file.close()

	# 3. Write diagnostics.txt
	var diag_file = FileAccess.open(output_dir + "/diagnostics.txt", FileAccess.WRITE)
	if diag_file != null:
		var diag_text = "================================================================================\n"
		diag_text += "🛡️ ASTG DIAGNOSTIC REPORT\n"
		diag_text += "================================================================================\n"
		diag_text += "Suite Status:      %s\n" % summary_dict.get("status", "UNKNOWN")
		diag_text += "Suite Level:       %s\n" % summary_dict.get("level", "QUICK")
		diag_text += "Tests Executed:    %d Total | %d Passed | %d Failed | %d Invalid | %d Skipped\n" % [
			summary_dict.get("total_tests", 0),
			summary_dict.get("passed", 0),
			summary_dict.get("failed", 0),
			summary_dict.get("invalid", 0),
			summary_dict.get("skipped", 0)
		]
		diag_text += "--------------------------------------------------------------------------------\n"
		diag_text += "FAILURES & DIAGNOSES:\n"

		var found_issues = false
		for r in results:
			if r.status != 0: # Status.PASS
				found_issues = true
				diag_text += "\n • [%s] %s (%s)\n" % [r.get_status_string(), r.name, r.evidence_level]
				for err in r.errors:
					diag_text += "    - Error: %s\n" % err
				diag_text += "    - %s\n" % r.diagnosis

		if not found_issues:
			diag_text += "\n  All executed tests passed cleanly! No diagnostic anomalies detected.\n"

		diag_text += "================================================================================\n"
		diag_file.store_string(diag_text)
		diag_file.close()
