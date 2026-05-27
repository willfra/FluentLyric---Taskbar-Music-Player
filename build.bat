@echo off
setlocal

:: Find MSBuild path using vswhere
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) do (
  set "MSBUILD_PATH=%%i"
)

if not defined MSBUILD_PATH (
  echo Erro: MSBuild não encontrado. Certifique-se de ter o Visual Studio 2022 instalado.
  exit /b 1
)

echo Usando MSBuild em: %MSBUILD_PATH%
echo Compilando projeto em modo Release...

"%MSBUILD_PATH%" "TaskbarMediaPlayer.sln" /p:Configuration=Release /p:Platform=x64

if %ERRORLEVEL% equ 0 (
  echo.
  echo === COMPILACAO BEM SUCEDIDA ===
  echo O executavel encontra-se na pasta x64\Release\
) else (
  echo.
  echo === FALHA NA COMPILACAO ===
)


