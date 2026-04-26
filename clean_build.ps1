# Очистка кэша компиляции OrcaSlicer (PowerShell)
# Удаляет все временные файлы и папки сборки для чистого билда

$ErrorActionPreference = "Stop"

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "OrcaSlicer - Очистка кэша компиляции" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# Проверяем, что мы в правильной директории
if (-not (Test-Path "src")) {
    Write-Host "Ошибка: Не найдено src директории" -ForegroundColor Red
    Write-Host "Убедитесь, что вы находитесь в директории docs\OrcaSlicer" -ForegroundColor Red
    exit 1
}

Write-Host "Шаг 1: Удаляем папку build..." -ForegroundColor Yellow
if (Test-Path "build") {
    Remove-Item -Recurse -Force "build" -ErrorAction SilentlyContinue
    Write-Host "  Папка build удалена" -ForegroundColor Green
} else {
    Write-Host "  Папка build не найдена (уже удалена или не создана)" -ForegroundColor Gray
}

Write-Host ""
Write-Host "Шаг 2: Удаляем временные файлы CMake в корне..." -ForegroundColor Yellow
$cmakeFiles = @("CMakeCache.txt", "cmake_install.cmake")
foreach ($file in $cmakeFiles) {
    if (Test-Path $file) {
        Remove-Item -Force $file -ErrorAction SilentlyContinue
        Write-Host "  Удален: $file" -ForegroundColor Green
    }
}

if (Test-Path "CMakeFiles") {
    Remove-Item -Recurse -Force "CMakeFiles" -ErrorAction SilentlyContinue
    Write-Host "  Удалена папка: CMakeFiles" -ForegroundColor Green
}

Write-Host ""
Write-Host "Шаг 3: Удаляем файлы Visual Studio в build..." -ForegroundColor Yellow
$vsPatterns = @("*.vcxproj.user", "*.suo", "*.sdf", "*.opensdf")
$vsFilesDeleted = 0
foreach ($pattern in $vsPatterns) {
    $files = Get-ChildItem -Path . -Filter $pattern -Recurse -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -like "*\build\*" -or $_.FullName -like "*\.vs\*" }
    foreach ($file in $files) {
        Remove-Item -Force $file.FullName -ErrorAction SilentlyContinue
        $vsFilesDeleted++
    }
}
Write-Host "  Удалено файлов Visual Studio: $vsFilesDeleted" -ForegroundColor Green

Write-Host ""
Write-Host "Шаг 4: Удаляем папку .vs (если есть)..." -ForegroundColor Yellow
if (Test-Path ".vs") {
    Remove-Item -Recurse -Force ".vs" -ErrorAction SilentlyContinue
    Write-Host "  Папка .vs удалена" -ForegroundColor Green
} else {
    Write-Host "  Папка .vs не найдена" -ForegroundColor Gray
}

Write-Host ""
Write-Host "========================================" -ForegroundColor Green
Write-Host "Очистка завершена!" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Green
Write-Host ""
Write-Host "Теперь вы можете:" -ForegroundColor Cyan
Write-Host "  1. Запустить CMake для настройки проекта (если build был удален)" -ForegroundColor Gray
Write-Host "  2. Открыть OrcaSlicer.sln в Visual Studio для полной пересборки" -ForegroundColor Gray
Write-Host "  3. Или запустить build_quick_check.ps1 для быстрой сборки (если build настроен)" -ForegroundColor Gray
Write-Host ""
