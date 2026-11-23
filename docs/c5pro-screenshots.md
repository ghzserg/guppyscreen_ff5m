# Скриншоты Guppy Screen — C5 Pro

Снимки сделаны **тестовым LVGL-стендом 800×480** с фикстурой C5 Pro (четыре экструдера, вентиляторы камеры). Это не фотографии реального принтера. Доступность кнопок на устройстве зависит от его конфигурации Z-Mod и Klipper.

Screenshots are captured from the **800×480 LVGL test framebuffer** using the C5 Pro fixture. They are not photos of the printer. Controls shown depend on the printer configuration.

Экран LED не включён: в тестовой конфигурации C5 Pro нет управляемых светодиодов, поэтому эта панель пустая.

Сценарий: [`ci/ui-scenarios/c5pro-gallery.txt`](../ci/ui-scenarios/c5pro-gallery.txt). Обновление изображений — только вручную через GitHub Actions → **Rebuild C5 Pro gallery** → **Run workflow** (выбрать ветку). Локально: `bash ci/generate-c5pro-gallery.sh` после установки зависимостей и применения патчей. Для Input Shaper необходимы `tests/fixtures/resonances_X.png` и `tests/fixtures/resonances_Y.png`.

| **Главная / Home** | **Файлы / Print** |
|:--|:--|
| ![Главная / Home](screenshots/c5pro/home.png) | ![Файлы / Print](screenshots/c5pro/print.png) |

| **Настройка / Tune** | **Настройки / Settings** |
|:--|:--|
| ![Настройка / Tune](screenshots/c5pro/tune.png) | ![Настройки / Settings](screenshots/c5pro/settings.png) |

| **Просмотр файла / File preview** | **Идёт печать / Printing** |
|:--|:--|
| ![Просмотр файла / File preview](screenshots/c5pro/file-preview.png) | ![Идёт печать / Printing](screenshots/c5pro/print-status.png) |

| **Пауза / Paused** | **Главная при печати / Home while printing** |
|:--|:--|
| ![Пауза / Paused](screenshots/c5pro/print-paused.png) | ![Главная при печати / Home while printing](screenshots/c5pro/home-printing.png) |

| **Главная на паузе / Home while paused** | **Исключение объектов / Exclude map** |
|:--|:--|
| ![Главная на паузе / Home while paused](screenshots/c5pro/home-paused.png) | ![Исключение объектов / Exclude map](screenshots/c5pro/exclude-map.png) |

| **Парковка / Homing** | **Экструдеры / Extrude** |
|:--|:--|
| ![Парковка / Homing](screenshots/c5pro/homing.png) | ![Экструдеры / Extrude](screenshots/c5pro/extrude.png) |

| **Вентиляторы / Fans** | **Точная настройка / Fine Tune** |
|:--|:--|
| ![Вентиляторы / Fans](screenshots/c5pro/fans.png) | ![Точная настройка / Fine Tune](screenshots/c5pro/fine-tune.png) |

| **Карта стола / Bed Mesh** | **Лимиты / Limits** |
|:--|:--|
| ![Карта стола / Bed Mesh](screenshots/c5pro/bed-mesh.png) | ![Лимиты / Limits](screenshots/c5pro/limits.png) |

| **Ретракт / Retraction** | **Input Shaper** |
|:--|:--|
| ![Ретракт / Retraction](screenshots/c5pro/retract.png) | ![Input Shaper](screenshots/c5pro/input-shaper.png) |

| **Ремни / Belts** | **PID** |
|:--|:--|
| ![Ремни / Belts](screenshots/c5pro/belts.png) | ![PID](screenshots/c5pro/pid.png) |

| **Система / System** | **Макросы / Macros** |
|:--|:--|
| ![Система / System](screenshots/c5pro/system.png) | ![Макросы / Macros](screenshots/c5pro/macros.png) |

| **Консоль / Console** | |
|:--|:--|
| ![Консоль / Console](screenshots/c5pro/console.png) | |

