/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/profile/info_profile_section_stack.h"

#include "ui/widgets/box_content_divider.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/vertical_list.h"
#include "ui/rect_part.h"
#include "ui/ui_utility.h"

#include "styles/style_info.h"

// AyuGram includes
#include "ayu/ayu_ui_settings.h"
#include <algorithm>
#include "ui/effects/animation_value.h"
#include "ui/painter.h"
#include "ui/widgets/buttons.h"
#include "styles/palette.h"

namespace Info::Profile {
namespace {

[[nodiscard]] not_null<Ui::SlideWrap<>*> CreatePlainSeparator(
		not_null<Ui::VerticalLayout*> layout) {
	auto inner = object_ptr<Ui::VerticalLayout>(layout);
	const auto material = AyuUiSettings::isMaterialSettings();
	if (!material) {
		Ui::AddSkip(inner.data(), st::infoProfileSkip);
	}
	inner->add(object_ptr<Ui::BoxContentDivider>(
		inner.data(),
		material ? style::ConvertScale(4) : st::boxDividerHeight,
		st::defaultDividerBar));
	if (!material) {
		Ui::AddSkip(inner.data(), st::infoProfileSkip);
	}
	return layout->add(object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
		layout,
		std::move(inner)));
}

[[nodiscard]] not_null<Ui::SlideWrap<>*> CreateTextSeparator(
		not_null<Ui::VerticalLayout*> layout,
		rpl::producer<TextWithEntities> text,
		Fn<void(not_null<Ui::FlatLabel*>)> setup) {
	auto inner = object_ptr<Ui::VerticalLayout>(layout);
	Ui::AddSkip(inner.data(), st::infoProfileSkip);
	auto label = object_ptr<Ui::FlatLabel>(
		inner.data(),
		std::move(text),
		st::defaultDividerLabel.label);
	const auto rawLabel = label.data();
	inner->add(object_ptr<Ui::DividerLabel>(
		inner.data(),
		std::move(label),
		st::defaultBoxDividerLabelPadding,
		st::defaultDividerLabel.bar,
		RectPart::Top | RectPart::Bottom));
	Ui::AddSkip(inner.data(), st::infoProfileSkip);
	const auto wrap = layout->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			layout,
			std::move(inner)));
	if (setup) {
		setup(rawLabel);
	}
	return wrap;
}

} // namespace

SectionStack::SectionStack(not_null<Ui::VerticalLayout*> layout)
: _layout(layout) {
}

void SectionStack::add(Section section) {
	Expects(!_finalized);
	Expects(section.widget != nullptr);

	_rows.push_back({
		.type = RowType::Block,
		.widget = std::move(section.widget),
		.shown = std::move(section.shown),
	});
}

void SectionStack::addPlainSeparator() {
	Expects(!_finalized);

	if (_rows.empty() || _rows.back().type == RowType::PlainSeparator) {
		return;
	}
	_rows.push_back({ .type = RowType::PlainSeparator });
}

void SectionStack::addTextSeparator(
		rpl::producer<TextWithEntities> text,
		rpl::producer<bool> shown,
		Fn<void(not_null<Ui::FlatLabel*>)> setup) {
	Expects(!_finalized);

	_rows.push_back({
		.type = RowType::TextSeparator,
		.shown = std::move(shown),
		.text = std::move(text),
		.textSetup = std::move(setup),
	});
}

not_null<Ui::VerticalLayout*> SectionStack::layout() const {
	return _layout;
}

// Decides which separators are visible given the intrinsic visibility of
// every row. Blocks and text separators are shown by their own data; the
// computation only resolves plain separators, which exist purely to delimit
// content. A plain separator stays visible only when there is a visible block
// in the run immediately above and the run immediately below it, where each
// run is bounded by the nearest visible divider on that side. Scanning down a
// plain separator treats every later plain separator as a boundary so that, in
// a single gap, the lowest plain separator wins; scanning up it sees through
// plain separators that turned out hidden. Visible text separators bound runs
// on both sides, which keeps a plain separator from doubling a text divider.
std::vector<bool> SectionStack::ComputeVisibility(
		const std::vector<RowType> &kinds,
		const std::vector<bool> &intrinsic) {
	const auto count = int(kinds.size());
	auto result = std::vector<bool>(count, false);
	for (auto i = 0; i != count; ++i) {
		if (kinds[i] != RowType::PlainSeparator) {
			result[i] = intrinsic[i];
		}
	}
	const auto blockVisibleAbove = [&](int separator) {
		for (auto j = separator - 1; j >= 0; --j) {
			switch (kinds[j]) {
			case RowType::Block:
				if (intrinsic[j]) {
					return true;
				}
				break;
			case RowType::TextSeparator:
				if (intrinsic[j]) {
					return false;
				}
				break;
			case RowType::PlainSeparator:
				if (result[j]) {
					return false;
				}
				break;
			}
		}
		return false;
	};
	const auto blockVisibleBelow = [&](int separator) {
		for (auto j = separator + 1; j != count; ++j) {
			switch (kinds[j]) {
			case RowType::Block:
				if (intrinsic[j]) {
					return true;
				}
				break;
			case RowType::TextSeparator:
				if (intrinsic[j]) {
					return false;
				}
				break;
			case RowType::PlainSeparator:
				return false;
			}
		}
		return false;
	};
	for (auto i = 0; i != count; ++i) {
		if (kinds[i] == RowType::PlainSeparator) {
			result[i] = blockVisibleAbove(i) && blockVisibleBelow(i);
		}
	}
	return result;
}

