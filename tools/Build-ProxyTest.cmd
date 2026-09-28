@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" >nul
cd /d "%~dp0.."
cl /nologo /EHsc /std:c++20 /O2 ProxyVolumeSmokeTest.cpp /Febuild\Release\ProxyVolumeSmokeTest.exe /Fobuild\atmosphere-test-obj\ /link user32.lib d3d9.lib
