@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cl /nologo /c /std:c++17 /O2 /MT /EHsc /utf-8 /DUNICODE /D_UNICODE /W4 /I "src" /Fo:"build\keygen_crypto.obj" "src\crypto.cpp"
if errorlevel 1 exit /b 1
cl /nologo /c /std:c++17 /O2 /MT /EHsc /utf-8 /DUNICODE /D_UNICODE /W4 /I "src" /Fo:"build\keygen_main.obj" "tools\keygen.cpp"
if errorlevel 1 exit /b 1
link /nologo /SUBSYSTEM:CONSOLE /OUT:"build\keygen.exe" build\keygen_crypto.obj build\keygen_main.obj bcrypt.lib
if errorlevel 1 exit /b 1
exit /b 0
