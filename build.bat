@echo off
rem One-step build for Windows (needs Visual Studio 2022 with C++ and CMake)
cmake -S . -B build
if errorlevel 1 exit /b 1
cmake --build build --config Release
if errorlevel 1 exit /b 1
echo.
echo Built. Start the game with:  build\Release\living_marble.exe
