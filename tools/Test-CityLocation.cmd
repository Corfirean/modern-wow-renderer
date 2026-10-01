@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" >nul
cd /d "%~dp0.."
if not exist build\city-location-test mkdir build\city-location-test
cl /nologo /EHsc /std:c++20 /O2 /DWIN32_LEAN_AND_MEAN tools\CityLocationRegression.cpp src\Game\WoWClientContext.cpp /Febuild\city-location-test\CityLocationRegression.exe /Fobuild\city-location-test\ /link user32.lib d3d9.lib bcrypt.lib
if errorlevel 1 exit /b 1
copy /y build\Release\d3d9.dll build\city-location-test\d3d9.dll >nul
if not exist build\city-location-test\data mkdir build\city-location-test\data
copy /y C:\games\Ascension\data\areas.txt build\city-location-test\data\areas.txt >nul
cd build\city-location-test
if exist ModernWoWRenderer.log del ModernWoWRenderer.log
if exist VolumeEffects.log del VolumeEffects.log
CityLocationRegression.exe
