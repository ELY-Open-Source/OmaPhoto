#include "UI/ByteCounts.h"
#include "UI/ColorPaletteControls.h"
#include "UI/ExportAsSheet.h"
#include "UI/ColorPickerSheet+Dialog.h"
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QToolButton>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QStandardPaths>
#include <QTimer>
#include <QtTest>

// Swift's ExportAsSheet: the preview waits, encodes aside, then exports.
class ExportAsSheetTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void theSheetShowsTheRasterAndWaitsForItsPreview();
    void aChangeSupersedesThePreview();
    void aSupersededEncodingNeverLands();
    void theMatteComesFromTheAppsPicker();
    void exportKeepsTheQualityAndCancelDoesNot();
    void theSavedQualityIsReadAsSwiftReadsIt();
    void anEncodingThatFailsIsShown();
    void eachFormatShowsItsRowsAndWritesItsBytes();
    void theSizeKeepsTheProportionsWithinTheLimits();
    void aClearPNGShowsTheCheckerboard();
    void thePageRoundsAsSwiftsFormatter();
};

namespace {
// Clear on the left, opaque red on the right.
ExportRaster halfRed(int width = 64, int height = 32)
{
    QImage image(width, height, QImage::Format_RGBA8888_Premultiplied);
    image.fill(Qt::transparent);
    for (int y = 0; y < height; ++y)
        for (int x = width / 2; x < width; ++x)
            image.setPixel(x, y, qRgba(255, 0, 0, 255));
    return {image, 144};
}

struct Sheet {
    std::optional<std::optional<ExportAsSheet::Chosen>> answer;
    EditorSession session;
    ExportAsSheet sheet;
    explicit Sheet(ExportRaster raster = halfRed(), ExportFormat format = ExportFormat::jpeg)
        : sheet(std::move(raster), session, format, [this](std::optional<ExportAsSheet::Chosen> chosen) { answer = std::move(chosen); })
    {
        sheet.show();
    }
    template <typename Widget> Widget &find(const char *name)
    {
        Widget *found = sheet.findChild<Widget *>(QString::fromLatin1(name));
        if (!found)
            throw std::runtime_error(std::string("no widget named ") + name);
        return *found;
    }
    bool ready() { return find<QPushButton>("exportExport").isEnabled(); }
    QString note() { return find<QLabel>("exportUpdating").text(); }
    QColor previewAt(QPoint point) { return find<QWidget>("exportPreview").grab().toImage().pixelColor(point); }
};

struct PaintCount : QObject {
    int count = 0;
    explicit PaintCount(QWidget &widget) { widget.installEventFilter(this); }
    bool eventFilter(QObject *, QEvent *event) override
    {
        count += event->type() == QEvent::Paint;
        return false;
    }
};

QStringList labels(const QWidget &widget)
{
    QStringList texts;
    for (const QLabel *label : widget.findChildren<QLabel *>())
        texts << label->text();
    return texts;
}
}

void ExportAsSheetTests::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    QSettings().clear();
}

