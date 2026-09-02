$MSVC_DIR = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64"
$WIN_SDK_INC = "C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0"
$WIN_SDK_LIB = "C:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0\um\x64"
$WIN_SDK_UCRT = "C:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0\ucrt\x64"
$MSVC_INC = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207\include"
$MSVC_LIB = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207\lib\x64"

if (-not (Test-Path "bin")) {
    New-Item -ItemType Directory -Path "bin" | Out-Null
}

$GIT_COMMIT = (git rev-parse HEAD).Trim()
if (-not $GIT_COMMIT) {
    $GIT_COMMIT = "unknown_commit"
}
$GIT_STATUS = (git status --porcelain --untracked-files=all)
$WORKTREE_DIRTY = if ($GIT_STATUS) { 1 } else { 0 }

$cl = "$MSVC_DIR\cl.exe"
$diag_args = @(
    "/O2",
    "/std:c++17",
    "/EHsc",
    "/openmp",
    "/DASTG_BUILD_COMMIT=`"$GIT_COMMIT`"",
    "/DASTG_BUILD_WORKTREE_DIRTY=$WORKTREE_DIRTY",
    "/I$MSVC_INC",
    "/I$WIN_SDK_INC\um",
    "/I$WIN_SDK_INC\shared",
    "/I$WIN_SDK_INC\ucrt",
    "/I$WIN_SDK_INC\winrt",
    "/Isrc\common",
    "/Isrc\rtx",
    "/Isrc\astg",
    "/Isrc\diagnostics",
    "src\diagnostics\astg_e2e_tests_main.cpp",
    "/link",
    "/LIBPATH:$MSVC_LIB",
    "/LIBPATH:$WIN_SDK_LIB",
    "/LIBPATH:$WIN_SDK_UCRT",
    "/LIBPATH:bin",
    "/OUT:bin\astg_e2e_tests.exe",
    "astg_rtx.lib",
    "d3d12.lib",
    "dxgi.lib",
    "d3dcompiler.lib"
)

Write-Host "Compiling bin\astg_e2e_tests.exe (Commit: $GIT_COMMIT) with MSVC..."
& $cl $diag_args

if (Test-Path "bin\astg_e2e_tests.exe") {
    Write-Host "✅ Successfully built bin\astg_e2e_tests.exe!" -ForegroundColor Green
} else {
    Write-Error "❌ Build failed for astg_e2e_tests.exe"
}
