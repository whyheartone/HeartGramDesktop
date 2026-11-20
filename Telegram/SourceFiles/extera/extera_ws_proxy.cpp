#include "extera/extera_ws_proxy.h"

#include "core/application.h"
#include "core/core_settings.h"
#include "extera/extera_config.h"
#include "logs.h"
#include "mtproto/mtproto_proxy_data.h"
#include "storage/localstorage.h"

#include <QtCore/QCryptographicHash>
#include <QtCore/QPointer>
#include <QtCore/QRegularExpression>
#include <QtCore/QTimer>
#include <QtNetwork/QHostAddress>
#include <QtNetwork/QSslSocket>
#include <QtNetwork/QTcpServer>
#include <QtNetwork/QTcpSocket>

#include <openssl/evp.h>
#include <openssl/rand.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <memory>

namespace Extera::WsProxy {
namespace {

constexpr auto kHandshakeLen = 64;
constexpr auto kSkipLen = 8;
constexpr auto kPrekeyLen = 32;
constexpr auto kIvLen = 16;
constexpr auto kProtoTagPos = 56;
constexpr auto kDcIdxPos = 60;
constexpr quint32 kProtoAbridged = 0xEFEFEFEFU;
constexpr quint32 kProtoIntermediate = 0xEEEEEEEEU;
constexpr quint32 kProtoPadded = 0xDDDDDDDDU;
constexpr auto kDefaultPort = 14430;
constexpr auto kAttemptTimeout = 5000;
constexpr auto kTcpTimeout = 8000;
constexpr auto kMaxMessage = 16 * 1024 * 1024;

const auto kFrontingSni = u"sprinthost.ru"_q;
const auto kGatewayIp = u"149.154.167.220"_q;

[[nodiscard]] QString DefaultIp(int dc) {
	switch (dc) {
	case 1: return u"149.154.175.50"_q;
	case 2: return u"149.154.167.51"_q;
	case 3: return u"149.154.175.100"_q;
	case 4: return u"149.154.167.91"_q;
	case 5: return u"149.154.171.5"_q;
	case 203: return u"91.105.192.100"_q;
	}
	return QString();
}

[[nodiscard]] const uchar *Bytes(const QByteArray &data) {
	return reinterpret_cast<const uchar*>(data.constData());
}

[[nodiscard]] uchar *Bytes(QByteArray &data) {
	return reinterpret_cast<uchar*>(data.data());
}

[[nodiscard]] QByteArray RandomBytes(int count) {
	auto result = QByteArray(count, Qt::Uninitialized);
	RAND_bytes(Bytes(result), count);
	return result;
}

[[nodiscard]] QByteArray Sha256(
		const QByteArray &a,
		const QByteArray &b) {
	auto hash = QCryptographicHash(QCryptographicHash::Sha256);
	hash.addData(a);
	hash.addData(b);
	return hash.result();
}

[[nodiscard]] QByteArray Reversed(const QByteArray &data) {
	auto result = data;
	std::reverse(result.begin(), result.end());
	return result;
}

class Ctr final {
public:
	Ctr() = default;
	Ctr(const Ctr &other) = delete;
	Ctr &operator=(const Ctr &other) = delete;
	~Ctr() {
		if (_context) {
			EVP_CIPHER_CTX_free(_context);
		}
	}

	void init(const QByteArray &key, const QByteArray &iv) {
		if (_context) {
			EVP_CIPHER_CTX_free(_context);
		}
		_context = EVP_CIPHER_CTX_new();
		EVP_EncryptInit_ex(
			_context,
			EVP_aes_256_ctr(),
			nullptr,
			Bytes(key),
			Bytes(iv));
	}

