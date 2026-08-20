@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\install.ps1" %*
set "exit_code=%ERRORLEVEL%"
echo.
if not "%exit_code%"=="0" echo Installation failed with exit code %exit_code%.
pause
exit /b %exit_code%
