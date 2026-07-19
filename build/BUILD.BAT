@echo off
rem --------------------------------------------------------------------------
rem  Program:     Data Probe Build Script
rem  Version:     1.5
rem  Date:        1992
rem  Author:      Rohin Gosling
rem  Environment: DOS / DOSBox
rem
rem  BCC must be on the PATH.
rem
rem  Description:
rem
rem    Builds Data Probe (DPROBE.EXE), a full-screen DOS HEX editor, from its
rem    modules:
rem
rem      MicroText  (mtext.c)    C + inline asm, compiled with -2 (80286)
rem      MicroApp   (mapp.cpp)   C++ TUI class framework
rem      INI        (ini.cpp)    C++ INI-file settings library
rem      DataProbe  (hexview.cpp, filebuf.cpp, dprobe.cpp) C++ application
rem
rem    All modules compile with the large memory model (-ml) for far pointer
rem    and farmalloc support, then link into DPROBE.EXE.
rem
rem    Run this script from the build\ directory inside an already-configured
rem    DOSBox session. The .OBJ files, DPROBE.EXE, and BUILD.LOG all land
rem    in this directory.
rem
rem  Usage:
rem
rem    build          Build DPROBE.EXE
rem    build clean    Delete intermediate and output files
rem
rem --------------------------------------------------------------------------

cls

if "%1"=="clean" goto clean

echo Data Probe build started ...
echo Build started > build.log

rem --------------------------------------------------------------------------
rem  Compile
rem --------------------------------------------------------------------------

echo Compiling MTEXT.C ...
echo ---- MTEXT.C ---- >> build.log
bcc -c -ml -2 -I..\src\M-TEXT ..\src\M-TEXT\mtext.c >> build.log
if errorlevel 1 goto fail

echo Compiling MAPP.CPP ...
echo ---- MAPP.CPP ---- >> build.log
bcc -c -ml -I..\src\M-TEXT -I..\src\M-APP ..\src\M-APP\mapp.cpp >> build.log
if errorlevel 1 goto fail

echo Compiling HEXVIEW.CPP ...
echo ---- HEXVIEW.CPP ---- >> build.log
bcc -c -ml -I..\src\M-TEXT -I..\src\M-APP -I..\src\D-PROBE ..\src\D-PROBE\hexview.cpp >> build.log
if errorlevel 1 goto fail

echo Compiling FILEBUF.CPP ...
echo ---- FILEBUF.CPP ---- >> build.log
bcc -c -ml -I..\src\M-TEXT -I..\src\M-APP -I..\src\D-PROBE ..\src\D-PROBE\filebuf.cpp >> build.log
if errorlevel 1 goto fail

echo Compiling INI.CPP ...
echo ---- INI.CPP ---- >> build.log
bcc -c -ml -I..\src\INI ..\src\INI\ini.cpp >> build.log
if errorlevel 1 goto fail

echo Compiling DPROBE.CPP ...
echo ---- DPROBE.CPP ---- >> build.log
bcc -c -ml -I..\src\M-TEXT -I..\src\M-APP -I..\src\D-PROBE -I..\src\INI ..\src\D-PROBE\dprobe.cpp >> build.log
if errorlevel 1 goto fail

rem --------------------------------------------------------------------------
rem  Link
rem --------------------------------------------------------------------------

echo Linking DPROBE.EXE ...
echo ---- LINK DPROBE.EXE ---- >> build.log
bcc -ml -edprobe.exe mtext.obj mapp.obj hexview.obj filebuf.obj ini.obj dprobe.obj >> build.log
if errorlevel 1 goto fail

echo.
echo Build successful: DPROBE.EXE
echo Build successful >> build.log
goto end

rem --------------------------------------------------------------------------
rem  Clean
rem --------------------------------------------------------------------------

:clean
echo Cleaning ...
if exist mtext.obj   del mtext.obj
if exist mapp.obj    del mapp.obj
if exist hexview.obj del hexview.obj
if exist filebuf.obj del filebuf.obj
if exist ini.obj     del ini.obj
if exist dprobe.obj  del dprobe.obj
if exist dprobe.exe  del dprobe.exe
if exist build.log   del build.log
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