	[[nodiscard]] QByteArray update(const QByteArray &input) {
		if (!_context || input.isEmpty()) {
			return QByteArray();
		}
		auto output = QByteArray(input.size(), Qt::Uninitialized);
		auto length = 0;
		EVP_EncryptUpdate(
			_context,
			Bytes(output),
			&length,
			Bytes(input),
			int(input.size()));
		output.resize(length);
		return output;
	}

private:
	EVP_CIPHER_CTX *_context = nullptr;

};

// The header for the data center: random 64 bytes, the tail (protocol tag
// and data center number) is encrypted with the same key.
[[nodiscard]] QByteArray RelayInit(
		const QByteArray &protoTag,
		int dcIndex) {
	auto random = QByteArray();
	while (true) {
		random = RandomBytes(kHandshakeLen);
		const auto first = quint8(random[0]);
		if (first == 0xEF) {
			continue;
		}
		const auto start = (quint32(quint8(random[0])) << 24)
			| (quint32(quint8(random[1])) << 16)
			| (quint32(quint8(random[2])) << 8)
			| quint32(quint8(random[3]));
		if (start == 0x48454144U
			|| start == 0x504F5354U
			|| start == 0x47455420U
			|| start == 0xEEEEEEEEU
			|| start == 0xDDDDDDDDU
			|| start == 0x16030102U
			|| start == 0x4F505449U) {
			continue;
		}
		if (random[4] == 0
			&& random[5] == 0
			&& random[6] == 0
			&& random[7] == 0) {
			continue;
		}
		break;
	}
	auto encrypt = Ctr();
	encrypt.init(
		random.mid(kSkipLen, kPrekeyLen),
		random.mid(kSkipLen + kPrekeyLen, kIvLen));
	const auto full = encrypt.update(random);

	auto tail = QByteArray(8, Qt::Uninitialized);
	for (auto i = 0; i != 4; ++i) {
		tail[i] = protoTag[i];
	}
	tail[4] = char(dcIndex & 0xFF);
	tail[5] = char((dcIndex >> 8) & 0xFF);
	const auto extra = RandomBytes(2);
	tail[6] = extra[0];
	tail[7] = extra[1];

	auto result = random;
	for (auto i = 0; i != 8; ++i) {
		const auto keystream = char(
			full[kProtoTagPos + i] ^ random[kProtoTagPos + i]);
		result[kProtoTagPos + i] = char(tail[i] ^ keystream);
	}
	return result;
}

// Cuts the stream into MTProto packets, one per WebSocket frame.
class Splitter final {
public:
	explicit Splitter(quint32 proto) : _proto(proto) {
	}

	[[nodiscard]] QList<QByteArray> split(
			const QByteArray &chunk,
			const QByteArray &plainChunk) {
		auto parts = QList<QByteArray>();
		if (chunk.isEmpty()) {
			return parts;
		}
		if (_disabled) {
			parts.push_back(chunk);
			return parts;
		}
		_cipher.append(chunk);
		_plain.append(plainChunk);
		auto offset = 0;
		while (offset < _cipher.size()) {
			const auto packet = nextLength(offset, _cipher.size() - offset);
			if (packet == -1) {
				break;
			}
			if (packet <= 0) {
				parts.push_back(_cipher.mid(offset));
				offset = int(_cipher.size());
				_disabled = true;
				break;
			}
			parts.push_back(_cipher.mid(offset, packet));
			offset += packet;
		}
		if (offset > 0) {
			_cipher.remove(0, offset);
			_plain.remove(0, offset);
		}
		return parts;
	}

	[[nodiscard]] QByteArray flush() {
		auto result = _cipher;
		_cipher.clear();
		_plain.clear();
		return result;
	}

private:
	// -1: not enough data, 0: unknown format, else the packet length.
	[[nodiscard]] int nextLength(int offset, int available) const {
		if (available <= 0) {
			return -1;
		}
		const auto at = [&](int index) {
			return int(quint8(_plain[offset + index]));
		};
		if (_proto == kProtoAbridged) {
			const auto first = at(0);
			auto payload = 0;
			auto header = 0;
			if (first == 0x7F || first == 0xFF) {
				if (available < 4) {
					return -1;
				}
				payload = (at(1) | (at(2) << 8) | (at(3) << 16)) * 4;
				header = 4;
			} else {
				payload = (first & 0x7F) * 4;
				header = 1;
			}
			if (payload <= 0) {
				return 0;
			}
			const auto packet = header + payload;
			return (available < packet) ? -1 : packet;
		} else if (_proto == kProtoIntermediate || _proto == kProtoPadded) {
			if (available < 4) {
				return -1;
			}
			const auto payload = int(
				(quint32(at(0))
					| (quint32(at(1)) << 8)
					| (quint32(at(2)) << 16)
					| (quint32(at(3)) << 24)) & 0x7FFFFFFFU);
			if (payload <= 0) {
				return 0;
			}
			const auto packet = 4 + payload;
			return (available < packet) ? -1 : packet;
		}
		return 0;
	}

