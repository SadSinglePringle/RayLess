@echo off
setlocal enabledelayedexpansion

echo ===================================================
echo 🛠️ COMPILING ASTG NVIDIA HARDWARE RT CORE DLL (DXR 1.1)
echo ===================================================

set MSVC_DIR=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64
set WIN_SDK_INC=C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0
set WIN_SDK_LIB=C:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0\um\x64
set WIN_SDK_UCRT=C:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0\ucrt\x64
set MSVC_INC=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207\include
set MSVC_LIB=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207\lib\x64

if not exist bin mkdir bin

"%MSVC_DIR%\cl.exe" /LD /O2 /EHsc /openmp ^
  /I"%MSVC_INC%" ^
  /I"%WIN_SDK_INC%\um" ^
  /I"%WIN_SDK_INC%\shared" ^
  /I"%WIN_SDK_INC%\ucrt" ^
  /I"src\rtx" ^
  "src\rtx\rtx_raytracer.cpp" ^
  /link ^
  /LIBPATH:"%MSVC_LIB%" ^
  /LIBPATH:"%WIN_SDK_LIB%" ^
  /LIBPATH:"%WIN_SDK_UCRT%" ^
  /OUT:"bin\astg_rtx.dll" ^
  d3d12.lib dxgi.lib d3dcompiler.lib

if %ERRORLEVEL% EQU 0 (
    echo.
    echo ✅ SUCCESSFULLY BUILT bin\astg_rtx.dll!
    exit /b 0
) else (
    echo.
    echo ❌ FAILED TO BUILD bin\astg_rtx.dll (Error %ERRORLEVEL%)
    exit /b %ERRORLEVEL%
)
