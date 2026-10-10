@echo off
setlocal EnableExtensions

rem Execute a partir do "x64 Native Tools Command Prompt" do Visual Studio 2026.
rem Copie a pasta packaging/ para a raiz do repositorio LevelCAD.

for %%I in ("%~dp0..") do set "ROOT=%%~fI"
set "QT_DIR=C:\Qt\6.12.0\msvc2022_64"
set "STAGE=%ROOT%\dist\LevelCAD"
set "BUILD=%ROOT%\build\release"

if not exist "%QT_DIR%\bin\windeployqt.exe" (
  echo [ERRO] Qt MSVC nao encontrado em: %QT_DIR%
  exit /b 1
)
where cl >nul 2>nul
if errorlevel 1 (
  echo [ERRO] Abra o x64 Native Tools Command Prompt do Visual Studio antes de executar.
  exit /b 1
)

cd /d "%ROOT%" || exit /b 1

echo [1/6] Configurando Release...
cmake -S . -B "%BUILD%" -G Ninja -DCMAKE_BUILD_TYPE=Release -DLEVELCAD_BUILD_DESKTOP=ON -DCMAKE_PREFIX_PATH="%QT_DIR%"
if errorlevel 1 exit /b 1

echo [2/6] Compilando...
cmake --build "%BUILD%" --parallel 8
if errorlevel 1 exit /b 1

set "BIN=%BUILD%\application\desktop"
if not exist "%BIN%\LevelCAD.exe" (
  echo [ERRO] Nao encontrei "%BIN%\LevelCAD.exe".
  echo Verifique se a estrutura de saida do CMake mudou.
  exit /b 1
)

echo [3/6] Preparando pasta de distribuicao...
if exist "%STAGE%" rmdir /s /q "%STAGE%"
mkdir "%STAGE%" || exit /b 1
copy /y "%BIN%\LevelCAD.exe" "%STAGE%\" >nul || exit /b 1
if exist "%BIN%\LevelCADServer.exe" copy /y "%BIN%\LevelCADServer.exe" "%STAGE%\" >nul
if exist "%ROOT%\LICENSE" copy /y "%ROOT%\LICENSE" "%STAGE%\" >nul
if exist "%ROOT%\README.md" copy /y "%ROOT%\README.md" "%STAGE%\" >nul
rem Se o app carrega recursos em tempo de execucao, eles tambem precisam ser distribuidos.
if exist "%ROOT%\resources" xcopy "%ROOT%\resources" "%STAGE%\resources\" /E /I /Q /Y >nul
if exist "%ROOT%\libraries" xcopy "%ROOT%\libraries" "%STAGE%\libraries\" /E /I /Q /Y >nul

echo [4/6] Empacotando bibliotecas Qt Release...
set "PATH=%QT_DIR%\bin;%PATH%"
"%QT_DIR%\bin\windeployqt.exe" --release --no-compiler-runtime --dir "%STAGE%" "%STAGE%\LevelCAD.exe"
if errorlevel 1 exit /b 1
if exist "%STAGE%\LevelCADServer.exe" (
  "%QT_DIR%\bin\windeployqt.exe" --release --no-compiler-runtime --dir "%STAGE%" "%STAGE%\LevelCADServer.exe"
  if errorlevel 1 exit /b 1
)

echo [5/6] Baixando redistribuivel oficial do Visual C++...
curl.exe --fail --location --retry 2 "https://aka.ms/vc14/vc_redist.x64.exe" --output "%STAGE%\vc_redist.x64.exe"
if errorlevel 1 (
  echo [ERRO] Nao foi possivel baixar VC++ Redistributable.
  echo Baixe vc_redist.x64.exe pelo site da Microsoft e coloque em "%STAGE%".
  exit /b 1
)

echo [6/6] Compilando instalador Inno Setup...
set "ISCC=%ProgramFiles%\Inno Setup 7\ISCC.exe"
if not exist "%ISCC%" set "ISCC=%ProgramFiles%\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" set "ISCC=%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" (
  echo [AVISO] ISCC.exe nao encontrado. Abra packaging\LevelCAD.iss no Inno Setup e use Build - Compile.
  echo Os arquivos estao prontos em "%STAGE%".
  exit /b 0
)
"%ISCC%" "%ROOT%\packaging\LevelCAD.iss"
if errorlevel 1 exit /b 1

echo.
echo [OK] Setup criado em "%ROOT%\dist\setup".
endlocal