	const quint32 _proto;
	QByteArray _cipher;
	QByteArray _plain;
	bool _disabled = false;

};

[[nodiscard]] QByteArray WsFrame(int opcode, const QByteArray &data) {
	const auto length = data.size();
	auto frame = QByteArray();
	frame.reserve(int(length) + 14);
	frame.append(char(0x80 | opcode));
	if (length < 126) {
		frame.append(char(0x80 | int(length)));
	} else if (length < 65536) {
		frame.append(char(0x80 | 126));
		frame.append(char((length >> 8) & 0xFF));
		frame.append(char(length & 0xFF));
	} else {
		frame.append(char(0x80 | 127));
		for (auto i = 0; i != 8; ++i) {
			frame.append(char((qint64(length) >> (56 - 8 * i)) & 0xFF));
		}
	}
	const auto mask = RandomBytes(4);
	frame.append(mask);
	const auto start = frame.size();
	frame.append(data);
	for (auto i = 0; i != length; ++i) {
		frame[int(start) + i] = char(frame[int(start) + i] ^ mask[i & 3]);
	}
	return frame;
}

struct State {
	std::unique_ptr<QTcpServer> server;
	Stats stats;
};

// WHY: never destroyed, the sockets must not outlive the application object.
[[nodiscard]] State &Global() {
	static auto state = new State();
	return *state;
}

[[nodiscard]] QString SecretHex() {
	auto secret = Extera::String(u"wsproxy_secret"_q);
	if (secret.size() != 32) {
		secret = QString::fromLatin1(RandomBytes(16).toHex());
		Extera::Config::Instance().setString(u"wsproxy_secret"_q, secret);
	}
	return secret;
}

[[nodiscard]] QByteArray SecretBytes() {
	return QByteArray::fromHex(SecretHex().toLatin1());
}

[[nodiscard]] QString ClientSecret() {
	return u"dd"_q + SecretHex();
}

[[nodiscard]] int ConfiguredPort() {
	const auto port = Extera::Int(u"wsproxy_port"_q);
	return (port > 0 && port < 65536) ? port : kDefaultPort;
}

[[nodiscard]] QString DcOverride(int dc) {
	const auto list = Extera::String(u"wsproxy_dc_ips"_q);
	for (const auto &entry : list.split(
			QRegularExpression(u"[,;\\s]+"_q),
			Qt::SkipEmptyParts)) {
		const auto colon = entry.indexOf(':');
		if (colon > 0 && entry.left(colon).trimmed().toInt() == dc) {
			return entry.mid(colon + 1).trimmed();
		}
	}
	return QString();
}

class Connection final : public QObject {
public:
	explicit Connection(not_null<QTcpSocket*> client)
	: QObject(nullptr)
	, _client(client) {
		_client->setParent(this);
		++Global().stats.total;
		QObject::connect(_client, &QTcpSocket::readyRead, this, [=] {
			clientReadable();
		});
		QObject::connect(_client, &QTcpSocket::disconnected, this, [=] {
			clientClosed();
		});
		_client->setSocketOption(QAbstractSocket::LowDelayOption, 1);
		_handshakeTimer.setSingleShot(true);
		QObject::connect(&_handshakeTimer, &QTimer::timeout, this, [=] {
			if (_stage == Stage::Handshake) {
				fail();
			}
		});
		_handshakeTimer.start(10000);
		if (_client->bytesAvailable() > 0) {
			clientReadable();
		}
	}

private:
	enum class Stage {
		Handshake,
		Connecting,
		Bridging,
		Done,
	};
	struct Candidate {
		QString ip;
		QString domain;
		bool fronted = false;
	};

	void clientReadable() {
		if (_stage == Stage::Done) {
			return;
		}
		_clientBuffer.append(_client->readAll());
		if (_stage == Stage::Handshake) {
			if (_clientBuffer.size() >= kHandshakeLen) {
				processHandshake();
			}
		} else if (_stage == Stage::Bridging) {
			processClientData();
		}
	}

	void clientClosed() {
		if (_stage == Stage::Done) {
			return;
		}
		if (_stage == Stage::Bridging && _ws && _wsOpen && _splitter) {
			const auto tail = _splitter->flush();
			if (!tail.isEmpty()) {
				_ws->write(WsFrame(0x2, tail));
				_ws->flush();
			}
		}
		finish();
	}

	void fail() {
		++Global().stats.failed;
		finish();
	}

