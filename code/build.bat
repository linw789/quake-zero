@echo off

rem -MTd : instead of using dynamic library, build static library into our executable, d for debug
rem -nologo : remove MSVC compiler about infomation 
rem -W4 : enable the layer 4 warning
rem -wd4201 : disable warning nameless struct/union
rem -wd4100 : disable warning unreferenced formal parameter
rem -wd4189 : disable warning local variable is initialized but not referenced
rem -wd4505 : disable waring about unreferenced local functions
rem -WX : treats all compiler warnings as errors
rem -Gm- : disable minimal rebuild. We use unity build, entire code base will be rebuild for every change.
rem -GR- : disable c++ run-time type information
rem -Od : disable all compiler optimization
rem -Oi : replace function calls with intrinsic whenever possible
rem -Z7 : pobably works better with multi-core processer than -Zi
rem -EHa- : return off exception handling

set CommonCompilerFlags=-MTd -nologo -Gm- -GR- -EHa- -Od -Oi -WX -W4 -wd4100 -wd4201 -wd4189 -wd4505 ^
-DQUAKEREMAKE_INTERNAL=1 -DQUAKEREMAKE_SLOW=1 -DQUAKEREMAKE_WIN32=1 -FC -Z7

rem -opt:ref : something about including minimal libs. Handmade Hero Day 016(45:08)
set CommonLinkerFlags= -incremental:no -opt:ref user32.lib gdi32.lib winmm.lib

set DateTime=%date:~4,2%%date:~7,2%%date:~12,2%%time:~0,2%%time:~3,2%%time:~6,2%

IF NOT EXIST ..\build mkdir ..\build
pushd ..\build

rem -subsystem:windows : or -subsystem:console
rem 32-bit build
rem cl %CommonCompilerFlags% ..\code\sys_win32.cpp /link -subsystem:windows,5.1 %CommonLinkerFlags%

if exist *.pdb del *.pdb

rem DLL will be loaded immediately after it's been generated at which time pdb 
rem file has probably not been generated. We don't load new dll until lock.tmp 
rem has been deleted
echo WAITING FOR PDB > lock.tmp

rem 64-bit build

rem compile to DLL
cl %CommonCompilerFlags% ..\code\q_game.cpp -Fmq_game.map -LD /link -incremental:no -opt:ref -PDB:q_%Random%.pdb -EXPORT:GameInit -EXPORT:GameUpdateAndRender
del lock.tmp

cl %CommonCompilerFlags% ..\code\sys_win32.cpp -Fmsys_win32.map /link %CommonLinkerFlags%

popd
