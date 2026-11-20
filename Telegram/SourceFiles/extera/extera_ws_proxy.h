// A local MTProto proxy that carries Telegram traffic to the data centers
// over WebSocket (kws*.web.telegram.org), to get around blocks by IP and by
// protocol. The scheme is that of tg-ws-proxy (Flowseal, MIT):
// https://github.com/Flowseal/tg-ws-proxy.
// It listens on 127.0.0.1 only and talks only to Telegram addresses.
#pragma once

namespace Extera::WsProxy {

struct Stats {
	quint64 total = 0;
	quint64 ws = 0;
	quint64 tcp = 0;
	quint64 failed = 0;
	quint64 up = 0;
	quint64 down = 0;
};

// At application start, before the global proxy is applied: when the bypass
// is on, starts the server and selects our proxy.
void OnAppStart();

// The switch in the settings.
void SetEnabled(bool on);

[[nodiscard]] bool Running();
[[nodiscard]] Stats CurrentStats();
[[nodiscard]] QString Link();
[[nodiscard]] QString StatusText();

} // namespace Extera::WsProxy
