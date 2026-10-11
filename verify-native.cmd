@echo off
set VSLANG=1033
call "C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1
cd /d "C:\Users\wende\Documents\Closed Source Projects\LevelCAD"
cmake -S . -B build/native -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/Qt/6.12.0/msvc2022_64
if errorlevel 1 exit /b 1
cmake --build build/native --parallel 4
if errorlevel 1 exit /b 1
ctest --test-dir build/native --output-on-failure --timeout 90