@echo off
chcp 65001 >nul
setlocal

:: ═══════════════════════════════════════════════════════════════
:: BeehiveScale — бэкап проекта
:: Создаёт ZIP-архив на рабочем столе с датой и временем
:: Запуск: дважды кликнуть по backup.bat
:: ═══════════════════════════════════════════════════════════════

:: Папка для бэкапов (Рабочий стол\BeehiveScale_backups)
set "BACKUP_DIR=%USERPROFILE%\Desktop\BeehiveScale_backups"
if not exist "%BACKUP_DIR%" mkdir "%BACKUP_DIR%"

:: Имя архива: BeehiveScale_YYYYMMDD_HHMM.zip
for /f "tokens=2 delims==" %%I in ('wmic os get localdatetime /format:list 2^>nul') do set DT=%%I
set "STAMP=%DT:~0,8%_%DT:~8,4%"
set "ARCHIVE=%BACKUP_DIR%\BeehiveScale_%STAMP%.zip"

:: Папка проекта (где лежит этот .bat)
set "PROJECT=%~dp0"

echo.
echo  ╔═══════════════════════════════════════╗
echo  ║   BeehiveScale — Бэкап проекта        ║
echo  ╚═══════════════════════════════════════╝
echo.
echo  Проект:  %PROJECT%
echo  Архив:   %ARCHIVE%
echo.

:: Создаём ZIP через PowerShell (встроен в Windows 10+)
powershell -NoProfile -Command ^
  "Compress-Archive -Path '%PROJECT%BeehiveScale', '%PROJECT%docs', '%PROJECT%pages', '%PROJECT%CLAUDE.md', '%PROJECT%README.md', '%PROJECT%manual.html', '%PROJECT%.gitignore' -DestinationPath '%ARCHIVE%' -Force"

if %ERRORLEVEL% equ 0 (
    echo  [OK] Бэкап создан!
    echo.
    :: Показываем размер
    for %%A in ("%ARCHIVE%") do echo  Размер: %%~zA байт
    echo.
    :: Считаем старые бэкапы (оставляем 10 последних)
    set COUNT=0
    for /f %%F in ('dir /b /o-d "%BACKUP_DIR%\BeehiveScale_*.zip" 2^>nul') do (
        set /a COUNT+=1
        if !COUNT! gtr 10 del "%BACKUP_DIR%\%%F" 2>nul
    )
    echo  Папка бэкапов: %BACKUP_DIR%
) else (
    echo  [ОШИБКА] Не удалось создать архив!
)

echo.
pause