void ExportAsSheetTests::theSheetShowsTheRasterAndWaitsForItsPreview()
{
    Sheet shown(halfRed(1200, 800));
    const QStringList texts = labels(shown.sheet);
    QVERIFY(texts.contains("Export As") && texts.contains("Format") && texts.contains("Size") && texts.contains("Quality"));
    QVERIFY(texts.contains("Background") && texts.contains("sRGB"));
    QCOMPARE(shown.find<QLabel>("exportPercent").text(), QString("85%"));
    QCOMPARE(shown.find<QSlider>("exportQuality").value(), 85);
    QCOMPARE(shown.find<QWidget>("exportPreview").size(), QSize(560, 330));
    // Swift's task sleeps 200 ms before it encodes.
    auto &wait = shown.find<QTimer>("exportWait");
    QVERIFY(wait.isActive() && wait.isSingleShot());
    QCOMPARE(wait.interval(), 200);
    QCOMPARE(wait.timerType(), Qt::PreciseTimer);
    QVERIFY(!shown.ready());
    QCOMPARE(shown.note(), QString("Updating…"));
    QVERIFY(shown.find<QProgressBar>("exportSpinner").isVisible());
    QVERIFY(!shown.find<QLabel>("exportBytes").isVisible() && !shown.find<QLabel>("exportError").isVisible());
    // The material plate behind the spinner, over the dark gray.
    QCOMPARE(shown.previewAt({2, 2}), QColor(31, 31, 31));
    const QColor window = shown.sheet.palette().color(QPalette::Window), plate = shown.previewAt({252, 165});
    QVERIFY(qAbs(plate.red() - qRound(window.red() * 0.85 + 31 * 0.15)) <= 1);
    QVERIFY(qAbs(plate.blue() - qRound(window.blue() * 0.85 + 31 * 0.15)) <= 1);
    QTRY_VERIFY(shown.ready());
    const JPEGResult expected = ImageExporter::jpeg(halfRed(1200, 800), {});
    QCOMPARE(shown.find<QLabel>("exportBytes").text(), ByteCounts::file(expected.data.size()));
    QVERIFY(shown.find<QLabel>("exportBytes").isVisible() && !shown.find<QLabel>("exportUpdating").isVisible());
    QVERIFY(!shown.find<QProgressBar>("exportSpinner").isVisible());
    // Fitted to 495 × 330 and centred: white, then red.
    QCOMPARE(shown.previewAt({2, 2}), QColor(31, 31, 31));
    QCOMPARE(shown.previewAt({10, 165}), QColor(31, 31, 31));
    QVERIFY(qAbs(shown.previewAt({100, 165}).red() - 255) < 4 && qAbs(shown.previewAt({100, 165}).blue() - 255) < 4);
    const QColor red = shown.previewAt({460, 165});
    QVERIFY(red.red() > 240 && red.green() < 16 && red.blue() < 16);
}

void ExportAsSheetTests::aChangeSupersedesThePreview()
{
    Sheet shown;
    QTRY_VERIFY(shown.ready());
    auto &quality = shown.find<QSlider>("exportQuality");
    // Each change waits anew; the stale preview stays beneath.
    quality.setValue(10);
    QCOMPARE(shown.find<QLabel>("exportPercent").text(), QString("10%"));
    QVERIFY(!shown.ready());
    QCOMPARE(shown.note(), QString("Updating…"));
    QVERIFY(shown.find<QTimer>("exportWait").isActive());
    QVERIFY(shown.find<QProgressBar>("exportSpinner").isVisible());
    quality.setValue(100);
    QTRY_VERIFY(shown.ready());
    QCOMPARE(shown.find<QLabel>("exportBytes").text(), ByteCounts::file(ImageExporter::jpeg(halfRed(), {.quality = 1}).data.size()));
    shown.find<QPushButton>("exportExport").click();
    QCOMPARE(shown.answer.value().value().data, ImageExporter::jpeg(halfRed(), {.quality = 1}).data);
    QCOMPARE(shown.answer.value().value().format, ExportFormat::jpeg);
}

