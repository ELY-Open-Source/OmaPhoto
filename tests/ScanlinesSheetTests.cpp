#include "Document/BrushStroke.h"
#include "Document/EditorSession.h"
#include "Document/Scanlines.h"
#include "UI/ColorPaletteControls.h"
#include "UI/FilterSheet.h"
#include <QComboBox>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QSlider>
#include <QtTest>

// Swift 1.4.9's Scanlines in the Filter menu and its sheet.
namespace {
QImage filled(int width, int height, QColor colour)
{
    QImage image = BrushRaster::context(width, height, false);
    image.fill(colour);
    return image;
}

std::vector<int> column(const QImage &image, int x)
{
    std::vector<int> values;
    for (int y = 0; y < image.height(); ++y)
        values.push_back(image.constScanLine(y)[x * 4]);
    return values;
}

struct Shown {
    EditorSession session;
    std::unique_ptr<FilterSheet> sheet;
    Shown()
    {
        session.createDocument(40, 30);
        const QImage image = filled(40, 30, QColor(200, 120, 40));
        session.insert(ImportedImage(image, image, QStringLiteral("Colour")));
        session.beginFilter(FilterKind::scanlines);
        sheet = std::make_unique<FilterSheet>(session);
        sheet->show();
    }
    const ScanlinesSettings &lines() const { return session.filterEdit().value().settings.scanlines; }
    QStringList rows() const
    {
        QStringList titles;
        QLayout *column = sheet->layout();
        for (int index = 0; index < column->count(); ++index) {
            QWidget *each = column->itemAt(index)->widget();
            if (!each || each->isHidden())
                continue;
            for (QLabel *label : each->findChildren<QLabel *>(QString(), Qt::FindDirectChildrenOnly)) {
                if (label->buddy())
                    titles << label->text() + u'|' + each->toolTip();
            }
        }
        return titles;
    }
};
}

class ScanlinesSheetTests : public QObject {
    Q_OBJECT
private slots:
    void theFilterIsListedAndPreviewsWhole();
    void theSheetShowsSwiftsRows();
    void theRowsWriteTheirSettings();
    void twoColorsPickTheScreenAndTheLines();
};

void ScanlinesSheetTests::theFilterIsListedAndPreviewsWhole()
{
    QCOMPARE(rawValue(FilterKind::scanlines), QString("Scanlines"));
    const auto at = std::find(allFilterKinds.begin(), allFilterKinds.end(), FilterKind::scanlines);
    QVERIFY(at != allFilterKinds.end() && *(at - 1) == FilterKind::dither && *(at + 1) == FilterKind::tonalContrast);
    QVERIFY(!isImageAdjustment(FilterKind::scanlines) && !isAutomatic(FilterKind::scanlines) && repeatsAsLastFilter(FilterKind::scanlines));
    EditorSession session;
    session.createDocument(3000, 20);
    const QImage wide = filled(3000, 20, Qt::white);
    session.insert(ImportedImage(wide, wide, QStringLiteral("Wide")));
    session.beginFilter(FilterKind::scanlines);
    QCOMPARE(session.filterEdit().value().previewScale, 1.0);
    // Its commit is one step that draws the lines.
    QVERIFY(session.filterEdit().value().settings.scanlines == ScanlinesSettings());
    bool done = false;
    session.commitFilter([&done] { done = true; });
    QTRY_VERIFY(done);
    QCOMPARE(session.history.undoName(), QString("Scanlines"));
    const QImage drawn = session.document().value().layers.front().asset.value().image();
    // Beam 1.4 round the middle 2: row 0 covers 0.4.
    QCOMPARE(column(drawn, 5)[0], int(std::lround(255 * 1.35f * 0.4f)));
    QCOMPARE(column(drawn, 5)[2], 255);
}

void ScanlinesSheetTests::theSheetShowsSwiftsRows()
{
    Shown shown;
    QCOMPARE(shown.rows(),
             (QStringList{"Line Spacing|How far apart the screen's lines are", "Thickness|How much of the gap a bright line fills; dimmer parts draw it thinner",
                          "Glow|Light blooming around the lines, like a CRT's phosphors",
                          "Dots|Break the lines into beads where the picture is darker than this, blending into solid lines where it's brighter",
                          "Displace|Lift the lines where the picture is bright, or lower them when negative, so they swell into its shapes",
                          "Smoothness|How far the brightness is smoothed before it displaces the lines, from sharp ridges to rounded hills",
                          "Threshold|Draw no line where the picture is darker than this, leaving the screen dark",
                          "Wobble|Make the lines waver sideways down the screen, like a CRT losing sync",
                          "Color Split|Move the red and blue apart, for colored fringes on the lines", "Density|Darker or lighter before the lines are drawn",
                          "Contrast|", "Black Level|How bright the lines are where the picture is black, so they show even over black", "Colors|"}));
    using Field = double ScanlinesSettings::*;
    const std::tuple<const char *, Field, double, double, QString> sliders[] = {
        {"lineSpacing", &ScanlinesSettings::lineSpacing, 2, 32, "px"}, {"thickness", &ScanlinesSettings::thickness, 5, 100, "%"},
        {"glow", &ScanlinesSettings::glow, 0, 100, "%"},               {"dots", &ScanlinesSettings::dots, 0, 100, "%"},
        {"displace", &ScanlinesSettings::displace, -100, 100, "px"},   {"smoothness", &ScanlinesSettings::smoothness, 0, 100, "%"},
        {"threshold", &ScanlinesSettings::threshold, 0, 100, "%"},     {"wobble", &ScanlinesSettings::wobble, 0, 64, "px"},
        {"colorSplit", &ScanlinesSettings::split, 0, 16, "px"},        {"density", &ScanlinesSettings::density, -100, 100, ""},
        {"contrast", &ScanlinesSettings::contrast, -100, 100, ""},     {"blackLevel", &ScanlinesSettings::blackLevel, 0, 100, "%"}};
    for (const auto &[name, field, low, high, unit] : sliders) {
        QSlider *slider = shown.sheet->findChild<QSlider *>(QString::fromLatin1(name) + "Slider");
        QLineEdit *edit = shown.sheet->findChild<QLineEdit *>(QString::fromLatin1(name) + "Field");
        QVERIFY2(slider && edit, name);
        slider->setValue(slider->maximum());
        QCOMPARE(shown.lines().*field, high);
        slider->setValue(slider->minimum());
        QCOMPARE(shown.lines().*field, low);
        QCOMPARE(edit->parentWidget()->findChildren<QLabel *>(QString(), Qt::FindDirectChildrenOnly).last()->text(), unit);
    }
}

