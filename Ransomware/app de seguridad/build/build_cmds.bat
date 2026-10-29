@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
rc /nologo /I "src" /fo "build\app.res" "build\app.rc"
if errorlevel 1 exit /b 1
rc /nologo /I "src" /fo "build\private.res" "build\private.rc"
if errorlevel 1 exit /b 1
cl /nologo /c /std:c++17 /O2 /MT /EHsc /utf-8 /DUNICODE /D_UNICODE /W4 /I "src" /Fo:"build\crypto.obj" "src\crypto.cpp"
if errorlevel 1 exit /b 1
cl /nologo /c /std:c++17 /O2 /MT /EHsc /utf-8 /DUNICODE /D_UNICODE /W4 /I "src" /Fo:"build\vault.obj" "src\vault.cpp"
if errorlevel 1 exit /b 1
cl /nologo /c /std:c++17 /O2 /MT /EHsc /utf-8 /DUNICODE /D_UNICODE /W4 /I "src" /Fo:"build\ui.obj" "src\ui.cpp"
if errorlevel 1 exit /b 1
cl /nologo /c /std:c++17 /O2 /MT /EHsc /utf-8 /DUNICODE /D_UNICODE /W4 /I "src" /Fo:"build\encrypt_main.obj" "src\encrypt_main.cpp"
if errorlevel 1 exit /b 1
cl /nologo /c /std:c++17 /O2 /MT /EHsc /utf-8 /DUNICODE /D_UNICODE /W4 /I "src" /Fo:"build\decrypt_main.obj" "src\decrypt_main.cpp"
if errorlevel 1 exit /b 1
cl /nologo /c /std:c++17 /O2 /MT /EHsc /utf-8 /DUNICODE /D_UNICODE /W4 /I "src" /Fo:"build\keygen_crypto.obj" "src\crypto.cpp"
if errorlevel 1 exit /b 1
cl /nologo /c /std:c++17 /O2 /MT /EHsc /utf-8 /DUNICODE /D_UNICODE /W4 /I "src" /Fo:"build\keygen_main.obj" "tools\keygen.cpp"
if errorlevel 1 exit /b 1
cl /nologo /c /std:c++17 /O2 /MT /EHsc /utf-8 /DUNICODE /D_UNICODE /W4 /I "src" /Fo:"build\test_crypto.obj" "src\crypto.cpp"
if errorlevel 1 exit /b 1
cl /nologo /c /std:c++17 /O2 /MT /EHsc /utf-8 /DUNICODE /D_UNICODE /W4 /I "src" /Fo:"build\test_vault.obj" "src\vault.cpp"
if errorlevel 1 exit /b 1
cl /nologo /c /std:c++17 /O2 /MT /EHsc /utf-8 /DUNICODE /D_UNICODE /W4 /I "src" /Fo:"build\test_main.obj" "tools\test_vault.cpp"
if errorlevel 1 exit /b 1
link /nologo /SUBSYSTEM:WINDOWS /MANIFEST:NO /OUT:"dist\Cifrar.exe" build\app.res build\crypto.obj build\vault.obj build\ui.obj build\encrypt_main.obj bcrypt.lib comctl32.lib gdiplus.lib shlwapi.lib shell32.lib ole32.lib user32.lib gdi32.lib
if errorlevel 1 exit /b 1
link /nologo /SUBSYSTEM:WINDOWS /MANIFEST:NO /OUT:"dist\Descifrar.exe" build\app.res build\private.res build\crypto.obj build\vault.obj build\ui.obj build\decrypt_main.obj bcrypt.lib comctl32.lib gdiplus.lib shlwapi.lib shell32.lib ole32.lib user32.lib gdi32.lib
if errorlevel 1 exit /b 1
link /nologo /SUBSYSTEM:CONSOLE /OUT:"build\keygen.exe" build\keygen_crypto.obj build\keygen_main.obj bcrypt.lib
if errorlevel 1 exit /b 1
link /nologo /SUBSYSTEM:CONSOLE /OUT:"build\test_vault.exe" build\private.res build\test_crypto.obj build\test_vault.obj build\test_main.obj bcrypt.lib comctl32.lib gdiplus.lib shlwapi.lib shell32.lib ole32.lib user32.lib gdi32.lib
if errorlevel 1 exit /b 1
exit /b 0
