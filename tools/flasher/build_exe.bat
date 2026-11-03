@echo off
rem Сборка HorizonFlasher (окно + консоль) в одиночные .exe на домашнем ПК.
chcp 65001 >nul
cd /d "%~dp0"
rem Выбор Питона: предпочитаем 3.13 (колёса под все зависимости есть),
rem иначе первый доступный через py, иначе bare python.
set "PYCMD="
py -3.13 --version >nul 2>&1 && set "PYCMD=py -3.13"
if not defined PYCMD ( py --version >nul 2>&1 && set "PYCMD=py" )
if not defined PYCMD set "PYCMD=python"
echo Python: %PYCMD%
echo [1/4] Установка зависимостей...
%PYCMD% -m pip install --upgrade -r requirements.txt || (echo PIP FAILED & pause & exit /b 1)
echo [2/4] PyInstaller: оконный HorizonFlasher (продукт для покупателя)...
rem --collect-data esptool обязателен: в .exe должны попасть stub-загрузчики
rem (esptool/targets/stub_flasher/*.json), без них запись невозможна.
%PYCMD% -m PyInstaller --onefile --windowed --name HorizonFlasher --collect-data esptool --collect-all littlefs --clean flasher_gui.py || (echo BUILD GUI FAILED & pause & exit /b 1)
echo [3/4] PyInstaller: консольный HorizonFlasher-CLI...
%PYCMD% -m PyInstaller --onefile --console --name HorizonFlasher-CLI --collect-data esptool --collect-all littlefs --clean flasher.py || (echo BUILD CLI FAILED & pause & exit /b 1)
echo [4/4] Готово:
dir dist\\HorizonFlasher.exe dist\\HorizonFlasher-CLI.exe
pause
