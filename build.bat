@echo off
rem Builds xinput1_3.dll. Needs MinGW-w64 gcc on PATH (winget install BrechtSanders.WinLibs.POSIX.UCRT).
cd /d "%~dp0"
gcc -shared -O2 -s -Wall -Wno-cast-function-type -o xinput1_3.dll src\*.c xinput1_3.def -static-libgcc -lbcrypt || exit /b 1
echo Built xinput1_3.dll
