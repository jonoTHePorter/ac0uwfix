@echo off
setlocal
for %%d in ("%ProgramFiles%\Microsoft Visual Studio\2022\Community" "%ProgramFiles%\Microsoft Visual Studio\2022\Professional" "%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise" "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools") do if exist "%%~d\VC\Auxiliary\Build\vcvars64.bat" call "%%~d\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul
where cl >nul 2>nul || (echo Visual Studio 2022 C++ tools not found & exit /b 1)
cd /d "%~dp0"
if not exist build mkdir build
cl /nologo /O2 /MT /LD /EHsc /std:c++17 /W3 /Fobuild\ src\dllmain.cpp user32.lib /Febuild\version.dll
