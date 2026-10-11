#include "AcceptanceFixtures.h"
#include "DialogDesk.h"
#include "UI/ExportAsSheet.h"
#include "UI/NewCanvasSheet.h"
#include "Rendering/NavigatorGeometry.h"
#include <QAbstractButton>
#include <QLineEdit>
#include <QMenuBar>
#include <QPushButton>
#include <QToolBar>
#include <QToolButton>

// The acceptance run, part seven: 1.4.6 to 1.4.9.
class AcceptanceLatestTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }
    void searchRotateAndFullscreen();
    void lastFilterAndCameraRaw();
    void navigatorAndMiddleZoom();
    void newCanvasExportAndTabs();
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

// A shown widget of that name, once its panel shows.
template <typename Widget> Widget &shownChild(QWidget &root, const QString &name)
{
    Widget *shown = nullptr;
    static_cast<void>(QTest::qWaitFor([&] {
        for (Widget *each : root.findChildren<Widget *>(name))
            if (each->isVisible())
                shown = each;
        return shown != nullptr;
    }, 5000));
    if (!shown)
        throw std::runtime_error("no shown " + name.toStdString());
    return *shown;
}

QSize documentSize(App &app)
{
    return app.session().document().value().size().toSize();
}
}

void AcceptanceLatestTests::searchRotateAndFullscreen()
{
    App app;
    photo(app);
    const int steps = app.session().history.undoCount();
    // Ctrl+F finds Rotate Canvas 90° Clockwise by words.
    QTest::keyClick(&app.window, Qt::Key_F, Qt::ControlModifier);
    QLineEdit &search = shownChild<QLineEdit>(app.window, QStringLiteral("commandSearch"));
    QTest::keyClicks(&search, QStringLiteral("rotate canvas clockwise"));
    QTest::keyClick(&search, Qt::Key_Return);
    QTRY_COMPARE(documentSize(app), QSize(800, 1200));
    QCOMPARE(app.session().history.undoName(), QString("Rotate Canvas 90° Clockwise"));
    QCOMPARE(app.session().history.undoCount(), steps + 1);
    // Clockwise: the new top left was the old bottom left.
    const QImage old = scene(1200, 800);
    QCOMPARE(app.shown(100, 50), old.pixelColor(50, 799 - 100));
    QCOMPARE(app.shown(700, 1100), old.pixelColor(1100, 799 - 700));
    app.shot(QStringLiteral("19-search"));
    // F: the canvas alone, bars away; Escape brings them back.
    app.canvas().setFocus();
    QTest::keyClick(&app.canvas(), Qt::Key_F);
    QTRY_VERIFY(app.window.isFullScreen());
    QVERIFY(app.window.menuBar()->isHidden() && app.window.findChild<QToolBar *>()->isHidden());
    QTest::keyClick(&app.canvas(), Qt::Key_Escape);
    QTRY_VERIFY(!app.window.isFullScreen());
    QVERIFY(!app.window.menuBar()->isHidden() && !app.window.findChild<QToolBar *>()->isHidden());
}

void AcceptanceLatestTests::lastFilterAndCameraRaw()
{
    App app;
    photo(app);
    // Gaussian Blur at radius 3; Last Filter repeats it.
    app.action("gaussianBlur").trigger();
    QLineEdit &radius = shownChild<QLineEdit>(app.window, QStringLiteral("radiusField"));
    radius.setFocus();
    radius.selectAll();
    QTest::keyClicks(&radius, QStringLiteral("3"));
    QTest::keyClick(&radius, Qt::Key_Return);
    QTRY_VERIFY(!app.session().filterEdit() && !app.session().isProjectBusy());
    QCOMPARE(app.action("lastFilter").text(), QString("Last Filter: Gaussian Blur"));
    const int steps = app.session().history.undoCount();
    app.action("lastFilter").trigger();
    QTRY_COMPARE(app.session().history.undoCount(), steps + 1);
    QCOMPARE(app.session().history.undoName(), QString("Gaussian Blur"));
    QVERIFY(!app.session().filterEdit());
    const QImage repeated = app.session().activeLayer().value().asset.value().image();
    // The sheet again, as it opens, blurs the same.
    app.action("undo").trigger();
    app.action("gaussianBlur").trigger();
    QCOMPARE(shownChild<QLineEdit>(app.window, QStringLiteral("radiusField")).text(), QString("3"));
    shownChild<QPushButton>(app.window, QStringLiteral("filterOK")).click();
    QTRY_VERIFY(!app.session().filterEdit() && !app.session().isProjectBusy());
    QVERIFY(app.session().activeLayer().value().asset.value().image() == repeated);
    // Camera Raw's Temperature +100 on mid gray, as Photoshop's.
    app.session().createDocument(64, 64);
    QImage gray(64, 64, QImage::Format_RGB32);
    gray.fill(QColor(128, 128, 128));
    QVERIFY(gray.save(app.path("gray.png")));
    app.importFile(app.path("gray.png"));
    app.action("cameraRawFilter").trigger();
    QLineEdit &temperature = shownChild<QLineEdit>(app.window, QStringLiteral("temperatureField"));
    temperature.setFocus();
    temperature.selectAll();
    QTest::keyClicks(&temperature, QStringLiteral("100"));
    // Return takes the number and goes on to OK.
    QTest::keyClick(&temperature, Qt::Key_Return);
    QTRY_VERIFY(!app.session().filterEdit() && !app.session().isProjectBusy());
    QCOMPARE(app.session().history.undoName(), QString("Camera Raw Filter"));
    app.shot(QStringLiteral("20-camera-raw"));
    QCOMPARE(app.session().history.undoName(), QString("Camera Raw Filter"));
    const QColor warm = app.shown(32, 32);
    QVERIFY2(std::abs(warm.red() - 235) <= 4 && std::abs(warm.green() - 189) <= 4 && std::abs(warm.blue() - 117) <= 4, qPrintable(warm.name()));
}

