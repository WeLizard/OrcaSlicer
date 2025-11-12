# Быстрая проверка компиляции только изменённых файлов (PowerShell)
# Это пересоберёт только изменённые файлы, включая FilamentHubPanel и FilamentHubClient
# 
# ВАЖНО: Этот скрипт для быстрой разработки, не для полной сборки!
# Для полной сборки используйте build_release_vs2022.bat

$ErrorActionPreference = "Stop"

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "OrcaSlicer - Быстрая проверка компиляции" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "ВНИМАНИЕ: Это скрипт для быстрой разработки!" -ForegroundColor Yellow
Write-Host "Он собирает только изменённые файлы для тестирования." -ForegroundColor Yellow
Write-Host "Для полной сборки используйте: build_release_vs2022.bat" -ForegroundColor Yellow
Write-Host ""

# Проверяем, что мы в правильной директории
if (-not (Test-Path "build\OrcaSlicer.sln")) {
    Write-Host "Ошибка: Не найдено build\OrcaSlicer.sln" -ForegroundColor Red
    Write-Host "Убедитесь, что вы находитесь в директории docs\OrcaSlicer" -ForegroundColor Red
    Write-Host ""
    Write-Host "Для первой сборки используйте:" -ForegroundColor Yellow
    Write-Host "  .\build_release_vs2022.bat" -ForegroundColor Gray
    exit 1
}

Write-Host "Шаг 1: Переходим в build директорию" -ForegroundColor Yellow
Push-Location build

try {
    Write-Host ""
    Write-Host "Шаг 2: Собираем ALL_BUILD (все цели)" -ForegroundColor Yellow
    Write-Host "Это пересоберёт только изменённые файлы, включая FilamentHubPanel и FilamentHubClient" -ForegroundColor Gray
    Write-Host ""
    
    # ВАЖНО: Собираем ALL_BUILD, а не только libslic3r_gui
    # Это соответствует официальному процессу сборки (build_release_vs2022.bat)
    # ALL_BUILD собирает все цели, включая главное приложение OrcaSlicer
    cmake --build . --config Release --target ALL_BUILD -- -m
    
    if ($LASTEXITCODE -ne 0) {
        Write-Host ""
        Write-Host "========================================" -ForegroundColor Red
        Write-Host "Ошибка компиляции!" -ForegroundColor Red
        Write-Host "========================================" -ForegroundColor Red
        Write-Host "Проверьте ошибки выше." -ForegroundColor Red
        exit $LASTEXITCODE
    }
    
    Write-Host ""
    Write-Host "========================================" -ForegroundColor Green
    Write-Host "Компиляция успешна!" -ForegroundColor Green
    Write-Host "========================================" -ForegroundColor Green
    Write-Host ""
    
    Write-Host "Шаг 3: Запускаем gettext для локализации..." -ForegroundColor Yellow
    Pop-Location
    if (Test-Path "scripts\run_gettext.bat") {
        & "scripts\run_gettext.bat"
        if ($LASTEXITCODE -ne 0) {
            Write-Host "Предупреждение: gettext завершился с ошибкой (это нормально, если нет изменений в локализации)" -ForegroundColor Yellow
        }
    } else {
        Write-Host "Предупреждение: scripts\run_gettext.bat не найден, пропускаем локализацию" -ForegroundColor Yellow
    }
    Push-Location build
    
    Write-Host ""
    Write-Host "Шаг 4: Устанавливаем результат" -ForegroundColor Yellow
    cmake --build . --target install --config Release
    
    if ($LASTEXITCODE -ne 0) {
        Write-Host ""
        Write-Host "========================================" -ForegroundColor Red
        Write-Host "Ошибка установки!" -ForegroundColor Red
        Write-Host "========================================" -ForegroundColor Red
        exit $LASTEXITCODE
    }
    
    Write-Host ""
    Write-Host "========================================" -ForegroundColor Green
    Write-Host "Готово! OrcaSlicer обновлён." -ForegroundColor Green
    Write-Host "========================================" -ForegroundColor Green
    Write-Host ""
    
    # Проверяем возможные пути к исполняемому файлу
    $possiblePaths = @(
        "package\bin\orca-slicer.exe",
        "OrcaSlicer\orca-slicer.exe",
        "src\Release\orca-slicer.exe"
    )
    
    $exeFound = $false
    foreach ($exePath in $possiblePaths) {
        if (Test-Path $exePath) {
            $exe = Get-Item $exePath
            Write-Host "Исполняемый файл: $($exe.FullName)" -ForegroundColor Cyan
            Write-Host "  Размер: $([math]::Round($exe.Length / 1MB, 2)) MB" -ForegroundColor Gray
            Write-Host "  Дата: $($exe.LastWriteTime)" -ForegroundColor Gray
            $exeFound = $true
            break
        }
    }
    
    if (-not $exeFound) {
        Write-Host "Исполняемый файл не найден в стандартных местах:" -ForegroundColor Yellow
        foreach ($path in $possiblePaths) {
            Write-Host "  - $path" -ForegroundColor Gray
        }
        Write-Host ""
        Write-Host "Попробуйте найти его вручную в build директории." -ForegroundColor Yellow
    }
    Write-Host ""
    
} finally {
    Pop-Location
}
