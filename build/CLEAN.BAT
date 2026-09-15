@echo off
rem --------------------------------------------------------------------------
rem  Program:     Data Probe Clean Script
rem  Version:     1.5
rem  Date:        1992
rem  Author:      Rohin Gosling
rem  Environment: DOS / DOSBox
rem
rem  Description:
rem
rem    Removes all Data Probe build artifacts (the MicroText, MicroApp, INI,
rem    and DataProbe object files, DPROBE.EXE, and BUILD.LOG) from the build\
rem    directory.
rem
rem    Run this script from the build\ directory inside a DOSBox session.
rem
rem  Usage:
rem
rem    clean
rem
rem --------------------------------------------------------------------------

cls

echo Cleaning build artifacts ...

if exist mtext.obj   del mtext.obj
if exist mapp.obj    del mapp.obj
if exist hexview.obj del hexview.obj
if exist filebuf.obj del filebuf.obj
if exist ini.obj     del ini.obj
if exist dprobe.obj  del dprobe.obj
if exist dprobe.exe  del dprobe.exe
if exist build.log   del build.log

echo Done.
