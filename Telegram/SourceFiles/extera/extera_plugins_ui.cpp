#include "extera/extera_plugins_ui.h"

#include "apiwrap.h"
#include "boxes/sticker_set_box.h"
#include "data/stickers/data_custom_emoji.h"
#include "ui/text/text_custom_emoji.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_origin.h"
#include "data/data_session.h"
#include "data/stickers/data_stickers.h"
#include "data/stickers/data_stickers_set.h"
#include "main/main_session.h"
#include "ui/image/image.h"
#include "ui/text/text_entity.h"
#include "core/application.h"
#include "styles/style_chat_helpers.h"
#include "ayu/features/project_badges/project_badges.h"
#include "extera/extera_text.h"
#include <QtCore/QCryptographicHash>
#include "styles/style_boxes.h"
#include "ui/chat/attach/attach_prepare.h"
#include "api/api_common.h"
#include "window/window_peer_menu.h"
#include "storage/storage_media_prepare.h"
#include "storage/localimageloader.h"
#include "ui/effects/animations.h"
#include "core/ui_integration.h"
#include "core/file_utilities.h"
#include "extera/extera_plugins.h"
#include "ui/painter.h"
#include "extera/extera_config.h"
#include "history/history.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "settings/settings_common.h"
#include "ui/boxes/confirm_box.h"
#include "ui/boxes/single_choice_box.h"
#include "ui/layers/generic_box.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/rp_widget.h"
#include "ui/effects/animation_value.h"
#include <map>
#include "ui/widgets/popup_menu.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "window/main_window.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"

#include <QtCore/QJsonArray>
#include <QtCore/QFile>
#include <QtGui/QGuiApplication>
#include <QtGui/QClipboard>
#include <QtSvg/QSvgRenderer>
#include <QtCore/QJsonObject>

namespace Extera::Plugins {
namespace {

[[nodiscard]] QString Subtitle(const QJsonObject &info) {
	auto parts = QStringList();
	const auto version = info.value(u"version"_q).toString();
	const auto author = info.value(u"author"_q).toString();
	if (!version.isEmpty()) {
		parts.push_back(u"v"_q + version);
	}
	if (!author.isEmpty()) {
		parts.push_back(author);
	}
	return parts.join(QString::fromUtf8(" \xE2\x80\xA2 "));
}

[[nodiscard]] TextWithEntities WithLinks(const QString &text);

// The version stays quiet, the author is a bold link.
[[nodiscard]] TextWithEntities SubtitleMarked(const QJsonObject &info) {
	const auto version = info.value(u"version"_q).toString();
	const auto author = info.value(u"author"_q).toString();
	auto prefix = QString();
	if (!version.isEmpty()) {
		prefix = u"v"_q + version;
		if (!author.isEmpty()) {
			prefix += QString::fromUtf8(" \xE2\x80\xA2 ");
		}
	}
	auto result = TextWithEntities();
	result.append(prefix);
	auto linked = WithLinks(author);
	const auto offset = result.text.size();
	const auto length = linked.text.size();
	result.append(std::move(linked));
	if (length > 0) {
		result.entities.push_back(EntityInText(
			EntityType::Bold,
			offset,
			length));
	}
	return result;
}

void ShowInput(
		not_null<Window::SessionController*> controller,
		const QString &title,
		const QString &value,
		bool multiline,
		Fn<void(QString)> done) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(title));
		const auto field = box->addRow(object_ptr<Ui::InputField>(
			box,
			multiline
				? st::defaultInputField
				: st::defaultInputField,
			multiline
				? Ui::InputField::Mode::MultiLine
				: Ui::InputField::Mode::SingleLine,
			rpl::single(QString()),
			value));
		box->setFocusCallback([=] { field->setFocusFast(); });
		const auto submit = [=] {
			done(field->getLastText().trimmed());
			box->closeBox();
		};
		field->submits() | rpl::on_next(submit, field->lifetime());
		box->addButton(tr::lng_settings_save(), submit);
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	}));
}

void FillSettings(
		not_null<Ui::VerticalLayout*> container,
		not_null<Window::SessionController*> controller,
		const QString &id) {
	const auto items = Call(u"settings"_q, QJsonObject{
		{ u"id"_q, id },
	}).toArray();
	const auto changed = [=](int index, QJsonValue value) {
		Call(u"setting_changed"_q, QJsonObject{
			{ u"id"_q, id },
			{ u"index"_q, index },
			{ u"value"_q, value },
		});
	};
	Ui::AddSkip(container);
	for (const auto &entry : items) {
		const auto item = entry.toObject();
		const auto kind = item.value(u"kind"_q).toString();
		const auto index = item.value(u"index"_q).toInt();
		const auto text = item.value(u"text"_q).toString();
		const auto subtext = item.value(u"subtext"_q).toString();
		if (kind == u"header"_q) {
			Ui::AddSkip(container);
			Ui::AddSubsectionTitle(container, rpl::single(text));
		} else if (kind == u"divider"_q) {
			if (text.isEmpty()) {
				Ui::AddSkip(container);
				Ui::AddDivider(container);
			} else {
				Ui::AddDividerText(container, rpl::single(text));
			}
		} else if (kind == u"switch"_q) {
			const auto button = container->add(
				object_ptr<Ui::SettingsButton>(
					container,
					rpl::single(text),
					st::settingsButtonNoIcon));
			button->toggleOn(rpl::single(item.value(u"value"_q).toBool()));
			button->toggledChanges(
			) | rpl::on_next([=](bool value) {
				changed(index, value);
			}, button->lifetime());
			if (!subtext.isEmpty()) {
				container->add(
					object_ptr<Ui::FlatLabel>(
						container,
						rpl::single(subtext),
						st::exteraSubtitle),
					st::exteraPluginSubtextPadding);
			}
		} else if (kind == u"selector"_q) {
			auto options = std::vector<QString>();
			for (const auto &option : item.value(u"items"_q).toArray()) {
				options.push_back(option.toString());
			}
			const auto current = std::make_shared<rpl::variable<int>>(
				item.value(u"value"_q).toInt());
			const auto label = current->value(
			) | rpl::map([=](int value) {
				return (value >= 0 && value < int(options.size()))
					? options[value]
					: QString();
			});
			const auto button = Settings::AddButtonWithLabel(
				container,
				rpl::single(text),
				label,
				st::settingsButtonNoIcon);
			button->setClickedCallback([=] {
				controller->show(Box([=](not_null<Ui::GenericBox*> box) {
					SingleChoiceBox(box, {
						.title = rpl::single(text),
						.options = options,
						.initialSelection = current->current(),
						.callback = [=](int value) {
							*current = value;
							changed(index, value);
						},
					});
				}));
			});
		} else if (kind == u"input"_q || kind == u"edit_text"_q) {
			const auto title = (kind == u"edit_text"_q)
				? item.value(u"hint"_q).toString()
				: text;
			const auto current = std::make_shared<rpl::variable<QString>>(
				item.value(u"value"_q).toString());
			const auto secret = item.value(u"key"_q).toString().contains(
				u"key"_q,
				Qt::CaseInsensitive);
			const auto label = current->value(
			) | rpl::map([=](const QString &value) {
				return (secret && !value.isEmpty())
					? QString(std::min(int(value.size()), 12), QChar(0x2022))
					: value.left(40);
			});
			const auto button = Settings::AddButtonWithLabel(
				container,
				rpl::single(title),
				label,
				st::settingsButtonNoIcon);
			const auto multiline = item.value(u"multiline"_q).toBool();
			button->setClickedCallback([=] {
				ShowInput(controller, title, current->current(), multiline, [=](
						QString value) {
					*current = value;
					changed(index, value);
				});
			});
			if (!subtext.isEmpty()) {
				container->add(
					object_ptr<Ui::FlatLabel>(
						container,
						rpl::single(subtext),
						st::exteraSubtitle),
					st::exteraPluginSubtextPadding);
			}
		} else if (kind == u"text"_q) {
			const auto red = item.value(u"red"_q).toBool();
			const auto accent = item.value(u"accent"_q).toBool();
			const auto button = container->add(
				object_ptr<Ui::SettingsButton>(
					container,
					rpl::single(text),
					red
						? st::settingsAttentionButton
						: accent
						? st::settingsButtonLightNoIcon
						: st::settingsButtonNoIcon));
			if (item.value(u"clickable"_q).toBool()) {
				button->setClickedCallback([=] {
					Call(u"setting_click"_q, QJsonObject{
						{ u"id"_q, id },
						{ u"index"_q, index },
					});
				});
			} else {
				button->setAttribute(Qt::WA_TransparentForMouseEvents);
			}
			if (!subtext.isEmpty()) {
				container->add(
					object_ptr<Ui::FlatLabel>(
						container,
						rpl::single(subtext),
						st::exteraSubtitle),
					st::exteraPluginSubtextPadding);
			}
		}
	}
	Ui::AddSkip(container);
}

