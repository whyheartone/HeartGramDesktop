#include "extera/extera_url_repair.h"

#ifdef Q_OS_WIN

#include <QtCore/QDir>
#include <QtCore/QFileInfo>

#include <windows.h>

namespace Extera {
namespace {

[[nodiscard]] QString ReadDefault(const QString &key) {
	const auto wkey = key.toStdWString();
	auto size = DWORD(0);
	if (RegGetValueW(
			HKEY_CURRENT_USER,
			wkey.c_str(),
			nullptr,
			RRF_RT_REG_SZ,
			nullptr,
			nullptr,
			&size) != ERROR_SUCCESS || size < 2) {
		return QString();
	}
	auto buffer = std::wstring(size / sizeof(wchar_t), L'\0');
	if (RegGetValueW(
			HKEY_CURRENT_USER,
			wkey.c_str(),
			nullptr,
			RRF_RT_REG_SZ,
			nullptr,
			buffer.data(),
			&size) != ERROR_SUCCESS) {
		return QString();
	}
	return QString::fromWCharArray(buffer.c_str());
}

bool WriteDefault(const QString &key, const QString &value) {
	const auto wkey = key.toStdWString();
	const auto wvalue = value.toStdWString();
	return RegSetKeyValueW(
		HKEY_CURRENT_USER,
		wkey.c_str(),
		nullptr,
		REG_SZ,
		wvalue.c_str(),
		DWORD((wvalue.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
}

// The executable is the first quoted word, or the first word.
[[nodiscard]] QString ExecutableOf(const QString &command) {
	const auto trimmed = command.trimmed();
	if (trimmed.startsWith('"')) {
		const auto end = trimmed.indexOf('"', 1);
		return (end > 1) ? trimmed.mid(1, end - 1) : QString();
	}
	return trimmed.section(' ', 0, 0);
}

} // namespace

int RepairStaleUrlHandlers(
		const QString &executable,
		const QString &arguments) {
	const auto native = QDir::toNativeSeparators(executable);
	const auto command = '"' + native + "\" " + arguments + " -- \"%1\"";
	auto fixed = 0;
	for (const auto &progId : {
		u"tdesktop.tg"_q,
		u"tdesktop.tonsite"_q,
		u"AyuGram.tg"_q,
	}) {
		const auto base = u"Software\Classes\\"_q + progId;
		const auto current = ReadDefault(base + u"\shell\open\command"_q);
		if (current.isEmpty()) {
			continue; // Not registered at all, nothing is broken.
		}
		const auto target = ExecutableOf(current);
		if (target.isEmpty() || QFileInfo::exists(target)) {
			continue;
		}
		if (WriteDefault(base + u"\shell\open\command"_q, command)) {
			WriteDefault(
				base + u"\DefaultIcon"_q,
				'"' + native + ",1\"");
			++fixed;
		}
	}
	return fixed;
}

} // namespace Extera

#else // Q_OS_WIN

namespace Extera {

int RepairStaleUrlHandlers(const QString &, const QString &) {
	return 0;
}

} // namespace Extera

#endif // Q_OS_WIN
