# OrcaSlicer FilamentHub Edition - TODO

## Цель

Полноценная интеграция всех типов пресетов OrcaSlicer с FilamentHub:
- **Filament profiles** (профили филамента)
- **Print/Process profiles** (профили печати)
- **Printer/Machine profiles** (профили принтера)
- **Bundles** (пакеты профилей)

Все профили должны быть валидны согласно OrcaSlicer Wiki и корректно синхронизироваться.

---

## Выполнено ✅

### Архитектура: флаг is_filamenthub

- [x] **Preset.hpp** - добавлен флаг `is_filamenthub` и метод `is_filamenthub_preset()`
- [x] **PresetBundle.cpp** - при импорте JSON определяем FilamentHub пресеты по `fhub_source`
- [x] **PresetComboBoxes.cpp** - отдельная секция "FilamentHub presets" в выпадающем списке
- [x] **Локализация** - перевод "FilamentHub presets" → "Пресеты FilamentHub"

---

## В работе 🔄

### 1. Filament Profiles (Профили филамента)

#### Backend (Python)

| Задача | Файл | Статус |
|--------|------|--------|
| Экспорт в JSON | `orcaslicer_exporter.py` | ✅ Готово |
| Поле `fhub_source` | `orcaslicer_exporter.py` | ✅ Готово |
| Валидация обязательных полей | `profile_validator.py` | ✅ Готово |
| Тесты экспорта | `tests/test_orcaslicer_export.py` | 🔲 |

**Обязательные поля (Wiki):**
```json
{
  "type": "filament",
  "name": "...",
  "version": "2.3.0.0",
  "from": "user",
  "inherits": "Generic PLA @System",
  "instantiation": "true",
  "filament_settings_id": ["..."],
  "setting_id": "FHUB000123",
  "filament_id": "FHUB000456",
  "compatible_printers": [],
  "fhub_source": "filamenthub"
}
```

#### OrcaSlicer (C++)

| Задача | Файл | Статус |
|--------|------|--------|
| Импорт JSON | `FilamentHubPanel.cpp` | ✅ Готово |
| Синхронизация | `FilamentHubPanel.cpp` | ✅ Готово |
| Флаг is_filamenthub | `Preset.hpp` | ✅ Готово |
| UI секция в списке | `PresetComboBoxes.cpp` | ✅ Готово |

---

### 2. Print/Process Profiles (Профили печати)

#### Backend (Python)

| Задача | Файл | Статус |
|--------|------|--------|
| API endpoint | `print_profiles.py` | ✅ Готово |
| Схема Pydantic | `schemas/print_profile.py` | ✅ Готово |
| Модель SQLAlchemy | `models/print_profile.py` | ✅ Готово |
| Экспорт в JSON | `orcaslicer_machine_exporter.py` | ✅ Готово |
| Валидация полей | `profile_validator.py` | ✅ Готово |

**Обязательные поля (Wiki):**
```json
{
  "type": "process",
  "name": "0.20mm Standard",
  "version": "2.3.0.0",
  "from": "user",
  "inherits": "0.20mm Standard @System",
  "print_settings_id": ["..."],
  "setting_id": "FHUB000789",
  "compatible_printers": ["Printer Model A", "Printer Model B"],
  "fhub_source": "filamenthub"
}
```

**ВАЖНО:** `compatible_printers` НЕ должен быть пустым для print profiles!

#### OrcaSlicer (C++)

| Задача | Файл | Статус |
|--------|------|--------|
| Sync print profiles | `FilamentHubPanel.cpp` | 🔲 Добавить |
| API метод | `FilamentHubClient.cpp` | 🔲 Добавить |

---

### 3. Printer/Machine Profiles (Профили принтера)

#### Backend (Python)

| Задача | Файл | Статус |
|--------|------|--------|
| API endpoint | `printer_profiles.py` | ✅ Готово |
| Экспорт в JSON | `orcaslicer_machine_exporter.py` | ✅ Готово |
| Валидация полей | `profile_validator.py` | ✅ Готово |

**Обязательные поля (Wiki):**
```json
{
  "type": "machine",
  "name": "My Printer 0.4 nozzle",
  "version": "2.3.0.0",
  "from": "user",
  "inherits": "Bambu Lab X1 Carbon 0.4 nozzle",
  "printer_settings_id": ["..."],
  "setting_id": "FHUB001000",
  "printer_model": "Bambu Lab X1 Carbon",
  "printer_variant": "0.4",
  "nozzle_diameter": ["0.4"],
  "fhub_source": "filamenthub"
}
```