[[nodiscard]] QStringList PinnedPlugins() {
	return String(u"plugins_pinned"_q).split(',', Qt::SkipEmptyParts);
}

void SetPluginPinned(const QString &id, bool pinned) {
	auto list = PinnedPlugins();
	list.removeAll(id);
	if (pinned) {
		list.push_front(id);
	}
	Config::Instance().setString(u"plugins_pinned"_q, list.join(','));
}

void SharePluginFile(
		not_null<Window::SessionController*> controller,
		const QString &path) {
	if (path.isEmpty() || !QFile::exists(path)) {
		controller->showToast(tr::lng_extera_plugins_bad_file(tr::now));
		return;
	}
	const auto session = &controller->session();
	Window::ShowChooseRecipientBox(controller, [=](
			not_null<Data::Thread*> thread) {
		auto list = Storage::PrepareMediaList(
			QStringList{ path },
			st::sendMediaPreviewSize,
			session->premium());
		if (list.error != Ui::PreparedList::Error::None
			|| list.files.empty()) {
			controller->showToast(tr::lng_extera_plugins_bad_file(tr::now));
			return true;
		}
		session->api().sendFiles(
			std::move(list),
			SendMediaType::File,
			nullptr,
			Api::SendAction(thread));
		controller->showToast(tr::lng_extera_plugins_shared(tr::now));
		return true;
	});
}

void ShowPluginCode(
		not_null<Window::SessionController*> controller,
		const QString &name,
		const QString &path) {
	auto file = QFile(path);
	if (!file.open(QIODevice::ReadOnly)) {
		controller->showToast(tr::lng_extera_plugins_bad_file(tr::now));
		return;
	}
	const auto code = QString::fromUtf8(file.readAll());
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(name));
		box->setWidth(st::exteraPluginCodeWidth);
		auto text = TextWithEntities{ code };
		text.entities.push_back(
			EntityInText(EntityType::Pre, 0, code.size(), u"python"_q));
		const auto label = box->addRow(
			object_ptr<Ui::FlatLabel>(box, st::exteraPluginCode));
		label->setMarkedText(text);
		label->setSelectable(true);
		box->addButton(tr::lng_extera_plugins_copy_code(), [=] {
			QGuiApplication::clipboard()->setText(code);
			controller->showToast(tr::lng_text_copied(tr::now));
		});
		box->addButton(tr::lng_close(), [=] { box->closeBox(); });
	}));
}

constexpr auto kPuzzleSvg = "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'><path fill='#ffffff' d='M20.5 11H19V7c0-1.1-.9-2-2-2h-4V3.5C13 2.12 11.88 1 10.5 1S8 2.12 8 3.5V5H4c-1.1 0-1.99.9-1.99 2v3.8H3.5c1.49 0 2.7 1.21 2.7 2.7s-1.21 2.7-2.7 2.7H2V20c0 1.1.9 2 2 2h3.8v-1.5c0-1.49 1.21-2.7 2.7-2.7 1.49 0 2.7 1.21 2.7 2.7V22H17c1.1 0 2-.9 2-2v-4h1.5c1.38 0 2.5-1.12 2.5-2.5S21.88 11 20.5 11z'/></svg>";

[[nodiscard]] QImage PuzzleImage(int size, QColor color) {
	const auto ratio = style::DevicePixelRatio();
	auto result = QImage(
		QSize(size, size) * ratio,
		QImage::Format_ARGB32_Premultiplied);
	result.fill(Qt::transparent);
	{
		auto p = QPainter(&result);
		QSvgRenderer(QByteArray(kPuzzleSvg)).render(
			&p,
			QRectF(0, 0, size * ratio, size * ratio));
	}
	result = style::colorizeImage(result, color);
	result.setDevicePixelRatio(ratio);
	return result;
}

[[nodiscard]] TextWithEntities WithLinks(const QString &text) {
	return TextUtilities::ParseEntities(
		text,
		TextParseLinks | TextParseMentions | TextParseHashtags);
}

void SetupLinks(
		not_null<Ui::FlatLabel*> label,
		not_null<Ui::GenericBox*> box) {
	label->setClickHandlerFilter([=](const ClickHandlerPtr &, Qt::MouseButton) {
		box->closeBox();
		return true;
	});
}