// Slow to encode: the change comes while it runs.
void ExportAsSheetTests::aSupersededEncodingNeverLands()
{
    QImage noisy(6000, 4000, QImage::Format_RGBA8888_Premultiplied);
    quint32 seed = 1;
    for (int y = 0; y < noisy.height(); ++y) {
        auto *row = reinterpret_cast<quint32 *>(noisy.scanLine(y));
        for (int x = 0; x < noisy.width(); ++x)
            row[x] = (seed = seed * 1664525u + 1013904223u) | 0xff000000u;
    }
    Sheet shown({noisy, 72});
    QTRY_VERIFY(!shown.find<QTimer>("exportWait").isActive());
    shown.find<QSlider>("exportQuality").setValue(50);
    // Only the newest request's preview may ever show.
    bool stale = false;
    while (!shown.ready()) {
        stale = stale || shown.previewAt({100, 165}) != QColor(31, 31, 31);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    QVERIFY(!stale);
    QCOMPARE(shown.find<QLabel>("exportPercent").text(), QString("50%"));
}

void ExportAsSheetTests::theMatteComesFromTheAppsPicker()
{
    QSettings().remove(ExportAsSheet::qualityKey);
    Sheet shown;
    QTRY_VERIFY(shown.ready());
    auto &matte = shown.find<DialogColorSwatch>("exportBackground");
    QVERIFY(matte.accessibleName() == QString("Export Background") && matte.toolTip() == QString("Color that fills transparent areas"));
    matte.click();
    QCOMPARE(shown.session.colorPicker().value().target.title(), QString("Color Picker (Export Background)"));
    QVERIFY(shown.session.colorPicker().value().original == PaletteColor::white());
    // The same colour asks for nothing.
    shown.session.closeColorPicker(true);
    QVERIFY(shown.ready() && !shown.find<QTimer>("exportWait").isActive());
    // The working colour reaches the preview; Cancel takes it back.
    matte.click();
    shown.session.setColorPickerHSB(PickerHSB(PaletteColor{0, 0, 1}));
    QVERIFY(!shown.ready());
    QCOMPARE(matte.grab().toImage().pixelColor(17, 9), QColor(0, 0, 255));
    shown.session.closeColorPicker(false);
    QCOMPARE(matte.grab().toImage().pixelColor(17, 9), QColor(Qt::white));
    QTRY_VERIFY(shown.ready());
    // Export closes an open picker, keeping its colour.
    matte.click();
    shown.session.setColorPickerHSB(PickerHSB(PaletteColor{0, 0, 1}));
    QTRY_VERIFY(shown.ready());
    shown.find<QPushButton>("exportExport").click();
    QVERIFY(!shown.session.colorPicker());
    const QImage written = QImage::fromData(shown.answer.value().value().data, "jpeg");
    const QColor left = written.pixelColor(8, 16), right = written.pixelColor(56, 16);
    QVERIFY(left.blue() > 240 && left.red() < 16 && left.green() < 16);
    QVERIFY(right.red() > 240 && right.blue() < 16);
    QCOMPARE(shown.answer.value().value().data, ImageExporter::jpeg(halfRed(), {.red = 0, .green = 0, .blue = 1}).data);
    // Cancel and the sheet going close it too.
    Sheet cancelled;
    cancelled.find<DialogColorSwatch>("exportBackground").click();
    cancelled.find<QPushButton>("exportCancel").click();
    QVERIFY(!cancelled.session.colorPicker() && !cancelled.answer.value());
    EditorSession session;
    auto gone = std::make_unique<ExportAsSheet>(halfRed(), session, ExportFormat::jpeg, [](std::optional<ExportAsSheet::Chosen>) {});
    gone->findChild<DialogColorSwatch *>()->click();
    gone.reset();
    QVERIFY(!session.colorPicker());
}

void ExportAsSheetTests::exportKeepsTheQualityAndCancelDoesNot()
{
    QSettings().remove(ExportAsSheet::qualityKey);
    {
        Sheet shown;
        shown.find<QSlider>("exportQuality").setValue(40);
        QTRY_VERIFY(shown.ready());
        shown.find<QPushButton>("exportCancel").click();
        QVERIFY(!shown.answer.value().has_value());
        QVERIFY(!QSettings().contains(ExportAsSheet::qualityKey));
    }
    {
        Sheet shown;
        QCOMPARE(shown.find<QSlider>("exportQuality").value(), 85);
        shown.find<QSlider>("exportQuality").setValue(40);
        QTRY_VERIFY(shown.ready());
        shown.find<QPushButton>("exportExport").click();
        QCOMPARE(QSettings().value(ExportAsSheet::qualityKey).toDouble(), 0.4);
    }
    Sheet next;
    QCOMPARE(next.find<QSlider>("exportQuality").value(), 40);
    QCOMPARE(next.find<QLabel>("exportPercent").text(), QString("40%"));
    // Return reaches Export once it is ready.
    QTRY_VERIFY(next.ready());
    QVERIFY(next.find<QPushButton>("exportExport").isDefault());
    QCOMPARE(next.find<QPushButton>("exportExport").text(), QString("Export…"));
}

void ExportAsSheetTests::theSavedQualityIsReadAsSwiftReadsIt()
{
    const auto started = [](const QVariant &saved) {
        QSettings().setValue(ExportAsSheet::qualityKey, saved);
        Sheet shown;
        return shown.find<QLabel>("exportPercent").text();
    };
    QCOMPARE(started(0.333), QString("33%"));
    QCOMPARE(started(0.875), QString("88%"));
    QCOMPARE(started(7), QString("100%"));
    QCOMPARE(started(-2), QString("0%"));
    QCOMPARE(started(QStringLiteral("high")), QString("85%"));
    QCOMPARE(started(std::nan("")), QString("85%"));
    QCOMPARE(started(qInf()), QString("85%"));
    QSettings().remove(ExportAsSheet::qualityKey);
}

void ExportAsSheetTests::anEncodingThatFailsIsShown()
{
    // JPEG holds at most 65,500 pixels a side.
    Sheet shown(halfRed(70'000, 1));
    auto &error = shown.find<QLabel>("exportError");
    QTRY_VERIFY(error.isVisible());
    QCOMPARE(error.text(), QString::fromUtf8(ExportError(ExportError::Kind::encode).what()));
    QCOMPARE(error.foregroundRole(), QPalette::BrightText);
    QVERIFY(!shown.ready() && !shown.find<QLabel>("exportUpdating").isVisible() && !shown.find<QProgressBar>("exportSpinner").isVisible());
    // A change clears the error while it waits.
    shown.find<QSlider>("exportQuality").setValue(50);
    QVERIFY(!error.isVisible());
    QCOMPARE(shown.note(), QString("Updating…"));
    QTRY_VERIFY(error.isVisible());
}

void ExportAsSheetTests::eachFormatShowsItsRowsAndWritesItsBytes()
{
    const ExportRaster raster = halfRed();
    const auto rows = [](Sheet &shown) {
        return std::array<bool, 3>{shown.find<QSlider>("exportQuality").isVisible(), shown.find<QLabel>("exportPage").isVisible(),
                                   shown.find<DialogColorSwatch>("exportBackground").isVisible()};
    };
    QSettings().remove(ExportAsSheet::qualityKey);
    {
        Sheet shown(raster, ExportFormat::png);
        QVERIFY(shown.find<QAbstractButton>("exportFormatPNG").isChecked());
        QCOMPARE(rows(shown), (std::array<bool, 3>{false, false, false}));
        QCOMPARE(shown.find<QLabel>("exportNote").text(), QString("Transparency kept · sRGB"));
        QTRY_VERIFY(shown.ready());
        QCOMPARE(shown.find<QLabel>("exportBytes").text(), ByteCounts::file(ImageExporter::pngData(raster).size()));
        shown.find<QPushButton>("exportExport").click();
        QCOMPARE(shown.answer.value().value().data, ImageExporter::pngData(raster));
        QCOMPARE(shown.answer.value().value().format, ExportFormat::png);
        // Only a JPEG keeps its quality for next time.
        QVERIFY(!QSettings().contains(ExportAsSheet::qualityKey));
    }
    Sheet shown(raster, ExportFormat::png);
    QTRY_VERIFY(shown.ready());
    // Another format waits for its own result.
    shown.find<QAbstractButton>("exportFormatPDF").click();
    QVERIFY(!shown.ready() && shown.find<QAbstractButton>("exportFormatPDF").isChecked() && !shown.find<QAbstractButton>("exportFormatPNG").isChecked());
    QCOMPARE(rows(shown), (std::array<bool, 3>{false, true, true}));
    QCOMPARE(shown.find<QLabel>("exportNote").text(), QString("sRGB"));
    // 64 by 32 at 144 per inch.
    QCOMPARE(shown.find<QLabel>("exportPage").text(), QString("0.44 × 0.22 in at 144 DPI"));
    QTRY_VERIFY(shown.ready());
    shown.find<QPushButton>("exportExport").click();
    QCOMPARE(shown.answer.value().value().data, ImageExporter::pdfData(raster, QColor(Qt::white)));
    QCOMPARE(shown.answer.value().value().format, ExportFormat::pdf);
    // The PDF preview lies over its page's white.
    QCOMPARE(shown.previewAt({100, 165}), QColor(Qt::white));
    shown.find<QAbstractButton>("exportFormatJPEG").click();
    QCOMPARE(rows(shown), (std::array<bool, 3>{true, false, true}));
}

void ExportAsSheetTests::theSizeKeepsTheProportionsWithinTheLimits()
{
    const ExportRaster raster = halfRed();
    Sheet shown(raster, ExportFormat::png);
    auto &width = shown.find<QLineEdit>("exportWidth"), &height = shown.find<QLineEdit>("exportHeight");
    auto &scale = shown.find<QToolButton>("exportScale");
    QVERIFY(width.text() == "64" && height.text() == "32" && scale.text() == "100%");
    QCOMPARE(width.toolTip(), QString("Width in pixels; the height follows, keeping the proportions"));
    const auto type = [](QLineEdit &field, const QString &text) {
        field.setFocus();
        field.selectAll();
        QTest::keyClicks(&field, text);
        QTest::keyClick(&field, Qt::Key_Return);
    };
    type(width, "32");
    QVERIFY(height.text() == "16" && scale.text() == "50%");
    QCOMPARE(shown.sheet.settings(), (ExportAsSheet::Settings{ExportFormat::png, shown.sheet.settings().options, 32, 16}));
    QTRY_VERIFY(shown.ready());
    shown.find<QPushButton>("exportExport").click();
    QCOMPARE(shown.answer.value().value().data, ImageExporter::pngData(ImageExporter::resized(raster, 32, 16)));
    type(height, "64");
    QVERIFY(width.text() == "128" && scale.text() == "200%");
    // Other text goes back; a side holds a pixel.
    type(width, "wide");
    QCOMPARE(width.text(), QString("128"));
    type(width, "0");
    QVERIFY(width.text() == "1" && height.text() == "1" && scale.text() == "2%");
    // The surface's limit caps a growth: 312.5 times here.
    type(width, "100000");
    QVERIFY(width.text() == "20000" && height.text() == "10000");
    // The menu's steps.
    QStringList steps;
    for (QAction *each : scale.menu()->actions())
        steps << each->text();
    QCOMPARE(steps, (QStringList{"25%", "50%", "75%", "100%", "200%"}));
    scale.menu()->actions().at(0)->trigger();
    QVERIFY(width.text() == "16" && height.text() == "8" && scale.text() == "25%");
    // The same size asks nothing new, as Swift's task id.
    QTRY_VERIFY(shown.ready());
    scale.menu()->actions().at(0)->trigger();
    QVERIFY(!shown.find<QTimer>("exportWait").isActive());
    QCOMPARE(scale.toolTip(), QString("Scale the export: the document stays as it is"));
    // A hundredth at least: 1120 wide stops at 11.
    Sheet large(halfRed(1120, 660), ExportFormat::png);
    auto &wide = large.find<QLineEdit>("exportWidth");
    auto &in = large.find<QPushButton>("exportZoomIn");
    QVERIFY(in.isEnabled() && in.toolTip().contains("now 50%"));
    type(wide, "-5");
    QVERIFY(wide.text() == "11" && large.find<QLineEdit>("exportHeight").text() == "7");
    // Fit follows the export's size: 11 by 7, 47 times.
    QVERIFY(!in.isEnabled() && in.toolTip().contains("now 4714%"));
}

void ExportAsSheetTests::aClearPNGShowsTheCheckerboard()
{
    // 330 square fits the frame's height, one pixel a point.
    QImage clear(330, 330, QImage::Format_RGBA8888_Premultiplied);
    clear.fill(Qt::transparent);
    Sheet shown({clear, 72}, ExportFormat::png);
    QTRY_VERIFY(shown.ready());
    // The image starts at x 115: grays 0.35, 0.30.
    QCOMPARE(shown.previewAt({116, 1}).rgb(), QColor::fromRgbF(0.35f, 0.35f, 0.35f).rgb());
    QCOMPARE(shown.previewAt({126, 1}).rgb(), QColor::fromRgbF(0.30f, 0.30f, 0.30f).rgb());
    QCOMPARE(shown.previewAt({126, 11}).rgb(), QColor::fromRgbF(0.35f, 0.35f, 0.35f).rgb());
    // Outside the image, the frame's own gray.
    QCOMPARE(shown.previewAt({100, 165}), QColor(31, 31, 31));
}

// Swift 6.0.3's formatter, measured: halves of its digits to even.
void ExportAsSheetTests::thePageRoundsAsSwiftsFormatter()
{
    const auto page = [](int width, int height, double dpi) {
        Sheet shown({halfRed(width, height).image, dpi}, ExportFormat::pdf);
        return shown.find<QLabel>("exportPage").text();
    };
    QCOMPARE(page(45, 9, 72), QString("0.62 × 0.12 in at 72 DPI"));
    QCOMPARE(page(27, 1, 72), QString("0.38 × 0.01 in at 72 DPI"));
    QCOMPARE(page(1, 3, 200), QString("0 × 0.02 in at 200 DPI"));
    QCOMPARE(page(27, 29, 200), QString("0.14 × 0.14 in at 200 DPI"));
    QCOMPARE(page(100, 2999, 300), QString("0.33 × 10 in at 300 DPI"));
    QCOMPARE(page(30000, 1, 1), QString("30,000 × 1 in at 1 DPI"));
}

QTEST_MAIN(ExportAsSheetTests)
#include "ExportAsSheetTests.moc"
