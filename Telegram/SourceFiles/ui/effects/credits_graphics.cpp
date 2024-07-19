/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/effects/credits_graphics.h"

#include <QtCore/QDateTime>

#include "data/data_credits.h"
#include "data/data_file_origin.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_photo.h"
#include "data/data_photo_media.h"
#include "data/data_session.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/effects/premium_graphics.h"
#include "ui/effects/spoiler_mess.h"
#include "ui/empty_userpic.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/widgets/fields/number_input.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "styles/style_channel_earn.h"
#include "styles/style_credits.h"
#include "styles/style_dialogs.h"
#include "styles/style_intro.h" // introFragmentIcon.
#include "styles/style_layers.h"
#include "styles/style_settings.h"
#include "styles/style_widgets.h"

#include <QtSvg/QSvgRenderer>

namespace Ui {
namespace {

PaintRoundImageCallback MultiThumbnail(
		PaintRoundImageCallback first,
		PaintRoundImageCallback second,
		int totalCount) {
	const auto cache = std::make_shared<QImage>();
	return [=](Painter &p, int x, int y, int outerWidth, int size) {
		const auto stroke = st::lineWidth * 2;
		const auto shift = stroke * 3;
		if (size <= 2 * shift) {
			first(p, x, y, outerWidth, size);
			return;
		}
		const auto smaller = size - shift;
		const auto ratio = style::DevicePixelRatio();
		const auto full = QSize(size, size) * ratio;
		if (cache->size() != full) {
			*cache = QImage(full, QImage::Format_ARGB32_Premultiplied);
			cache->setDevicePixelRatio(ratio);
		}
		cache->fill(Qt::transparent);
		auto q = Painter(cache.get());
		second(q, shift, 0, outerWidth, smaller);
		q.setCompositionMode(QPainter::CompositionMode_Source);
		q.setPen(QPen(Qt::transparent, 2 * stroke));
		q.setBrush(Qt::NoBrush);
		const auto radius = st::roundRadiusLarge;
		auto hq = PainterHighQualityEnabler(q);
		q.drawRoundedRect(QRect(0, shift, smaller, smaller), radius, radius);
		q.setCompositionMode(QPainter::CompositionMode_SourceOver);
		first(q, 0, shift, outerWidth, smaller);
		q.setPen(Qt::NoPen);
		q.setBrush(st::shadowFg);
		q.drawRoundedRect(QRect(0, shift, smaller, smaller), radius, radius);
		q.setPen(st::toastFg);
		q.setFont(style::font(smaller / 2, style::FontFlag::Semibold, 0));
		q.drawText(
			QRect(0, shift, smaller, smaller),
			QString::number(totalCount),
			style::al_center);
		q.end();

		p.drawImage(x, y, *cache);
	};
}

} // namespace

QImage GenerateStars(int height, int count) {
	constexpr auto kOutlineWidth = .6;
	constexpr auto kStrokeWidth = 3;
	constexpr auto kShift = 3;

	auto colorized = qs(Premium::ColorizedSvg(
		Premium::CreditsIconGradientStops()));
	colorized.replace(
		u"stroke=\"none\""_q,
		u"stroke=\"%1\""_q.arg(st::creditsStroke->c.name()));
	colorized.replace(
		u"stroke-width=\"1\""_q,
		u"stroke-width=\"%1\""_q.arg(kStrokeWidth));
	auto svg = QSvgRenderer(colorized.toUtf8());
	svg.setViewBox(svg.viewBox() + Margins(kStrokeWidth));

	const auto starSize = Size(height - kOutlineWidth * 2);

	auto frame = QImage(
		QSize(
			(height + kShift * (count - 1)) * style::DevicePixelRatio(),
			height * style::DevicePixelRatio()),
		QImage::Format_ARGB32_Premultiplied);
	frame.setDevicePixelRatio(style::DevicePixelRatio());
	frame.fill(Qt::transparent);
	const auto drawSingle = [&](QPainter &q) {
		const auto s = kOutlineWidth;
		q.save();
		q.translate(s, s);
		q.setCompositionMode(QPainter::CompositionMode_Clear);
		svg.render(&q, QRectF(QPointF(s, 0), starSize));
		svg.render(&q, QRectF(QPointF(s, s), starSize));
		svg.render(&q, QRectF(QPointF(0, s), starSize));
		svg.render(&q, QRectF(QPointF(-s, s), starSize));
		svg.render(&q, QRectF(QPointF(-s, 0), starSize));
		svg.render(&q, QRectF(QPointF(-s, -s), starSize));
		svg.render(&q, QRectF(QPointF(0, -s), starSize));
		svg.render(&q, QRectF(QPointF(s, -s), starSize));
		q.setCompositionMode(QPainter::CompositionMode_SourceOver);
		svg.render(&q, Rect(starSize));
		q.restore();
	};
	{
		auto q = QPainter(&frame);
		q.translate(frame.width() / style::DevicePixelRatio() - height, 0);
		for (auto i = count; i > 0; --i) {
			drawSingle(q);
			q.translate(-kShift, 0);
		}
	}
	return frame;
}

not_null<RpWidget*> CreateSingleStarWidget(
		not_null<RpWidget*> parent,
		int height) {
	const auto widget = CreateChild<RpWidget>(parent);
	const auto image = GenerateStars(height, 1);
	widget->resize(image.size() / style::DevicePixelRatio());
	widget->paintRequest(
	) | rpl::start_with_next([=] {
		auto p = QPainter(widget);
		p.drawImage(0, 0, image);
	}, widget->lifetime());
	widget->setAttribute(Qt::WA_TransparentForMouseEvents);
	return widget;
}

not_null<MaskedInputField*> AddInputFieldForCredits(
		not_null<VerticalLayout*> container,
		rpl::producer<uint64> value) {
	const auto &st = st::botEarnInputField;
	const auto inputContainer = container->add(
		CreateSkipWidget(container, st.heightMin));
	const auto currentValue = rpl::variable<uint64>(
		rpl::duplicate(value));
	const auto input = CreateChild<NumberInput>(
		inputContainer,
		st,
		tr::lng_bot_earn_out_ph(),
		QString::number(currentValue.current()),
		currentValue.current());
	rpl::duplicate(
		value
	) | rpl::start_with_next([=](uint64 v) {
		input->changeLimit(v);
		input->setText(QString::number(v));
	}, input->lifetime());
	const auto icon = CreateSingleStarWidget(
		inputContainer,
		st.style.font->height);
	inputContainer->sizeValue(
	) | rpl::start_with_next([=](const QSize &size) {
		input->resize(
			size.width() - rect::m::sum::h(st::boxRowPadding),
			st.heightMin);
		input->moveToLeft(st::boxRowPadding.left(), 0);
		icon->moveToLeft(
			st::boxRowPadding.left(),
			st.textMargins.top());
	}, input->lifetime());
	ToggleChildrenVisibility(inputContainer, true);
	return input;
}

PaintRoundImageCallback GenerateCreditsPaintUserpicCallback(
		const Data::CreditsHistoryEntry &entry) {
	using PeerType = Data::CreditsHistoryEntry::PeerType;
	if (entry.peerType == PeerType::PremiumBot) {
		const auto svg = std::make_shared<QSvgRenderer>(Ui::Premium::Svg());
		return [=](Painter &p, int x, int y, int, int size) mutable {
			const auto hq = PainterHighQualityEnabler(p);
			p.setPen(Qt::NoPen);
			{
				auto gradient = QLinearGradient(x + size, y + size, x, y);
				gradient.setStops(Ui::Premium::ButtonGradientStops());
				p.setBrush(gradient);
			}
			p.drawEllipse(x, y, size, size);
			svg->render(&p, QRectF(x, y, size, size) - Margins(size / 5.));
		};
	}
	const auto bg = [&]() -> EmptyUserpic::BgColors {
		switch (entry.peerType) {
		case Data::CreditsHistoryEntry::PeerType::Peer:
			return EmptyUserpic::UserpicColor(0);
		case Data::CreditsHistoryEntry::PeerType::AppStore:
			return { st::historyPeer7UserpicBg, st::historyPeer7UserpicBg2 };
		case Data::CreditsHistoryEntry::PeerType::PlayMarket:
			return { st::historyPeer2UserpicBg, st::historyPeer2UserpicBg2 };
		case Data::CreditsHistoryEntry::PeerType::Fragment:
			return { st::historyPeer8UserpicBg, st::historyPeer8UserpicBg2 };
		case Data::CreditsHistoryEntry::PeerType::PremiumBot:
			return { st::historyPeer8UserpicBg, st::historyPeer8UserpicBg2 };
		case Data::CreditsHistoryEntry::PeerType::Ads:
			return { st::historyPeer6UserpicBg, st::historyPeer6UserpicBg2 };
		case Data::CreditsHistoryEntry::PeerType::Unsupported:
			return {
				st::historyPeerArchiveUserpicBg,
				st::historyPeerArchiveUserpicBg,
			};
		}
		Unexpected("Unknown peer type.");
	}();
	const auto userpic = std::make_shared<EmptyUserpic>(bg, QString());
	return [=](Painter &p, int x, int y, int outerWidth, int size) mutable {
		userpic->paintCircle(p, x, y, outerWidth, size);
		const auto rect = QRect(x, y, size, size);
		((entry.peerType == PeerType::AppStore)
			? st::sessionIconiPhone
			: (entry.peerType == PeerType::PlayMarket)
			? st::sessionIconAndroid
			: (entry.peerType == PeerType::Fragment)
			? st::introFragmentIcon
			: (entry.peerType == PeerType::Ads)
			? st::creditsHistoryEntryTypeAds
			: st::dialogsInaccessibleUserpic).paintInCenter(p, rect);
	};
}

PaintRoundImageCallback GenerateCreditsPaintEntryCallback(
		not_null<PhotoData*> photo,
		Fn<void()> update) {
	struct State {
		std::shared_ptr<Data::PhotoMedia> view;
		Image *imagePtr = nullptr;
		QImage image;
		rpl::lifetime downloadLifetime;
		bool entryImageLoaded = false;
	};
	const auto state = std::make_shared<State>();
	state->view = photo->createMediaView();
	photo->load(Data::PhotoSize::Large, {});

	rpl::single(rpl::empty_value()) | rpl::then(
		photo->owner().session().downloaderTaskFinished()
	) | rpl::start_with_next([=] {
		using Size = Data::PhotoSize;
		if (const auto large = state->view->image(Size::Large)) {
			state->imagePtr = large;
		} else if (const auto small = state->view->image(Size::Small)) {
			state->imagePtr = small;
		} else if (const auto t = state->view->image(Size::Thumbnail)) {
			state->imagePtr = t;
		}
		update();
		if (state->view->loaded()) {
			state->entryImageLoaded = true;
			state->downloadLifetime.destroy();
		}
	}, state->downloadLifetime);

	return [=](Painter &p, int x, int y, int outerWidth, int size) {
		if (state->imagePtr
			&& (!state->entryImageLoaded || state->image.isNull())) {
			const auto image = state->imagePtr->original();
			const auto minSize = std::min(image.width(), image.height());
			state->image = Images::Prepare(
				image.copy(
					(image.width() - minSize) / 2,
					(image.height() - minSize) / 2,
					minSize,
					minSize),
				size * style::DevicePixelRatio(),
				{ .options = Images::Option::RoundLarge });
		}
		p.drawImage(x, y, state->image);
	};
}

PaintRoundImageCallback GenerateCreditsPaintEntryCallback(
		not_null<DocumentData*> video,
		Fn<void()> update) {
	struct State {
		std::shared_ptr<Data::DocumentMedia> view;
		Image *imagePtr = nullptr;
		QImage image;
		rpl::lifetime downloadLifetime;
		bool entryImageLoaded = false;
	};
	const auto state = std::make_shared<State>();
	state->view = video->createMediaView();
	video->loadThumbnail({});

	rpl::single(rpl::empty_value()) | rpl::then(
		video->owner().session().downloaderTaskFinished()
	) | rpl::start_with_next([=] {
		if (const auto thumbnail = state->view->thumbnail()) {
			state->imagePtr = thumbnail;
		}
		update();
		if (state->imagePtr) {
			state->entryImageLoaded = true;
			state->downloadLifetime.destroy();
		}
	}, state->downloadLifetime);

	return [=](Painter &p, int x, int y, int outerWidth, int size) {
		if (state->imagePtr
			&& (!state->entryImageLoaded || state->image.isNull())) {
			const auto image = state->imagePtr->original();
			const auto minSize = std::min(image.width(), image.height());
			state->image = Images::Prepare(
				image.copy(
					(image.width() - minSize) / 2,
					(image.height() - minSize) / 2,
					minSize,
					minSize),
				size * style::DevicePixelRatio(),
				{ .options = Images::Option::RoundLarge });
		}
		p.drawImage(x, y, state->image);
	};
}

PaintRoundImageCallback GenerateCreditsPaintEntryCallback(
		not_null<Main::Session*> session,
		Data::CreditsHistoryMedia media,
		Fn<void()> update) {
	return (media.type == Data::CreditsHistoryMediaType::Photo)
		? GenerateCreditsPaintEntryCallback(
			session->data().photo(media.id),
			std::move(update))
		: GenerateCreditsPaintEntryCallback(
			session->data().document(media.id),
			std::move(update));
}

PaintRoundImageCallback GenerateCreditsPaintEntryCallback(
		not_null<Main::Session*> session,
		const std::vector<Data::CreditsHistoryMedia> &media,
		Fn<void()> update) {
	if (media.size() == 1) {
		return GenerateCreditsPaintEntryCallback(
			session,
			media.front(),
			std::move(update));
	}
	return MultiThumbnail(
		GenerateCreditsPaintEntryCallback(session, media[0], update),
		GenerateCreditsPaintEntryCallback(session, media[1], update),
		media.size());
}

PaintRoundImageCallback GeneratePaidPhotoPaintCallback(
		not_null<PhotoData*> photo,
		Fn<void()> update) {
	struct State {
		explicit State(Fn<void()> update) : spoiler(std::move(update)) {
		}

		QImage image;
		QImage spoilerCornerCache;
		SpoilerAnimation spoiler;
	};
	const auto state = std::make_shared<State>(update);

	return [=](Painter &p, int x, int y, int outerWidth, int size) {
		if (state->image.isNull()) {
			const auto media = photo->createMediaView();
			const auto thumbnail = media->thumbnailInline();
			const auto ratio = style::DevicePixelRatio();
			const auto scaled = QSize(size, size) * ratio;
			auto image = thumbnail
				? Images::Blur(thumbnail->original(), true)
				: QImage(scaled, QImage::Format_ARGB32_Premultiplied);
			if (!thumbnail) {
				image.fill(Qt::black);
				image.setDevicePixelRatio(ratio);
			}
			const auto minSize = std::min(image.width(), image.height());
			state->image = Images::Prepare(
				image.copy(
					(image.width() - minSize) / 2,
					(image.height() - minSize) / 2,
					minSize,
					minSize),
				size * ratio,
				{ .options = Images::Option::RoundLarge });
		}
		p.drawImage(x, y, state->image);
		FillSpoilerRect(
			p,
			QRect(x, y, size, size),
			Images::CornersMaskRef(
				Images::CornersMask(ImageRoundRadius::Large)),
			DefaultImageSpoiler().frame(
				state->spoiler.index(crl::now(), false)),
			state->spoilerCornerCache);
	};
}

PaintRoundImageCallback GeneratePaidMediaPaintCallback(
		not_null<PhotoData*> photo,
		PhotoData *second,
		int totalCount,
		Fn<void()> update) {
	if (!second) {
		return GeneratePaidPhotoPaintCallback(photo, update);
	}
	return MultiThumbnail(
		GeneratePaidPhotoPaintCallback(photo, update),
		GeneratePaidPhotoPaintCallback(second, update),
		totalCount);
}

Fn<PaintRoundImageCallback(Fn<void()>)> PaintPreviewCallback(
		not_null<Main::Session*> session,
		const Data::CreditsHistoryEntry &entry) {
	const auto &extended = entry.extended;
	if (!extended.empty()) {
		return [=](Fn<void()> update) {
			return GenerateCreditsPaintEntryCallback(
				session,
				extended,
				std::move(update));
		};
	} else if (entry.photoId) {
		const auto photo = session->data().photo(entry.photoId);
		return [=](Fn<void()> update) {
			return GenerateCreditsPaintEntryCallback(
				photo,
				std::move(update));
		};
	}
	return nullptr;
}

TextWithEntities GenerateEntryName(const Data::CreditsHistoryEntry &entry) {
	return ((entry.peerType == Data::CreditsHistoryEntry::PeerType::Fragment)
		? tr::lng_bot_username_description1_link
		: (entry.peerType == Data::CreditsHistoryEntry::PeerType::PremiumBot)
		? tr::lng_credits_box_history_entry_premium_bot
		: (entry.peerType == Data::CreditsHistoryEntry::PeerType::Ads)
		? tr::lng_credits_box_history_entry_ads
		: tr::lng_credits_summary_history_entry_inner_in)(
			tr::now,
			TextWithEntities::Simple);
}

} // namespace Ui