void SetupPluginIcon(
		not_null<Ui::AbstractButton*> widget,
		not_null<Window::SessionController*> controller,
		const QString &icon,
		int size,
		int badge) {
	struct State {
		std::shared_ptr<Data::DocumentMedia> media;
		std::unique_ptr<Ui::Text::CustomEmoji> animated;
		StickerSetIdentifier set;
		QImage image;
		QImage puzzle;
		Ui::Animations::Simple appear;
		bool failed = false;
	};
	widget->resize(size, size + badge / 3);
	const auto state = widget->lifetime().make_state<State>();
	const auto slash = icon.lastIndexOf('/');
	state->failed = (slash <= 0);
	const auto ready = [=] {
		if (!state->image.isNull() || !state->media) {
			return;
		}
		auto large = state->media->getStickerLarge();
		if (!large) {
			// Animated or video sticker: fall back to its still thumbnail.
			large = state->media->goodThumbnail();
		}
		if (!large) {
			large = state->media->thumbnail();
		}
		if (!large) {
			return;
		}
		const auto ratio = style::DevicePixelRatio();
		state->image = large->original().scaled(
			QSize(size, size) * ratio,
			Qt::KeepAspectRatio,
			Qt::SmoothTransformation);
		state->image.setDevicePixelRatio(ratio);
		state->appear.start(
			[=] { widget->update(); },
			0.,
			1.,
			st::exteraInstallIconDuration,
			anim::easeOutCirc);
	};
	widget->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(widget);
		auto hq = PainterHighQualityEnabler(p);
		const auto left = (widget->width() - size) / 2;
		const auto rect = QRect(left, 0, size, size);
		const auto radius = st::exteraInstallIconRadius;
		p.setPen(Qt::NoPen);
		if (state->failed && state->image.isNull()) {
			p.setBrush(st::windowBgActive);
			p.drawRoundedRect(rect, radius, radius);
			st::exteraIconPlugins.paintInCenter(
				p,
				rect,
				st::windowFgActive->c);
		} else {
			const auto shown = state->image.isNull()
				? 0.
				: state->appear.value(1.);
			const auto animatedReady = state->animated
				&& state->animated->ready();
			if (shown < 1. && !animatedReady) {
				p.setOpacity(1. - shown);
				p.setBrush(st::windowBgOver);
				p.drawRoundedRect(rect, radius, radius);
				p.setOpacity(1.);
			}
			if (state->animated && state->animated->ready()) {
				state->animated->paint(p, {
					.textColor = st::windowFg->c,
					.now = crl::now(),
					.position = rect.topLeft(),
					.paused = false,
				});
			} else if (shown > 0.) {
				const auto real = state->image.size()
					/ state->image.devicePixelRatio();
				const auto scale = 0.85 + 0.15 * shown;
				p.setOpacity(shown);
				p.translate(rect.center());
				p.scale(scale, scale);
				p.drawImage(
					QPoint(-real.width() / 2, -real.height() / 2),
					state->image);
				p.resetTransform();
				p.setOpacity(1.);
			}
		}
		if (badge > 0) {
			const auto circle = QRect(
				rect.x() + rect.width() - badge * 2 / 3,
				rect.y() + rect.height() - badge * 2 / 3,
				badge,
				badge);
			p.setBrush(st::boxBg);
			p.drawEllipse(circle);
			const auto stroke = st::exteraInstallBadgeStroke;
			const auto inner = circle.marginsRemoved(
				{ stroke, stroke, stroke, stroke });
			p.setBrush(st::windowSubTextFg);
			p.drawEllipse(inner);
			const auto glyph = st::exteraInstallBadgeGlyph;
			if (state->puzzle.isNull()) {
				state->puzzle = PuzzleImage(glyph, st::boxBg->c);
			}
			p.drawImage(
				QPoint(
					inner.x() + (inner.width() - glyph) / 2,
					inner.y() + (inner.height() - glyph) / 2),
				state->puzzle);
		}
	}, widget->lifetime());
	widget->setClickedCallback([=] {
		if (state->set.shortName.isEmpty() && !state->set.id) {
			return;
		}
		controller->show(Box<StickerSetBox>(
			controller->uiShow(),
			state->set,
			Data::StickersType::Stickers));
	});

	if (state->failed) {
		return;
	}
	const auto name = icon.left(slash);
	const auto index = icon.mid(slash + 1).toInt();
	const auto session = &controller->session();
	const auto weak = base::make_weak(widget);
	session->api().request(MTPmessages_GetStickerSet(
		MTP_inputStickerSetShortName(MTP_string(name)),
		MTP_int(0)
	)).done([=](const MTPmessages_StickerSet &result) {
		if (!weak) {
			return;
		}
		result.match([&](const MTPDmessages_stickerSet &data) {
			const auto set = session->data().stickers().feedSetFull(data);
			state->set = set->identifier();
			if (index < 0 || index >= set->stickers.size()) {
				state->failed = true;
				widget->update();
				return;
			}
			const auto document = set->stickers[index];
			state->animated = session->data().customEmojiManager().create(
				document,
				[=] { widget->update(); },
				Data::CustomEmojiSizeTag::Normal,
				size);
			state->media = document->createMediaView();
			state->media->goodThumbnailWanted();
			state->media->thumbnailWanted(document->stickerOrGifOrigin());
			state->media->checkStickerLarge();
			if (!state->media->loaded()) {
				document->save(document->stickerOrGifOrigin(), QString());
			}
			ready();
			session->downloaderTaskFinished(
			) | rpl::filter([=] {
				return state->image.isNull();
			}) | rpl::on_next([=] {
				state->media->checkStickerLarge();
				ready();
			}, widget->lifetime());
		}, [&](const MTPDmessages_stickerSetNotModified &) {
			state->failed = true;
			widget->update();
		});
	}).fail([=] {
		if (weak) {
			state->failed = true;
			widget->update();
		}
	}).send();
}

void AddInstallIcon(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		const QString &icon) {
	const auto widget = box->addRow(
		object_ptr<Ui::AbstractButton>(box),
		st::exteraInstallIconPadding);
	SetupPluginIcon(
		widget,
		controller,
		icon,
		st::exteraInstallIcon,
		st::exteraInstallBadge);
}

} // namespace

void ShowPluginSettings(
		not_null<Window::SessionController*> controller,
		const QString &id) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		auto title = id;
		for (const auto &entry : Call(u"list"_q).toArray()) {
			const auto info = entry.toObject();
			if (info.value(u"id"_q).toString() == id) {
				title = info.value(u"name"_q).toString();
			}
		}
		box->setTitle(rpl::single(title));
		box->setWidth(st::boxWideWidth);
		const auto content = box->verticalLayout();
		const auto list = content->add(
			object_ptr<Ui::VerticalLayout>(content));
		FillSettings(list, controller, id);
		settingsChanges(
		) | rpl::filter([=](const QString &changed) {
			return changed == id;
		}) | rpl::on_next([=] {
			list->clear();
			FillSettings(list, controller, id);
			list->resizeToWidth(content->width());
		}, box->lifetime());
		box->addButton(tr::lng_close(), [=] { box->closeBox(); });
	}));
}

