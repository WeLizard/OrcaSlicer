# PowerShell скрипт для быстрой проверки компиляции
# Запускать из PowerShell: .\build_quick_check.ps1

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "OrcaSlicer - Быстрая проверка компиляции" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# Проверяем, что мы в правильной директории
if (-not (Test-Path "build\OrcaSlicer.sln")) {
    Write-Host "Ошибка: Не найдено build\OrcaSlicer.sln" -ForegroundColor Red
    Write-Host "Убедитесь, что вы находитесь в директории docs\OrcaSlicer" -ForegroundColor Red
    exit 1
}

Write-Host "Шаг 1: Переходим в build директорию" -ForegroundColor Yellow
Set-Location build

Write-Host "Шаг 2: Собираем только libslic3r_gui (инкрементально)" -ForegroundColor Yellow
Write-Host "Это пересоберёт только изменённые файлы, включая FilamentHubPanel и FilamentHubClient" -ForegroundColor Gray
Write-Host ""

cmake --build . --config Release --target libslic3r_gui -- -m

if ($LASTEXITCODE -ne 0) {
    Write-Host ""
    Write-Host "========================================" -ForegroundColor Red
    Write-Host "Ошибка компиляции!" -ForegroundColor Red
    Write-Host "========================================" -ForegroundColor Red
    Write-Host "Проверьте ошибки выше." -ForegroundColor Red
    Set-Location ..
    exit $LASTEXITCODE
}

Write-Host ""
Write-Host "========================================" -ForegroundColor Green
Write-Host "Компиляция успешна!" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Green
Write-Host ""

Write-Host "Шаг 3: Устанавливаем результат" -ForegroundColor Yellow
cmake --build . --target install --config Release

if ($LASTEXITCODE -ne 0) {
    Write-Host ""
    Write-Host "========================================" -ForegroundColor Red
    Write-Host "Ошибка установки!" -ForegroundColor Red
    Write-Host "========================================" -ForegroundColor Red
    Set-Location ..
    exit $LASTEXITCODE
}

Write-Host ""
Write-Host "========================================" -ForegroundColor Green
Write-Host "Готово! OrcaSlicer обновлён." -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Green
Write-Host ""
Write-Host "Исполняемый файл: build\package\bin\orca-slicer.exe" -ForegroundColor Cyan
Write-Host ""

Set-Location ..