void SectionStack::finalize() {
	Expects(!_finalized);

	_finalized = true;

	Ui::AddSkip(
		_layout,
		AyuUiSettings::isMaterialSettings()
			? style::ConvertScale(4)
			: st::infoProfileSkip);

	const auto count = int(_rows.size());
	const auto cards = std::make_shared<std::vector<QPointer<QWidget>>>();
	const auto blocks = std::make_shared<std::vector<QPointer<QWidget>>>();
	auto kinds = std::vector<RowType>();
	auto intrinsic = std::vector<rpl::producer<bool>>();
	auto separators = std::vector<Ui::SlideWrap<>*>(count, nullptr);
	kinds.reserve(count);
	intrinsic.reserve(count);
	const auto shownOrTrue = [](rpl::producer<bool> shown) {
		return shown
			? std::move(shown)
			: rpl::producer<bool>(rpl::single(true));
	};
	for (auto i = 0; i != count; ++i) {
		auto &row = _rows[i];
		kinds.push_back(row.type);
		switch (row.type) {
		case RowType::Block:
			{
				const auto hasButtons = [](not_null<QWidget*> widget) {
					if (dynamic_cast<Ui::SettingsButton*>(widget.get())) {
						return true;
					}
					for (const auto child : widget->findChildren<QWidget*>()) {
						if (dynamic_cast<Ui::SettingsButton*>(child)) {
							return true;
						}
					}
					return false;
				};
				const auto card = !hasButtons(row.widget.data());
				const auto block = _layout->add(std::move(row.widget));
				blocks->push_back(block);
				if (card) {
					cards->push_back(block);
					block->geometryValue(
					) | rpl::on_next([layout = _layout] {
						layout->update();
					}, block->lifetime());
				}
			}
			intrinsic.push_back(shownOrTrue(std::move(row.shown)));
			break;
		case RowType::PlainSeparator:
			separators[i] = CreatePlainSeparator(_layout);
			intrinsic.push_back(rpl::single(true));
			break;
		case RowType::TextSeparator:
			separators[i] = CreateTextSeparator(
				_layout,
				std::move(row.text),
				std::move(row.textSetup));
			intrinsic.push_back(shownOrTrue(std::move(row.shown)));
			break;
		}
	}

	_layout->paintRequest(
	) | rpl::on_next([=, layout = _layout] {
		if (!AyuUiSettings::isMaterialSettings() || cards->empty()) {
			return;
		}
		auto buttons = std::vector<std::pair<Ui::SettingsButton*, QRect>>();
		for (const auto child : layout->findChildren<QWidget*>()) {
			const auto button = dynamic_cast<Ui::SettingsButton*>(child);
			if (button
				&& button->height() > 0
				&& button->isVisibleTo(layout)
				&& (button->visibleRegion().boundingRect().height()
					>= style::ConvertScale(16))) {
				buttons.emplace_back(
					button,
					QRect(button->mapTo(layout, QPoint()), button->size()));
			}
		}
		auto p = QPainter(layout);
		auto hq = PainterHighQualityEnabler(p);
		const auto side = style::ConvertScale(8);
		const auto join = style::ConvertScale(2);
		const auto reach = style::ConvertScale(12);
		const auto extend = style::ConvertScale(6);
		p.setPen(Qt::NoPen);
		p.setBrush(anim::color(
			st::windowBg->c,
			st::windowFg->c,
			0.045));
		auto joinTop = std::vector<Ui::SettingsButton*>();
		auto joinBottom = std::vector<Ui::SettingsButton*>();
		for (const auto &weak : *cards) {
			const auto block = weak.data();
			if (!block || block->isHidden() || block->height() <= 0) {
				continue;
			}
			const auto blockTop = block->y();
			const auto blockBottom = block->y() + block->height();
			auto below = (Ui::SettingsButton*)nullptr;
			auto belowRect = QRect();
			auto above = (Ui::SettingsButton*)nullptr;
			auto aboveRect = QRect();
			for (const auto &[button, rect] : buttons) {
				if (rect.y() >= blockBottom - reach
					&& rect.y() <= blockBottom + reach
					&& (!below || rect.y() < belowRect.y())) {
					below = button;
					belowRect = rect;
				}
				const auto end = rect.y() + rect.height();
				if (end <= blockTop + reach
					&& end >= blockTop - reach
					&& (!above || end > aboveRect.y() + aboveRect.height())) {
					above = button;
					aboveRect = rect;
				}
			}
			if (below) {
				joinTop.push_back(below);
			}
			if (above) {
				joinBottom.push_back(above);
			}
			const auto along = below
				? belowRect
				: above
				? aboveRect
				: buttons.empty()
				? QRect()
				: buttons.front().second;
			const auto inset = style::ConvertScale(12);
const auto joined = (below || above);
const auto left = joined ? (along.x() + side) : inset;
const auto right = joined
? (along.x() + along.width() - side)
: (layout->width() - inset);
			const auto top = above
				? (aboveRect.y() + aboveRect.height() + join / 2)
				: std::max(
					blockTop + style::ConvertScale(5),
					style::ConvertScale(7));
			const auto bottom = below
				? (belowRect.y() - join / 2)
				: (blockBottom + extend - style::ConvertScale(5));
			const auto r = QRectF(left, top, right - left, bottom - top);
			if (r.width() <= 0 || r.height() <= 0) {
				continue;
			}
			const auto big = std::min(
				qreal(style::ConvertScale(20)),
				r.height() * 0.2);
			const auto small = qreal(style::ConvertScale(6));
			const auto upper = above ? small : big;
			const auto lower = below ? small : big;
			auto path = QPainterPath();
			path.moveTo(r.left() + upper, r.top());
			path.lineTo(r.right() - upper, r.top());
			path.arcTo(QRectF(r.right() - 2 * upper, r.top(), 2 * upper, 2 * upper), 90, -90);
			path.lineTo(r.right(), r.bottom() - lower);
			path.arcTo(QRectF(r.right() - 2 * lower, r.bottom() - 2 * lower, 2 * lower, 2 * lower), 0, -90);
			path.lineTo(r.left() + lower, r.bottom());
			path.arcTo(QRectF(r.left(), r.bottom() - 2 * lower, 2 * lower, 2 * lower), 270, -90);
			path.lineTo(r.left(), r.top() + upper);
			path.arcTo(QRectF(r.left(), r.top(), 2 * upper, 2 * upper), 180, -90);
			path.closeSubpath();
			p.drawPath(path);
		}
		for (const auto &[button, rect] : buttons) {
			const auto top = std::find(joinTop.begin(), joinTop.end(), button) != joinTop.end();
			const auto bottom = std::find(joinBottom.begin(), joinBottom.end(), button) != joinBottom.end();
			if (button->property("hgJoinTop").toBool() != top) {
				button->setProperty("hgJoinTop", top);
				button->update();
			}
			if (button->property("hgJoinBottom").toBool() != bottom) {
				button->setProperty("hgJoinBottom", bottom);
				button->update();
			}
		}
	}, _layout->lifetime());

	Ui::AddSkip(_layout, st::infoProfileSkip);

	if (intrinsic.empty()) {
		return;
	}
	const auto visible = _layout->lifetime().make_state<
		rpl::variable<std::vector<bool>>>(rpl::combine(
			std::move(intrinsic),
			[kinds = std::move(kinds)](const std::vector<bool> &values) {
				return SectionStack::ComputeVisibility(kinds, values);
			}));
	for (auto i = 0; i != count; ++i) {
		const auto wrap = separators[i];
		if (!wrap) {
			continue;
		}
		wrap->setDuration(st::infoSlideDuration)->toggleOn(
			visible->value() | rpl::map([=](const std::vector<bool> &v) {
				return (i < int(v.size())) && v[i];
			}) | rpl::distinct_until_changed());
	}
	visible->changes() | rpl::on_next([layout = _layout] {
		// A toggle landing inside a layout resize pass is skipped there,
		// leaving the rows below the separator overlapped by it.
		InvokeQueued(layout, [=] {
			layout->resizeToWidth(layout->widthNoMargins());
		});
	}, _layout->lifetime());
}

} // namespace Info::Profile