void FillPluginsList(
		not_null<Ui::VerticalLayout*> container,
		not_null<Window::SessionController*> controller) {
	const auto fill = [=] {
		container->clear();
		auto list = Call(u"list"_q).toArray().toVariantList();
		const auto pinned = PinnedPlugins();
		std::stable_sort(list.begin(), list.end(), [&](
				const QVariant &a,
				const QVariant &b) {
			const auto left = pinned.contains(
				a.toMap().value(u"id"_q).toString());
			const auto right = pinned.contains(
				b.toMap().value(u"id"_q).toString());
			return left && !right;
		});
		if (list.isEmpty()) {
			container->add(
				object_ptr<Ui::FlatLabel>(
					container,
					tr::lng_extera_plugins_empty(),
					st::exteraAbout),
				st::exteraAboutPadding);
		}
		for (const auto &entry : list) {
			const auto info = QJsonObject::fromVariantMap(entry.toMap());
			const auto id = info.value(u"id"_q).toString();
			const auto card = container->add(
				object_ptr<Ui::VerticalLayout>(container),
				st::exteraPluginCardMargin);
			card->paintRequest() | rpl::on_next([=] {
				auto p = QPainter(card);
				auto hq = PainterHighQualityEnabler(p);
				p.setPen(Qt::NoPen);
				p.setBrush(st::windowBgOver);
				p.drawRoundedRect(
					card->rect(),
					st::exteraPluginCardRadius,
					st::exteraPluginCardRadius);
			}, card->lifetime());
			Ui::AddSkip(card, st::exteraPluginCardSkip);
			const auto button = card->add(
				object_ptr<Ui::SettingsButton>(
					card,
					rpl::single(info.value(u"name"_q).toString()),
					st::exteraPluginNameButton));
			const auto iconSize = st::exteraPluginIcon;
			const auto iconLeft = st::settingsButtonNoIcon.padding.left();
			const auto shift = iconSize + st::exteraPluginIconSkip;
			auto namePadding = st::settingsButtonNoIcon.padding;
			namePadding.setLeft(namePadding.left() + shift);
			button->setPaddingOverride(namePadding);
			button->setTransparentBg();
			button->toggleOn(rpl::single(info.value(u"enabled"_q).toBool()));
			button->toggledChanges(
			) | rpl::on_next([=](bool enabled) {
				Call(u"set_enabled"_q, QJsonObject{
					{ u"id"_q, id },
					{ u"enabled"_q, enabled },
				});
			}, button->lifetime());
			const auto textContext = Core::TextContext({
				.session = &controller->session(),
			});
			const auto meta = card->add(
				object_ptr<Ui::FlatLabel>(card, st::exteraSubtitle),
				st::exteraPluginMetaPadding + style::margins(shift, 0, 0, 0));
			meta->setMarkedText(SubtitleMarked(info), textContext);
			const auto icon = Ui::CreateChild<Ui::AbstractButton>(card);
			SetupPluginIcon(
				icon,
				controller,
				info.value(u"icon"_q).toString(),
				iconSize,
				0);
			icon->show();
			rpl::combine(
				button->geometryValue(),
				meta->geometryValue()
			) | rpl::on_next([=](QRect name, QRect line) {
				const auto top = name.y();
				const auto bottom = line.y() + line.height();
				icon->moveToLeft(
					iconLeft,
					top + (bottom - top - iconSize) / 2);
				icon->raise();
			}, icon->lifetime());
			const auto description = info.value(u"description"_q).toString();
			if (!description.isEmpty() && !Bool(u"plugins_compact_view"_q)) {
				const auto text = card->add(
					object_ptr<Ui::FlatLabel>(card, st::boxLabel),
					st::exteraPluginDescriptionPadding);
				text->setMarkedText(WithLinks(description), textContext);
			}
			const auto error = info.value(u"error"_q).toString();
			if (!error.isEmpty()) {
				card->add(
					object_ptr<Ui::FlatLabel>(
						card,
						rpl::single(error.trimmed().split('\n').last()),
						st::exteraAbout),
					st::exteraPluginDescriptionPadding);
			}
			const auto path = info.value(u"path"_q).toString();
			const auto name = info.value(u"name"_q).toString();
			const auto actions = card->add(
				object_ptr<Ui::FixedHeightWidget>(
					card,
					st::exteraPluginAction.height),
				st::exteraPluginActionsPadding);
			auto buttons = std::vector<not_null<Ui::IconButton*>>();
			const auto addAction = [&](
					const style::icon &icon,
					Fn<void()> callback) {
				const auto button = Ui::CreateChild<Ui::IconButton>(
					actions,
					st::exteraPluginAction);
				button->setIconOverride(&icon, &icon);
				button->setClickedCallback(std::move(callback));
				button->show();
				buttons.push_back(button);
				return button;
			};
			addAction(st::menuIconShare, [=] {
				SharePluginFile(controller, path);
			});
			addAction(st::exteraIconOpenCode, [=] {
				ShowPluginCode(controller, name, path);
			});
			const auto isPinned = pinned.contains(id);
			addAction(
				isPinned ? st::menuIconUnpin : st::menuIconPin,
				[=] {
					SetPluginPinned(id, !isPinned);
					notifyListChanged();
				});
			if (info.value(u"has_settings"_q).toBool()) {
				addAction(st::menuIconSettings, [=] {
					ShowPluginSettings(controller, id);
				});
			}
			const auto remove = Ui::CreateChild<Ui::IconButton>(
				actions,
				st::exteraPluginAction);
			remove->setIconOverride(
				&st::menuIconDeleteAttention,
				&st::menuIconDeleteAttention);
			remove->show();
			remove->setClickedCallback([=] {
				controller->show(Ui::MakeConfirmBox({
					.text = tr::lng_extera_plugins_delete_sure(),
					.confirmed = [=](Fn<void()> close) {
						Call(u"delete"_q, QJsonObject{ { u"id"_q, id } });
						SetPluginPinned(id, false);
						notifyListChanged();
						close();
					},
					.confirmText = tr::lng_extera_plugins_delete(),
					.confirmStyle = &st::attentionBoxButton,
				}));
			});
			actions->widthValue() | rpl::on_next([=](int width) {
				auto left = 0;
				for (const auto button : buttons) {
					button->moveToLeft(left, 0, width);
					left += button->width();
				}
				remove->moveToRight(0, 0, width);
			}, actions->lifetime());
			Ui::AddSkip(card, st::exteraPluginCardSkip);
		}
		container->resizeToWidth(container->width());
	};
	fill();
	listChanges(
	) | rpl::on_next(fill, container->lifetime());
}

void AddSourcePill(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		bool trusted,
		const QString &verifiedBy = QString()) {
	const auto text = !verifiedBy.isEmpty()
		? Extera::Text(u"Проверено: %1"_q, u"Verified by %1"_q).arg(verifiedBy)
		: trusted
		? tr::lng_extera_plugins_trusted_source(tr::now)
		: tr::lng_extera_plugins_unknown_source(tr::now);
	const auto &font = st::exteraInstallPillFont;
	const auto &icon = st::exteraInstallShield;
	const auto padding = st::exteraInstallPillPadding;
	const auto width = padding.left()
		+ icon.width()
		+ st::exteraInstallPillSkip
		+ font->width(text)
		+ padding.right();
	const auto widget = box->addRow(
		object_ptr<Ui::AbstractButton>(box),
		st::exteraInstallPillMargin);
	widget->resize(width, st::exteraInstallPillHeight);
	widget->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(widget);
		auto hq = PainterHighQualityEnabler(p);
		const auto color = trusted
			? QColor(0x5c, 0xb8, 0x5f)
			: st::attentionButtonFg->c;
		auto bg = color;
		bg.setAlphaF(0.16);
		const auto left = (widget->width() - width) / 2;
		const auto rect = QRect(left, 0, width, widget->height());
		p.setPen(Qt::NoPen);
		p.setBrush(bg);
		p.drawRoundedRect(rect, rect.height() / 2., rect.height() / 2.);
		const auto iconLeft = left + padding.left();
		icon.paint(
			p,
			iconLeft,
			(rect.height() - icon.height()) / 2,
			widget->width(),
			color);
		p.setPen(color);
		p.setFont(font);
		p.drawText(
			iconLeft + icon.width() + st::exteraInstallPillSkip,
			(rect.height() - font->height) / 2 + font->ascent,
			text);
	}, widget->lifetime());
	widget->setClickedCallback([=] {
		controller->showToast((trusted || !verifiedBy.isEmpty())
			? tr::lng_extera_plugins_trusted_info(tr::now)
			: tr::lng_extera_plugins_unknown_info(tr::now));
	});
}

[[nodiscard]] QString ReportText(const char *ru, const char *en) {
	return Extera::Text(QString::fromUtf8(ru), QString::fromUtf8(en));
}

// The report follows exteraGram's sheet: a summary card, then every group of
// findings as a card of rows, each row opens into the lines that caused it.
[[nodiscard]] QColor AlphaOf(QColor color, float64 alpha) {
	color.setAlphaF(std::clamp(color.alphaF() * alpha, 0., 1.));
	return color;
}

[[nodiscard]] style::font ScaledFont(int size) {
	const auto &base = st::semiboldFont;
	return style::font(style::ConvertScale(size), base->flags(), base->family());
}

class AnalysisReport final : public Ui::RpWidget {
public:
	AnalysisReport(
		QWidget *parent,
		const QString &pluginName,
		const QJsonObject &report);

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void leaveEventHook(QEvent *e) override;

private:
	struct Row {
		QString title;
		QString text;
		QString glyph;
		QString level;
		std::vector<std::pair<int, QString>> lines;
		int more = 0;
		bool expanded = false;
		std::unique_ptr<Ui::Animations::Simple> animation = std::make_unique<Ui::Animations::Simple>();
		QRectF rect;
		float64 base = 0.;
	};
	struct Section {
		QString title;
		std::vector<Row> rows;
	};

