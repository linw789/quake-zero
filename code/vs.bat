@echo off

IF EXIST ..\build\sys_win32.sln (
devenv ..\build\sys_win32.sln
) ELSE (
devenv ..\build\sys_win32.exe
)
