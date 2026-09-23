#include "homepage.h"

#include <QAbstractButton>
#include <QEnterEvent>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QRadialGradient>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QScroller>
#include <QTimer>
#include <QVariantAnimation>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace {

constexpr auto kCyan = "#78f5e7";
constexpr auto kCyanLight = "#b6fff6";
constexpr auto kViolet = "#a788ff";
constexpr auto kBlue = "#6aa8ff";
constexpr auto kText = "#f5f7fb";

QColor themeColor(int theme)
{
    switch (theme % 5) {
    case 1: return QColor(kViolet);
    case 2: return QColor(kBlue);
    case 3: return QColor("#e17cff");
    case 4: return QColor("#ff9f70");
    default: return QColor(kCyan);
    }
}

class VuiCard final : public QAbstractButton
{
public:
    enum class Kind {
        Continue,
        Poster,
        CollectionLarge,
        CollectionSmall,
        Landscape
    };

    VuiCard(Kind kind,
            QString title,
            QString meta,
            QString badge,
            int theme,
            QWidget *parent = nullptr)
        : QAbstractButton(parent)
        , kind_(kind)
        , title_(std::move(title))
        , meta_(std::move(meta))
        , badge_(std::move(badge))
        , theme_(theme)
    {
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::StrongFocus);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        setAttribute(Qt::WA_Hover, true);
    }

    QSize sizeHint() const override
    {
        switch (kind_) {
        case Kind::Continue: return {390, 220};
        case Kind::Poster: return {216, 322};
        case Kind::CollectionLarge: return {610, 300};
        case Kind::CollectionSmall: return {300, 144};
        case Kind::Landscape: return {350, 202};
        }
        return {320, 200};
    }

    void setTitle(const QString &title)
    {
        title_ = title;
        update();
    }

    void setMeta(const QString &meta)
    {
        meta_ = meta;
        update();
    }

    void setBadge(const QString &badge)
    {
        badge_ = badge;
        update();
    }

    void setProgress(qreal progress)
    {
        progress_ = std::clamp(progress, qreal(0), qreal(1));
        update();
    }

