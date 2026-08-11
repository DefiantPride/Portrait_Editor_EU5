@echo off
REM ---------------------------------------------------------------------------
REM  ACE Portrait Bridge - build
REM  Run from an "x64 Native Tools Command Prompt for VS".
REM  Produces build\ace_portrait_bridge.dll and build\ace_bridge_loader.exe
REM ---------------------------------------------------------------------------

if not exist build mkdir build
cd build

echo Building the bridge DLL...
cl /nologo /LD /EHsc /O2 /std:c++17 /W3 ^
   ..\src\dllmain.cpp ^
   /link /OUT:ace_portrait_bridge.dll user32.lib
if errorlevel 1 goto fail

echo.
echo Building the loader...
cl /nologo /EHsc /O2 /std:c++17 /W3 ^
   ..\src\loader.cpp ^
   /link /OUT:ace_bridge_loader.exe
if errorlevel 1 goto fail

echo.
echo Built:
echo   build\ace_portrait_bridge.dll
echo   build\ace_bridge_loader.exe
cd ..
exit /b 0

:fail
echo.
echo BUILD FAILED
cd ..
exit /b 1
