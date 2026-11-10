#pragma once

#include <QtCore/QCoreApplication>
#include <QtCore/QFile>

// A "lite" copy is the same exe with a marker appended after the PE image
// (or a heartgram.lite next to it): AyuGram's ghost mode, filters and saved
// deleted messages stay off and its promo is hidden.
namespace HeartGram {

#ifdef HEARTGRAM_LITE
// The real Lite build (cmake -D HEARTGRAM_LITE=ON) doesn't compile those
// features at all and is always lite.
[[nodiscard]] constexpr bool IsLite() {
	return true;
}
#else // HEARTGRAM_LITE
[[nodiscard]] inline bool IsLite() {
	static const auto result = [] {
		static constexpr auto marker = "HEARTGRAM-LITE-BUILD";
		static constexpr auto length = qint64(20);
		auto exe = QFile(QCoreApplication::applicationFilePath());
		if (exe.open(QIODevice::ReadOnly)
			&& exe.size() > length
			&& exe.seek(exe.size() - length)
			&& exe.read(length) == QByteArray(marker)) {
			return true;
		}
		return QFile::exists(
			QCoreApplication::applicationDirPath() + u"/heartgram.lite"_q);
	}();
	return result;
}
#endif // HEARTGRAM_LITE

} // namespace HeartGram