protected:
    void enterEvent(QEnterEvent *event) override
    {
        animateHover(1.0);
        QAbstractButton::enterEvent(event);
    }

    void leaveEvent(QEvent *event) override
    {
        animateHover(0.0);
        QAbstractButton::leaveEvent(event);
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const qreal lift = hover_ * 5.0;
        p.translate(0, -lift);

        QRectF r = rect();
        r.adjust(1.5, 7.0 + lift, -1.5, -2.0);
        const qreal radius = kind_ == Kind::Poster ? 22.0 : 24.0;

        QPainterPath clip;
        clip.addRoundedRect(r, radius, radius);
        p.setClipPath(clip);

        const QColor accent = themeColor(theme_);
        QLinearGradient base(r.topLeft(), r.bottomRight());
        base.setColorAt(0.0, QColor(13, 17, 25));
        base.setColorAt(0.48, QColor(accent.red(), accent.green(), accent.blue(), 52 + int(hover_ * 22)));
        base.setColorAt(1.0, QColor(5, 6, 9));
        p.fillPath(clip, base);

        // Abstract Vui artwork: orb, horizon slash and ambient glow.
        const QPointF orbCenter(r.left() + r.width() * (kind_ == Kind::Poster ? .55 : .66),
                                r.top() + r.height() * (kind_ == Kind::Poster ? .40 : .38));
        const qreal orbRadius = std::min(r.width(), r.height()) * (kind_ == Kind::Poster ? .34 : .31);
        QRadialGradient orb(orbCenter - QPointF(orbRadius * .28, orbRadius * .30), orbRadius * 1.25);
        orb.setColorAt(0.0, QColor(225, 255, 252, 120));
        orb.setColorAt(0.18, QColor(accent.red(), accent.green(), accent.blue(), 135));
        orb.setColorAt(0.56, QColor(accent.red() / 2, accent.green() / 2, accent.blue() / 2, 90));
        orb.setColorAt(1.0, QColor(5, 7, 12, 0));
        p.setPen(Qt::NoPen);
        p.setBrush(orb);
        p.drawEllipse(orbCenter, orbRadius, orbRadius);

        p.setPen(QPen(QColor(accent.red(), accent.green(), accent.blue(), 80 + int(hover_ * 70)), 1.0));
        const QPointF a(r.left() + r.width() * .12, r.top() + r.height() * .28);
        const QPointF b(r.right() - r.width() * .08, r.top() + r.height() * .64);
        p.drawLine(a, b);

        p.setPen(Qt::NoPen);
        QLinearGradient shade(r.left(), r.top() + r.height() * .36, r.left(), r.bottom());
        shade.setColorAt(0.0, QColor(5, 6, 9, 0));
        shade.setColorAt(0.55, QColor(5, 6, 9, 92));
        shade.setColorAt(1.0, QColor(5, 6, 9, 235));
        p.fillRect(r, shade);

        p.setClipping(false);
        p.setPen(QPen(QColor(255, 255, 255, 22 + int(hover_ * 26)), 1));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(r, radius, radius);

        const int left = int(r.left()) + 18;
        const int right = int(r.right()) - 18;
        const int bottom = int(r.bottom());

        if (kind_ == Kind::Poster) {
            QFont rankFont = font();
            rankFont.setPixelSize(43);
            rankFont.setWeight(QFont::Black);
            p.setFont(rankFont);
            p.setPen(QColor(255, 255, 255, 30));
            p.drawText(QRect(left, int(r.top()) + 13, int(r.width()) - 28, 55),
                       Qt::AlignLeft | Qt::AlignVCenter,
                       QString("%1").arg(theme_ + 1, 2, 10, QChar('0')));

            drawPill(p, QRect(left, bottom - 91, 72, 22), badge_, accent);
            drawTitle(p, QRect(left, bottom - 63, right - left, 27), title_, 16);
            drawMeta(p, QRect(left, bottom - 35, right - left, 20), meta_);
        } else if (kind_ == Kind::CollectionLarge || kind_ == Kind::CollectionSmall) {
            QFont indexFont = font();
            indexFont.setPixelSize(10);
            indexFont.setWeight(QFont::DemiBold);
            indexFont.setLetterSpacing(QFont::AbsoluteSpacing, 1.6);
            p.setFont(indexFont);
            p.setPen(QColor(accent.red(), accent.green(), accent.blue(), 205));
            p.drawText(QRect(left, int(r.top()) + 20, 90, 20),
                       Qt::AlignLeft | Qt::AlignVCenter,
                       badge_);
            const int titleSize = kind_ == Kind::CollectionLarge ? 29 : 20;
            drawTitle(p, QRect(left, bottom - (kind_ == Kind::CollectionLarge ? 114 : 77),
                               right - left, kind_ == Kind::CollectionLarge ? 72 : 48),
                      title_, titleSize);
            drawMeta(p, QRect(left, bottom - 35, right - left, 20), meta_);
            if (kind_ == Kind::CollectionLarge) {
                p.setPen(QColor(kCyanLight));
                QFont arrowFont = font();
                arrowFont.setPixelSize(20);
                p.setFont(arrowFont);
                p.drawText(QRect(right - 36, bottom - 55, 34, 34), Qt::AlignCenter, QStringLiteral("↗"));
            }
        } else {
            if (!badge_.isEmpty()) {
                drawPill(p, QRect(left, int(r.top()) + 17, 84, 22), badge_, accent);
            }
            drawTitle(p, QRect(left, bottom - 64, right - left - 42, 28), title_, 16);
            drawMeta(p, QRect(left, bottom - 35, right - left - 42, 20), meta_);

            p.setPen(QColor(kCyanLight));
            QFont playFont = font();
            playFont.setPixelSize(14);
            playFont.setWeight(QFont::Bold);
            p.setFont(playFont);
            p.drawText(QRect(right - 34, bottom - 62, 34, 34), Qt::AlignCenter, QStringLiteral("▶"));

            if (kind_ == Kind::Continue) {
                const QRectF track(r.left(), r.bottom() - 4, r.width(), 4);
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(255, 255, 255, 28));
                p.drawRect(track);
                p.setBrush(accent);
                p.drawRect(QRectF(track.left(), track.top(), track.width() * progress_, track.height()));
            }
        }

        if (hover_ > .01) {
            p.setPen(QPen(QColor(accent.red(), accent.green(), accent.blue(), int(72 * hover_)), 2));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(r.adjusted(1, 1, -1, -1), radius - 1, radius - 1);
        }
    }

