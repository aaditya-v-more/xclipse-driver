@echo off
rem Puts this project's portable build tools on PATH for the current cmd window only.
rem Usage: call tools\env.cmd
set "XD_TOOLS=%~dp0"
set "XD_TOOLS=%XD_TOOLS:~0,-1%"
set "ANDROID_NDK_HOME=%XD_TOOLS%\android-ndk-r29"
set "PATH=%XD_TOOLS%\python;%XD_TOOLS%\python\Scripts;%XD_TOOLS%\glslang\bin;%USERPROFILE%\platform-tools;%PATH%"
