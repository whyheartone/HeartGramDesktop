// Guesses which device a voice message or a round video came from, by the
// metadata the sending app wrote into the file: the Opus vendor and tags in
// an Ogg voice, the brand, handler names and vendor strings of an MP4.
#include "extera/extera_media_origin.h"

#include "apiwrap.h"
#include "base/flat_map.h"
#include "base/flat_set.h"
#include "data/data_document.h"
#include "data/data_session.h"
#include "main/main_session.h"
#include "mtproto/facade.h"
#include "data/data_document_media.h"

#include <QtCore/QFile>
#include <QtCore/QRegularExpression>

namespace Extera {
namespace {

constexpr auto kHead = 64 * 1024;
constexpr auto kTail = 64 * 1024;
constexpr auto kChunk = 64 * 1024;

[[nodiscard]] QByteArray ReadBytes(not_null<DocumentData*> document) {
	if (const auto media = document->createMediaView()) {
		const auto bytes = media->bytes();
		if (!bytes.isEmpty()) {
			return bytes;
		}
	}
	const auto path = document->filepath(true);
	if (!path.isEmpty()) {
		// Only the ends of the file: the metadata is there, and a video
		// can be gigabytes.
		auto file = QFile(path);
		if (file.open(QIODevice::ReadOnly)) {
			auto result = file.read(kHead);
			if (file.size() > kHead) {
				file.seek(std::max<qint64>(kHead, file.size() - kTail));
				result += file.read(kTail);
			}
			return result;
		}
	}
	return {};
}

[[nodiscard]] quint32 ReadLe32(const QByteArray &bytes, int at) {
	if (at < 0 || at + 4 > bytes.size()) {
		return 0;
	}
	const auto *data = reinterpret_cast<const uchar*>(bytes.constData()) + at;
	return quint32(data[0])
		| (quint32(data[1]) << 8)
		| (quint32(data[2]) << 16)
		| (quint32(data[3]) << 24);
}

// "OpusTags", vendor string, then "KEY=value" comments.
[[nodiscard]] QStringList OpusStrings(const QByteArray &bytes) {
	auto result = QStringList();
	const auto at = bytes.left(kHead).indexOf("OpusTags");
	if (at < 0) {
		return result;
	}
	auto position = at + 8;
	const auto vendorLength = int(ReadLe32(bytes, position));
	position += 4;
	if (vendorLength < 0 || vendorLength > 512 || position + vendorLength > bytes.size()) {
		return result;
	}
	result.push_back(QString::fromUtf8(bytes.mid(position, vendorLength)));
	position += vendorLength;
	const auto count = int(ReadLe32(bytes, position));
	position += 4;
	for (auto i = 0; i != std::min(count, 16); ++i) {
		const auto length = int(ReadLe32(bytes, position));
		position += 4;
		if (length < 0 || length > 512 || position + length > bytes.size()) {
			break;
		}
		result.push_back(QString::fromUtf8(bytes.mid(position, length)));
		position += length;
	}
	return result;
}

// MP4: the major brand, plus every readable word that tells about the
// encoder or the phone (the metadata sits in the head or in the tail).
[[nodiscard]] QStringList Mp4Strings(const QByteArray &bytes) {
	auto result = QStringList();
	if (bytes.size() > 12 && bytes.mid(4, 4) == "ftyp") {
		result.push_back(u"brand "_q + QString::fromLatin1(bytes.mid(8, 4)).trimmed());
	}
	const auto scan = QString::fromLatin1(bytes.left(kHead))
		+ QChar(' ')
		+ QString::fromLatin1(bytes.right(kTail));
	static const auto pattern = QRegularExpression(
		u"(Lavf[0-9.]+|Lavc[0-9.]+|Core Media (?:Video|Audio)|VideoHandle|SoundHandle"
		"|com\\.apple\\.quicktime\\.[a-z]+|com\\.android\\.[a-z.]+"
		"|iPhone ?[0-9]{1,2}(?:,[0-9])?(?: Pro)?(?: Max| Plus| mini)?"
		"|iPad[0-9,]*|Android [0-9.]+|SM-[A-Z][0-9]{3}[A-Z0-9]*"
		"|Pixel [0-9]+[A-Za-z ]{0,6}|Xiaomi|Redmi [A-Za-z0-9 ]{1,12}"
		"|Telegram[ A-Za-z0-9.]{0,20}|Samsung)"_q);
	auto match = pattern.globalMatch(scan);
	while (match.hasNext() && result.size() < 8) {
		const auto text = match.next().captured(1).trimmed();
		if (!result.contains(text)) {
			result.push_back(text);
		}
	}
	return result;
}

[[nodiscard]] QString GuessDevice(const QStringList &strings) {
	const auto all = strings.join(QChar('\n'));
	const auto has = [&](const char *pattern) {
		return QRegularExpression(
			QString::fromLatin1(pattern),
			QRegularExpression::CaseInsensitiveOption).match(all).hasMatch();
	};
	const auto model = [&](const char *pattern) {
		const auto m = QRegularExpression(QString::fromLatin1(pattern)).match(all);
		return m.hasMatch() ? m.captured(0).trimmed() : QString();
	};
	if (has("iPhone|iPad|iPod|com\\.apple|Core Media|\\bAppleTV\\b")) {
		const auto name = model("iPhone ?[0-9]{1,2}(?: Pro)?(?: Max| Plus| mini)?|iPad[0-9,]*");
		return name.isEmpty() ? u"iPhone / iPad"_q : (u"iPhone / iPad ("_q + name + u")"_q);
	} else if (has("Android|VideoHandle|SoundHandle|SM-[A-Z][0-9]{3}|Pixel [0-9]|Xiaomi|Redmi|Samsung|com\\.android")) {
		const auto name = model("SM-[A-Z][0-9]{3}[A-Z0-9]*|Pixel [0-9]+[A-Za-z ]{0,6}|Redmi [A-Za-z0-9 ]{1,12}");
		return name.isEmpty() ? u"Android"_q : (u"Android ("_q + name + u")"_q);
	} else if (has("Telegram Desktop|tdesktop")) {
		return u"ПК (Telegram Desktop)"_q;
	} else if (has("Lavf|Lavc|ffmpeg")) {
		return u"ПК или сторонний клиент"_q;
	}
	return QString();
}

[[nodiscard]] base::flat_map<DocumentId, MediaOrigin> &Known() {
	static auto result = base::flat_map<DocumentId, MediaOrigin>();
	return result;
}

[[nodiscard]] base::flat_set<DocumentId> &Fetching() {
	static auto result = base::flat_set<DocumentId>();
	return result;
}

MediaOrigin Analyze(not_null<DocumentData*> document, const QByteArray &bytes) {
	const auto strings = document->isVoiceMessage()
		? OpusStrings(bytes)
		: Mp4Strings(bytes);
	auto result = MediaOrigin();
	result.raw = strings.join(u"; "_q);
	result.device = GuessDevice(strings);
	return result;
}

} // namespace

bool HasOrigin(not_null<DocumentData*> document) {
	return document->isVoiceMessage()
		|| document->isVideoMessage()
		|| document->isVideoFile()
		|| document->isAnimation();
}

std::optional<MediaOrigin> DetectOrigin(not_null<DocumentData*> document) {
	if (!HasOrigin(document)) {
		return std::nullopt;
	}
	const auto bytes = ReadBytes(document);
	if (!bytes.isEmpty()) {
		return Analyze(document, bytes);
	}
	const auto &known = Known();
	const auto i = known.find(document->id);
	return (i != known.end())
		? std::make_optional(i->second)
		: std::nullopt;
}

void FetchOrigin(not_null<DocumentData*> document, Fn<void()> done) {
	if (!HasOrigin(document)
		|| Known().contains(document->id)
		|| Fetching().contains(document->id)) {
		if (done && Known().contains(document->id)) {
			done();
		}
		return;
	}
	Fetching().emplace(document->id);
	const auto session = &document->session();
	const auto id = document->id;
	const auto dc = document->getDC();
	const auto size = document->size;
	auto input = std::optional<MTPInputFileLocation>();
	document->mtpInput().match([&](const MTPDinputDocument &data) {
		input = MTP_inputDocumentFileLocation(
			data.vid(),
			data.vaccess_hash(),
			data.vfile_reference(),
			MTP_string());
	}, [](const auto &) {});
	if (!dc || !input) {
		Fetching().remove(id);
		return;
	}
	struct State {
		QByteArray head;
		QByteArray tail;
		int pending = 0;
	};
	const auto state = std::make_shared<State>();
	const auto finish = [=] {
		if (--state->pending > 0) {
			return;
		}
		Fetching().remove(id);
		auto origin = MediaOrigin();
		if (const auto document = session->data().document(id)) {
			origin = Analyze(document, state->head + state->tail);
		}
		LOG(("Media origin: %1 + %2 bytes, device '%3', raw '%4'."
			).arg(state->head.size()
			).arg(state->tail.size()
			).arg(origin.device
			).arg(origin.raw));
		Known().emplace(id, std::move(origin));
		if (done) {
			done();
		}
	};
	const auto ask = [=](int64 offset, bool head) {
		++state->pending;
		session->api().request(MTPupload_GetFile(
			MTP_flags(0),
			*input,
			MTP_long(offset),
			MTP_int(kChunk)
		)).done([=](const MTPupload_File &result) {
			result.match([&](const MTPDupload_file &data) {
				(head ? state->head : state->tail) = QByteArray(
					data.vbytes().v.constData(),
					data.vbytes().v.size());
			}, [](const auto &) {});
			finish();
		}).fail([=](const MTP::Error &error) {
			LOG(("Media origin: request failed, %1.").arg(error.type()));
			finish();
		}).toDC(MTP::downloadDcId(dc, 0)).send();
	};
	ask(0, true);
	if (size > kChunk * 2) {
		// A multiple of the chunk, so it never crosses a megabyte border.
		ask(((size - 1) / kChunk) * kChunk, false);
	}
}

} // namespace Extera
