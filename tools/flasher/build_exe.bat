@echo off
rem Сборка HorizonFlasher.exe (один файл) на домашнем ПК с интернетом.
chcp 65001 >nul
cd /d "%~dp0"
echo [1/3] Установка зависимостей...
python -m pip install --upgrade -r requirements.txt || (echo PIP FAILED & pause & exit /b 1)
echo [2/3] PyInstaller...
rem --collect-data esptool обязателен: в .exe должны попасть stub-загрузчики
rem (esptool/targets/stub_flasher/*.json), без них запись невозможна.
python -m PyInstaller --onefile --console --name HorizonFlasher --collect-data esptool --clean flasher.py || (echo BUILD FAILED & pause & exit /b 1)
echo [3/3] Готово: dist\HorizonFlasher.exe
dir dist\HorizonFlasher.exe
pause
