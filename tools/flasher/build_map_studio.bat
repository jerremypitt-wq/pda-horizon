@echo off
rem HorizonMapStudio.exe — отдельное окно; HorizonFlasher.exe не собирает и не меняет.
chcp 65001 >nul
cd /d "%~dp0"
set "PYCMD="
py -3.13 --version >nul 2>&1 && set "PYCMD=py -3.13"
if not defined PYCMD ( py --version >nul 2>&1 && set "PYCMD=py" )
if not defined PYCMD set "PYCMD=python"
%PYCMD% -m pip install --upgrade -r requirements.txt || exit /b 1
%PYCMD% -m PyInstaller --onefile --windowed --name HorizonMapStudio --collect-data esptool --collect-all littlefs --collect-all PIL --paths ..\build --clean map_studio.py || exit /b 1
dir dist\HorizonMapStudio.exe
if /i not "%~1"=="--ci" pause
