#include "Document/EditorSession.h"
#include "Rendering/NavigatorGeometry.h"
#include "SessionFixtures.h"
#include "UI/NavigatorMinimap.h"
#include <QSettings>
#include <QStandardPaths>
#include <QtTest>

// Swift 1.4.9's Navigator: the geometry, the picture, the minimap.
class NavigatorTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void documentFitsTheBoxCentered();
    void visibleRectFollowsZoomAndPan();
    void centeringKeepsZoomAndStopsFollowingFit();
    void pictureFitsTheLongerSideAndShowsTheComposite();
    void smallDocumentsAreNotEnlarged();
    void minimapFitsTheDocument();
    void hugeDocumentsWithAdjustmentsStillShow();
    void blursShrinkWithThePicture();
    void selectionChangesDoNotRedrawThePicture();
    void placedMasksMotionAndEachAxisScale();
    void editsInProgressStayOut();
    void theMinimapShowsFrom300PercentWhenTurnedOn();
    void thePictureRedrawsOnceEditsSettle();
    void aClickOrDragMovesTheView();
    void theViewsBoxIsDrawnInTheAccent();
};

namespace {
ImportedImage red(int width, int height)
{
    QImage image(width, height, QImage::Format_RGBA8888_Premultiplied);
    image.fill(Qt::red);
    return ImportedImage(image, image, QStringLiteral("Red"));
}

// A 400 by 300 document, viewed 800 by 600.
CanvasViewport viewport()
{
    CanvasViewport viewport;
    viewport.resize(QSizeF(800, 600), 1, QSizeF(400, 300));
    return viewport;
}

QColor at(const QImage &image, int x, int y)
{
    return image.pixelColor(x, y);
}

// A red layer of `size` at the origin.
QUuid placeRed(EditorSession &session, QSize pixels, QSizeF size)
{
    session.insert(red(pixels.width(), pixels.height()));
    const QUuid id = session.activeLayerID().value();
    rewrite(session, [&](ProjectSnapshot &snapshot) {
        record(snapshot, id).transform.origin = QPointF(0, 0);
        record(snapshot, id).transform.size = size;
    });
    session.selectLayer(id);
    return id;
}

struct Shown {
    EditorSession session;
    NavigatorMinimap minimap{session};
    Shown()
    {
        NavigatorMinimap::setVisibleSetting(true);
        session.createDocument(400, 300);
        placeRed(session, QSize(400, 300), QSizeF(400, 300));
        session.viewport.resize(QSizeF(800, 600), 1, QSizeF(400, 300));
        session.zoom(4, session.viewport.center());
    }
};
}

void NavigatorTests::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    QSettings().clear();
}

void NavigatorTests::documentFitsTheBoxCentered()
{
    const NavigatorGeometry geometry{QSizeF(400, 200), QSizeF(200, 200)};
    QCOMPARE(geometry.imageRect(), QRectF(0, 50, 200, 100));
    QCOMPARE(geometry.thumbnailRect(QRectF(100, 50, 200, 100)), QRectF(50, 75, 100, 50));
    QCOMPARE(geometry.documentPoint(QPointF(100, 100)), QPointF(200, 100));
    // Points outside the picture land on the document's edge.
    QCOMPARE(geometry.documentPoint(QPointF(-20, 199)), QPointF(0, 200));
    QCOMPARE((NavigatorGeometry{QSizeF(0, 0), QSizeF(10, 10)}.imageRect()), QRectF());
    QCOMPARE((NavigatorGeometry{QSizeF(0, 0), QSizeF(10, 10)}.documentPoint(QPointF(5, 5))), QPointF());
    QCOMPARE((NavigatorGeometry{QSizeF(10, 10), QSizeF(0, 10)}.imageRect()), QRectF());
    QCOMPARE((NavigatorGeometry{QSizeF(10, 0), QSizeF(10, 10)}.imageRect()), QRectF());
    QCOMPARE((NavigatorGeometry{QSizeF(0, 10), QSizeF(10, 10)}.thumbnailRect(QRectF(1, 1, 2, 2))), QRectF());
    QCOMPARE((NavigatorGeometry{QSizeF(0, 10), QSizeF(10, 10)}.documentPoint(QPointF(5, 5))), QPointF());
}