	void finish() {
		if (_stage == Stage::Done) {
			return;
		}
		_stage = Stage::Done;
		_attemptTimer.stop();
		_handshakeTimer.stop();
		dropWs();
		if (_direct) {
			_direct->disconnect(this);
			_direct->abort();
			_direct->deleteLater();
			_direct = nullptr;
		}
		_client->disconnect(this);
		_client->abort();
		deleteLater();
	}

	void dropWs() {
		if (_ws) {
			_ws->disconnect(this);
			if (_wsOpen) {
				_ws->write(WsFrame(0x8, QByteArray()));
				_ws->flush();
			}
			_ws->abort();
			_ws->deleteLater();
			_ws = nullptr;
		}
		_wsOpen = false;
		_wsBuffer.clear();
	}

	void processHandshake() {
		_handshakeTimer.stop();
		const auto handshake = _clientBuffer.left(kHandshakeLen);
		_clientBuffer.remove(0, kHandshakeLen);

		const auto secret = SecretBytes();
		const auto prekeyIv = handshake.mid(kSkipLen, kPrekeyLen + kIvLen);
		_cltDec.init(
			Sha256(prekeyIv.left(kPrekeyLen), secret),
			prekeyIv.mid(kPrekeyLen, kIvLen));
		const auto decrypted = _cltDec.update(handshake);
		const auto protoTag = decrypted.mid(kProtoTagPos, 4);
		const auto proto = (quint32(quint8(protoTag[0])) << 24)
			| (quint32(quint8(protoTag[1])) << 16)
			| (quint32(quint8(protoTag[2])) << 8)
			| quint32(quint8(protoTag[3]));
		if (proto != kProtoAbridged
			&& proto != kProtoIntermediate
			&& proto != kProtoPadded) {
			LOG(("WsProxy: bad header (wrong secret or protocol)."));
			fail();
			return;
		}
		const auto dcIndex = qint16(
			quint16(quint8(decrypted[kDcIdxPos]))
			| (quint16(quint8(decrypted[kDcIdxPos + 1])) << 8));
		_dc = std::abs(int(dcIndex));
		_media = (dcIndex < 0);
		if (_dc >= 10000) {
			_dc -= 10000;
		}
		_proto = proto;

		_relay = RelayInit(protoTag, _media ? -_dc : _dc);
		const auto rev = Reversed(prekeyIv);
		_cltEnc.init(
			Sha256(rev.left(kPrekeyLen), secret),
			rev.mid(kPrekeyLen, kIvLen));
		_tgEnc.init(
			_relay.mid(kSkipLen, kPrekeyLen),
			_relay.mid(kSkipLen + kPrekeyLen, kIvLen));
		const auto relayRev = Reversed(
			_relay.mid(kSkipLen, kPrekeyLen + kIvLen));
		_tgDec.init(
			relayRev.left(kPrekeyLen),
			relayRev.mid(kPrekeyLen, kIvLen));
		// The header is sent as is, the cipher stream goes on after it.
		(void)_tgEnc.update(QByteArray(kHandshakeLen, '\0'));
		_splitter = std::make_unique<Splitter>(_proto);

		buildCandidates();
		_stage = Stage::Connecting;
		tryNextCandidate();
	}

	void buildCandidates() {
		const auto gateway = (_dc == 203) ? 2 : _dc;
		auto domains = QStringList();
		if (_media) {
			domains.push_back(
				u"kws%1-1.web.telegram.org"_q.arg(gateway));
		}
		domains.push_back(u"kws%1.web.telegram.org"_q.arg(gateway));

		auto ips = QStringList();
		if (const auto custom = DcOverride(_dc); !custom.isEmpty()) {
			ips.push_back(custom);
		}
		if (!ips.contains(kGatewayIp)) {
			ips.push_back(kGatewayIp);
		}
		if (const auto own = DefaultIp(_dc);
			!own.isEmpty() && !ips.contains(own)) {
			ips.push_back(own);
		}
		const auto fronting = Extera::Bool(u"wsproxy_fronting"_q);
		for (const auto &ip : ips) {
			for (const auto &domain : domains) {
				_candidates.push_back({ ip, domain, false });
				if (fronting) {
					_candidates.push_back({ ip, domain, true });
				}
			}
		}
	}

