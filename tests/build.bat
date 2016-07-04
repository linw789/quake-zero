@echo off

rem -MTd : instead of using dynamic library, build static library into our executable, d for debug
rem -nologo : remove MSVC compiler about infomation 
rem -W4 : enable the layer 4 warning
rem -wd4201 : disable warning nameless struct/union
rem -wd4100 : disable warning unreferenced formal parameter
rem -wd4189 : disable warning local variable is initialized but not referenced
rem -wd4505 : disable warning about unreferenced local functions
rem -wd4127 : disable warning about conditional expression is constant
rem -WX : treats all compiler warnings as errors
rem -Gm- : disable minimal rebuild. We use unity build, entire code base will be rebuild for every change.
rem -GR- : diable c++ run-time type information
rem -Od : disable all compiler optimization
rem -Oi : replace function calls with intrinsic whenever possible
rem -Z7 : pobably works better with multi-core processer than -Zi
rem -EHa- : return off exception handling

set CommonCompilerFlags=-MTd -nologo -Gm- -GR- -EHa- -Od -Oi -WX -W4 ^
-wd4127 -wd4100 -wd4201 -wd4189 -wd4505 ^
-DQUAKEREMAKE_INTERNAL=1 -DQUAKEREMAKE_SLOW=1 -DQUAKEREMAKE_WIN32=0 -FC -Z7

rem -opt:ref : something about including minimal libs. Handmade Hero Day 016(45:08)
set CommonLinkerFlags= -incremental:no -opt:ref 

cl %CommonCompilerFlags% tests.cpp -Fmtests.map /link %CommonLinkerFlags%
