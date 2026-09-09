@echo off
setlocal

set PORT=COM7
set BAUD=460800

if "%1" neq "" set PORT=%1

echo ============================================
echo   ESP32-S3 VR Controller Flash Tool
echo ============================================
echo.
echo   Target: %PORT%
echo   Flash:  16MB / DIO / 80MHz
echo.
echo   Press BOOT button on board, then press any key...
pause >nul

echo.
echo [1/3] Erasing flash...
uvx --from esptool esptool --chip esp32s3 --port %PORT% --baud %BAUD% erase_flash
if %ERRORLEVEL% neq 0 (
    echo.
    echo ERROR: Erase failed. Check connection and BOOT button.
    pause
    exit /b 1
)

echo.
echo [2/3] Writing bootloader...
uvx --from esptool esptool --chip esp32s3 --port %PORT% --baud %BAUD% ^
  write_flash --flash_mode dio --flash_size 16MB --flash_freq 80m ^
  0x0 bootloader\bootloader.bin
if %ERRORLEVEL% neq 0 (
    echo.
    echo ERROR: Bootloader write failed.
    pause
    exit /b 1
)

echo.
echo [3/3] Writing app + partition table...
uvx --from esptool esptool --chip esp32s3 --port %PORT% --baud %BAUD% ^
  write_flash --flash_mode dio --flash_size 16MB --flash_freq 80m ^
  0x8000 partition_table\partition-table.bin ^
  0x10000 esp32_s3_vr_controller.bin
if %ERRORLEVEL% neq 0 (
    echo.
    echo ERROR: App write failed.
    pause
    exit /b 1
)

echo.
echo ============================================
echo   Flash complete! Device will reboot now.
echo ============================================
pause
