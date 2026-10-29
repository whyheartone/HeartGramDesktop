#pragma once

#include <QtGui/QColor>
#include <QtGui/QImage>

#include <array>

class DocumentData;

namespace Window {
class SessionController;
} // namespace Window

namespace Extera {

struct PlayerCover {
	QImage image;
	QRgb seed = 0;
	bool has = false;
};

struct PlayerPalette {
	QColor background;
	QColor surface;
	QColor text;
	QColor textSecondary;
	QColor accent;
	QColor onAccent;
	QColor tonal;
	QColor onTonal;
	QColor track;
	std::array<QColor, 4> glow;
};

// PlayerColors.fromSeed: Lab tones of the seed colour of the cover.
[[nodiscard]] PlayerPalette MakePalette(QRgb seed, bool light);
[[nodiscard]] QRgb DefaultPlayerSeed();
[[nodiscard]] bool PlayerLightTheme();

// May call back right away (cached) or later; the caller guards its own lifetime.
void LoadPlayerCover(
	not_null<DocumentData*> document,
	FullMsgId context,
	int sizePx,
	Fn<void(const PlayerCover &)> done);

void ShowPlayerBox(not_null<Window::SessionController*> controller);
void ShowPlayerScreen(not_null<Window::SessionController*> controller);
void ShowPlayerSettingsBox(
	not_null<Window::SessionController*> controller);

} // namespace Extera