	[[nodiscard]] static QColor LevelColor(const QString &level);
	[[nodiscard]] static int Dp(int value);
	[[nodiscard]] static float64 DpF(float64 value);
	[[nodiscard]] float64 evidenceHeight(const Row &row) const;
	[[nodiscard]] float64 codeHeight(const QString &code) const;
	[[nodiscard]] float64 textHeight(
		const QString &text,
		const style::font &font,
		float64 width) const;
	void drawGlyph(
		QPainter &p,
		const QString &name,
		QPointF center,
		int size,
		QColor color) const;
	struct Chip {
		QString text;
		QString level;
		QString glyph;
		QRectF rect;
	};
	void layoutChips(float64 width);
	void toggle(Row &row);
	void toggleOpen();
	void buildFooter();
	[[nodiscard]] Row *rowAt(QPoint point);

	QString _name;
	QString _verdict;
	int _danger = 0;
	int _warn = 0;
	int _info = 0;
	std::vector<Section> _sections;
	QString _footer;
	QString _sha;
	QRectF _footerRect;
	bool _copied = false;
	float64 _summaryHeight = 0.;
	float64 _codeWidth = 0.;
	float64 _contentHeight = 0.;
	float64 _sectionsTop = 0.;
	float64 _sectionsFull = 0.;
	QRectF _summaryRect;
	std::vector<Chip> _chips;
	float64 _chipsHeight = 0.;
	Ui::Animations::Simple _openAnimation;
	bool _open = false;
	int _hover = -1;
	mutable std::map<QString, QImage> _glyphs;

};

int AnalysisReport::Dp(int value) {
	return style::ConvertScale(value);
}

float64 AnalysisReport::DpF(float64 value) {
	return style::ConvertScaleExact(value);
}

QColor AnalysisReport::LevelColor(const QString &level) {
	return (level == u"danger"_q)
		? QColor(0xE5, 0x48, 0x4D)
		: (level == u"warn"_q)
		? QColor(0xF5, 0xA5, 0x24)
		: (level == u"safe"_q)
		? QColor(0x30, 0xA4, 0x6C)
		: QColor(0x8A, 0x94, 0xA6);
}

AnalysisReport::AnalysisReport(
	QWidget *parent,
	const QString &pluginName,
	const QJsonObject &report)
: RpWidget(parent)
, _name(pluginName) {
	setMouseTracking(true);
	_verdict = report.value(u"verdict"_q).toString();
	const auto counts = report.value(u"counts"_q).toObject();
	_danger = counts.value(u"danger"_q).toInt();
	_warn = counts.value(u"warn"_q).toInt();
	_info = counts.value(u"info"_q).toInt();
	const auto categories = report.value(u"categories"_q).toObject();
	auto last = QString();
	for (const auto &entry : report.value(u"findings"_q).toArray()) {
		const auto finding = entry.toObject();
		const auto category = finding.value(u"category"_q).toString();
		if (category != last || _sections.empty()) {
			last = category;
			_sections.push_back({
				.title = categories.value(category).toString(),
			});
		}
		auto row = Row();
		row.title = finding.value(u"name"_q).toString();
		row.text = finding.value(u"text"_q).toString();
		row.glyph = category;
		row.level = finding.value(u"level"_q).toString();
		const auto snippets = finding.value(u"snippets"_q).toArray();
		for (const auto &snippet : snippets) {
			const auto item = snippet.toObject();
			row.lines.emplace_back(
				item.value(u"line"_q).toInt(),
				item.value(u"code"_q).toString());
		}
		row.more = std::max(0, finding.value(u"count"_q).toInt() - int(snippets.size()));
		_sections.back().rows.push_back(std::move(row));
	}
	_sha = report.value(u"sha256"_q).toString();
	buildFooter();
}

void AnalysisReport::buildFooter() {
	_footer = Extera::Text(
		QString::fromUtf8("Разбор сделан на вашем компьютере по тексту плагина. Это подсказка, а не гарантия: плагин мог скрыть своё поведение."),
		QString::fromUtf8("The review ran on your computer over the plugin's text. It is a hint, not a guarantee: a plugin can hide what it does."));
}

float64 AnalysisReport::textHeight(
		const QString &text,
		const style::font &font,
		float64 width) const {
	if (text.isEmpty()) {
		return 0.;
	}
	return QFontMetricsF(font->f).boundingRect(
		QRectF(0., 0., width, 100000.),
		Qt::AlignLeft | Qt::TextWordWrap,
		text).height();
}

float64 AnalysisReport::codeHeight(const QString &code) const {
	const auto mono = st::normalFont->monospace();
	const auto wrapped = QFontMetricsF(mono->f).boundingRect(
		QRectF(0., 0., std::max(_codeWidth, DpF(40.)), 100000.),
		Qt::AlignLeft | Qt::TextWrapAnywhere,
		code).height();
	return std::max(DpF(18.), wrapped + DpF(2.));
}

float64 AnalysisReport::evidenceHeight(const Row &row) const {
	if (row.lines.empty()) {
		return 0.;
	}
	auto result = DpF(10.);
	for (const auto &[number, code] : row.lines) {
		result += codeHeight(code);
	}
	if (row.more > 0) {
		result += DpF(20.);
	}
	return result + DpF(8.);
}

void AnalysisReport::layoutChips(float64 width) {
	_chips.clear();
	const auto font = st::semiboldFont;
	const auto start = DpF(16.);
	const auto limit = width - DpF(16.);
	auto x = start;
	auto y = 0.;
	for (const auto &section : _sections) {
		if (section.rows.empty() || section.title.isEmpty()) {
			continue;
		}
		auto level = u"info"_q;
		for (const auto &row : section.rows) {
			if (row.level == u"danger"_q) {
				level = u"danger"_q;
			} else if (row.level == u"warn"_q && level != u"danger"_q) {
				level = u"warn"_q;
			}
		}
		const auto chipWidth = DpF(12.) + Dp(18) + DpF(8.)
			+ font->width(section.title) + DpF(14.);
		if (x + chipWidth > limit && x > start) {
			x = start;
			y += DpF(40.);
		}
		_chips.push_back({
			section.title,
			level,
			section.rows.front().glyph,
			QRectF(x, y, chipWidth, DpF(32.)),
		});
		x += chipWidth + DpF(8.);
	}
	_chipsHeight = _chips.empty() ? 0. : _chips.back().rect.bottom();
}

int AnalysisReport::resizeGetHeight(int newWidth) {
	const auto padding = DpF(12.);
	const auto side = DpF(16.);
	const auto inner = newWidth - side * 2;
	layoutChips(inner);
	_summaryHeight = DpF(16.) + DpF(56.)
		+ (_chips.empty() ? 0. : (DpF(14.) + _chipsHeight))
		+ DpF(16.);
	auto y = side + _summaryHeight + DpF(8.);
	_sectionsTop = y;
	const auto textFont = st::normalFont;
	const auto textWidth = inner - padding * 2 - DpF(40.) - DpF(12.) - DpF(30.);
	_codeWidth = inner - padding * 2 - DpF(52.) - DpF(62.);
	for (auto &section : _sections) {
		y += DpF(34.);
		for (auto &row : section.rows) {
			const auto content = std::max(
				DpF(40.),
				DpF(2.) + DpF(20.) + DpF(2.)
					+ textHeight(row.text, textFont, textWidth) + DpF(2.));
			const auto base = padding + content + padding;
			row.base = base;
			const auto extra = evidenceHeight(row)
				* row.animation->value(row.expanded ? 1. : 0.);
			row.rect = QRectF(side, y, inner, base + extra);
			y += base + extra;
		}
		y += DpF(4.);
	}
	_sectionsFull = y - _sectionsTop;
	y = _sectionsTop + _sectionsFull * _openAnimation.value(_open ? 1. : 0.);
	y += DpF(8.) + textHeight(_footer, textFont, inner)
		+ (_sha.isEmpty() ? 0. : DpF(12.) + DpF(92.))
		+ DpF(24.);
	_contentHeight = y;
	return int(std::ceil(y));
}

