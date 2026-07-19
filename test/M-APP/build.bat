@echo off
rem --------------------------------------------------------------------------
rem  Program:     MicroEdit (MicroApp Test Editor) Build Script
rem  Version:     1.0
rem  Date:        1992
rem  Author:      Rohin Gosling
rem  Environment: DOS / DOSBox
rem
rem  BCC must be on the PATH.
rem
rem  Description:
rem
rem    Builds MicroEdit (TEST.EXE), the minimal text editor that validates
rem    the MicroApp framework, from the library sources and the local
rem    driver:
rem
rem      MicroText  (..\..\src\M-TEXT\mtext.c)   C + inline asm, -2 (80286)
rem      MicroApp   (..\..\src\M-APP\mapp.cpp)   C++ TUI class framework
rem      MicroEdit  (test.cpp)                   C++ test editor
rem
rem    All compile with the large memory model (-ml) for far pointer and
rem    farmalloc support, then link into TEST.EXE.
rem
rem    Run this script from the test\M-APP\ directory inside an already-
rem    configured DOSBox session. The .OBJ files, TEST.EXE, and BUILD.LOG
rem    all land in this directory.
rem
rem  Usage:
rem
rem    build          Build TEST.EXE
rem    build clean    Delete intermediate and output files
rem
rem --------------------------------------------------------------------------

cls

if "%1"=="clean" goto clean

echo MicroEdit build started ...
echo Build started > build.log

rem --------------------------------------------------------------------------
rem  Compile
rem --------------------------------------------------------------------------

echo Compiling MTEXT.C ...
echo ---- MTEXT.C ---- >> build.log
bcc -c -ml -2 -I..\..\src\M-TEXT ..\..\src\M-TEXT\mtext.c >> build.log
if errorlevel 1 goto fail

echo Compiling MAPP.CPP ...
echo ---- MAPP.CPP ---- >> build.log
bcc -c -ml -I..\..\src\M-TEXT -I..\..\src\M-APP ..\..\src\M-APP\mapp.cpp >> build.log
if errorlevel 1 goto fail

echo Compiling TEST.CPP ...
echo ---- TEST.CPP ---- >> build.log
bcc -c -ml -I..\..\src\M-TEXT -I..\..\src\M-APP test.cpp >> build.log
if errorlevel 1 goto fail

rem --------------------------------------------------------------------------
rem  Link
rem --------------------------------------------------------------------------

echo Linking TEST.EXE ...
echo ---- LINK TEST.EXE ---- >> build.log
bcc -ml -etest.exe mtext.obj mapp.obj test.obj >> build.log
if errorlevel 1 goto fail

echo.
echo Build successful: TEST.EXE
echo Build successful >> build.log
goto end

rem --------------------------------------------------------------------------
rem  Clean
rem --------------------------------------------------------------------------

:clean
echo Cleaning ...
if exist mtext.obj del mtext.obj
if exist mapp.obj  del mapp.obj
if exist test.obj  del test.obj
if exist test.exe  del test.exe
if exist build.log del build.log
echo Done.
goto end

rem --------------------------------------------------------------------------
rem  Error
rem --------------------------------------------------------------------------

:fail
echo.
echo Build failed! See BUILD.LOG for details.
echo Build failed >> build.log

:end
