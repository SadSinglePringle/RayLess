---
trigger: always_on
description: Guidelines for clean-commit builds, full test execution, and evidence sealing.
---

# Evidence Verification Workflow

Before publishing milestone completions or claiming validation:
1. **Commit First**: Stage and commit all functional code changes so `git rev-parse HEAD` yields the canonical commit SHA.
2. **Rebuild with Clean Commit**: Rebuild all binaries (`astg_rtx.dll`, `astg_diagnostics.exe`, `astg_e2e_tests.exe`) with the commit SHA embedded via compiler definitions (`/DASTG_BUILD_COMMIT`).
3. **Execute Full Test Suites**: Run `astg_diagnostics.exe` and `astg_e2e_tests.exe` to verify 100% passing status across all tiers and categories without skipping.
4. **Atomic Evidence Sealing**: Copy generated run artifacts into `results/latest/` and commit/push alongside code updates.