void AcceptanceLatestTests::navigatorAndMiddleZoom()
{
    App app;
    photo(app);
    // From 300% the Navigator shows; a click moves the view.
    app.action("navigator").trigger();
    app.session().zoom(4, app.session().viewport.center());
    QWidget &minimap = shownChild<QWidget>(app.window, QStringLiteral("navigatorMinimap"));
    QTest::mouseClick(&minimap, Qt::LeftButton, Qt::NoModifier, QPoint(4, 4));
    // The view's middle on the document point under the click.
    const QPointF target = NavigatorGeometry{QSizeF(1200, 800), QSizeF(minimap.size())}.documentPoint(QPointF(4, 4));
    const QPointF middle = app.session().viewport.visibleDocumentRect(QSizeF(1200, 800)).center();
    QVERIFY2(std::abs(middle.x() - target.x()) < 1 && std::abs(middle.y() - target.y()) < 1,
             qPrintable(QStringLiteral("%1,%2 %3,%4").arg(middle.x()).arg(middle.y()).arg(target.x()).arg(target.y())));
    QVERIFY(target.x() < 300 && target.y() < 200);
    app.shot(QStringLiteral("21-navigator"));
    app.action("navigator").trigger();
    // Ctrl with the middle button: a hundred points up doubles.
    app.session().zoom(1, app.session().viewport.center());
    const QPoint start = app.at(QPointF(600, 400));
    QTest::mousePress(&app.canvas(), Qt::MiddleButton, Qt::ControlModifier, start);
    const QPointF up = start - QPointF(0, 100);
    QMouseEvent event(QEvent::MouseMove, up, up, app.canvas().mapToGlobal(up.toPoint()), Qt::NoButton, Qt::MiddleButton, Qt::ControlModifier);
    QApplication::sendEvent(&app.canvas(), &event);
    QTest::mouseRelease(&app.canvas(), Qt::MiddleButton, Qt::ControlModifier, up.toPoint());
    QCOMPARE(app.session().viewport.zoom(), 2.0);
}

void AcceptanceLatestTests::newCanvasExportAndTabs()
{
    App app;
    // The blank start tab has no cross; Close leaves it.
    QToolButton *cross = app.window.findChild<QToolButton *>(QStringLiteral("closeTab"));
    QVERIFY(cross && cross->isHidden());
    const QUuid start = app.workspace.current().id;
    app.action("closeProject").trigger();
    QTest::qWait(100);
    QVERIFY(app.workspace.tabs().size() == 1 && app.workspace.selectedID() == start && app.workspace.tabs()[0]->id == start);
    // New Canvas: two by one inches, 300 DPI, white.
    NewCanvasSheet &sheet = shownChild<NewCanvasSheet>(app.window, QString());
    QAbstractButton &unit = shownChild<QAbstractButton>(sheet, QStringLiteral("canvasUnit"));
    while (!unit.text().contains(QStringLiteral("Inches")))
        unit.click();
    QAbstractButton &dpi = shownChild<QAbstractButton>(sheet, QStringLiteral("canvasResolution"));
    while (!dpi.text().startsWith(QStringLiteral("300")))
        dpi.click();
    QAbstractButton &ground = shownChild<QAbstractButton>(sheet, QStringLiteral("canvasBackground"));
    while (!ground.text().startsWith(QStringLiteral("White")))
        ground.click();
    QLineEdit &width = shownChild<QLineEdit>(sheet, QStringLiteral("widthInput"));
    QLineEdit &height = shownChild<QLineEdit>(sheet, QStringLiteral("heightInput"));
    width.selectAll();
    QTest::keyClicks(&width, QStringLiteral("2"));
    height.selectAll();
    QTest::keyClicks(&height, QStringLiteral("1"));
    shownChild<QAbstractButton>(sheet, QStringLiteral("createCanvas")).click();
    QTRY_COMPARE(documentSize(app), QSize(600, 300));
    QCOMPARE(app.session().document().value().resolution, 300.0);
    QCOMPARE(app.shown(300, 150), QColor(Qt::white));
    QTRY_VERIFY(!cross->isHidden());
    // File › Export As, PDF chosen: two inches by one.
    DialogDesk desk;
    desk.replies = {app.path("page.pdf")};
    app.action("exportAs").trigger();
    shownChild<QAbstractButton>(app.window, QStringLiteral("exportFormatPDF")).click();
    QPushButton &exporting = shownChild<QPushButton>(app.window, QStringLiteral("exportExport"));
    QTRY_VERIFY(shownChild<QWidget>(app.window, QStringLiteral("exportPage")).isVisible() && exporting.isEnabled());
    app.shot(QStringLiteral("22-export-as"));
    exporting.click();
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(app.path("page.pdf")) && !app.session().isProjectBusy(), 30000);
    QFile pdf(app.path("page.pdf"));
    QVERIFY(pdf.open(QIODevice::ReadOnly));
    const QByteArray bytes = pdf.readAll();
    QVERIFY(bytes.startsWith("%PDF-1.4") && bytes.contains("/MediaBox [0 0 144.0000 72.0000]"));
}

QTEST_MAIN(AcceptanceLatestTests)
#include "AcceptanceLatestTests.moc"
