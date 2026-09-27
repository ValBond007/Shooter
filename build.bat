@echo off
REM Builds shooter.exe and aimbot.exe with Visual Studio 2022 (needs CMake + Git).
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 || goto :error
cmake --build build --config Release || goto :error
echo.
echo Done. Executables are in build\bin\Release\
goto :eof
:error
echo Build failed.
exit /b 1
