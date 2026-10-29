# HeartGram Desktop

Неофициальный клиент Telegram для Windows на базе [AyuGram Desktop](https://github.com/AyuGram/AyuGramDesktop) с функциями [exteraGram](https://exteragram.app) и поддержкой плагинов.
Сборка и изменения: [@whyheartone](https://t.me/whyheartone).

> HeartGram не разрабатывается и не поддерживается Telegram, AyuGram и exteraGram. Используйте на свой риск.

[ Русский | [English](#english) ]

## Возможности

### Из AyuGram Desktop
Режим призрака, история правок и удалённых сообщений, фильтры, оформление, приблизительное «был(а) в сети».

### Плеер
- Полноэкранный плеер в стиле Material Design 3: обложка, очередь со всеми треками, панель скорости.
- Текст песни с плавной сменой строк и выбором шрифта плеера.
- Фон в такт музыке по алгоритму exteraGram: анализ темпа, сильных долей и фраз, вращающиеся пятна, пульс и вспышка на дропе. Рисуется на видеокарте.
- Отдельная громкость для песен и для голосовых и кружков (включается в настройках).

### Чаты и интерфейс
- Настройки exteraGram: чаты, боковое меню, навигация, быстрые действия, значки.
- Подменю «Плагины» в меню сообщения. Режим задаётся в «Элементы контекстного меню»: «Показывать», «Скрывать» или «В расширенное» (с Shift или Ctrl).
- «Детали» сообщения: ID сообщения, чата и отправителя, дата, правки, пересылка, файл.
- Animated Messages встроено: 12 стилей появления сообщений, пресеты, анимация дат.
- Калькулятор в поле ввода: `2+2=` показывает результат серым, пробел вставляет.
- Значки проекта (необязательно): сердечки у имён и окно «Поддержать разработку». Данные приходят с вашего сервера значков.

### Плагины
- Python-плагины в формате `.plugin`, как у exteraGram: установка по клику на файл, плашка «Проверено».
- Разбор безопасности при установке: локально, по тексту плагина, с кусками кода и пояснениями. Показывается прямо в окне установки, SHA-256 копируется по клику.
- Встроенный Python 3.11, Pillow, PyAV и rlottie-python. Заглушки Android-классов, чтобы многие плагины с телефона загружались на ПК.
- Гифки, которые отправляет плагин, не остаются в «Сохранённых GIF».

### Сборки
- **HeartGram**: полный клиент.
- **HeartGram Lite**: тот же клиент без функций AyuGram (режим призрака, сохранение удалённых, фильтры и т. п.).
- Один exe: Python и модули распаковываются рядом при первом запуске.

## Установка

1. Скачайте `HeartGram.exe` (или `HeartGram-lite.exe`) из [Releases](../../releases).
2. Положите в любую папку и запустите. При первом запуске рядом появятся `python/` и `modules/` (это нормально).
3. Данные аккаунта хранятся в `tdata/` рядом с exe (портативный режим).

## Сборка

Нужны Windows 10/11 x64, Visual Studio 2022 (MSVC 14.44), CMake, Python 3.11 (embeddable) и подготовленные библиотеки, как для [Telegram Desktop](https://github.com/telegramdesktop/tdesktop/blob/dev/docs/building-win.md) (см. `docs/building-win.md`). Дополнительно для плагинов нужен `Libraries/win64/python311` с заголовками и `python311.lib`.

Скрипты, которыми собираются и пакуются exe, лежат в `heartgram-tools/`: `build_low.ps1` (сборка с пониженным приоритетом, финальная линковка занимает около 10 ГБ памяти) и `make_portable.py` (упаковка в один exe, ключ `--lite` для Lite). Список изменений: `heartgram-tools/PATCHNOTES.md`.

Файл `Telegram/SourceFiles/ayu/features/project_badges/badge_secrets.h` с адресом и ключом сервера значков в репозиторий **не входит**. Без него клиент собирается, но список значков не загружается (используется `badge_secrets.example.h`).

## Благодарности и лицензия

- [Telegram Desktop](https://github.com/telegramdesktop/tdesktop), Telegram FZ-LLC.
- [AyuGram Desktop](https://github.com/AyuGram/AyuGramDesktop), Radolyn Labs.
- [exteraDesktop](https://github.com/metawka/exteraDesktop), [@metawka](https://t.me/metawka), и команда [exteraGram](https://exteragram.app).

Лицензия: [GNU GPL v3](LICENSE) с [исключением OpenSSL](LEGAL). Оригинальный README AyuGram: [README.AyuGram.md](README.AyuGram.md).

## English

HeartGram Desktop is an unofficial Telegram client for Windows built on top of AyuGram Desktop, with exteraGram features and Python plugin support.

- Everything from AyuGram Desktop (ghost mode, message history, filters) plus exteraGram-style settings.
- A full-screen Material Design 3 player: queue with every track, synced lyrics, speed panel, player font, and a music-synced background ported from exteraGram's beat tracker. Separate volume for songs and for voice/round messages.
- Python plugins (`.plugin`) with an embedded Python 3.11 runtime and Android-class shims, plus a local safety review with code snippets shown right in the install window.
- A "Plugins" submenu in the message menu (shown, hidden, or with Shift/Ctrl), message details with chat and sender IDs, built-in Animated Messages, an inline calculator, project badges.
- A single-file portable exe and a Lite build without AyuGram's ghost-mode/anti-recall features.

Download the exe from [Releases](../../releases), put it in any folder and run it. Licensed under GNU GPL v3. Not affiliated with Telegram, AyuGram or exteraGram.