void AnalysisReport::drawGlyph(
		QPainter &p,
		const QString &name,
		QPointF center,
		int size,
		QColor color) const {
	const auto ratio = style::DevicePixelRatio();
	const auto key = name + '/' + QString::number(size) + '/' + QString::number(color.rgba(), 16);
	auto i = _glyphs.find(key);
	if (i == _glyphs.end()) {
		auto renderer = QSvgRenderer(u":/gui/art/extera/analysis/"_q + name + u".svg"_q);
		auto image = QImage(
			QSize(size, size) * ratio,
			QImage::Format_ARGB32_Premultiplied);
		image.fill(Qt::transparent);
		{
			auto q = QPainter(&image);
			renderer.render(&q, QRectF(0, 0, size * ratio, size * ratio));
			q.setCompositionMode(QPainter::CompositionMode_SourceIn);
			q.fillRect(image.rect(), color);
		}
		image.setDevicePixelRatio(ratio);
		i = _glyphs.emplace(key, std::move(image)).first;
	}
	p.drawImage(
		QRectF(center.x() - size / 2., center.y() - size / 2., size, size),
		i->second);
}

void AnalysisReport::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);
	const auto side = DpF(16.);
	const auto inner = width() - side * 2;
	const auto padding = DpF(12.);
	const auto bg = st::windowBgOver->c;

	// summary
	const auto verdictLevel = (_verdict == u"danger"_q)
		? u"danger"_q
		: (_verdict == u"caution"_q)
		? u"warn"_q
		: u"safe"_q;
	const auto verdictColor = LevelColor(verdictLevel);
	const auto summary = QRectF(side, side, inner, _summaryHeight);
	_summaryRect = summary;
	p.setPen(Qt::NoPen);
	p.setBrush(bg);
	p.drawRoundedRect(summary, DpF(16.), DpF(16.));
	const auto circle = QRectF(
		summary.left() + DpF(16.),
		summary.top() + DpF(16.),
		DpF(56.),
		DpF(56.));
	p.setBrush(AlphaOf(verdictColor, 0.16));
	p.drawEllipse(circle);
	drawGlyph(
		p,
		(_verdict == u"safe"_q) ? u"shield"_q : u"warning"_q,
		circle.center(),
		Dp(28),
		verdictColor);
	const auto textLeft = circle.right() + DpF(14.);
	p.setPen(st::windowFg);
	p.setFont(ScaledFont(18)->f);
	p.drawText(
		QRectF(textLeft, circle.top(), summary.right() - textLeft - DpF(44.), DpF(30.)),
		Qt::AlignLeft | Qt::AlignVCenter,
		(_verdict == u"danger"_q)
			? ReportText("Есть опасные возможности", "Has dangerous abilities")
			: (_verdict == u"caution"_q)
			? ReportText("Нужна осторожность", "Be careful")
			: ReportText("Явных угроз не найдено", "No clear threats found"));
	p.setPen(st::windowSubTextFg);
	p.setFont(st::normalFont->f);
	p.drawText(
		QRectF(textLeft, circle.top() + DpF(30.), summary.right() - textLeft - DpF(16.), DpF(24.)),
		Qt::AlignLeft | Qt::AlignVCenter,
		_name);
	const auto chipFont = st::semiboldFont;
	const auto chipTop = circle.bottom() + DpF(14.);
	for (const auto &chip : _chips) {
		const auto color = LevelColor(chip.level);
		const auto rect = chip.rect.translated(summary.left(), chipTop);
		p.setPen(QPen(AlphaOf(color, 0.5), DpF(1.)));
		p.setBrush(AlphaOf(color, 0.1));
		p.drawRoundedRect(
			rect.adjusted(0.5, 0.5, -0.5, -0.5),
			DpF(9.),
			DpF(9.));
		drawGlyph(
			p,
			chip.glyph,
			QPointF(rect.left() + DpF(12.) + Dp(18) / 2., rect.center().y()),
			Dp(18),
			color);
		p.setPen(color);
		p.setFont(chipFont->f);
		p.drawText(
			QRectF(
				rect.left() + DpF(12.) + Dp(18) + DpF(8.),
				rect.top(),
				rect.width(),
				rect.height()),
			Qt::AlignLeft | Qt::AlignVCenter,
			chip.text);
	}

	const auto openProgress = _openAnimation.value(_open ? 1. : 0.);
	{
		p.save();
		p.translate(QPointF(summary.right() - DpF(26.), circle.center().y()));
		p.rotate(openProgress * 180.);
		auto pen = QPen(st::windowSubTextFg->c, DpF(2.));
		pen.setCapStyle(Qt::RoundCap);
		pen.setJoinStyle(Qt::RoundJoin);
		p.setPen(pen);
		p.setBrush(Qt::NoBrush);
		p.drawLine(QPointF(-DpF(6.), -DpF(3.)), QPointF(0., DpF(3.)));
		p.drawLine(QPointF(0., DpF(3.)), QPointF(DpF(6.), -DpF(3.)));
		p.restore();
	}

	// sections
	p.save();
	p.setClipRect(QRectF(0., _sectionsTop - DpF(4.), width(), _sectionsFull * openProgress + DpF(4.)));
	p.setOpacity(p.opacity() * openProgress);
	for (const auto &section : _sections) {
		if (openProgress <= 0.01) {
			break;
		}
		if (section.rows.empty()) {
			continue;
		}
		const auto top = section.rows.front().rect.top();
		const auto bottom = section.rows.back().rect.bottom();
		p.setPen(st::windowActiveTextFg);
		p.setFont(st::semiboldFont->f);
		p.drawText(
			QRectF(side + padding, top - DpF(30.), inner - padding * 2, DpF(26.)),
			Qt::AlignLeft | Qt::AlignVCenter,
			section.title);
		p.setPen(Qt::NoPen);
		p.setBrush(bg);
		p.drawRoundedRect(QRectF(side, top, inner, bottom - top), DpF(14.), DpF(14.));
		for (auto k = 0; k != int(section.rows.size()); ++k) {
			const auto &row = section.rows[k];
			const auto color = LevelColor(row.level);
			const auto base = row.rect.top() + padding;
			if (k > 0) {
				p.setPen(Qt::NoPen);
				p.setBrush(st::shadowFg);
				p.drawRect(QRectF(
					row.rect.left() + DpF(64.),
					row.rect.top(),
					row.rect.width() - DpF(64.),
					DpF(1.)));
			}
			const auto icon = QRectF(
				row.rect.left() + padding,
				base,
				DpF(40.),
				DpF(40.));
			p.setPen(Qt::NoPen);
			p.setBrush(AlphaOf(color, 0.16));
			p.drawEllipse(icon);
			drawGlyph(p, row.glyph, icon.center(), Dp(20), color);
			const auto left = icon.right() + DpF(12.);
			const auto textWidth = row.rect.right() - padding - DpF(30.) - left;
			p.setPen(st::windowFg);
			p.setFont(st::semiboldFont->f);
			p.drawText(
				QRectF(left, base + DpF(2.), textWidth, DpF(20.)),
				Qt::AlignLeft | Qt::AlignVCenter,
				row.title);
			p.setPen(st::windowSubTextFg);
			p.setFont(st::normalFont->f);
			p.drawText(
				QRectF(left, base + DpF(24.), textWidth, 1000.),
				Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
				row.text);
			// the arrow turns when the row opens
			const auto progress = row.animation->value(row.expanded ? 1. : 0.);
			const auto arrow = QPointF(
				row.rect.right() - padding - DpF(8.),
				base + DpF(20.));
			p.save();
			p.translate(arrow);
			p.rotate(progress * 180.);
			auto pen = QPen(st::windowSubTextFg->c, DpF(2.));
			pen.setCapStyle(Qt::RoundCap);
			pen.setJoinStyle(Qt::RoundJoin);
			p.setPen(pen);
			p.setBrush(Qt::NoBrush);
			p.drawLine(QPointF(-DpF(5.), -DpF(2.5)), QPointF(0., DpF(2.5)));
			p.drawLine(QPointF(0., DpF(2.5)), QPointF(DpF(5.), -DpF(2.5)));
			p.restore();
			// the lines that caused it
			if (!row.lines.empty() && progress > 0.01) {
				const auto block = QRectF(
					left,
					row.rect.top() + row.base - padding + DpF(2.),
					row.rect.right() - padding - left,
					evidenceHeight(row) - DpF(8.));
				p.save();
				p.setClipRect(QRectF(
					row.rect.left(),
					row.rect.top(),
					row.rect.width(),
					row.rect.height()));
				p.setOpacity(p.opacity() * progress);
				p.setPen(Qt::NoPen);
				p.setBrush(st::windowBg);
				p.drawRoundedRect(block, DpF(10.), DpF(10.));
				const auto mono = st::normalFont->monospace();
				p.setFont(mono->f);
				auto y = block.top() + DpF(5.);
				for (const auto &[number, code] : row.lines) {
					p.setPen(st::windowSubTextFg);
					const auto height = codeHeight(code);
					p.drawText(
						QRectF(block.left() + DpF(6.), y, DpF(40.), DpF(18.)),
						Qt::AlignRight | Qt::AlignVCenter,
						QString::number(number));
					p.setPen(st::windowFg);
					p.drawText(
						QRectF(block.left() + DpF(54.), y, block.width() - DpF(62.), height),
						Qt::AlignLeft | Qt::AlignTop | Qt::TextWrapAnywhere,
						code);
					y += height;
				}
				if (row.more > 0) {
					p.setPen(st::windowSubTextFg);
					p.setFont(st::normalFont->f);
					p.drawText(
						QRectF(block.left() + DpF(54.), y, block.width() - DpF(62.), DpF(20.)),
						Qt::AlignLeft | Qt::AlignVCenter,
						ReportText("и ещё мест: ", "more places: ") + QString::number(row.more));
				}
				p.restore();
			}
		}
	}
	p.restore();
	const auto hashHeight = _sha.isEmpty() ? 0. : DpF(12.) + DpF(92.);
	const auto footerTop = _contentHeight - DpF(24.) - hashHeight
		- textHeight(_footer, st::normalFont, inner);
	p.setPen(st::windowSubTextFg);
	p.setFont(st::normalFont->f);
	p.drawText(
		QRectF(side, footerTop, inner, 1000.),
		Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
		_footer);
	_footerRect = QRectF();
	if (!_sha.isEmpty()) {
		const auto card = QRectF(
			side,
			_contentHeight - DpF(24.) - DpF(92.),
			inner,
			DpF(92.));
		_footerRect = card;
		p.setPen(Qt::NoPen);
		p.setBrush(bg);
		p.drawRoundedRect(card, DpF(14.), DpF(14.));
		const auto left = card.left() + DpF(16.);
		const auto wide = card.width() - DpF(32.);
		p.setPen(st::windowSubTextFg);
		p.setFont(st::semiboldFont->f);
		p.drawText(
			QRectF(left, card.top() + DpF(10.), wide, DpF(20.)),
			Qt::AlignLeft | Qt::AlignVCenter,
			u"SHA-256"_q);
		const auto mono = st::normalFont->monospace();
		p.setPen(st::windowActiveTextFg);
		p.setFont(mono->f);
		p.drawText(
			QRectF(left, card.top() + DpF(32.), wide, DpF(20.)),
			Qt::AlignLeft | Qt::AlignVCenter,
			_sha.left(32));
		p.drawText(
			QRectF(left, card.top() + DpF(52.), wide, DpF(20.)),
			Qt::AlignLeft | Qt::AlignVCenter,
			_sha.mid(32));
		p.setPen(st::windowSubTextFg);
		p.setFont(st::normalFont->f);
		p.drawText(
			QRectF(left, card.top() + DpF(70.), wide, DpF(18.)),
			Qt::AlignLeft | Qt::AlignVCenter,
			_copied
				? ReportText("Скопировано", "Copied")
				: ReportText("Нажмите, чтобы скопировать", "Click to copy"));
	}
}