#### OrcaSlicer (C++)

| Задача | Файл | Статус |
|--------|------|--------|
| Sync printer profiles | `FilamentHubPanel.cpp` | 🔲 Проверить |
| API метод | `FilamentHubClient.cpp` | 🔲 Проверить |

---

### 4. Bundles (Пакеты профилей)

Bundle = набор связанных профилей (printer + prints + filaments).

#### Backend (Python)

| Задача | Файл | Статус |
|--------|------|--------|
| API endpoint | `bundles.py` | 🔲 Создать |
| Модель Bundle | `models/bundle.py` | 🔲 Создать |
| Связи Bundle → Profiles | - | 🔲 |
| Экспорт bundle ZIP | - | 🔲 |

**Структура Bundle (Wiki):**
```
bundle.zip
├── printer/
│   └── My Printer.json
├── process/
│   ├── 0.20mm Standard.json
│   └── 0.28mm Draft.json
├── filament/
│   ├── PLA Generic.json
│   └── PETG Generic.json
└── vendor_meta.json
```

#### OrcaSlicer (C++)

| Задача | Файл | Статус |
|--------|------|--------|
| Import bundle | `FilamentHubPanel.cpp` | 🔲 |
| UI для bundles | - | 🔲 |

---

## Валидация профилей

### Инструменты

1. **OrcaSlicer Profile Validator** (официальный):
   ```bash
   ./OrcaSlicer_profile_validator -p profiles/ -l 2
   ```
   [Скачать](https://github.com/SoftFever/Orca_tools/releases/tag/1)

2. **JSON Schema Validation** (на backend):
   ```python
   from jsonschema import validate
   validate(profile_json, FILAMENT_SCHEMA)
   ```

### Схемы валидации

#### Filament Profile Schema
```python
FILAMENT_SCHEMA = {
    "type": "object",
    "required": [
        "type", "name", "version", "from", "inherits",
        "filament_settings_id", "setting_id", "filament_id"
    ],
    "properties": {
        "type": {"const": "filament"},
        "name": {"type": "string", "minLength": 1},
        "version": {"type": "string", "pattern": r"^\d+\.\d+\.\d+\.\d+$"},
        "from": {"enum": ["system", "user"]},
        "inherits": {"type": "string"},
        "filament_settings_id": {"type": "array", "items": {"type": "string"}},
        "setting_id": {"type": "string", "pattern": r"^FHUB\d+$"},
        "filament_id": {"type": "string"},
        "compatible_printers": {"type": "array"},
        "fhub_source": {"const": "filamenthub"}
    }
}
```

#### Print Profile Schema
```python
PRINT_SCHEMA = {
    "type": "object",
    "required": [
        "type", "name", "version", "from",
        "print_settings_id", "setting_id", "compatible_printers"
    ],
    "properties": {
        "type": {"const": "process"},
        "print_settings_id": {"type": "array", "items": {"type": "string"}},
        "compatible_printers": {
            "type": "array",
            "minItems": 1  # НЕ пустой!
        }
    }
}
```

---

## Roadmap

### Фаза 1: Filament Profiles ✅
- [x] Архитектура is_filamenthub
- [x] UI секция в списке
- [x] Валидация на backend (`profile_validator.py`)
- [ ] Тесты

### Фаза 2: Print Profiles ✅
- [x] Backend API + модели
- [x] Экспорт JSON
- [x] Синхронизация в OrcaSlicer
- [x] Валидация

### Фаза 3: Printer Profiles ✅
- [x] Проверить текущую реализацию
- [x] Экспорт JSON
- [x] Валидация

### Фаза 4: Bundles
- [ ] Backend API + модели
- [ ] UI для создания/редактирования
- [ ] Экспорт/импорт
- [ ] Синхронизация

### Фаза 5: Quality
- [ ] Интеграция OrcaSlicer Profile Validator
- [ ] CI/CD валидация
- [ ] Документация

---

## Ссылки

- [OrcaSlicer Wiki - Preset and Bundle](../OrcaSlicer-Wiki/developer-reference/Preset-and-bundle.md)
- [OrcaSlicer Wiki - How to create profiles](../OrcaSlicer-Wiki/developer-reference/How-to-create-profiles.md)
- [OrcaSlicer Profile Validator](https://github.com/SoftFever/Orca_tools/releases/tag/1)
- [FilamentHub Backend](../../backend/)