void NavigatorTests::visibleRectFollowsZoomAndPan()
{
    const QSizeF size(400, 300);
    CanvasViewport view = viewport();
    view.setZoom(4, view.center(), size);
    const QRectF visible = view.visibleDocumentRect(size);
    QVERIFY(std::abs(visible.width() - 200) < 0.001 && std::abs(visible.height() - 150) < 0.001);
    QVERIFY(std::abs(visible.center().x() - 200) < 0.001 && std::abs(visible.center().y() - 150) < 0.001);
    // Panned by 40 points: 10 pixels across at 4×.
    view.translate(QSizeF(40, -80));
    QVERIFY(std::abs(view.visibleDocumentRect(size).center().x() - 190) < 0.001 && std::abs(view.visibleDocumentRect(size).center().y() - 170) < 0.001);
}

void NavigatorTests::centeringKeepsZoomAndStopsFollowingFit()
{
    const QSizeF size(400, 300);
    CanvasViewport view = viewport();
    const double zoom = view.zoom();
    view.centerView(QPointF(50, 60), size);
    const QRectF visible = view.visibleDocumentRect(size);
    QVERIFY(std::abs(visible.center().x() - 50) < 0.001 && std::abs(visible.center().y() - 60) < 0.001);
    QCOMPARE(view.zoom(), zoom);
    QVERIFY(!view.followsFit());
    // A later window resize keeps the view where it was.
    view.resize(QSizeF(1000, 700), 1, size);
    QCOMPARE(view.zoom(), zoom);
}

void NavigatorTests::pictureFitsTheLongerSideAndShowsTheComposite()
{
    EditorSession session;
    QVERIFY(!session.navigatorImage(100));
    session.createDocument(400, 200);
    QVERIFY(!session.navigatorImage(0));
    placeRed(session, QSize(200, 200), QSizeF(200, 200));
    const QImage picture = session.navigatorImage(100).value();
    QCOMPARE(picture.size(), QSize(100, 50));
    QCOMPARE(at(picture, 25, 25), QColor(255, 0, 0));
    QCOMPARE(at(picture, 75, 25).alpha(), 0);
    // Adjustment layers apply, as on the canvas.
    session.addAdjustment(AdjustmentKind::invert);
    QCOMPARE(at(session.navigatorImage(100).value(), 25, 25), QColor(0, 255, 255));
}

void NavigatorTests::smallDocumentsAreNotEnlarged()
{
    EditorSession session;
    session.createDocument(40, 30);
    QCOMPARE(session.navigatorImage(900).value().size(), QSize(40, 30));
    // Each side rounded, a pixel at least.
    session.createDocument(1000, 3);
    QCOMPARE(session.navigatorImage(100).value().size(), QSize(100, 1));
    session.createDocument(1000, 15);
    QCOMPARE(session.navigatorImage(100).value().size(), QSize(100, 2));
}

void NavigatorTests::minimapFitsTheDocument()
{
    QCOMPARE(NavigatorMinimap::sizeFor(QSizeF(4000, 3000)), QSizeF(120, 90));
    QCOMPARE(NavigatorMinimap::sizeFor(QSizeF(6000, 2000)), QSizeF(126, 42));
    QCOMPARE(NavigatorMinimap::sizeFor(QSizeF(1000, 999)), QSizeF(90, 90));
    // 125.7 rounds up.
    QCOMPARE(NavigatorMinimap::sizeFor(QSizeF(1000, 716)), QSizeF(126, 90));
    QCOMPARE(NavigatorMinimap::sizeFor(QSizeF(0, 0)), QSizeF(0, 0));
}

