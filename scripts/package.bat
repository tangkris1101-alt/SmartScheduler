@echo off
rem ============================================================
rem SmartScheduler 打包脚本（Windows）
rem 用法：scripts\package.bat [版本号]   例：scripts\package.bat 1.0.2
rem 产物：deploy\SmartScheduler-<版本>-win64-portable.zip
rem       若已安装 Inno Setup，还会生成 installer\Output\SmartScheduler-<版本>-setup.exe
rem ============================================================
setlocal enabledelayedexpansion
cd /d %~dp0\..

set VER=%~1
if "%VER%"=="" set VER=1.0.39

set QTDIR=C:\Qt\6.8.1\mingw_64
set PATH=C:\Qt\Tools\CMake_64\bin;C:\Qt\Tools\Ninja;C:\Qt\Tools\mingw1310_64\bin;%QTDIR%\bin;%PATH%

echo [1/4] Release 构建...
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/Qt/6.8.1/mingw_64 || exit /b 1
cmake --build build || exit /b 1
ctest --test-dir build --output-on-failure || exit /b 1

echo [2/4] windeployqt 收集运行时到 deploy\...
if exist deploy rmdir /s /q deploy
mkdir deploy
copy /y build\SmartScheduler.exe deploy\ >nul
windeployqt --release --compiler-runtime --qmldir src\ui deploy\SmartScheduler.exe || exit /b 1

echo [3/4] 打便携 zip 包...
set ZIP=SmartScheduler-%VER%-win64-portable.zip
if exist %ZIP% del /q %ZIP%
powershell -NoProfile -Command "Compress-Archive -Path deploy\* -DestinationPath '%ZIP%' -CompressionLevel Optimal" 2>nul
if not exist %ZIP% tar -a -cf %ZIP% -C deploy . || exit /b 1
echo 已生成 %ZIP%

echo [4/4] Inno Setup 安装包（可选）...
rem Inno Setup 按用户安装时可能未入 PATH，补上默认位置
if exist "%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe" set PATH=%LOCALAPPDATA%\Programs\Inno Setup 6;%PATH%
where iscc >nul 2>nul
if %errorlevel% equ 0 (
    iscc /DMyAppVersion=%VER% installer\SmartScheduler.iss && echo 安装包见 installer\Output\
) else (
    echo 未找到 iscc（Inno Setup）。跳过安装包编译。
    echo 安装方式：winget install JRSoftware.InnoSetup 后重新运行本脚本。
)
endlocal
