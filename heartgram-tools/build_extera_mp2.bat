@echo off
rem Relinks AyuGram+exteraGram without a PDB (the PDB write keeps failing with LNK1201). _LINK_ is applied after the project's own options.
set _LINK_=/DEBUG:NONE
rem HG_FAST=1: the link is 4x faster and needs less memory, but the exe is 53 MB bigger.
if "%HG_FAST%"=="1" set _LINK_=/DEBUG:NONE /OPT:REF,NOICF
rem _CL_ is appended after the project options, so this cap wins over the bare /MP the generated project passes.
set _CL_=/MP3
call "D:\PythonProjects\AyuGramDesktop\_toolchain\VS2022BuildTools\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44
if errorlevel 1 exit /b 1
cd /d D:\PythonProjects\AyuGramDesktop\source_extera
cmake --build out --config Release --target Telegram -- /p:CL_MPCount=3 /m:1
exit /b %errorlevel%