	void tryNextCandidate() {
		if (_stage != Stage::Connecting) {
			return;
		}
		dropWs();
		if (_candidateIndex >= _candidates.size()) {
			startTcpFallback();
			return;
		}
		const auto candidate = _candidates[_candidateIndex++];
		_ws = new QSslSocket(this);
		_ws->setSocketOption(QAbstractSocket::LowDelayOption, 1);
		if (candidate.fronted) {
			_ws->setPeerVerifyMode(QSslSocket::VerifyNone);
		} else {
			_ws->setPeerVerifyMode(QSslSocket::VerifyPeer);
		}
		const auto ws = _ws;
		QObject::connect(ws, &QSslSocket::encrypted, this, [=] {
			sendUpgrade(candidate);
		});
		QObject::connect(ws, &QSslSocket::readyRead, this, [=] {
			wsReadable();
		});
		QObject::connect(
			ws,
			&QSslSocket::errorOccurred,
			this,
			[=](QAbstractSocket::SocketError) {
				wsFailed();
			});
		QObject::connect(
			ws,
			qOverload<const QList<QSslError>&>(&QSslSocket::sslErrors),
			this,
			[=](const QList<QSslError> &errors) {
				if (candidate.fronted) {
					ws->ignoreSslErrors(errors);
				}
			});
		QObject::connect(ws, &QSslSocket::disconnected, this, [=] {
			if (_ws == ws && !_wsOpen) {
				wsFailed();
			} else if (_ws == ws && _wsOpen) {
				finish();
			}
		});
		_attemptTimer.setSingleShot(true);
		_attemptTimer.disconnect(this);
		QObject::connect(&_attemptTimer, &QTimer::timeout, this, [=] {
			if (_stage == Stage::Connecting && _ws == ws && !_wsOpen) {
				wsFailed();
			}
		});
		_attemptTimer.start(kAttemptTimeout);
		ws->connectToHostEncrypted(
			candidate.ip,
			443,
			candidate.fronted ? kFrontingSni : candidate.domain);
	}

	void wsFailed() {
		if (_stage != Stage::Connecting) {
			return;
		}
		tryNextCandidate();
	}

	void sendUpgrade(const Candidate &candidate) {
		const auto key = RandomBytes(16).toBase64();
		const auto request = QByteArray("GET /apiws HTTP/1.1\r\nHost: ")
			+ candidate.domain.toLatin1()
			+ "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
				"Sec-WebSocket-Key: " + key
			+ "\r\nSec-WebSocket-Version: 13\r\n"
				"Sec-WebSocket-Protocol: binary\r\n\r\n";
		_ws->write(request);
	}

	void wsReadable() {
		if (!_ws || _stage == Stage::Done) {
			return;
		}
		_wsBuffer.append(_ws->readAll());
		if (!_wsOpen) {
			const auto end = _wsBuffer.indexOf("\r\n\r\n");
			if (end < 0) {
				if (_wsBuffer.size() > 16384) {
					wsFailed();
				}
				return;
			}
			const auto status = _wsBuffer.left(_wsBuffer.indexOf("\r\n"));
			const auto parts = status.split(' ');
			const auto code = (parts.size() >= 2) ? parts[1].toInt() : 0;
			_wsBuffer.remove(0, end + 4);
			if (code != 101) {
				wsFailed();
				return;
			}
			_attemptTimer.stop();
			_wsOpen = true;
			_stage = Stage::Bridging;
			++Global().stats.ws;
			_ws->write(WsFrame(0x2, _relay));
			processClientData();
		}
		if (_wsOpen) {
			parseFrames();
		}
	}

