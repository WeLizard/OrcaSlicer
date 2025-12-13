# Анализ функционала синхронизации в OrcaSlicer ↔ FilamentHub

**Дата анализа:** 2025-01-XX  
**Файл:** `docs/OrcaSlicer/SYNC_ANALYSIS.md`

---

## 📊 Текущее состояние

### ✅ Реализовано

#### 1. **Синхронизация ИЗ FilamentHub → OrcaSlicer (PULL)**

Функции:
- `synchronize_presets()` - синхронизирует filament presets
- `synchronize_printer_profiles()` - синхронизирует printer profiles  
- `synchronize_print_profiles()` - синхронизирует print profiles

**Как работает:**
- При нажатии кнопки **"Synchronize"** вызывается `on_sync_button_click()`
- `on_sync_button_click()` вызывает `synchronize_presets(false)` - только **incremental sync**
- `synchronize_presets()` загружает пресеты через API (`get_my_presets`) с параметром `updated_since`
- Импортирует новые/обновлённые пресеты в OrcaSlicer через `import_preset_silent()`
- Сохраняет маппинг `external_id → fhub_id` в AppConfig
- Обновляет `last_sync_time` после успешной синхронизации

**Особенности:**
- ✅ Поддерживает incremental sync (только изменения после `last_sync_time`)
- ✅ Поддерживает full sync (все пресеты, если `force_full_sync=true`)
- ✅ Обрабатывает удалённые пресеты (если список пуст, но есть `last_sync_time` - делает full sync)
- ✅ Синхронизирует только пользовательские пресеты (не системные)
- ✅ Добавляет `[FilamentHub]` постфикс к именам импортированных пресетов
- ✅ Сохраняет маппинг для обратной синхронизации

#### 2. **Экспорт ИЗ OrcaSlicer → FilamentHub (PUSH)**

Функции:
- `export_filament_presets_to_filamenthub()` - экспортирует filament presets
- `export_printer_profiles_to_filamenthub()` - экспортирует printer profiles
- `export_print_profiles_to_filamenthub()` - экспортирует print profiles

**Как работает:**
- Вызывается через JavaScript команду `"export_filament_presets"` из фронтенда
- Экспортирует все пользовательские пресеты из OrcaSlicer в FilamentHub
- Отправляет пресеты через API (`import_presets` endpoint) как drafts
- Backend создаёт или обновляет пресеты и возвращает маппинг `external_id → fhub_id`
- Сохраняет маппинг в AppConfig для будущей синхронизации

**Особенности:**
- ✅ Создаёт новые пресеты в FilamentHub (как drafts)
- ✅ Обновляет существующие пресеты (если есть маппинг `fhub_id`)
- ✅ Проверяет разрешения пользователя перед экспортом
- ✅ Сохраняет полный JSON профиль OrcaSlicer в поле `orcaslicer_settings`

---

## ❌ Что НЕ реализовано

### **Двусторонняя синхронизация при нажатии кнопки "Synchronize"**

**Проблема:**
- Кнопка "Synchronize" делает **только PULL** (загрузку из FilamentHub)
- **НЕ делает PUSH** (экспорт в FilamentHub)
- Экспорт доступен только через JavaScript команду из фронтенда

**Текущее поведение:**
```cpp
void FilamentHubPanel::on_sync_button_click(wxCommandEvent& evt)
{
    // ...
    synchronize_presets(false); // ТОЛЬКО PULL из FilamentHub → OrcaSlicer
    // НЕТ экспорта (PUSH) из OrcaSlicer → FilamentHub
}
```

**Что должно быть (для двусторонней синхронизации):**
```cpp
void FilamentHubPanel::on_sync_button_click(wxCommandEvent& evt)
{
    // 1. PUSH: Экспорт пресетов из OrcaSlicer → FilamentHub
    export_filament_presets_to_filamenthub();
    export_printer_profiles_to_filamenthub();
    export_print_profiles_to_filamenthub();
    
    // 2. PULL: Импорт пресетов из FilamentHub → OrcaSlicer
    synchronize_presets(false);
    synchronize_printer_profiles(false);
    synchronize_print_profiles(false);
}
```

---

## 🔍 Детальный анализ функций

### `synchronize_presets(bool force_full_sync)`

**Расположение:** `FilamentHubPanel.cpp:868`

**Что делает:**
1. Проверяет авторизацию
2. Валидирует токен через `get_current_user()`
3. Загружает `last_sync_time` из AppConfig (если не `force_full_sync`)
4. Вызывает API `get_my_presets(updated_since)` для получения списка пресетов
5. Импортирует каждый пресет через `import_preset_silent()`
6. Сохраняет маппинг `external_id → fhub_id`
7. Обновляет `last_sync_time` после успешной синхронизации

