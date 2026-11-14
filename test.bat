@echo off
rem Builds and runs the self-test. Prints "ok" when every check passes.
cd /d "%~dp0"
gcc -DTEST -O2 -Wall -Wno-cast-function-type -o test.exe tests/test.c -lbcrypt || exit /b 1
.\test.exe