// Past one surface, drawn at the picture's size, it shows.
void NavigatorTests::hugeDocumentsWithAdjustmentsStillShow()
{
    EditorSession session;
    session.createDocument(20'000, 12'000);
    placeRed(session, QSize(200, 120), QSizeF(20'000, 12'000));
    session.addAdjustment(AdjustmentKind::invert);
    const QImage picture = session.navigatorImage(100).value();
    QCOMPARE(picture.size(), QSize(100, 60));
    QCOMPARE(at(picture, 50, 30), QColor(0, 255, 255));
}

void NavigatorTests::blursShrinkWithThePicture()
{
    EditorSession session;
    session.createDocument(2000, 2000);
    placeRed(session, QSize(100, 200), QSizeF(1000, 2000));
    session.addAdjustment(AdjustmentKind::gaussianBlur);
    session.setAdjustmentEditingID(std::nullopt);
    const QUuid blur = session.activeLayerID().value();
    LayerAdjustment adjustment = layerWith(session, blur).adjustment.value();
    adjustment.blurRadius = 100;
    session.updateAdjustment(blur, adjustment);
    const QImage picture = session.navigatorImage(100).value();
    // Five radii from every edge at the picture's scale: solid.
    QVERIFY2(at(picture, 25, 50).alpha() >= 250, qPrintable(QString::number(at(picture, 25, 50).alpha())));
    // Unshrunk, a radius of 100 would reach the edge here.
    QVERIFY(at(picture, 49, 50).alpha() > 0 && at(picture, 49, 50).alpha() < 250);
}

void NavigatorTests::selectionChangesDoNotRedrawThePicture()
{
    EditorSession session;
    session.createDocument(100, 100);
    placeRed(session, QSize(10, 10), QSizeF(10, 10));
    const NavigatorMinimap::RenderKey key(session.document(), 0);
    session.selectAll();
    QVERIFY(NavigatorMinimap::RenderKey(session.document(), 0) == key);
    QVERIFY(!(NavigatorMinimap::RenderKey(session.document(), 1) == key));
    std::optional<CanvasDocument> hidden = session.document();
    hidden->layers.back().isVisible = false;
    QVERIFY(!(NavigatorMinimap::RenderKey(hidden, 0) == key));
    std::optional<CanvasDocument> other = session.document();
    other->id = QUuid::createUuid();
    QVERIFY(!(NavigatorMinimap::RenderKey(other, 0) == key));
}

void NavigatorTests::placedMasksMotionAndEachAxisScale()
{
    // A placed mask shrinks with its layer: white, then black.
    EditorSession session;
    session.createDocument(400, 200);
    const QUuid id = placeRed(session, QSize(300, 200), QSizeF(300, 200));
    QImage halves(4, 1, QImage::Format_Grayscale8);
    const uchar values[] = {255, 255, 0, 0};
    std::copy_n(values, 4, halves.scanLine(0));
    rewrite(session, [&](ProjectSnapshot &snapshot) {
        setMask(snapshot, id, LayerMask::assetFrom(halves));
        record(snapshot, id).maskPlacement = LayerTransform{.origin = QPointF(0, 0), .size = QSizeF(200, 200)};
    });
    const QImage masked = session.navigatorImage(100).value();
    // At a quarter: white to 25, black to 50.
    QVERIFY(at(masked, 15, 25).alpha() == 255 && at(masked, 40, 25).alpha() == 0);
    // Each side alone: 1000 by 15 is 100 by 2.
    session.createDocument(1000, 15);
    placeRed(session, QSize(1000, 15), QSizeF(1000, 15));
    // The halving softens the last row; half would show half.
    const int bottom = at(session.navigatorImage(100).value(), 50, 1).alpha();
    QVERIFY2(bottom > 200, qPrintable(QString::number(bottom)));
    // A streak measured in document pixels shrinks too.
    session.createDocument(2000, 2000);
    placeRed(session, QSize(100, 200), QSizeF(1000, 2000));
    session.addAdjustment(AdjustmentKind::motionBlur);
    session.setAdjustmentEditingID(std::nullopt);
    const QUuid streak = session.activeLayerID().value();
    LayerAdjustment adjustment = layerWith(session, streak).adjustment.value();
    adjustment.motionAngle = 0;
    adjustment.motionDistance = 100;
    session.updateAdjustment(streak, adjustment);
    const QImage picture = session.navigatorImage(100).value();
    QVERIFY2(at(picture, 25, 50).alpha() >= 250, qPrintable(QString::number(at(picture, 25, 50).alpha())));
}

