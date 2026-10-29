// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

namespace Window {
class SessionController;
}

namespace Ui {

class GenericBox;

void FillDonateInfoBox(not_null<Ui::GenericBox*> box, not_null<Window::SessionController*> controller);

// The same box for HeartGram: amount and contact come from the badge list.
void FillProjectDonateInfoBox(
	not_null<Ui::GenericBox*> box,
	not_null<Window::SessionController*> controller,
	const QString &rubles,
	const QString &ton,
	const QString &contact);

} // namespace Ui