void ScanlinesSheetTests::theRowsWriteTheirSettings()
{
    Shown shown;
    const std::tuple<const char *, double ScanlinesSettings::*, const char *, double> rows[] = {
        {"lineSpacingField", &ScanlinesSettings::lineSpacing, "12", 12}, {"displaceField", &ScanlinesSettings::displace, "-30", -30},
        {"colorSplitField", &ScanlinesSettings::split, "5", 5},          {"blackLevelField", &ScanlinesSettings::blackLevel, "40", 40},
        {"smoothnessField", &ScanlinesSettings::smoothness, "10", 10},   {"thicknessField", &ScanlinesSettings::thickness, "55", 55}};
    for (const auto &[name, field, typed, expected] : rows) {
        ScanlinesSettings wanted = shown.lines();
        QLineEdit *edit = shown.sheet->findChild<QLineEdit *>(QString::fromLatin1(name));
        QVERIFY2(edit, name);
        edit->setFocus();
        edit->selectAll();
        QTest::keyClicks(edit, QString::fromLatin1(typed));
        QTest::keyClick(edit, Qt::Key_Return);
        wanted.*field = expected;
        QVERIFY2(shown.lines() == wanted, name);
    }
}

void ScanlinesSheetTests::twoColorsPickTheScreenAndTheLines()
{
    Shown shown;
    auto *colors = shown.sheet->findChild<QComboBox *>(QStringLiteral("scanlinesColors"));
    auto *dark = shown.sheet->findChild<SwatchButton *>(QStringLiteral("scanlinesDark"));
    auto *light = shown.sheet->findChild<SwatchButton *>(QStringLiteral("scanlinesLight"));
    QVERIFY(colors && dark && light && !dark->isVisible());
    QVERIFY(dark->toolTip() == QString("Choose the screen's color") && light->toolTip() == QString("Choose the lines' color"));
    colors->activated(int(DitherColors::twoColors));
    QVERIFY(shown.lines().colors == DitherColors::twoColors && dark->isVisible() && light->isVisible());
    // The menu follows the settings, however they change.
    FilterSettings original = shown.session.filterEdit().value().settings;
    original.scanlines.colors = DitherColors::original;
    shown.session.updateFilter(original, true);
    QVERIFY(colors->currentIndex() == int(DitherColors::original) && !dark->isVisible());
    original.scanlines.colors = DitherColors::twoColors;
    shown.session.updateFilter(original, true);
    QCOMPARE(colors->currentIndex(), int(DitherColors::twoColors));
    // The picker starts from the lines' own colour.
    FilterSettings settings = shown.session.filterEdit().value().settings;
    settings.scanlines.light = AdjustmentColor(0, 1, 0), settings.scanlines.dark = AdjustmentColor(0.2, 0, 0);
    shown.session.updateFilter(settings, true);
    light->click();
    QCOMPARE(shown.session.colorPicker().value().target.title(), QString("Color Picker (Light Color)"));
    QVERIFY(shown.session.colorPicker().value().original == (PaletteColor{0, 1, 0}));
    struct Paints : QObject {
        int count = 0;
        bool eventFilter(QObject *, QEvent *event) override { return count += event->type() == QEvent::Paint, false; }
    } paints;
    QCoreApplication::processEvents();
    light->installEventFilter(&paints);
    shown.session.setColorPickerHSB(PickerHSB(PaletteColor{1, 0, 0}));
    shown.session.previewDitherColor();
    // A new colour repaints the swatch.
    QTRY_VERIFY(paints.count > 0);
    QVERIFY(shown.lines().light == AdjustmentColor(1, 0, 0));
    QVERIFY(shown.session.filterEdit().value().settings.dither.light == AdjustmentColor(1, 1, 1));
    QCOMPARE(light->grab().toImage().pixelColor(12, 12), QColor(255, 0, 0));
    shown.session.closeColorPicker(false);
    QVERIFY(shown.lines().light == AdjustmentColor(0, 1, 0));
    dark->click();
    QCOMPARE(shown.session.colorPicker().value().target.title(), QString("Color Picker (Dark Color)"));
    QVERIFY(shown.session.colorPicker().value().original == (PaletteColor{0.2, 0, 0}));
    shown.session.setColorPickerHSB(PickerHSB(PaletteColor{0, 0, 1}));
    shown.session.closeColorPicker(true);
    QVERIFY(shown.lines().dark == AdjustmentColor(0, 0, 1));
}

QTEST_MAIN(ScanlinesSheetTests)
#include "ScanlinesSheetTests.moc"