// Swift draws a bare session: a pending transform stays out.
void NavigatorTests::editsInProgressStayOut()
{
    EditorSession session;
    session.createDocument(400, 200);
    placeRed(session, QSize(200, 200), QSizeF(200, 200));
    session.selectTool(NavigationTool::move);
    session.beginTransform();
    LayerTransform moved = session.transformEdit().value().draft;
    moved.origin = QPointF(200, 0);
    session.previewTransform(moved);
    const QImage picture = session.navigatorImage(100).value();
    QVERIFY(at(picture, 25, 25).alpha() == 255 && at(picture, 75, 25).alpha() == 0);
}

void NavigatorTests::theMinimapShowsFrom300PercentWhenTurnedOn()
{
    NavigatorMinimap::setVisibleSetting(false);
    QCOMPARE(QSettings().value(QLatin1String(NavigatorMinimap::visibleKey)).toBool(), false);
    Shown shown;
    QVERIFY(!shown.minimap.isHidden() && shown.minimap.size() == QSize(120, 90));
    QCOMPARE(shown.minimap.picture().size(), QSize(300, 225));
    QCOMPARE(shown.minimap.accessibleName(), QString("Navigator"));
    QCOMPARE(shown.minimap.accessibleDescription(), QString("Click or drag to move the view"));
    shown.session.zoom(2.9, shown.session.viewport.center());
    QVERIFY(shown.minimap.isHidden() && shown.minimap.picture().isNull());
    shown.session.zoom(3, shown.session.viewport.center());
    QVERIFY(!shown.minimap.isHidden() && !shown.minimap.picture().isNull());
    shown.session.setCanvasOnly(true);
    QVERIFY(shown.minimap.isHidden());
    shown.session.setCanvasOnly(false);
    NavigatorMinimap::setVisibleSetting(false);
    shown.session.notify();
    QVERIFY(shown.minimap.isHidden());
    NavigatorMinimap::setVisibleSetting(true);
    shown.session.notify();
    QVERIFY(!shown.minimap.isHidden());
    shown.session.clearProject();
    QVERIFY(shown.minimap.isHidden());
}

void NavigatorTests::thePictureRedrawsOnceEditsSettle()
{
    Shown shown;
    QTimer &wait = *shown.minimap.findChild<QTimer *>(QStringLiteral("navigatorWait"));
    QVERIFY(wait.interval() == 250 && wait.isSingleShot() && wait.timerType() == Qt::PreciseTimer && !wait.isActive());
    const QImage first = shown.minimap.picture();
    QCOMPARE(at(first, 150, 100), QColor(255, 0, 0));
    // A selection redraws nothing.
    shown.session.selectAll();
    QVERIFY(!wait.isActive());
    // An edit waits a moment, then redraws once.
    shown.session.addAdjustment(AdjustmentKind::invert);
    QVERIFY(wait.isActive());
    QCOMPARE(shown.minimap.picture().cacheKey(), first.cacheKey());
    QTRY_COMPARE(at(shown.minimap.picture(), 150, 100), QColor(0, 255, 255));
    QVERIFY(!wait.isActive());
    // Hidden mid-wait, the wait goes with the picture.
    shown.session.setLayerOpacity(0.5);
    QVERIFY(wait.isActive());
    shown.session.zoom(2, shown.session.viewport.center());
    QVERIFY(!wait.isActive() && shown.minimap.picture().isNull());
}