	void parseFrames() {
		while (_wsOpen && _ws) {
			if (_wsBuffer.size() < 2) {
				return;
			}
			const auto b0 = int(quint8(_wsBuffer[0]));
			const auto b1 = int(quint8(_wsBuffer[1]));
			const auto fin = (b0 & 0x80) != 0;
			const auto opcode = (b0 & 0x0F);
			auto length = qint64(b1 & 0x7F);
			auto offset = 2;
			if (length == 126) {
				if (_wsBuffer.size() < 4) {
					return;
				}
				length = (qint64(quint8(_wsBuffer[2])) << 8)
					| qint64(quint8(_wsBuffer[3]));
				offset = 4;
			} else if (length == 127) {
				if (_wsBuffer.size() < 10) {
					return;
				}
				length = 0;
				for (auto i = 0; i != 8; ++i) {
					length = (length << 8) | qint64(quint8(_wsBuffer[2 + i]));
				}
				offset = 10;
			}
			if (length > kMaxMessage) {
				fail();
				return;
			}
			const auto masked = (b1 & 0x80) != 0;
			auto mask = QByteArray();
			if (masked) {
				if (_wsBuffer.size() < offset + 4) {
					return;
				}
				mask = _wsBuffer.mid(offset, 4);
				offset += 4;
			}
			if (_wsBuffer.size() < offset + length) {
				return;
			}
			auto payload = _wsBuffer.mid(offset, int(length));
			_wsBuffer.remove(0, offset + int(length));
			if (masked) {
				for (auto i = 0; i != payload.size(); ++i) {
					payload[i] = char(payload[i] ^ mask[i & 3]);
				}
			}
			if (opcode == 0x8) {
				finish();
				return;
			} else if (opcode == 0x9) {
				_ws->write(WsFrame(0xA, payload));
				continue;
			} else if (opcode == 0xA) {
				continue;
			}
			if (opcode == 0x0 || opcode == 0x1 || opcode == 0x2) {
				if (fin && _fragments.isEmpty()) {
					deliver(payload);
					continue;
				}
				_fragments.append(payload);
				if (_fragments.size() > kMaxMessage) {
					fail();
					return;
				}
				if (fin) {
					const auto message = _fragments;
					_fragments.clear();
					deliver(message);
				}
			}
		}
	}

	void deliver(const QByteArray &data) {
		Global().stats.down += quint64(data.size());
		const auto plain = _tgDec.update(data);
		_client->write(_cltEnc.update(plain));
	}

	void processClientData() {
		if (_clientBuffer.isEmpty() || _stage != Stage::Bridging) {
			return;
		}
		const auto chunk = _clientBuffer;
		_clientBuffer.clear();
		Global().stats.up += quint64(chunk.size());
		const auto plain = _cltDec.update(chunk);
		const auto encrypted = _tgEnc.update(plain);
		if (_direct) {
			_direct->write(encrypted);
			return;
		}
		if (!_ws || !_wsOpen) {
			return;
		}
		for (const auto &part : _splitter->split(encrypted, plain)) {
			_ws->write(WsFrame(0x2, part));
		}
	}

	void startTcpFallback() {
		if (!Extera::Bool(u"wsproxy_tcp"_q)) {
			fail();
			return;
		}
		const auto ip = DefaultIp(_dc);
		if (ip.isEmpty()) {
			fail();
			return;
		}
		_direct = new QTcpSocket(this);
		_direct->setSocketOption(QAbstractSocket::LowDelayOption, 1);
		const auto direct = _direct;
		QObject::connect(direct, &QTcpSocket::connected, this, [=] {
			if (_stage != Stage::Connecting) {
				return;
			}
			_attemptTimer.stop();
			++Global().stats.tcp;
			_stage = Stage::Bridging;
			direct->write(_relay);
			processClientData();
		});
		QObject::connect(direct, &QTcpSocket::readyRead, this, [=] {
			const auto data = direct->readAll();
			Global().stats.down += quint64(data.size());
			_client->write(_cltEnc.update(_tgDec.update(data)));
		});
		QObject::connect(direct, &QTcpSocket::disconnected, this, [=] {
			if (_stage == Stage::Bridging) {
				finish();
			}
		});
		QObject::connect(
			direct,
			&QTcpSocket::errorOccurred,
			this,
			[=](QAbstractSocket::SocketError) {
				if (_stage == Stage::Connecting) {
					fail();
				} else {
					finish();
				}
			});
		_attemptTimer.setSingleShot(true);
		_attemptTimer.disconnect(this);
		QObject::connect(&_attemptTimer, &QTimer::timeout, this, [=] {
			if (_stage == Stage::Connecting) {
				fail();
			}
		});
		_attemptTimer.start(kTcpTimeout);
		direct->connectToHost(ip, 443);
	}

