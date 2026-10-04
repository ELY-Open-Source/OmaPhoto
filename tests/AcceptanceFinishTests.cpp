#include "AcceptanceFixtures.h"
#include "DialogDesk.h"
#include "UI/JPEGExportSheet.h"
#include <QComboBox>
#include <map>
#include <QLineEdit>
#include <QPushButton>
#include <QToolButton>

// The acceptance run, part six: 1.4.5's filters, export, Quit.
class AcceptanceFinishTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }
    void cameraRawCurvesAndDither();
    void jpegPreviewAndQuitMidEdit();
};

namespace {
QUuid photo(App &app)
{
    if (!scene(1200, 800).save(app.path("photo.png")))
        throw std::runtime_error("the photo could not be written");
    app.session().createDocument(1200, 800);
    app.importFile(app.path("photo.png"));
    return app.session().activeLayerID().value();
}

void drag(QWidget *widget, QPoint from, QPoint to)
{
    QTest::mousePress(widget, Qt::LeftButton, Qt::NoModifier, from);
    for (int step = 1; step <= 4; ++step) {
        const QPointF at = from + (to - from) * (step / 4.0);
        QMouseEvent event(QEvent::MouseMove, at, widget->mapToGlobal(at), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(widget, &event);
    }
    QTest::mouseRelease(widget, Qt::LeftButton, Qt::NoModifier, to);
}

// A shown widget of that name, once its panel shows.
template <typename Widget> Widget &shownChild(App &app, const char *name)
{
    Widget *shown = nullptr;
    static_cast<void>(QTest::qWaitFor([&] {
        for (Widget *each : app.window.findChildren<Widget *>(QString::fromLatin1(name)))
            if (each->isVisible())
                shown = each;
        return shown != nullptr;
    }, 5000));
    if (!shown)
        throw std::runtime_error(std::string("no shown ") + name);
    return *shown;
}

// The filter panel's OK, then the commit's landing.
bool committed(App &app)
{
    shownChild<QPushButton>(app, "filterOK").click();
    return QTest::qWaitFor([&app] { return !app.session().filterEdit() && !app.session().isProjectBusy(); }, 30000);
}

void typeInto(App &app, const char *name, const QString &text, bool submit)
{
    auto &field = shownChild<QLineEdit>(app, name);
    field.setFocus();
    field.selectAll();
    QTest::keyClicks(&field, text);
    if (submit)
        QTest::keyClick(&field, Qt::Key_Return);
}

// The first glyph in the ground: its box, inked share.
std::pair<QRect, double> glyph(const QImage &image)
{
    const QRect ground(400, 640, 400, 120);
    std::map<QRgb, int> counts;
    for (int y = ground.top(); y < ground.bottom(); ++y)
        for (int x = ground.left(); x < ground.right(); ++x)
            ++counts[image.pixel(x, y)];
    const int paper = qGray(std::max_element(counts.begin(), counts.end(), [](auto a, auto b) { return a.second < b.second; })->first);
    // Ink lies past halfway from the paper's gray.
    const auto inked = [&image, paper](QPoint at) { return std::abs(qGray(image.pixel(at)) - paper) > 127; };
    std::vector<bool> seen(size_t(image.width()) * size_t(image.height()));
    const auto index = [&image](QPoint at) { return size_t(at.y()) * size_t(image.width()) + size_t(at.x()); };
    for (int y = ground.top() + 20; y < ground.bottom() - 40; ++y)
        for (int x = ground.left(); x < ground.right(); ++x) {
            if (!inked(QPoint(x, y)))
                continue;
            QRect box(x, y, 1, 1);
            int count = 0;
            std::vector<QPoint> stack{QPoint(x, y)};
            while (!stack.empty()) {
                const QPoint at = stack.back();
                stack.pop_back();
                if (!image.rect().contains(at) || !inked(at) || seen[index(at)])
                    continue;
                seen[index(at)] = true;
                ++count;
                box |= QRect(at, QSize(1, 1));
                for (const QPoint step : {QPoint(1, 0), QPoint(-1, 0), QPoint(0, 1), QPoint(0, -1), QPoint(1, 1), QPoint(-1, -1), QPoint(1, -1), QPoint(-1, 1)})
                    stack.push_back(at + step);
            }
            return {box, double(count) / (box.width() * box.height())};
        }
    throw std::runtime_error("no glyph in the ground");
}
}

void AcceptanceFinishTests::cameraRawCurvesAndDither()
{
    App app;
    const QUuid layer = photo(app);
    const int before = pixel(app.session(), layer, 600, 700).green();
    // Camera Raw: a curve region lifted, a point dragged.
    app.action("cameraRawFilter").trigger();
    QTRY_VERIFY(app.window.findChild<QToolButton *>(QStringLiteral("CurveSection")));
    app.window.findChild<QToolButton *>(QStringLiteral("CurveSection"))->click();
    QWidget *graph = app.window.findChild<QWidget *>(QStringLiteral("cameraRawCurveGraph"));
    QTRY_VERIFY(graph && graph->isVisible());
    const int w = graph->width(), h = graph->height();
    drag(graph, QPoint(int(w * 0.6), h / 2), QPoint(int(w * 0.6), h / 2 - h / 4));
    QVERIFY2(app.session().filterEdit().value().settings.cameraRaw.curve.lights > 20,
             qPrintable(QString::number(app.session().filterEdit().value().settings.cameraRaw.curve.lights)));
    app.window.findChild<QToolButton *>(QStringLiteral("curvePage1"))->click();
    const size_t points = app.session().filterEdit().value().settings.cameraRaw.curve.rgb.size();
    drag(graph, QPoint(w / 4, h * 3 / 4), QPoint(w / 4, h / 2));
    QCOMPARE(app.session().filterEdit().value().settings.cameraRaw.curve.rgb.size(), points + 1);
    QVERIFY(committed(app));
    QVERIFY2(pixel(app.session(), layer, 600, 700).green() > before + 10, qPrintable(pixel(app.session(), layer, 600, 700).name()));
    app.action("undo").trigger();
    // ASCII of "@" draws that letter, holes and all.
    app.action("dither").trigger();
    choose(shownChild<QComboBox>(app, "ditherStyle"), QStringLiteral("ASCII"));
    typeInto(app, "ditherCharacters", QStringLiteral("@"), false);
    QVERIFY(committed(app));
    const QImage small = app.session().activeLayer().value().asset.value().image();
    const auto [letter, inked] = glyph(small);
    QVERIFY2(inked < 0.75 && letter.height() >= 6, qPrintable(QString("%1 %2").arg(inked).arg(letter.height())));
    app.action("undo").trigger();
    app.action("dither").trigger();
    QCOMPARE(shownChild<QComboBox>(app, "ditherStyle").currentText(), QStringLiteral("ASCII"));
    typeInto(app, "textSizeField", QStringLiteral("28"), true);
    QTRY_VERIFY(!app.session().filterEdit() && !app.session().isProjectBusy());
    const auto [large, inkedLarge] = glyph(app.session().activeLayer().value().asset.value().image());
    const double grown = double(large.height()) / letter.height();
    QVERIFY2(grown > 1.6 && grown < 2.4 && inkedLarge < 0.75, qPrintable(QString::number(grown)));
    app.action("undo").trigger();
    // Scanlines: dark gaps between the beam's rows across the sky.
    app.action("dither").trigger();
    choose(shownChild<QComboBox>(app, "ditherStyle"), QStringLiteral("Scanlines (CRT)"));
    QVERIFY(committed(app));
    const QImage lines = app.session().activeLayer().value().asset.value().image();
    int darkest = 255, brightest = 0;
    for (int y = 100; y < 110; ++y) {
        darkest = std::min(darkest, qGray(lines.pixel(600, y)));
        brightest = std::max(brightest, qGray(lines.pixel(600, y)));
    }
    QVERIFY2(brightest - darkest > 40, qPrintable(QString("%1 %2").arg(darkest).arg(brightest)));
}

void AcceptanceFinishTests::jpegPreviewAndQuitMidEdit()
{
    App app;
    photo(app);
    // Export JPEG: View keys zoom to 100% and back.
    bool done = false;
    app.workspace.current().controller.exportJPEG([&done] { done = true; });
    QTRY_VERIFY(app.window.findChild<JPEGExportSheet *>());
    auto *preview = app.window.findChild<JPEGPreview *>();
    QVERIFY(preview && !preview->zoom());
    JPEGExportSheet *sheet = app.window.findChild<JPEGExportSheet *>();
    QTest::keyClick(sheet, Qt::Key_1, Qt::ControlModifier);
    QCOMPARE(preview->zoom(), std::optional<double>(1));
    QTest::keyClick(sheet, Qt::Key_Equal, Qt::ControlModifier);
    QVERIFY(preview->zoom().value() > 1);
    QTest::keyClick(sheet, Qt::Key_0, Qt::ControlModifier);
    QVERIFY(!preview->zoom());
    QTest::keyClick(sheet, Qt::Key_Escape);
    QTRY_VERIFY(done);
    // Quit with a gradient pending: applied before the one question.
    app.session().selectTool(NavigationTool::gradient);
    app.drag(QPointF(0, 400), QPointF(1200, 400));
    QVERIFY(app.session().gradientEdit());
    DialogDesk desk;
    desk.replies = {"Cancel"};
    desk.note = [&] { return QStringLiteral("pending=%1 levels=%2").arg(app.session().gradientEdit().has_value()).arg(app.session().levels().has_value()); };
    app.window.close();
    QTRY_COMPARE(desk.seen.size(), 1);
    QVERIFY2(desk.seen[0].endsWith("|pending=0 levels=0"), qPrintable(desk.seen[0]));
    QTRY_VERIFY(!app.workspace.isManaging());
    QVERIFY(app.window.isVisible());
    // With Levels open: cancelled, one question, then the window goes.
    app.action("levels").trigger();
    QVERIFY(app.session().levels());
    desk.replies = {"Don’t Save"};
    app.window.close();
    QTRY_COMPARE(desk.seen.size(), 2);
    QVERIFY2(desk.seen[1].endsWith("|pending=0 levels=0"), qPrintable(desk.seen[1]));
    QTRY_VERIFY(!app.window.isVisible());
}

QTEST_MAIN(AcceptanceFinishTests)
#include "AcceptanceFinishTests.moc"