void NavigatorTests::aClickOrDragMovesTheView()
{
    Shown shown;
    const QSizeF size(400, 300);
    QSignalSpy changes(&shown.session, &EditorSession::changed);
    QTest::mousePress(&shown.minimap, Qt::LeftButton, Qt::NoModifier, QPoint(30, 30));
    QCOMPARE(changes.count(), 1);
    QRectF visible = shown.session.viewport.visibleDocumentRect(size);
    QVERIFY(std::abs(visible.center().x() - 100) < 0.001 && std::abs(visible.center().y() - 100) < 0.001);
    QCOMPARE(shown.session.viewport.zoom(), 4.0);
    // A drag past the picture holds to the document's edge.
    QTest::mouseMove(&shown.minimap, QPoint(119, 89));
    QMouseEvent move(QEvent::MouseMove, QPointF(200, 200), QPointF(200, 200), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&shown.minimap, &move);
    visible = shown.session.viewport.visibleDocumentRect(size);
    QVERIFY(std::abs(visible.center().x() - 400) < 0.001 && std::abs(visible.center().y() - 300) < 0.001);
    // Without the left button nothing moves.
    QMouseEvent hover(QEvent::MouseMove, QPointF(60, 45), QPointF(60, 45), Qt::NoButton, Qt::RightButton, Qt::NoModifier);
    QApplication::sendEvent(&shown.minimap, &hover);
    QTest::mousePress(&shown.minimap, Qt::RightButton, Qt::NoModifier, QPoint(60, 45));
    QVERIFY(std::abs(shown.session.viewport.visibleDocumentRect(size).center().x() - 400) < 0.001);
    // Over the canvas's cross, the arrow.
    QWidget canvas;
    canvas.setCursor(Qt::CrossCursor);
    NavigatorMinimap inside(shown.session, &canvas);
    QCOMPARE(inside.cursor().shape(), Qt::ArrowCursor);
}

void NavigatorTests::theViewsBoxIsDrawnInTheAccent()
{
    Shown shown;
    QPalette palette = shown.minimap.palette();
    palette.setColor(QPalette::Active, QPalette::Highlight, QColor(0, 200, 0));
    shown.minimap.setPalette(palette);
    const QImage drawn = shown.minimap.grab().toImage();
    // The view's middle 200 × 150 pixels: 30 to 90.
    QCOMPARE(at(drawn, 30, 45), QColor(0, 200, 0));
    QCOMPARE(at(drawn, 31, 45), QColor(0, 200, 0));
    QCOMPARE(at(drawn, 89, 45), QColor(0, 200, 0));
    QCOMPARE(at(drawn, 60, 23), QColor(0, 200, 0));
    QCOMPARE(at(drawn, 60, 45), QColor(255, 0, 0));
    QCOMPARE(at(drawn, 28, 45), QColor(255, 0, 0));
    // The rim: white at 25% over red at the edge.
    QCOMPARE(at(drawn, 0, 45), QColor(255, 64, 64));
    // Clear pixels show the minimap's own gray 0.11.
    rewrite(shown.session, [](ProjectSnapshot &snapshot) { snapshot.manifest.layers.back().transform.size = QSizeF(200, 300); });
    shown.session.zoom(4, shown.session.viewport.center());
    QTRY_COMPARE(at(shown.minimap.grab().toImage(), 110, 80), QColor(28, 28, 28));
    // A view smaller than three points still shows three.
    shown.session.viewport.resize(QSizeF(40, 30), 1, QSizeF(400, 300));
    shown.session.zoom(32, shown.session.viewport.center());
    const QRectF view = shown.session.viewport.visibleDocumentRect(QSizeF(400, 300));
    QVERIFY(view.width() * 0.3 < 1);
    const int left = int(std::floor(view.left() * 0.3)), row = int(view.center().y() * 0.3);
    const QImage tiny = shown.minimap.grab().toImage();
    QVERIFY(at(tiny, left + 1, row).green() > 100 && at(tiny, left + 1, row).red() < 60);
    QCOMPARE(at(tiny, left - 2, row), QColor(255, 0, 0));
}

QTEST_MAIN(NavigatorTests)
#include "NavigatorTests.moc"
