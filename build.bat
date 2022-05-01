
@echo off
IF NOT EXIST dist mkdir dist

setlocal
set COMPILEROPTION=/Zi /MD /Fo"./dist/" /Fd"./dist/"
set INCLUDES=/I "W:\CppProject\CppLib\RayLib\include"

set LIBPATH=/LIBPATH:"W:\CppProject\CppLib\RayLib\lib"
set LINK=opengl32.lib raylib.lib user32.lib winmm.lib shell32.lib gdi32.lib
set LINKOPTION=/INCREMENTAL:NO /pdb:"./dist/" /out:"./dist/main.exe" /DEBUG:FULL

set FILE=./src/main.cpp

rem "[Build]: Building executables."
cl.exe %COMPILEROPTION% %INCLUDES% %FILE% /link %LINKOPTION% %LIBPATH% %LINKS%
endlocal
