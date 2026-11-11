# HeartGram Desktop

[ English  |   [Русский](README-RU.md) ]

Unofficial Telegram client for Windows and Linux. A fork of [AyuGram Desktop](https://github.com/AyuGram/AyuGramDesktop)
with [exteraGram](https://exteragram.app) features and Python plugins, based on [Telegram Desktop](https://github.com/telegramdesktop/tdesktop).

> HeartGram is not developed or supported by Telegram, AyuGram or exteraGram. Use at your own risk.

## Features

- Everything from AyuGram Desktop: ghost mode, message history, filters, font and appearance customization
- exteraGram settings: chats, side menu, navigation, quick actions
- Full-screen music player in Material Design 3: queue, lyrics, speed panel, music-synced background
- Separate volume for songs and for voice and round messages
- Python plugins (`.plugin`) with a local safety review and a trusted sources check on install
- "Plugins" submenu in the message menu, message details (IDs, dates, edits, forwards, file)
- Built-in Animated Messages, calculator in the message field

## Downloads

Prebuilt binaries are in the [Releases tab](../../releases).

### Windows

Download the Windows archive, unpack it to any folder and run `HeartGram.exe`.
Data is stored next to the executable (portable mode).

### Linux

Prebuilt Linux binaries are not published yet. Build from source following [docs/building-linux.md](docs/building-linux.md);
the Python plugin runtime is Windows-only for now, so plugins are not available on Linux.

## Building

HeartGram is built like Telegram Desktop, see the instructions in [docs](docs): [Windows](docs/building-win.md),
[Linux](docs/building-linux.md). You will need your own `api_id` and `api_hash`, see the notes in the docs.
GitHub Actions in this repository build the Windows binary for each release.

## Credits and license

- [Telegram Desktop](https://github.com/telegramdesktop/tdesktop), Telegram FZ-LLC
- [AyuGram Desktop](https://github.com/AyuGram/AyuGramDesktop), Radolyn Labs
- [exteraGram](https://exteragram.app) team and [exteraDesktop](https://github.com/metawka/exteraDesktop), [@metawka](https://t.me/metawka)

Licensed under [GNU GPL v3](LICENSE) with the [OpenSSL exception](LEGAL).
