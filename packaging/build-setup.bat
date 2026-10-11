@echo off
setlocal EnableExtensions

for %%I in ("%~dp0..") do set "ROOT=%%~fI"

set "QT_DIR=C:\Qt\6.12.0\msvc2022_64"
set "BUILD=%ROOT%\build\release"
set "STAGE=%ROOT%\dist\LevelCAD-release-%RANDOM%-%RANDOM%"

where cl >nul 2>nul
if errorlevel 1 (
    echo [ERRO] Execute primeiro o vcvars64.bat do Visual Studio.
    exit /b 1
)

if not exist "%QT_DIR%\bin\windeployqt.exe" (
    echo [ERRO] Qt nao encontrado em "%QT_DIR%".
    exit /b 1
)

if not exist "%ROOT%\libraries\official\mesh-cache" (
    echo [ERRO] Cache dos modelos 3D nao encontrado.
    exit /b 1
)

cd /d "%ROOT%" || exit /b 1

echo [1/5] Configurando Release...
cmake -S . -B "%BUILD%" -G Ninja ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DLEVELCAD_BUILD_DESKTOP=ON ^
    -DLEVELCAD_BUILD_TESTS=ON ^
    -DCMAKE_PREFIX_PATH="%QT_DIR%"
if errorlevel 1 exit /b 1

echo [2/5] Compilando...
cmake --build "%BUILD%" --parallel 8
if errorlevel 1 exit /b 1

set "PATH=%QT_DIR%\bin;%PATH%"

echo [3/5] Executando testes...
ctest --test-dir "%BUILD%" --output-on-failure --timeout 180
if errorlevel 1 exit /b 1

echo [4/5] Preparando executaveis e dependencias...
if exist "%STAGE%" (
    echo [ERRO] A pasta de distribuicao ja existe. Execute novamente.
    exit /b 1
)

mkdir "%STAGE%" || exit /b 1

copy /y "%BUILD%\application\desktop\LevelCAD.exe" "%STAGE%\" >nul
if errorlevel 1 exit /b 1

copy /y "%BUILD%\application\desktop\LevelCADServer.exe" "%STAGE%\" >nul
if errorlevel 1 exit /b 1

"%QT_DIR%\bin\windeployqt.exe" --release --no-compiler-runtime ^
    --dir "%STAGE%" "%STAGE%\LevelCAD.exe"
if errorlevel 1 exit /b 1

"%QT_DIR%\bin\windeployqt.exe" --release --no-compiler-runtime ^
    --dir "%STAGE%" "%STAGE%\LevelCADServer.exe"
if errorlevel 1 exit /b 1

rem Reutilize o redistribuivel local quando disponivel.
if exist "%ROOT%\dist\LevelCAD\vc_redist.x64.exe" (
    copy /y "%ROOT%\dist\LevelCAD\vc_redist.x64.exe" "%STAGE%\" >nul
)

if not exist "%STAGE%\vc_redist.x64.exe" (
    curl.exe --fail --location --retry 2 ^
        "https://aka.ms/vc14/vc_redist.x64.exe" ^
        --output "%STAGE%\vc_redist.x64.exe"
    if errorlevel 1 exit /b 1
)

echo [5/5] Gerando instalador...
set "ISCC=%ProgramFiles%\Inno Setup 7\ISCC.exe"
if not exist "%ISCC%" set "ISCC=%ProgramFiles%\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" set "ISCC=%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" set "ISCC=%LOCALAPPDATA%\Programs\Inno Setup 7\ISCC.exe"
if not exist "%ISCC%" set "ISCC=%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe"

if not exist "%ISCC%" (
    echo [ERRO] Instale o Inno Setup 6 ou 7.
    echo Distribuicao preparada em "%STAGE%".
    exit /b 1
)

"%ISCC%" "/DStageDir=%STAGE%" "%ROOT%\packaging\LevelCAD.iss"
if errorlevel 1 exit /b 1

echo.
echo [OK] Instalador gerado em "%ROOT%\dist\setup".
echo Distribua o EXE e todos os arquivos BIN dessa pasta.
endlocal
