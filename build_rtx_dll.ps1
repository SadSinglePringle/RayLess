$MSVC_DIR = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64"
$WIN_SDK_INC = "C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0"
$WIN_SDK_LIB = "C:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0\um\x64"
$WIN_SDK_UCRT = "C:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0\ucrt\x64"
$MSVC_INC = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207\include"
$MSVC_LIB = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207\lib\x64"
$DXC = "C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\dxc.exe"

if (-not (Test-Path "bin")) {
    New-Item -ItemType Directory -Path "bin" | Out-Null
}

Write-Host "Compiling HLSL DXR 1.1 RayQuery compute shader..."
& $DXC -T cs_6_5 -E CSMain -Fh src/rtx/rtx_shader_cso.h -Vn g_rtx_shader_bytecode src/rtx/rtx_shader.hlsl

Write-Host "Compiling HLSL GPU Lazy Probe Refresh compute shader..."
& $DXC -T cs_6_5 -E CSMain -Fh src/rtx/rtx_lazy_probe_refresh_cso.h -Vn g_rtx_lazy_probe_refresh_bytecode src/rtx/rtx_lazy_probe_refresh.hlsl

Write-Host "Compiling HLSL GPU Massive Light Animator compute shader..."
& $DXC -T cs_6_5 -E CSMain -Fh src/rtx/rtx_light_animator_cso.h -Vn g_rtx_light_animator_bytecode src/rtx/rtx_light_animator.hlsl

Write-Host "Compiling HLSL GPU ASTG Transport Visibility compute shader..."
& $DXC -T cs_6_5 -E CSMain -Fh src/rtx/rtx_gpu_transport_cso.h -Vn g_rtx_gpu_transport_bytecode src/rtx/rtx_gpu_transport.hlsl

Write-Host "Compiling HLSL Part J Continuous B0 compute shaders..."
& $DXC -T cs_6_5 -E CSProjectDynamicBounds -Fh src/rtx/rtx_b0_project_bounds_cso.h -Vn g_rtx_b0_project_bounds_bytecode src/rtx/rtx_b0_angular_runtime.hlsl
& $DXC -T cs_6_5 -E CSTraverseB0AngularBVH -Fh src/rtx/rtx_b0_traverse_bvh_cso.h -Vn g_rtx_b0_traverse_bvh_bytecode src/rtx/rtx_b0_angular_runtime.hlsl
& $DXC -T cs_6_5 -E CSApplyB0MembershipDeltas -Fh src/rtx/rtx_b0_apply_deltas_cso.h -Vn g_rtx_b0_apply_deltas_bytecode src/rtx/rtx_b0_angular_runtime.hlsl
& $DXC -T cs_6_5 -E CSCompactB0Transitions -Fh src/rtx/rtx_b0_compact_transitions_cso.h -Vn g_rtx_b0_compact_transitions_bytecode src/rtx/rtx_b0_angular_runtime.hlsl

Write-Host "Compiling HLSL Part K Dynamic Receiver compute shaders..."
& $DXC -T cs_6_5 -E CSTransformBoneBounds -Fh src/rtx/rtx_rec_transform_bones_cso.h -Vn g_rtx_rec_transform_bones_bytecode src/rtx/rtx_dynamic_receiver_runtime.hlsl
& $DXC -T cs_6_5 -E CSTransformReceiverClusters -Fh src/rtx/rtx_rec_transform_clusters_cso.h -Vn g_rtx_rec_transform_clusters_bytecode src/rtx/rtx_dynamic_receiver_runtime.hlsl
& $DXC -T cs_6_5 -E CSTransformSurfaceProbes -Fh src/rtx/rtx_rec_transform_probes_cso.h -Vn g_rtx_rec_transform_probes_bytecode src/rtx/rtx_dynamic_receiver_runtime.hlsl
& $DXC -T cs_6_5 -E CSCullReceiverHierarchy -Fh src/rtx/rtx_rec_cull_hierarchy_cso.h -Vn g_rtx_rec_cull_hierarchy_bytecode src/rtx/rtx_dynamic_receiver_runtime.hlsl
& $DXC -T cs_6_5 -E CSEvaluateReceiverVisibility -Fh src/rtx/rtx_rec_eval_visibility_cso.h -Vn g_rtx_rec_eval_visibility_bytecode src/rtx/rtx_dynamic_receiver_runtime.hlsl
& $DXC -T cs_6_5 -E CSAccumulateReceiverIrradiance -Fh src/rtx/rtx_rec_accum_irradiance_cso.h -Vn g_rtx_rec_accum_irradiance_bytecode src/rtx/rtx_dynamic_receiver_runtime.hlsl

$GIT_COMMIT = (git rev-parse HEAD).Trim()
if (-not $GIT_COMMIT) {
    $GIT_COMMIT = "unknown_commit"
}

$cl = "$MSVC_DIR\cl.exe"
$args = @(
    "/LD",
    "/O2",
    "/std:c++17",
    "/EHsc",
    "/openmp",
    "/DASTG_BUILD_COMMIT=`"$GIT_COMMIT`"",
    "/I$MSVC_INC",
    "/I$WIN_SDK_INC\um",
    "/I$WIN_SDK_INC\shared",
    "/I$WIN_SDK_INC\ucrt",
    "/I$WIN_SDK_INC\winrt",
    "/Isrc\rtx",
    "src\rtx\rtx_raytracer.cpp",
    "/link",
    "/LIBPATH:$MSVC_LIB",
    "/LIBPATH:$WIN_SDK_LIB",
    "/LIBPATH:$WIN_SDK_UCRT",
    "/IMPLIB:bin\astg_rtx.lib",
    "/OUT:bin\astg_rtx.dll",
    "d3d12.lib",
    "dxgi.lib",
    "d3dcompiler.lib"
)

Write-Host "Compiling bin\astg_rtx.dll with MSVC and DXR 1.1..."
& $cl $args

$runner_args = @(
    "/O2",
    "/std:c++17",
    "/EHsc",
    "/openmp",
    "/I$MSVC_INC",
    "/I$WIN_SDK_INC\um",
    "/I$WIN_SDK_INC\shared",
    "/I$WIN_SDK_INC\ucrt",
    "/I$WIN_SDK_INC\winrt",
    "/Isrc",
    "/Isrc\astg",
    "/Isrc\rtx",
    "src\rtx\rtx_runner.cpp",
    "/link",
    "/LIBPATH:$MSVC_LIB",
    "/LIBPATH:$WIN_SDK_LIB",
    "/LIBPATH:$WIN_SDK_UCRT",
    "/LIBPATH:bin",
    "/OUT:bin\astg_rtx_runner.exe",
    "astg_rtx.lib",
    "d3d12.lib",
    "dxgi.lib",
    "d3dcompiler.lib"
)

Write-Host "Compiling bin\astg_rtx_runner.exe..."
& $cl $runner_args

if ((Test-Path "bin\astg_rtx.dll") -and (Test-Path "bin\astg_rtx_runner.exe")) {
    Write-Host "✅ Successfully built bin\astg_rtx.dll and bin\astg_rtx_runner.exe!" -ForegroundColor Green
} else {
    Write-Error "❌ Build failed!"
}
