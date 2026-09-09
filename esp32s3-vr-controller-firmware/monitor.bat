@echo off
setlocal

set PORT=COM7

if "%1" neq "" set PORT=%1

echo ============================================
echo   ESP32-S3 Serial Monitor
echo   Port: %PORT%  Baud: 115200
echo   Press Ctrl+C to exit
echo ============================================
echo.

uv run --with pyserial python -m serial.tools.miniterm %PORT% 115200
