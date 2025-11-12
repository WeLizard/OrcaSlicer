@REM Быстрая проверка компиляции только изменённых файлов
@echo off
set WP=%CD%

echo ========================================
echo OrcaSlicer - Быстрая проверка компиляции
echo ========================================
echo.

REM Проверяем, что мы в правильной директории
if not exist "build\OrcaSlicer.sln" (
    echo Ошибка: Не найдено build\OrcaSlicer.sln
    echo Убедитесь, что вы находитесь в директории docs\OrcaSlicer
    pause
    exit /b 1
)

echo Шаг 1: Переходим в build директорию
cd build

echo Шаг 2: Собираем только libslic3r_gui (инкрементально)
echo Это пересоберёт только изменённые файлы, включая FilamentHubPanel и FilamentHubClient
echo.

cmake --build . --config Release --target libslic3r_gui -- -m

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo ========================================
    echo Ошибка компиляции!
    echo ========================================
    echo Проверьте ошибки выше.
    pause
    exit /b %ERRORLEVEL%
)

echo.
echo ========================================
echo Компиляция успешна!
echo ========================================
echo.

echo Шаг 3: Устанавливаем результат
cmake --build . --target install --config Release

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo ========================================
    echo Ошибка установки!
    echo ========================================
    pause
    exit /b %ERRORLEVEL%
)

echo.
echo ========================================
echo Готово! OrcaSlicer обновлён.
echo ========================================
echo.
echo Исполняемый файл: build\package\bin\orca-slicer.exe
echo.

pause