private:
    void animateHover(qreal target)
    {
        if (hoverAnimation_) {
            hoverAnimation_->stop();
            hoverAnimation_->deleteLater();
        }
        hoverAnimation_ = new QVariantAnimation(this);
        hoverAnimation_->setStartValue(hover_);
        hoverAnimation_->setEndValue(target);
        hoverAnimation_->setDuration(220);
        hoverAnimation_->setEasingCurve(QEasingCurve::OutCubic);
        connect(hoverAnimation_, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
            hover_ = v.toReal();
            update();
        });
        connect(hoverAnimation_, &QVariantAnimation::finished, this, [this] {
            hoverAnimation_->deleteLater();
            hoverAnimation_ = nullptr;
        });
        hoverAnimation_->start();
    }

    static void drawTitle(QPainter &p, const QRect &rect, const QString &text, int px)
    {
        QFont f = p.font();
        f.setPixelSize(px);
        f.setWeight(QFont::DemiBold);
        p.setFont(f);
        p.setPen(QColor(kText));
        p.drawText(rect, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap, text);
    }

    static void drawMeta(QPainter &p, const QRect &rect, const QString &text)
    {
        QFont f = p.font();
        f.setPixelSize(10);
        f.setWeight(QFont::Medium);
        p.setFont(f);
        p.setPen(QColor(235, 239, 248, 128));
        p.drawText(rect, Qt::AlignLeft | Qt::AlignVCenter, text);
    }

    static void drawPill(QPainter &p, const QRect &rect, const QString &text, const QColor &accent)
    {
        p.setPen(QPen(QColor(accent.red(), accent.green(), accent.blue(), 80), 1));
        p.setBrush(QColor(accent.red(), accent.green(), accent.blue(), 22));
        p.drawRoundedRect(rect, 7, 7);
        QFont f = p.font();
        f.setPixelSize(8);
        f.setWeight(QFont::Bold);
        f.setLetterSpacing(QFont::AbsoluteSpacing, 1.0);
        p.setFont(f);
        p.setPen(QColor(accent.red(), accent.green(), accent.blue(), 220));
        p.drawText(rect, Qt::AlignCenter, text);
    }

    Kind kind_;
    QString title_;
    QString meta_;
    QString badge_;
    int theme_ = 0;
    qreal progress_ = .5;
    qreal hover_ = 0.0;
    QVariantAnimation *hoverAnimation_ = nullptr;
};

