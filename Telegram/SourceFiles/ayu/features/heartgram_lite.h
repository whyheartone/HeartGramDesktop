#pragma once

#include <QtCore/QCoreApplication>
#include <QtCore/QFile>

// A "lite" copy is the same exe with a marker appended after the PE image
// (or a heartgram.lite next to it): AyuGram's ghost mode, filters and saved
// deleted messages stay off and its promo is hidden.
namespace HeartGram {

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

} // namespace HeartGram
