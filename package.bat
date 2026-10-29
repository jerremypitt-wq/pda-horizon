@echo off
rem Шаг 2 из 2: ZIP-пакет прошивки. Двойной клик или: package.bat [--out ИМЯ] [--flash-mode dio]
chcp 65001 >nul
cd /d "%~dp0"
set "PYCMD="
py -3.13 --version >nul 2>&1 && set "PYCMD=py -3.13"
if not defined PYCMD ( py --version >nul 2>&1 && set "PYCMD=py" )
if not defined PYCMD (
  echo Не найден Python через py-launcher ^(нужен 3.10-3.14, лучше 3.13^).
  echo Скачай "Windows installer (64-bit)" с https://www.python.org/downloads/ и повтори.
  pause & exit /b 1
)
%PYCMD% tools\build\package.py %* || (echo: & echo УПАКОВКА НЕ УДАЛАСЬ & pause & exit /b 1)
echo: & echo ШАГ 2 ГОТОВ.
pause
