// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/ui/ayu_logo.h"

#include "ayu/ayu_settings.h"
#include "styles/style_ayu_styles.h"
#include "ui/rect.h"

#include "ui/painter.h"

#include <QBuffer>
#include <QDataStream>
#include <QSvgRenderer>

static QString LAST_LOADED_NAME;
static QImage LAST_LOADED;
static QImage LAST_LOADED_PAD;

namespace AyuAssets {

QImage CreateImage(const QString &name, const QSize resultImageSize, const int padding = 0);

bool IsExteraIcon(const QString &name) {
	return name.startsWith(EXTERA_PREFIX);
}

// No ICO image plugin is linked, so the .ico is one PNG-compressed entry.
void WriteIco(const QImage &image, const QString &path) {
	auto png = QByteArray();
	auto buffer = QBuffer(&png);
	buffer.open(QIODevice::WriteOnly);
	auto plain = image;
	plain.setDevicePixelRatio(1.);
	plain.scaled(256, 256, Qt::KeepAspectRatio, Qt::SmoothTransformation)
		.save(&buffer, "PNG");
	auto data = QByteArray();
	auto stream = QDataStream(&data, QIODevice::WriteOnly);
	stream.setByteOrder(QDataStream::LittleEndian);
	stream << quint16(0) << quint16(1) << quint16(1);
	stream << quint8(0) << quint8(0) << quint8(0) << quint8(0);
	stream << quint16(1) << quint16(32);
	stream << quint32(png.size()) << quint32(6 + 16);
	data.append(png);
	auto file = QFile(path);
	if (file.open(QIODevice::WriteOnly)) {
		file.write(data);
	}
}

QString appIcoPath() {
	const auto &settings = AyuSettings::getInstance();
	return cWorkingDir()
		+ u"tdata/AyuGram-"_q
		+ settings.appIcon()
		+ u".ico"_q;
}

void loadAppIco() {
	const auto &settings = AyuSettings::getInstance();
	const auto iconPath = appIcoPath();

	auto f = QFile(iconPath);
	if (f.exists()) {
		f.setPermissions(QFile::WriteOther);
		f.remove();
	}
	f.close();
	if (IsExteraIcon(settings.appIcon())) {
		WriteIco(CreateImage(settings.appIcon(), Size(256)), iconPath);
		return;
	}
	QFile::copy(qsl(":/gui/art/ayu/%1/app_icon.ico").arg(settings.appIcon()), iconPath);
}

QImage CreateImage(const QString &name, const QSize resultImageSize, const int padding) {
	const auto iconSize = resultImageSize.shrunkBy(QMargins(padding, padding, padding, padding));

	if (IsExteraIcon(name)) {
		const auto source = QImage(
			qsl(":/gui/art/extera_icons/%1.png").arg(name.mid(EXTERA_PREFIX.size())));
		if (source.isNull()) {
			return {};
		}
		auto res = QImage(
			resultImageSize * style::DevicePixelRatio(),
			QImage::Format_ARGB32_Premultiplied);
		res.setDevicePixelRatio(style::DevicePixelRatio());
		res.fill(Qt::transparent);
		{
			auto p = QPainter(&res);
			auto hq = PainterHighQualityEnabler(p);
			const auto rect = QRect(QPoint(padding, padding), iconSize);
			auto path = QPainterPath();
			path.addRoundedRect(rect, rect.width() / 2., rect.height() / 2.);
			p.setClipPath(path);
			p.drawImage(rect, source);
		}
		return res;
	}

	const auto pngPath = qsl(":/gui/art/ayu/%1/app.png").arg(name);
	if (QFile::exists(pngPath)) {
		const auto loaded = QImage(pngPath).scaled(iconSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
		auto res = QImage(
			resultImageSize * style::DevicePixelRatio(),
			QImage::Format_ARGB32_Premultiplied);
		res.setDevicePixelRatio(style::DevicePixelRatio());
		res.fill(Qt::transparent);
		{
			auto p = QPainter(&res);
			p.drawImage(QRect(padding, padding, iconSize.width(), iconSize.height()), loaded);
		}
		return res;
	}

	const auto svgPath = qsl(":/gui/art/ayu/%1/app.svg").arg(name);
	if (!QFile::exists(svgPath)) {
		return {};
	}

	auto svg = QSvgRenderer(svgPath);
	auto image = QImage(
		resultImageSize * style::DevicePixelRatio(),
		QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(style::DevicePixelRatio());
	image.fill(Qt::transparent);
	{
		auto p = QPainter(&image);

		QPainterPath path;
		path.addRoundedRect(
			QRect(padding, padding, iconSize.width(), iconSize.height()),
			iconSize.width() / 2.0f,
			iconSize.height() / 2.0f
		);

		p.save();

		p.setRenderHint(QPainter::Antialiasing, true);
		p.setClipPath(path);
		p.setRenderHint(QPainter::Antialiasing, false);

		svg.render(&p, QRect(padding, padding, iconSize.width(), iconSize.height()));

		p.restore();
	}
	return image;
}

void loadIcons() {
	const auto &settings = AyuSettings::getInstance();
	if (LAST_LOADED_NAME != settings.appIcon()) {
		LAST_LOADED_NAME = settings.appIcon();
		LAST_LOADED = CreateImage(settings.appIcon(), Size(256));
		LAST_LOADED_PAD = CreateImage(settings.appIcon(), Size(256), 12);
	}
}

QImage loadPreview(const QString &name) {
	return CreateImage(name, Size(st::iconPickerIconSize), st::iconPickerImagePadding);
}

QString currentAppLogoName() {
	return LAST_LOADED_NAME;
}

QImage currentAppLogo() {
	loadIcons();
	return LAST_LOADED;
}

QImage currentAppLogoPad() {
	loadIcons();
	return LAST_LOADED_PAD;
}

}
