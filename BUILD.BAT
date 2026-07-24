@echo off
rem Build FLINGO.EXE with Open Watcom 1.9 (16-bit, real mode, small model).
set WATCOM=C:\WATCOM
set INCLUDE=C:\WATCOM\H
set PATH=C:\WATCOM\BINW;C:\DOS
wcc -ms -ox FLINGO.C -fo=FLINGO.obj
if errorlevel 1 goto end
wlink system dos name FLINGO.exe file FLINGO.obj
:end
