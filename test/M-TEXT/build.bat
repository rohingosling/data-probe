@echo off
rem --------------------------------------------------------------------------
rem  Program:     MicroText Test Build Script
rem  Version:     1.0
rem  Date:        1992
rem  Author:      Rohin Gosling
rem  Environment: DOS / DOSBox
rem
rem  BCC must be on the PATH.
rem
rem  Description:
rem
rem    Builds the MicroText test driver (TEST.EXE) from the library source
rem    and the local test driver:
rem
rem      MicroText  (..\..\src\M-TEXT\mtext.c)  C + inline asm, compiled
rem                                             with -2 (80286)
rem      Driver     (test.c)                    C test driver
rem
rem    Both compile with the large memory model (-ml) for far pointer and
rem    farmalloc support, then link into TEST.EXE.
rem
rem    Run this script from the test\M-TEXT\ directory inside an already-
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

echo MicroText test build started ...
echo Build started > build.log

rem --------------------------------------------------------------------------
rem  Compile
rem --------------------------------------------------------------------------

echo Compiling MTEXT.C ...
echo ---- MTEXT.C ---- >> build.log
bcc -c -ml -2 -I..\..\src\M-TEXT ..\..\src\M-TEXT\mtext.c >> build.log
if errorlevel 1 goto fail

echo Compiling TEST.C ...
echo ---- TEST.C ---- >> build.log
bcc -c -ml -2 -I..\..\src\M-TEXT test.c >> build.log
if errorlevel 1 goto fail

rem --------------------------------------------------------------------------
rem  Link
rem --------------------------------------------------------------------------

echo Linking TEST.EXE ...
echo ---- LINK TEST.EXE ---- >> build.log
bcc -ml -etest.exe mtext.obj test.obj >> build.log
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