	const not_null<QTcpSocket*> _client;
	QTcpSocket *_direct = nullptr;
	QSslSocket *_ws = nullptr;
	bool _wsOpen = false;
	Stage _stage = Stage::Handshake;
	QByteArray _clientBuffer;
	QByteArray _wsBuffer;
	QByteArray _fragments;
	QByteArray _relay;
	Ctr _cltDec;
	Ctr _cltEnc;
	Ctr _tgEnc;
	Ctr _tgDec;
	std::unique_ptr<Splitter> _splitter;
	std::vector<Candidate> _candidates;
	size_t _candidateIndex = 0;
	quint32 _proto = 0;
	int _dc = 0;
	bool _media = false;
	QTimer _handshakeTimer;
	QTimer _attemptTimer;

};

[[nodiscard]] int StartServer(int port) {
	auto &state = Global();
	if (state.server && state.server->isListening()) {
		return int(state.server->serverPort());
	}
	state.server = std::make_unique<QTcpServer>();
	auto listening = state.server->listen(QHostAddress::LocalHost, quint16(port));
	if (!listening) {
		LOG(("WsProxy: port %1 is busy, taking a free one.").arg(port));
		listening = state.server->listen(QHostAddress::LocalHost, 0);
	}
	if (!listening) {
		LOG(("WsProxy: could not start: %1").arg(state.server->errorString()));
		state.server = nullptr;
		return -1;
	}
	QObject::connect(state.server.get(), &QTcpServer::newConnection, [] {
		auto &server = Global().server;
		while (server && server->hasPendingConnections()) {
			new Connection(not_null<QTcpSocket*>(server->nextPendingConnection()));
		}
	});
	LOG(("WsProxy: listening on 127.0.0.1:%1.").arg(state.server->serverPort()));
	return int(state.server->serverPort());
}

void StopServer() {
	auto &state = Global();
	if (state.server) {
		state.server->close();
		state.server = nullptr;
	}
}

[[nodiscard]] MTP::ProxyData OurProxy(int port) {
	auto result = MTP::ProxyData();
	result.type = MTP::ProxyData::Type::Mtproto;
	result.host = u"127.0.0.1"_q;
	result.port = uint32(port);
	result.password = ClientSecret();
	return result;
}

void Register(int port, bool on, bool early) {
	auto &settings = Core::App().settings().proxy();
	const auto previous = Extera::Int(u"wsproxy_registered_port"_q);
	if (previous > 0 && previous != port) {
		auto old = OurProxy(previous);
		if (settings.indexInList(old) >= 0) {
			if (settings.selected() == old) {
				settings.setSelected(MTP::ProxyData());
			}
			settings.removeFromList(old);
		}
	}
	const auto data = OurProxy(port);
	if (on) {
		if (settings.indexInList(data) < 0) {
			settings.addToList(data);
		}
		Extera::Config::Instance().setInt(
			u"wsproxy_registered_port"_q,
			port);
		if (early) {
			settings.setSelected(data);
			settings.setSettings(MTP::ProxyData::Settings::Enabled);
		} else {
			Core::App().setCurrentProxy(
				data,
				MTP::ProxyData::Settings::Enabled);
		}
	} else if (!early
		&& settings.isEnabled()
		&& settings.selected() == data) {
		Core::App().setCurrentProxy(
			MTP::ProxyData(),
			MTP::ProxyData::Settings::System);
	}
	if (!early) {
		Local::writeSettings();
	}
}

} // namespace

void OnAppStart() {
	if (!Extera::Bool(u"wsproxy_enabled"_q)) {
		return;
	}
	const auto port = StartServer(ConfiguredPort());
	if (port > 0) {
		Extera::Config::Instance().setInt(u"wsproxy_port"_q, port);
		Register(port, true, true);
	}
}

void SetEnabled(bool on) {
	if (on) {
		const auto port = StartServer(ConfiguredPort());
		if (port > 0) {
			Extera::Config::Instance().setInt(u"wsproxy_port"_q, port);
			Register(port, true, false);
		} else {
			Extera::Config::Instance().setBool(u"wsproxy_enabled"_q, false);
		}
	} else {
		Register(ConfiguredPort(), false, false);
		StopServer();
	}
}

bool Running() {
	return Global().server && Global().server->isListening();
}

Stats CurrentStats() {
	return Global().stats;
}

QString Link() {
	return u"tg://proxy?server=127.0.0.1&port=%1&secret=%2"_q
		.arg(ConfiguredPort())
		.arg(ClientSecret());
}

QString StatusText() {
	const auto &s = Global().stats;
	return u"%1 | WebSocket: %2, TCP: %3, errors: %4 | %5 / %6 KB"_q
		.arg(Running() ? u"on"_q : u"off"_q)
		.arg(s.ws)
		.arg(s.tcp)
		.arg(s.failed)
		.arg(s.up / 1024)
		.arg(s.down / 1024);
}

} // namespace Extera::WsProxy
