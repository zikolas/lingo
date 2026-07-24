@echo off
rem Build LFLASH.EXE with Open Watcom 1.9 (16-bit, real mode, small model).
rem Expects the Watcom environment to be set, e.g.:
rem   set WATCOM=C:\WATCOM
rem   set INCLUDE=C:\WATCOM\H
rem   set PATH=C:\WATCOM\BINW;%PATH%
wcc -ms LFLASH.C -fo=LFLASH.obj
if errorlevel 1 goto end
wlink system dos name LFLASH.exe file LFLASH.obj
:end
