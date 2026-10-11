#include "UI/FilterSheet.h"
#include "UI/ColorPaletteControls.h"
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QSlider>
#include <QVBoxLayout>

// Swift's scanlinesControls, and the colours Dither shares.
void FilterSheet::scanlines()
{
    const auto key = [](double ScanlinesSettings::*field) { return [field](FilterSettings &settings) -> double & { return settings.scanlines.*field; }; };
    const auto row = [this, &key](const QString &title, double ScanlinesSettings::*field, double low, double high, const QString &unit, const QString &help) {
        control(title, key(field), low, high, unit, 0, false);
        if (!help.isEmpty())
            m_controls.back().slider->parentWidget()->setToolTip(help);
    };
    using S = ScanlinesSettings;
    const QString px = QStringLiteral("px"), percent = QStringLiteral("%");
    row(QStringLiteral("Line Spacing"), &S::lineSpacing, S::lineSpacingLow, S::lineSpacingHigh, px, QStringLiteral("How far apart the screen's lines are"));
    row(QStringLiteral("Thickness"), &S::thickness, 5, 100, percent, QStringLiteral("How much of the gap a bright line fills; dimmer parts draw it thinner"));
    row(QStringLiteral("Glow"), &S::glow, 0, 100, percent, QStringLiteral("Light blooming around the lines, like a CRT's phosphors"));
    row(QStringLiteral("Dots"), &S::dots, 0, 100, percent,
        QStringLiteral("Break the lines into beads where the picture is darker than this, blending into solid lines where it's brighter"));
    row(QStringLiteral("Displace"), &S::displace, S::displaceLow, S::displaceHigh, px,
        QStringLiteral("Lift the lines where the picture is bright, or lower them when negative, so they swell into its shapes"));
    row(QStringLiteral("Smoothness"), &S::smoothness, 0, 100, percent,
        QStringLiteral("How far the brightness is smoothed before it displaces the lines, from sharp ridges to rounded hills"));
    row(QStringLiteral("Threshold"), &S::threshold, 0, 100, percent, QStringLiteral("Draw no line where the picture is darker than this, leaving the screen dark"));
    row(QStringLiteral("Wobble"), &S::wobble, S::wobbleLow, S::wobbleHigh, px, QStringLiteral("Make the lines waver sideways down the screen, like a CRT losing sync"));
    row(QStringLiteral("Color Split"), &S::split, S::splitLow, S::splitHigh, px, QStringLiteral("Move the red and blue apart, for colored fringes on the lines"));
    row(QStringLiteral("Density"), &S::density, -100, 100, QString(), QStringLiteral("Darker or lighter before the lines are drawn"));
    row(QStringLiteral("Contrast"), &S::contrast, -100, 100, QString(), QString());
    row(QStringLiteral("Black Level"), &S::blackLevel, 0, 100, percent,
        QStringLiteral("How bright the lines are where the picture is black, so they show even over black"));
    colorChoices(QStringLiteral("scanlines"), [](FilterSettings &settings) -> DitherColors & { return settings.scanlines.colors; },
                 [](const FilterSettings &settings) { return std::pair(settings.scanlines.dark, settings.scanlines.light); },
                 QStringLiteral("Choose the screen's color"), QStringLiteral("Choose the lines' color"));
}

void FilterSheet::colorChoices(const QString &prefix, std::function<DitherColors &(FilterSettings &)> colors,
                               std::function<std::pair<AdjustmentColor, AdjustmentColor>(const FilterSettings &)> pair, const QString &darkHelp,
                               const QString &lightHelp)
{
    // Colors: a menu, as Swift's: the panel keeps its size.
    QComboBox *menuBox = menu(QStringLiteral("Colors"), prefix + QStringLiteral("Colors"), QString());
    for (const DitherColors each : allDitherColors)
        menuBox->addItem(rawValue(each), int(each));
    connect(menuBox, &QComboBox::activated, this, [this, colors](int index) { update([&](FilterSettings &settings) { colors(settings) = DitherColors(index); }); });
    m_follows.emplace_back([menuBox, colors](const FilterSettings &settings) {
        const QSignalBlocker quiet(menuBox);
        FilterSettings shown = settings;
        menuBox->setCurrentIndex(int(colors(shown)));
    });
    // Two Colors: the dark and the light swatch.
    auto *both = new QWidget(this);
    auto *swatches = new QHBoxLayout(both);
    swatches->setContentsMargins(0, 0, 0, 0);
    swatches->setSpacing(8);
    for (const bool light : {false, true}) {
        auto *title = new QLabel(light ? QStringLiteral("Light") : QStringLiteral("Dark"), both);
        auto *swatch = new SwatchButton([this, light, pair] {
            const auto [dark, lit] = pair(settings());
            const AdjustmentColor colour = light ? lit : dark;
            return PaletteColor{colour.red, colour.green, colour.blue};
        }, 6, 1.5, 1, both);
        swatch->setObjectName(prefix + (light ? QStringLiteral("Light") : QStringLiteral("Dark")));
        swatch->setFixedSize(24, 24);
        swatch->setToolTip(light ? lightHelp : darkHelp);
        swatch->setAccessibleName(light ? QStringLiteral("Light color") : QStringLiteral("Dark color"));
        connect(swatch, &QAbstractButton::clicked, this, [this, light] { m_session.openDitherColorPicker(light); });
        m_follows.emplace_back([swatch](const FilterSettings &) { swatch->update(); });
        if (light)
            swatches->addSpacing(10);
        swatches->addWidget(title);
        swatches->addWidget(swatch);
    }
    swatches->addStretch(1);
    m_column->addWidget(both);
    m_shownWhen.emplace_back(both, [colors](const FilterSettings &settings) {
        FilterSettings shown = settings;
        return colors(shown) == DitherColors::twoColors;
    });
}
