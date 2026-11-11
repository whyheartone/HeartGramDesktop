# HeartGram Desktop

[ [English](README.md)  | Русский ]

Неофициальный клиент Telegram для Windows и Linux. Форк [AyuGram Desktop](https://github.com/AyuGram/AyuGramDesktop)
с функциями [exteraGram](https://exteragram.app) и Python-плагинами, основан на [Telegram Desktop](https://github.com/telegramdesktop/tdesktop).

> HeartGram не разрабатывается и не поддерживается Telegram, AyuGram и exteraGram. Используйте на свой риск.

## Функции и Фишки

- Всё из AyuGram Desktop: режим призрака, история правок и удалённых сообщений, фильтры, кастомизация шрифта и оформления
- Настройки exteraGram: чаты, боковое меню, навигация, быстрые действия
- Полноэкранный плеер в Material Design 3: очередь, текст песни, панель скорости, фон в такт музыке
- Отдельная громкость для песен и для голосовых и кружков
- Python-плагины (`.plugin`) с локальным разбором безопасности и проверкой источника при установке
- Подменю «Плагины» в меню сообщения, «Детали» сообщения (ID, даты, правки, пересылки, файл)
- Встроенные Animated Messages, калькулятор в поле ввода

## Установка

Готовые сборки лежат во вкладке [Releases](../../releases).

### Windows

Скачайте архив для Windows, распакуйте в любую папку и запустите `HeartGram.exe`.
Данные хранятся рядом с программой (портативный режим).

### Linux

Готовые сборки для Linux пока не публикуются. Соберите из исходников по инструкции [docs/building-linux.md](docs/building-linux.md);
среда Python-плагинов пока только для Windows, поэтому плагины на Linux недоступны.

## Сборка

HeartGram собирается так же, как Telegram Desktop, инструкции в [docs](docs): [Windows](docs/building-win.md),
[Linux](docs/building-linux.md). Понадобятся свои `api_id` и `api_hash`, см. заметки в документации.
GitHub Actions в этом репозитории собирают бинарник для Windows на каждый релиз.

## Благодарности и лицензия

- [Telegram Desktop](https://github.com/telegramdesktop/tdesktop), Telegram FZ-LLC
- [AyuGram Desktop](https://github.com/AyuGram/AyuGramDesktop), Radolyn Labs
- Команда [exteraGram](https://exteragram.app) и [exteraDesktop](https://github.com/metawka/exteraDesktop), [@metawka](https://t.me/metawka)

Лицензия: [GNU GPL v3](LICENSE) с [исключением OpenSSL](LEGAL).
