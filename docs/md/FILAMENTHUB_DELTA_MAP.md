# Карта отличий ветки Orca от upstream по интеграции FilamentHub

## Agent navigation

- [AGENTS.md](../../AGENTS.md)
- [CHANGES.md](../../CHANGES.md)
- [SYNC_ANALYSIS.md](../../SYNC_ANALYSIS.md)
- [TESTING_PLAN.md](../../TESTING_PLAN.md)
- [TESTING_REPORT.md](../../TESTING_REPORT.md)

## Новые файлы (5)

1. `src/slic3r/GUI/FilamentHubPanel.cpp` — реализация UI-панели FilamentHub.
2. `src/slic3r/GUI/FilamentHubPanel.hpp` — декларации панели и связанного интерфейса.
3. `src/slic3r/Utils/FilamentHubClient.cpp` — реализация клиентской логики API FilamentHub.
4. `src/slic3r/Utils/FilamentHubClient.hpp` — интерфейс клиента FilamentHub.
5. `resources/images/tab_filamenthub_active.svg` — активная иконка вкладки FilamentHub.

## Модифицированные файлы (8)

1. `src/slic3r/GUI/MainFrame.cpp` — интеграция вкладки/панели FilamentHub в основной фрейм.
2. `src/slic3r/GUI/MainFrame.hpp` — обновлённые декларации MainFrame для FilamentHub.
3. `src/slic3r/GUI/PresetComboBoxes.cpp` — связь пресетов с источником/данными FilamentHub.
4. `src/libslic3r/Preset.hpp` — расширение структуры/метаданных пресета.
5. `src/libslic3r/Preset.cpp` — обработка и сериализация новых метаданных пресета.
6. `src/libslic3r/PresetBundle.cpp` — корректная агрегация/сохранение метаданных в наборах пресетов.
7. `src/libslic3r/Config.cpp` — изменения persist-логики конфигурации.
8. `src/slic3r/CMakeLists.txt` — подключение новых исходников и ресурсов FilamentHub в сборку.

## Критичный фикс persist метаданных

Зона фикса: `Config` / `Preset` / `PresetBundle`.

- В `src/libslic3r/Config.cpp` исправлен путь сохранения метаданных, чтобы они не терялись при сериализации.
- В `src/libslic3r/Preset.cpp` и `src/libslic3r/PresetBundle.cpp` синхронизирована логика чтения/записи метаданных пресетов.
- В `src/libslic3r/Preset.hpp` зафиксирован состав полей, участвующих в persist.
- `src/libslic3r/Config.hpp` отмечен как часть фикса: сигнатура `save_to_json` должна соответствовать реализации и не ломать перенос метаданных между слоями.

## Проверка после обновления upstream

- [ ] Сверить наличие и подключение всех 5 новых файлов в проекте и сборке.
- [ ] Проверить, что изменения в 8 модифицированных файлах применены без конфликтов после rebase/merge.
- [ ] Перепроверить связку `Config/Preset/PresetBundle` на сохранение и повторную загрузку метаданных.
- [ ] Валидировать соответствие сигнатуры `save_to_json` между `src/libslic3r/Config.hpp` и реализацией.
- [ ] Собрать проект и убедиться, что FilamentHub UI/клиент доступны и функциональны.
- [ ] Выполнить smoke-тест: импорт/выбор пресета, сохранение, перезапуск, проверка сохранённых метаданных.