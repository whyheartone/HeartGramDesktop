#include "extera/extera_runtime.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QCryptographicHash>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>

#include <zlib.h>

namespace Extera::Runtime {
namespace {

constexpr auto kEndSignature = quint32(0x06054b50);
constexpr auto kCentralSignature = quint32(0x02014b50);
constexpr auto kLocalSignature = quint32(0x04034b50);
constexpr auto kSignatureFile = "python/.runtime_sig";

[[nodiscard]] quint16 U16(const QByteArray &data, int offset) {
	const auto bytes = reinterpret_cast<const uchar*>(data.constData());
	return quint16(bytes[offset]) | (quint16(bytes[offset + 1]) << 8);
}

[[nodiscard]] quint32 U32(const QByteArray &data, int offset) {
	return quint32(U16(data, offset))
		| (quint32(U16(data, offset + 2)) << 16);
}

struct Archive {
	QFile file;
	qint64 start = 0;
	QByteArray directory;
	int count = 0;
	QByteArray signature;
};

// The zip is appended to the exe; offsets inside it are relative to its own
// start, so the start is recovered from where the end record points.
[[nodiscard]] bool Open(Archive &archive) {
	archive.file.setFileName(QCoreApplication::applicationFilePath());
	if (!archive.file.open(QIODevice::ReadOnly)) {
		return false;
	}
	const auto size = archive.file.size();
	const auto tailSize = std::min<qint64>(size, 65557);
	if (!archive.file.seek(size - tailSize)) {
		return false;
	}
	const auto tail = archive.file.read(tailSize);
	for (auto i = int(tail.size()) - 22; i >= 0; --i) {
		if (U32(tail, i) != kEndSignature) {
			continue;
		}
		const auto comment = int(U16(tail, i + 20));
		if (i + 22 + comment != tail.size()) {
			continue;
		}
		const auto count = int(U16(tail, i + 10));
		const auto directorySize = qint64(U32(tail, i + 12));
		const auto directoryOffset = qint64(U32(tail, i + 16));
		const auto directoryAbsolute = (size - tailSize) + i - directorySize;
		archive.start = directoryAbsolute - directoryOffset;
		if (archive.start < 0 || count <= 0) {
			return false;
		}
		if (!archive.file.seek(directoryAbsolute)) {
			return false;
		}
		archive.directory = archive.file.read(directorySize);
		archive.count = count;
		auto hash = QCryptographicHash(QCryptographicHash::Sha1);
		hash.addData(archive.directory);
		hash.addData(QByteArray::number(size));
		archive.signature = hash.result().toHex();
		return archive.directory.size() == directorySize;
	}
	return false;
}

[[nodiscard]] bool Inflate(
		const QByteArray &packed,
		QByteArray &result,
		qint64 size) {
	result.resize(int(size));
	auto stream = z_stream();
	stream.next_in = reinterpret_cast<Bytef*>(
		const_cast<char*>(packed.constData()));
	stream.avail_in = uInt(packed.size());
	stream.next_out = reinterpret_cast<Bytef*>(result.data());
	stream.avail_out = uInt(result.size());
	if (inflateInit2(&stream, -15) != Z_OK) {
		return false;
	}
	const auto code = inflate(&stream, Z_FINISH);
	inflateEnd(&stream);
	return (code == Z_STREAM_END) && (stream.total_out == size_t(size));
}

[[nodiscard]] bool SafeRelative(const QString &name) {
	return !name.isEmpty()
		&& !name.startsWith('/')
		&& !name.contains(u".."_q)
		&& !name.contains(':');
}

[[nodiscard]] bool Extract(Archive &archive, const QString &root) {
	auto offset = 0;
	for (auto i = 0; i != archive.count; ++i) {
		if (offset + 46 > archive.directory.size()
			|| U32(archive.directory, offset) != kCentralSignature) {
			return false;
		}
		const auto method = int(U16(archive.directory, offset + 10));
		const auto compressed = qint64(U32(archive.directory, offset + 20));
		const auto size = qint64(U32(archive.directory, offset + 24));
		const auto nameLength = int(U16(archive.directory, offset + 28));
		const auto extraLength = int(U16(archive.directory, offset + 30));
		const auto commentLength = int(U16(archive.directory, offset + 32));
		const auto localOffset = qint64(U32(archive.directory, offset + 42));
		const auto name = QString::fromUtf8(
			archive.directory.mid(offset + 46, nameLength));
		offset += 46 + nameLength + extraLength + commentLength;
		if (name.endsWith('/')) {
			continue;
		} else if (!SafeRelative(name)) {
			return false;
		}
		if (!archive.file.seek(archive.start + localOffset)) {
			return false;
		}
		const auto header = archive.file.read(30);
		if (header.size() != 30 || U32(header, 0) != kLocalSignature) {
			return false;
		}
		const auto skip = int(U16(header, 26)) + int(U16(header, 28));
		if (!archive.file.seek(archive.start + localOffset + 30 + skip)) {
			return false;
		}
		const auto packed = archive.file.read(compressed);
		auto data = QByteArray();
		if (method == 0) {
			data = packed;
		} else if (method != 8 || !Inflate(packed, data, size)) {
			return false;
		}
		const auto path = root + name;
		QDir().mkpath(QFileInfo(path).absolutePath());
		auto out = QFile(path);
		if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)
			|| out.write(data) != data.size()) {
			return false;
		}
	}
	return true;
}

} // namespace

bool Ensure() {
	auto archive = Archive();
	if (!Open(archive)) {
		return false;
	}
	const auto root = QCoreApplication::applicationDirPath() + '/';
	auto marker = QFile(root + QString::fromLatin1(kSignatureFile));
	if (marker.open(QIODevice::ReadOnly)
		&& marker.readAll() == archive.signature) {
		return true;
	}
	marker.close();
	if (!Extract(archive, root)) {
		return false;
	}
	auto written = QFile(root + QString::fromLatin1(kSignatureFile));
	if (written.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		written.write(archive.signature);
	}
	return true;
}

} // namespace Extera::Runtime
