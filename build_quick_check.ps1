# Быстрая сборка OrcaSlicer (только slicer, без зависимостей)
# Используется когда зависимости уже собраны и нужно собрать только изменения в коде

$ErrorActionPreference = "Stop"

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "OrcaSlicer - Быстрая сборка (только изменения)" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# Проверяем, что мы в правильной директории
if (-not (Test-Path "src")) {
    Write-Host "Ошибка: Не найдено src директории" -ForegroundColor Red
    Write-Host "Убедитесь, что вы находитесь в директории docs\OrcaSlicer" -ForegroundColor Red
    exit 1
}

# Проверяем наличие зависимостей
if (-not (Test-Path "deps\build")) {
    Write-Host "Ошибка: Зависимости не собраны!" -ForegroundColor Red
    Write-Host "Сначала запустите полную сборку: build_release_vs2022.bat" -ForegroundColor Red
    Write-Host "Или сборку только зависимостей: build_release_vs2022.bat deps" -ForegroundColor Yellow
    exit 1
}

Write-Host "✓ Зависимости найдены (deps\build)" -ForegroundColor Green
Write-Host ""

# Находим Visual Studio
$vsPaths = @(
    "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat",
    "C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat",
    "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\Tools\VsDevCmd.bat"
)

$vsPath = $null
foreach ($path in $vsPaths) {
    if (Test-Path $path) {
        $vsPath = $path
        break
    }
}

if (-not $vsPath) {
    Write-Host "Ошибка: Visual Studio 2022 не найдена!" -ForegroundColor Red
    Write-Host "Установите Visual Studio 2022 (Community/Professional/Enterprise)" -ForegroundColor Red
    exit 1
}

Write-Host "Шаг 1: Загрузка переменных окружения Visual Studio..." -ForegroundColor Yellow
Write-Host "  Путь: $vsPath" -ForegroundColor Gray
Write-Host "  ✓ Будет загружено при запуске сборки" -ForegroundColor Green
Write-Host ""

Write-Host "Шаг 2: Сборка OrcaSlicer (только изменения, без зависимостей)..." -ForegroundColor Yellow
Write-Host "  CMake пересоберёт только изменённые файлы автоматически" -ForegroundColor Gray
Write-Host "  Время: ~5-15 минут (зависит от количества изменений)" -ForegroundColor Gray
Write-Host ""

# Запускаем сборку через cmd, чтобы переменные окружения VS работали
Write-Host "Запуск: build_release_vs2022.bat slicer" -ForegroundColor Cyan
Write-Host ""

# Создаём временный bat файл для запуска сборки с VS окружением
$tempBat = Join-Path $env:TEMP "build_orca_quick_$(Get-Date -Format 'yyyyMMddHHmmss').bat"
$batContent = @"
@echo off
call "$vsPath"
cd /d "$PWD"
call build_release_vs2022.bat slicer
"@
$batContent | Out-File -FilePath $tempBat -Encoding ASCII -Force

# Запускаем через cmd
cmd /c $tempBat

# Удаляем временный файл
Remove-Item $tempBat -Force -ErrorAction SilentlyContinue

if ($LASTEXITCODE -ne 0) {
    Write-Host ""
    Write-Host "========================================" -ForegroundColor Red
    Write-Host "Ошибка при сборке! (код: $LASTEXITCODE)" -ForegroundColor Red
    Write-Host "========================================" -ForegroundColor Red
    Write-Host ""
    Write-Host "Проверьте ошибки выше" -ForegroundColor Yellow
    exit $LASTEXITCODE
}

Write-Host ""
Write-Host "Шаг 3: Проверка результата..." -ForegroundColor Yellow

$exePath = "build\package\bin\orca-slicer.exe"
if (Test-Path $exePath) {
    $fileInfo = Get-Item $exePath
    Write-Host "  ✓ Исполняемый файл создан: $exePath" -ForegroundColor Green
    Write-Host "  Размер: $([math]::Round($fileInfo.Length / 1MB, 2)) MB" -ForegroundColor Gray
    Write-Host "  Дата: $($fileInfo.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss'))" -ForegroundColor Gray
} else {
    Write-Host "  ⚠ Исполняемый файл не найден: $exePath" -ForegroundColor Yellow
    Write-Host "  Проверьте логи сборки выше на наличие ошибок" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "  Возможные решения:" -ForegroundColor Cyan
    Write-Host "    - Проверьте, что build\ директория существует" -ForegroundColor Gray
    Write-Host "    - Попробуйте полную пересборку: build_release_vs2022.bat" -ForegroundColor Gray
}

Write-Host ""
Write-Host "========================================" -ForegroundColor Green
Write-Host "Сборка завершена!" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Green
Write-Host ""
Write-Host "Запуск OrcaSlicer:" -ForegroundColor Cyan
Write-Host "  .\build\package\bin\orca-slicer.exe" -ForegroundColor Gray
Write-Host ""