class HeroWidget final : public QWidget
{
public:
    explicit HeroWidget(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setObjectName("homeHero");
        setMinimumHeight(700);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(72, 170, 70, 72);
        layout->setSpacing(0);

        auto *kicker = new QLabel("—  NOVA / VUI ORIGINAL", this);
        kicker->setObjectName("heroKicker");
        layout->addWidget(kicker, 0, Qt::AlignLeft);
        layout->addSpacing(17);

        auto *title = new QLabel(
            "<span style='color:#f5f7fb'>Beyond the</span><br>"
            "<span style='color:rgba(245,247,251,0.54);font-weight:400'>Synthetic Horizon</span>",
            this);
        title->setTextFormat(Qt::RichText);
        title->setObjectName("heroTitle");
        title->setMaximumWidth(650);
        layout->addWidget(title, 0, Qt::AlignLeft);
        layout->addSpacing(20);

        auto *summary = new QLabel(
            "A signal from the edge of mapped space changes everything. Drift deeper into a world "
            "where memory, machine and light begin to merge.",
            this);
        summary->setObjectName("heroSummary");
        summary->setWordWrap(true);
        summary->setMaximumWidth(560);
        layout->addWidget(summary, 0, Qt::AlignLeft);
        layout->addSpacing(16);

        auto *meta = new QWidget(this);
        auto *metaLayout = new QHBoxLayout(meta);
        metaLayout->setContentsMargins(0, 0, 0, 0);
        metaLayout->setSpacing(12);
        const QStringList items = {"98% Match", "2026", "16+", "1h 58m", "4K", "HDR"};
        for (int i = 0; i < items.size(); ++i) {
            auto *label = new QLabel(items[i], meta);
            label->setProperty("heroMeta", true);
            if (i == 0) label->setProperty("accent", true);
            if (i >= 2 && i != 3) label->setProperty("boxed", true);
            metaLayout->addWidget(label);
        }
        metaLayout->addStretch();
        layout->addWidget(meta, 0, Qt::AlignLeft);
        layout->addSpacing(28);

        auto *actions = new QWidget(this);
        auto *actionLayout = new QHBoxLayout(actions);
        actionLayout->setContentsMargins(0, 0, 0, 0);
        actionLayout->setSpacing(10);

        playButton_ = new QPushButton("▶   Play", actions);
        playButton_->setObjectName("heroPlay");
        playButton_->setCursor(Qt::PointingHandCursor);
        auto *more = new QPushButton("ⓘ   More info", actions);
        more->setObjectName("heroMore");
        more->setCursor(Qt::PointingHandCursor);
        actionLayout->addWidget(playButton_);
        actionLayout->addWidget(more);
        actionLayout->addStretch();
        layout->addWidget(actions, 0, Qt::AlignLeft);
        layout->addStretch();

        timer_.setInterval(16);
        timer_.setTimerType(Qt::PreciseTimer);
        connect(&timer_, &QTimer::timeout, this, [this] {
            phase_ += .009;
            if (phase_ > 1000.0) phase_ = 0.0;
            update();
        });
    }

    QPushButton *playButton() const { return playButton_; }

protected:
    void showEvent(QShowEvent *event) override
    {
        QWidget::showEvent(event);
        if (!timer_.isActive()) timer_.start();
    }

