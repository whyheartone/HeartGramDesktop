// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

#include "ui/rp_widget.h"
#include "ui/effects/animations.h"

namespace Ui {
class GenericBox;
} // namespace Ui

class IconPicker : public Ui::RpWidget
{
public:
	IconPicker(QWidget *parent, bool extera = false);
	~IconPicker() = default;

	static constexpr int kColumns = 4;

protected:
	void paintEvent(QPaintEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;

private:
	void drawIcon(QPainter &p, const QImage &icon, int x, int y, float strokeOpacity);
	[[nodiscard]] int cellWidth() const;

	const QVector<QString> *_icons = nullptr;
	int _rows = 0;
	Ui::Animations::Simple _animation;
	QString _wasSelected;
	std::unordered_map<QString, QImage> _cachedIcons;
};

void ShowIconPickerBox(not_null<Ui::GenericBox*> box);
