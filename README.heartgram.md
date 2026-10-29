# HeartGram Desktop

Неофициальный клиент Telegram для Windows на базе [AyuGram Desktop](https://github.com/AyuGram/AyuGramDesktop) с функциями [exteraGram](https://exteragram.app) и поддержкой плагинов.
Сборка и изменения: [@whyheartone](https://t.me/whyheartone).

> HeartGram не разрабатывается и не поддерживается Telegram, AyuGram и exteraGram. Используйте на свой риск.

[ Русский | [English](#english) ]

## Что внутри

- **Всё из AyuGram Desktop**: режим призрака, история правок и удалённых, фильтры, оформление.
- **Функции exteraGram**: настройки чатов, боковое меню, навигация, значки, быстрые действия.
- **Плагины Python** (формат `.plugin`, как у exteraGram): установка по клику на файл, компактное окно проверки, плашка «Проверено». Встроенный Python 3.11, Pillow, PyAV и rlottie-python. Заглушки Android-классов, чтобы большинство плагинов с телефона загружались на ПК.
- **Animated Messages** встроено: 12 стилей появления сообщений, пресеты, анимация дат.
- **Значки проекта** (необязательно): сердечки у имён, окно «Поддержать разработку», искорки как в плагине. Данные приходят с вашего сервера значков.
- **Калькулятор в поле ввода**: `2+2=` показывает результат серым, пробел вставляет.
- **Один exe**: Python и модули распаковываются рядом при первом запуске.
- **HeartGram Lite**: тот же клиент без функций AyuGram (призрак, сохранение удалённых, фильтры и т. п.).

## Установка

1. Скачайте `HeartGram.exe` (или `HeartGram-lite.exe`) из [Releases](../../releases).
2. Положите в любую папку и запустите. При первом запуске рядом появятся `python/` и `modules/` (это нормально).
3. Данные аккаунта хранятся в `tdata/` рядом с exe (портативный режим).

## Сборка

Нужны Windows 10/11 x64, Visual Studio 2022 (MSVC 14.44), CMake, Python 3.11 (embeddable) и подготовленные библиотеки, как для [Telegram Desktop](https://github.com/telegramdesktop/tdesktop/blob/dev/docs/building-win.md) (см. `docs/building-win.md`). Дополнительно для плагинов нужен `Libraries/win64/python311` с заголовками и `python311.lib`.

Файл `Telegram/SourceFiles/ayu/features/project_badges/badge_secrets.h` с адресом и ключом сервера значков в репозиторий **не входит**. Без него клиент собирается, но список значков не загружается (используется `badge_secrets.example.h`).

## Благодарности и лицензия

- [Telegram Desktop](https://github.com/telegramdesktop/tdesktop), Telegram FZ-LLC.
- [AyuGram Desktop](https://github.com/AyuGram/AyuGramDesktop), Radolyn Labs.
- [exteraDesktop](https://github.com/metawka/exteraDesktop), [@metawka](https://t.me/metawka), и команда [exteraGram](https://exteragram.app).

Лицензия: [GNU GPL v3](LICENSE) с [исключением OpenSSL](LEGAL). Оригинальный README AyuGram: [README.AyuGram.md](README.AyuGram.md).

## English

HeartGram Desktop is an unofficial Telegram client for Windows built on top of AyuGram Desktop, with exteraGram features and Python plugin support.

- Everything from AyuGram Desktop, plus exteraGram-style settings.
- Python plugins (`.plugin`), with an embedded Python 3.11 runtime and Android-class shims so most phone plugins load on desktop.
- Built-in Animated Messages, project badges with sparkles, an inline calculator, single-file portable exe, and a Lite build without AyuGram's ghost-mode/anti-recall features.

Download the exe from [Releases](../../releases), put it in any folder and run it. Licensed under GNU GPL v3. Not affiliated with Telegram, AyuGram or exteraGram.