    void hideEvent(QHideEvent *event) override
    {
        timer_.stop();
        QWidget::hideEvent(event);
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const QRectF r = rect();
        QLinearGradient base(r.topLeft(), r.bottomRight());
        base.setColorAt(0, QColor("#080b12"));
        base.setColorAt(.46, QColor("#090c13"));
        base.setColorAt(1, QColor("#040506"));
        p.fillRect(r, base);

        const qreal w = r.width();
        const qreal h = r.height();
        const qreal floatY = std::sin(phase_ * 2.0) * 9.0;
        const QPointF planetCenter(w * .73, h * .43 + floatY);
        const qreal planetRadius = std::min(w * .215, 300.0);

        QRadialGradient halo(planetCenter, planetRadius * 1.55);
        halo.setColorAt(0, QColor(120, 245, 231, 25));
        halo.setColorAt(.55, QColor(167, 136, 255, 18));
        halo.setColorAt(1, QColor(5, 6, 9, 0));
        p.setPen(Qt::NoPen);
        p.setBrush(halo);
        p.drawEllipse(planetCenter, planetRadius * 1.55, planetRadius * 1.55);

        QRadialGradient planet(planetCenter - QPointF(planetRadius * .30, planetRadius * .30), planetRadius * 1.25);
        planet.setColorAt(0, QColor(189, 255, 248, 78));
        planet.setColorAt(.24, QColor(84, 163, 157, 62));
        planet.setColorAt(.58, QColor(45, 37, 80, 180));
        planet.setColorAt(.78, QColor("#0a0c12"));
        planet.setColorAt(1, QColor("#050609"));
        p.setBrush(planet);
        p.drawEllipse(planetCenter, planetRadius, planetRadius);

        p.setPen(QPen(QColor(120, 245, 231, 52), 1));
        p.save();
        p.translate(planetCenter);
        p.rotate(-12);
        p.drawEllipse(QPointF(0, 0), planetRadius * 1.34, planetRadius * .20);
        p.restore();

        const qreal smallR = 58.0;
        const QPointF small(w * .50, h * .28 - std::sin(phase_ * 1.4) * 8.0);
        QRadialGradient smallOrb(small - QPointF(14, 14), smallR * 1.2);
        smallOrb.setColorAt(0, QColor(190, 170, 255, 120));
        smallOrb.setColorAt(.5, QColor(60, 48, 105, 120));
        smallOrb.setColorAt(1, QColor(5, 6, 9, 0));
        p.setPen(Qt::NoPen);
        p.setBrush(smallOrb);
        p.drawEllipse(small, smallR, smallR);

        // Perspective grid. One animation source drives all decorative motion.
        p.save();
        p.setClipRect(QRectF(w * .28, h * .48, w * .72, h * .52));
        const qreal drift = std::fmod(phase_ * 90.0, 56.0);
        p.setPen(QPen(QColor(120, 245, 231, 24), 1));
        for (int y = int(h * .58 - drift); y < h + 80; y += 56) {
            const qreal t = (y - h * .50) / (h * .50);
            p.drawLine(QPointF(w * (.34 - t * .08), y), QPointF(w * (1.04 + t * .10), y));
        }
        for (int i = -8; i < 18; ++i) {
            const qreal xBottom = w * .34 + i * 76.0;
            p.drawLine(QPointF(w * .69, h * .50), QPointF(xBottom, h + 40));
        }
        p.restore();

        p.setPen(QPen(QColor(120, 245, 231, 68 + int(24 * std::sin(phase_ * 2.7))), 1));
        p.drawLine(QPointF(w * .54, h * .29), QPointF(w * .98, h * .18));
        p.setPen(QPen(QColor(167, 136, 255, 54), 1));
        p.drawLine(QPointF(w * .60, h * .66), QPointF(w * .94, h * .76));

        p.setPen(Qt::NoPen);
        for (int i = 0; i < 54; ++i) {
            const qreal x = std::fmod(i * 97.0 + 17.0 + phase_ * 5.0, w);
            const qreal y = std::fmod(i * 53.0 + 31.0, h * .82);
            const int alpha = 30 + (i % 5) * 12;
            p.setBrush(QColor(255, 255, 255, alpha));
            p.drawEllipse(QPointF(x, y), .8 + (i % 3) * .35, .8 + (i % 3) * .35);
        }

        QLinearGradient vignette(0, 0, w, 0);
        vignette.setColorAt(0, QColor(4, 5, 8, 244));
        vignette.setColorAt(.31, QColor(4, 5, 8, 187));
        vignette.setColorAt(.58, QColor(4, 5, 8, 38));
        vignette.setColorAt(.80, QColor(4, 5, 8, 24));
        vignette.setColorAt(1, QColor(4, 5, 8, 135));
        p.fillRect(r, vignette);

        QLinearGradient bottomFade(0, h * .55, 0, h);
        bottomFade.setColorAt(0, QColor(5, 6, 9, 0));
        bottomFade.setColorAt(1, QColor(5, 6, 9, 245));
        p.fillRect(r, bottomFade);
    }

private:
    QPushButton *playButton_ = nullptr;
    QTimer timer_;
    qreal phase_ = 0.0;
};

QLabel *makeEyebrow(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setObjectName("sectionEyebrow");
    return label;
}

QLabel *makeSectionTitle(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setObjectName("sectionTitle");
    return label;
}

QWidget *makeSectionHeader(const QString &eyebrow,
                           const QString &title,
                           const QString &action,
                           QWidget *parent)
{
    auto *header = new QWidget(parent);
    auto *layout = new QHBoxLayout(header);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);

    auto *left = new QWidget(header);
    auto *leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(5);
    leftLayout->addWidget(makeEyebrow(eyebrow, left));
    leftLayout->addWidget(makeSectionTitle(title, left));

    layout->addWidget(left);
    layout->addStretch();

    if (!action.isEmpty()) {
        auto *label = new QLabel(action + "  ↗", header);
        label->setObjectName("sectionAction");
        layout->addWidget(label, 0, Qt::AlignBottom);
    }
    return header;
}

QScrollArea *makeRail(QWidget *parent, int height)
{
    auto *area = new QScrollArea(parent);
    area->setObjectName("homeRail");
    area->setWidgetResizable(true);
    area->setFrameShape(QFrame::NoFrame);
    area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    area->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    area->setFixedHeight(height);
    area->viewport()->setAutoFillBackground(false);
    QScroller::grabGesture(area->viewport(), QScroller::LeftMouseButtonGesture);
    return area;
}

