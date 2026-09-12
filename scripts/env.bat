@echo off
rem 将本机 Qt 6.8 工具链注入 PATH（在 cmd 中执行: scripts\env.bat）
set "PATH=C:\Qt\Tools\CMake_64\bin;C:\Qt\Tools\Ninja;C:\Qt\Tools\mingw1310_64\bin;C:\Qt\6.8.1\mingw_64\bin;%PATH%"
echo [env] Qt 6.8.1 + MinGW 13.1 + CMake + Ninja ready.