**Вызывается:**
- При нажатии кнопки "Synchronize" (через `on_sync_button_click()`)
- При первом входе (через JavaScript команду из фронтенда)

**Параметры:**
- `force_full_sync = false` - incremental sync (только изменения)
- `force_full_sync = true` - full sync (все пресеты)

---

### `export_filament_presets_to_filamenthub()`

**Расположение:** `FilamentHubPanel.cpp:4397`

**Что делает:**
1. Проверяет авторизацию
2. Проверяет разрешения пользователя (`allow_filament_presets_import`)
3. Получает все пользовательские filament presets из PresetBundle
4. Конвертирует каждый пресет в JSON (используя `get_config_json()`)
5. Отправляет все пресеты через API `import_presets` endpoint
6. Сохраняет маппинг `external_id → fhub_id` из ответа Backend
7. Показывает уведомление о результате

**Вызывается:**
- Только через JavaScript команду `"export_filament_presets"` из фронтенда
- НЕ вызывается при нажатии кнопки "Synchronize"

**Особенности:**
- Экспортирует все пользовательские пресеты (не системные)
- Использует `external_id` (setting_id из OrcaSlicer) для идентификации
- Если пресет уже синхронизирован (есть маппинг), добавляет `fhub_id` в запрос

---

## 💡 Рекомендации

### Вариант 1: Добавить двустороннюю синхронизацию в кнопку "Synchronize"

**Изменения:**
```cpp
void FilamentHubPanel::on_sync_button_click(wxCommandEvent& evt)
{
    if (m_is_syncing) {
        return;
    }
    
    update_sync_button_state(true);
    
    // Двусторонняя синхронизация:
    // 1. PUSH: Экспорт изменений из OrcaSlicer → FilamentHub
    // 2. PULL: Импорт изменений из FilamentHub → OrcaSlicer
    
    // Запускаем экспорт, а затем синхронизацию (последовательно)
    export_filament_presets_to_filamenthub();
    // После завершения экспорта - запускаем синхронизацию
    // (нужно добавить callback для последовательности)
}
```

**Проблема:** Экспорт асинхронный, нужно дождаться завершения перед запуском синхронизации.

### Вариант 2: Оставить как есть, но улучшить UX

**Текущее поведение корректно для MVP:**
- Кнопка "Synchronize" = PULL (загрузка из FilamentHub)
- Экспорт доступен через UI фронтенда (отдельная кнопка/команда)

**Улучшения:**
- Добавить отдельную кнопку "Export to FilamentHub" в C++ UI
- Или добавить опцию в настройках: "Sync direction" (PULL, PUSH, BOTH)

### Вариант 3: Умная синхронизация (рекомендуется)

**Логика:**
1. Сначала экспортировать локальные изменения (PUSH)
2. Затем импортировать изменения с сервера (PULL)
3. Разрешать конфликты (если пресет изменён и локально, и на сервере)

**Реализация:**
- Добавить функцию `synchronize_bidirectional()`
- Она последовательно вызывает PUSH, затем PULL
- Обрабатывает конфликты (например, показывая диалог или используя стратегию "last write wins")

---

## 📋 Итоговая таблица

| Функция | Направление | Вызывается из | Реализовано |
|---------|-------------|---------------|-------------|
| `synchronize_presets()` | FilamentHub → OrcaSlicer (PULL) | Кнопка "Synchronize" | ✅ |
| `synchronize_printer_profiles()` | FilamentHub → OrcaSlicer (PULL) | НЕТ (только код) | ✅ |
| `synchronize_print_profiles()` | FilamentHub → OrcaSlicer (PULL) | НЕТ (только код) | ✅ |
| `export_filament_presets_to_filamenthub()` | OrcaSlicer → FilamentHub (PUSH) | JavaScript команда | ✅ |
| `export_printer_profiles_to_filamenthub()` | OrcaSlicer → FilamentHub (PUSH) | JavaScript команда | ✅ |
| `export_print_profiles_to_filamenthub()` | OrcaSlicer → FilamentHub (PUSH) | JavaScript команда | ✅ |
| Двусторонняя синхронизация | Обе стороны | НЕТ | ❌ |

---

## 🎯 Вывод

**Текущее состояние:**
- ✅ Функционал синхронизации в **обе стороны** реализован на уровне функций
- ❌ При нажатии кнопки "Synchronize" выполняется **только PULL** (загрузка из FilamentHub)
- ✅ Экспорт (PUSH) доступен через JavaScript команду из фронтенда
- ❌ **Нет автоматической двусторонней синхронизации** при нажатии одной кнопки

**Для полноценной двусторонней синхронизации нужно:**
1. Модифицировать `on_sync_button_click()` для вызова экспорта перед синхронизацией
2. Или добавить отдельную кнопку "Export" в C++ UI
3. Или создать функцию `synchronize_bidirectional()` которая делает и PUSH, и PULL последовательно