QWidget *makeRailContent(QScrollArea *area, QHBoxLayout *&layout)
{
    auto *content = new QWidget(area);
    content->setObjectName("homeRailContent");
    layout = new QHBoxLayout(content);
    layout->setContentsMargins(0, 7, 0, 5);
    layout->setSpacing(16);
    area->setWidget(content);
    return content;
}

QFrame *makeStage(QWidget *parent)
{
    auto *stage = new QFrame(parent);
    stage->setObjectName("contentStage");
    auto *layout = new QVBoxLayout(stage);
    layout->setContentsMargins(68, 46, 68, 48);
    layout->setSpacing(22);
    return stage;
}

QPushButton *makeNavButton(const QString &text, QWidget *parent)
{
    auto *button = new QPushButton(text, parent);
    button->setProperty("homeNavLink", true);
    button->setCursor(Qt::PointingHandCursor);
    button->setFocusPolicy(Qt::NoFocus);
    return button;
}

} // namespace

HomePage::HomePage(QWidget *parent)
    : QWidget(parent)
{
    setObjectName("homePage");
    setMinimumWidth(720);

    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    scroll_ = new QScrollArea(this);
    scroll_->setObjectName("homeScroll");
    scroll_->setWidgetResizable(true);
    scroll_->setFrameShape(QFrame::NoFrame);
    scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto *canvas = new QWidget(scroll_);
    canvas->setObjectName("homeCanvas");
    auto *canvasLayout = new QVBoxLayout(canvas);
    canvasLayout->setContentsMargins(0, 0, 0, 0);
    canvasLayout->setSpacing(0);

    auto *hero = new HeroWidget(canvas);
    connect(hero->playButton(), &QPushButton::clicked, this, &HomePage::openVideoRequested);
    canvasLayout->addWidget(hero);

    auto *continueStage = makeStage(canvas);
    auto *continueLayout = qobject_cast<QVBoxLayout *>(continueStage->layout());
    continueLayout->addWidget(makeSectionHeader("RESUME YOUR JOURNEY", "Continue watching", "View all", continueStage));

    QHBoxLayout *continueRailLayout = nullptr;
    auto *continueRail = makeRail(continueStage, 235);
    makeRailContent(continueRail, continueRailLayout);

    auto *resume = new VuiCard(VuiCard::Kind::Continue, "Neon Fields", "31 min left", "S1 · E4", 0, continueRail);
    resume->setProgress(.68);
    resumeCard_ = resume;
    connect(resume, &QAbstractButton::clicked, this, [this] {
        if (currentMediaAvailable_) emit resumeRequested();
        else emit openVideoRequested();
    });
    continueRailLayout->addWidget(resume);

    auto *continue2 = new VuiCard(VuiCard::Kind::Continue, "Afterlight", "42 min left", "S2 · E1", 1, continueRail);
    continue2->setProgress(.44);
    connect(continue2, &QAbstractButton::clicked, this, &HomePage::openVideoRequested);
    continueRailLayout->addWidget(continue2);

    auto *continue3 = new VuiCard(VuiCard::Kind::Continue, "Signal Zero", "1h 12m left", "FILM", 2, continueRail);
    continue3->setProgress(.29);
    connect(continue3, &QAbstractButton::clicked, this, &HomePage::openVideoRequested);
    continueRailLayout->addWidget(continue3);
    continueRailLayout->addStretch();
    continueLayout->addWidget(continueRail);
    canvasLayout->addWidget(continueStage);

    auto *trendingStage = makeStage(canvas);
    auto *trendingLayout = qobject_cast<QVBoxLayout *>(trendingStage->layout());
    trendingLayout->addWidget(makeSectionHeader("WHAT EVERYONE IS WATCHING", "Trending now", "Browse", trendingStage));

    QHBoxLayout *posterLayout = nullptr;
    auto *posterRail = makeRail(trendingStage, 344);
    makeRailContent(posterRail, posterLayout);
    const struct { const char *title; const char *meta; const char *badge; } posters[] = {
        {"Orbital", "2026 · 1h 46m", "SCI-FI"},
        {"Vector", "2026 · 8 Episodes", "THRILLER"},
        {"Solace", "2025 · 2h 08m", "DRAMA"},
        {"Ghost Signal", "2026 · 6 Episodes", "MYSTERY"},
        {"Velocity", "2025 · 1h 51m", "ACTION"}
    };
    for (int i = 0; i < 5; ++i) {
        auto *card = new VuiCard(VuiCard::Kind::Poster,
                                 posters[i].title,
                                 posters[i].meta,
                                 posters[i].badge,
                                 i,
                                 posterRail);
        connect(card, &QAbstractButton::clicked, this, &HomePage::openVideoRequested);
        posterLayout->addWidget(card);
    }
    posterLayout->addStretch();
    trendingLayout->addWidget(posterRail);
    canvasLayout->addWidget(trendingStage);

    auto *collectionsStage = makeStage(canvas);
    auto *collectionsLayout = qobject_cast<QVBoxLayout *>(collectionsStage->layout());
    collectionsLayout->addWidget(makeSectionHeader("CURATED FOR LATE NIGHTS", "Worlds beyond", "Explore collection", collectionsStage));

    auto *collectionRow = new QWidget(collectionsStage);
    auto *collectionRowLayout = new QHBoxLayout(collectionRow);
    collectionRowLayout->setContentsMargins(0, 6, 0, 0);
    collectionRowLayout->setSpacing(16);

    auto *large = new VuiCard(VuiCard::Kind::CollectionLarge,
                              "Dreams\nof Europa",
                              "12 films · Science fiction",
                              "V/01",
                              0,
                              collectionRow);
    connect(large, &QAbstractButton::clicked, this, &HomePage::openVideoRequested);
    collectionRowLayout->addWidget(large, 1);

    auto *smallStack = new QWidget(collectionRow);
    auto *smallLayout = new QVBoxLayout(smallStack);
    smallLayout->setContentsMargins(0, 0, 0, 0);
    smallLayout->setSpacing(12);
    auto *smallA = new VuiCard(VuiCard::Kind::CollectionSmall, "Electric\nMemory", "8 films", "V/02", 1, smallStack);
    auto *smallB = new VuiCard(VuiCard::Kind::CollectionSmall, "Dark\nOceans", "10 films", "V/03", 2, smallStack);
    connect(smallA, &QAbstractButton::clicked, this, &HomePage::openVideoRequested);
    connect(smallB, &QAbstractButton::clicked, this, &HomePage::openVideoRequested);
    smallLayout->addWidget(smallA);
    smallLayout->addWidget(smallB);
    collectionRowLayout->addWidget(smallStack);
    collectionRowLayout->addStretch();
    collectionsLayout->addWidget(collectionRow);
    canvasLayout->addWidget(collectionsStage);

    auto *newStage = makeStage(canvas);
    auto *newLayout = qobject_cast<QVBoxLayout *>(newStage->layout());
    newLayout->addWidget(makeSectionHeader("JUST ADDED TO NOVA / VUI", "Fresh transmissions", "UPDATED NOW", newStage));

    QHBoxLayout *landscapeLayout = nullptr;
    auto *landscapeRail = makeRail(newStage, 220);
    makeRailContent(landscapeRail, landscapeLayout);
    const struct { const char *title; const char *meta; } fresh[] = {
        {"Continuum", "FILM · 2026"},
        {"Glass City", "SERIES · 2026"},
        {"Still Light", "FILM · 2025"},
        {"The Divide", "SERIES · 2026"}
    };
    for (int i = 0; i < 4; ++i) {
        auto *card = new VuiCard(VuiCard::Kind::Landscape, fresh[i].title, fresh[i].meta, "", i, landscapeRail);
        connect(card, &QAbstractButton::clicked, this, &HomePage::openVideoRequested);
        landscapeLayout->addWidget(card);
    }
    landscapeLayout->addStretch();
    newLayout->addWidget(landscapeRail);
    canvasLayout->addWidget(newStage);

    auto *footer = new QWidget(canvas);
    footer->setObjectName("homeFooter");
    auto *footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(68, 38, 68, 42);
    auto *footerBrand = new QLabel("◉  NOVA / VUI", footer);
    footerBrand->setObjectName("footerBrand");
    auto *footerCopy = new QLabel("Offline interface · Powered by NovaPlayer", footer);
    footerCopy->setObjectName("footerCopy");
    footerLayout->addWidget(footerBrand);
    footerLayout->addStretch();
    footerLayout->addWidget(footerCopy);
    canvasLayout->addWidget(footer);

    scroll_->setWidget(canvas);
    rootLayout->addWidget(scroll_);

    // Floating navigation remains outside the scroll area, matching Vui's fixed glass nav.
    nav_ = new QFrame(this);
    nav_->setObjectName("homeNav");
    nav_->setFixedHeight(66);
    auto *navLayout = new QHBoxLayout(nav_);
    navLayout->setContentsMargins(15, 9, 11, 9);
    navLayout->setSpacing(18);

    auto *brand = new QLabel("◉  NOVA / VUI", nav_);
    brand->setObjectName("homeBrand");
    navLayout->addWidget(brand);

    navLinks_ = new QWidget(nav_);
    auto *linkLayout = new QHBoxLayout(navLinks_);
    linkLayout->setContentsMargins(18, 0, 18, 0);
    linkLayout->setSpacing(18);
    auto *homeLink = makeNavButton("Home", navLinks_);
    homeLink->setProperty("active", true);
    auto *discoverLink = makeNavButton("Discover", navLinks_);
    auto *collectionsLink = makeNavButton("Collections", navLinks_);
    auto *newLink = makeNavButton("New", navLinks_);
    linkLayout->addWidget(homeLink);
    linkLayout->addWidget(discoverLink);
    linkLayout->addWidget(collectionsLink);
    linkLayout->addWidget(newLink);
    navLayout->addWidget(navLinks_, 1, Qt::AlignCenter);

    auto *openButton = new QPushButton("OPEN VIDEO", nav_);
    openButton->setObjectName("homeOpen");
    openButton->setCursor(Qt::PointingHandCursor);
    connect(openButton, &QPushButton::clicked, this, &HomePage::openVideoRequested);
    navLayout->addWidget(openButton);

    auto *search = new QPushButton("⌕", nav_);
    search->setProperty("homeRound", true);
    search->setToolTip("Search (visual only)");
    auto *notifications = new QPushButton("•", nav_);
    notifications->setProperty("homeRound", true);
    notifications->setToolTip("Notifications (visual only)");
    auto *profile = new QPushButton("M", nav_);
    profile->setObjectName("homeProfile");
    profile->setToolTip("Profile (visual only)");
    for (auto *b : {search, notifications, profile}) {
        b->setCursor(Qt::PointingHandCursor);
        b->setFocusPolicy(Qt::NoFocus);
        navLayout->addWidget(b);
    }

    connect(homeLink, &QPushButton::clicked, this, [this] {
        scroll_->verticalScrollBar()->setValue(0);
    });
    connect(discoverLink, &QPushButton::clicked, this, [this, trendingStage] {
        scroll_->ensureWidgetVisible(trendingStage, 0, 105);
    });
    connect(collectionsLink, &QPushButton::clicked, this, [this, collectionsStage] {
        scroll_->ensureWidgetVisible(collectionsStage, 0, 105);
    });
    connect(newLink, &QPushButton::clicked, this, [this, newStage] {
        scroll_->ensureWidgetVisible(newStage, 0, 105);
    });

    nav_->raise();
}

void HomePage::setCurrentMedia(const QString &displayName, bool available)
{
    currentMediaAvailable_ = available;
    currentMediaName_ = displayName;

    auto *card = static_cast<VuiCard *>(resumeCard_);
    if (!card) return;

    if (available && !displayName.isEmpty()) {
        card->setTitle(displayName);
        card->setMeta("Resume current session");
        card->setBadge("LOCAL");
        card->setProgress(.62);
    } else {
        card->setTitle("Neon Fields");
        card->setMeta("31 min left");
        card->setBadge("S1 · E4");
        card->setProgress(.68);
    }
}

void HomePage::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);

    const int margin = std::clamp(width() / 32, 18, 48);
    const int navWidth = std::min(width() - margin * 2, 1500);
    nav_->setGeometry((width() - navWidth) / 2, 20, navWidth, 66);
    nav_->raise();

    navLinks_->setVisible(width() >= 980);
}