AnalysisReport::Row *AnalysisReport::rowAt(QPoint point) {
	if (!_open) {
		return nullptr;
	}
	for (auto &section : _sections) {
		for (auto &row : section.rows) {
			if (row.rect.contains(point) && !row.lines.empty()) {
				return &row;
			}
		}
	}
	return nullptr;
}

void AnalysisReport::toggle(Row &row) {
	row.expanded = !row.expanded;
	row.animation->start(
		[=] {
			resizeToWidth(width());
			update();
		},
		row.expanded ? 0. : 1.,
		row.expanded ? 1. : 0.,
		crl::time(260),
		anim::easeOutCubic);
}

void AnalysisReport::toggleOpen() {
	_open = !_open;
	_openAnimation.start(
		[=] {
			resizeToWidth(width());
			update();
		},
		_open ? 0. : 1.,
		_open ? 1. : 0.,
		crl::time(300),
		anim::easeOutCubic);
}

void AnalysisReport::mousePressEvent(QMouseEvent *e) {
	if (_summaryRect.contains(e->pos())) {
		toggleOpen();
	} else if (const auto row = rowAt(e->pos())) {
		toggle(*row);
	} else if (!_sha.isEmpty() && _footerRect.contains(e->pos())) {
		QGuiApplication::clipboard()->setText(_sha);
		_copied = true;
		buildFooter();
		resizeToWidth(width());
		update();
	}
}

void AnalysisReport::mouseMoveEvent(QMouseEvent *e) {
	setCursor((rowAt(e->pos())
		|| _summaryRect.contains(e->pos())
		|| (!_sha.isEmpty() && _footerRect.contains(e->pos())))
		? style::cur_pointer
		: style::cur_default);
}

void AnalysisReport::leaveEventHook(QEvent *e) {
	setCursor(style::cur_default);
	Ui::RpWidget::leaveEventHook(e);
}

void ShowAnalysisBox(
		not_null<Window::SessionController*> controller,
		const QString &pluginName,
		const QJsonObject &report) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(ReportText("Разбор безопасности", "Safety review")));
		box->setWidth(st::boxWideWidth);
		box->addRow(
			object_ptr<AnalysisReport>(box, pluginName, report),
			style::margins());
		box->addButton(tr::lng_close(), [=] { box->closeBox(); });
	}));
}

