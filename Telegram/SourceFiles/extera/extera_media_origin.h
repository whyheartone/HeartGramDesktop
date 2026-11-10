#pragma once

class DocumentData;

namespace Extera {

struct MediaOrigin {
	// A guess like "Android" or "iPhone (iPhone 14 Pro)", empty when the
	// metadata does not say.
	QString device;
	// What the guess is built from: encoder, handler names, brands.
	QString raw;
};

// A voice message or a round video: the only kinds this looks at.
[[nodiscard]] bool HasOrigin(not_null<DocumentData*> document);

// Reads the metadata of the file if it is on the disk, or was fetched
// before. std::nullopt means it is not known yet.
[[nodiscard]] std::optional<MediaOrigin> DetectOrigin(
	not_null<DocumentData*> document);

// Asks Telegram for the first and the last 64 KB of the file (not the
// whole video), reads the metadata from them and remembers it. Does nothing
// if it is already known or being fetched; "done" is called when known.
void FetchOrigin(not_null<DocumentData*> document, Fn<void()> done = nullptr);

} // namespace Extera