void ShowInstallBox(
		not_null<Window::SessionController*> controller,
		const QString &path,
		bool trusted) {
	const auto meta = Call(u"metadata"_q, QJsonObject{
		{ u"path"_q, path },
	}).toObject();
	if (meta.isEmpty()) {
		controller->showToast(tr::lng_extera_plugins_bad_file(tr::now));
		return;
	}
	const auto id = meta.value(u"id"_q).toString();
	auto verifiedBy = QString();
	if (auto file = QFile(path); file.open(QIODevice::ReadOnly)) {
		const auto hash = QCryptographicHash::hash(
			file.readAll(),
			QCryptographicHash::Sha256).toHex();
		if (ProjectBadges::IsVerifiedPlugin(QString::fromLatin1(hash))) {
			verifiedBy = ProjectBadges::Project();
		}
	}
	auto installed = QString();
	auto exists = false;
	for (const auto &entry : Call(u"list"_q, QJsonObject()).toArray()) {
		const auto info = entry.toObject();
		if (info.value(u"id"_q).toString() == id) {
			exists = true;
			installed = info.value(u"version"_q).toString();
		}
	}
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setStyle(st::exteraInstallBox);
		box->setWidth(st::exteraInstallWidth);
		box->setNoContentMargin(true);
		const auto context = Core::TextContext({
			.session = &controller->session(),
		});
		AddInstallIcon(box, controller, meta.value(u"icon"_q).toString());
		box->addRow(
			object_ptr<Ui::FlatLabel>(
				box,
				rpl::single(meta.value(u"name"_q).toString()),
				st::exteraInstallName),
			st::exteraInstallNamePadding,
			style::al_top);

		const auto version = meta.value(u"version"_q).toString();
		const auto author = meta.value(u"author"_q).toString();
		auto line = TextWithEntities();
		if (!version.isEmpty()) {
			line.append(tr::lng_extera_plugins_version(tr::now) + ' ');
			if (exists && !installed.isEmpty()) {
				line.append(TextWithEntities{
					installed,
					{ EntityInText(EntityType::StrikeOut, 0, installed.size()) },
				});
				line.append(QString::fromUtf8(" \xE2\x86\x92 "));
			}
			line.append(version);
		}
		if (!author.isEmpty()) {
			if (!line.empty()) {
				line.append(QString::fromUtf8(" \xE2\x80\xA2 "));
			}
			line.append(WithLinks(author));
		}
		if (!line.empty()) {
			const auto label = box->addRow(
				object_ptr<Ui::FlatLabel>(box, st::exteraInstallVersion),
				st::exteraInstallVersionPadding,
			style::al_top);
			label->setMarkedText(line, context);
			SetupLinks(label, box);
		}
		AddSourcePill(box, controller, trusted, verifiedBy);

		const auto description = meta.value(u"description"_q).toString();
		if (!description.isEmpty()) {
			const auto card = box->addRow(
				object_ptr<Ui::VerticalLayout>(box),
				st::exteraInstallCardMargin);
			card->paintRequest() | rpl::on_next([=] {
				auto p = QPainter(card);
				auto hq = PainterHighQualityEnabler(p);
				p.setPen(Qt::NoPen);
				p.setBrush(st::windowBgOver);
				p.drawRoundedRect(
					card->rect(),
					st::exteraInstallCardRadius,
					st::exteraInstallCardRadius);
			}, card->lifetime());
			const auto label = card->add(
				object_ptr<Ui::FlatLabel>(card, st::exteraInstallDescription),
				st::exteraInstallCardPadding);
			label->setMarkedText(WithLinks(description), context);
			SetupLinks(label, box);
		}

		const auto report = Call(u"analyze"_q, QJsonObject{
			{ u"path"_q, path },
			{ u"lang"_q, Extera::Text(u"ru"_q, u"en"_q) },
		}).toObject();
		if (!report.isEmpty()) {
			box->addRow(
				object_ptr<AnalysisReport>(
					box,
					meta.value(u"name"_q).toString(),
					report),
				style::margins());
		}

		const auto install = box->addRow(
			object_ptr<Ui::RoundButton>(
				box,
				exists
					? tr::lng_extera_plugins_update()
					: tr::lng_extera_plugins_install(),
				st::exteraInstallButton),
			st::exteraInstallButtonMargin);
		const auto cancel = box->addRow(
			object_ptr<Ui::RoundButton>(
				box,
				tr::lng_cancel(),
				st::exteraInstallCancel),
			st::exteraInstallCancelMargin);
		for (const auto button : { install, cancel }) {
			button->setFullRadius(true);
			button->setTextTransform(Ui::RoundButtonTextTransform::NoTransform);
		}
		box->widthValue() | rpl::on_next([=](int width) {
			const auto margin = st::exteraInstallButtonMargin;
			const auto full = width - margin.left() - margin.right();
			install->setFullWidth(full);
			cancel->setFullWidth(full);
		}, install->lifetime());
		cancel->setClickedCallback([=] { box->closeBox(); });
		install->setClickedCallback([=] {
			const auto name = meta.value(u"name"_q).toString();
			const auto info = Call(u"install"_q, QJsonObject{
				{ u"path"_q, path },
			}).toObject();
			box->closeBox();
			notifyListChanged();
			controller->showToast(info.value(u"error"_q).toString().isEmpty()
				? (exists
					? tr::lng_extera_plugins_updated(tr::now, lt_name, name)
					: tr::lng_extera_plugins_installed_name(
						tr::now,
						lt_name,
						name))
				: tr::lng_extera_plugins_install_failed(tr::now));
		});
	}));
}

void ChooseAndInstall(not_null<Window::SessionController*> controller) {
	FileDialog::GetOpenPath(
		Core::App().getFileDialogParent(),
		tr::lng_extera_plugins_install(tr::now),
		u"exteraGram Plugin (*.plugin *.elyx);;Python (*.py)"_q,
		[=](FileDialog::OpenResult &&result) {
			if (!result.paths.isEmpty()) {
				ShowInstallBox(controller, result.paths.front(), false);
			}
		});
}

void AddChatMenuItems(
		const Fn<void(QString, Fn<void()>)> &add,
		not_null<PeerData*> peer) {
	if (!Started()) {
		return;
	}
	const auto dialog = DialogId(peer);
	for (const auto &item : MenuItems(u"chat_action_menu"_q)) {
		const auto id = item.id;
		add(item.text, [=] {
			ClickMenuItem(id, QJsonObject{
				{ u"peer"_q, double(dialog) },
			});
		});
	}
}

void AddMessageMenuItems(
		not_null<Ui::PopupMenu*> menu,
		not_null<HistoryItem*> item) {
	static auto last = QPointer<QWidget>();
	if (last == menu.get()) {
		return;
	}
	if (!Started()) {
		return;
	}
	const auto entries = MenuItems(u"message_context_menu"_q);
	if (entries.empty()) {
		return;
	}
	last = menu.get();
	auto submenu = std::make_unique<Ui::PopupMenu>(
		menu,
		st::defaultPopupMenu);
	// The context (it starts the download of the replied media) is made
	// when an entry is clicked, not for every right click on a message.
	const auto itemId = item->fullId();
	const auto session = &item->history()->session();
	for (const auto &entry : entries) {
		const auto id = entry.id;
		submenu->addAction(entry.text, [=] {
			if (const auto item = session->data().message(itemId)) {
				ClickMenuItem(id, MenuContext(item));
			}
		});
	}
	menu->addAction(
		Extera::Text(
			QString::fromUtf8("Плагины"),
			QString::fromUtf8("Plugins")),
		std::move(submenu),
		&st::exteraIconPlugins);
}

} // namespace Extera::Plugins
