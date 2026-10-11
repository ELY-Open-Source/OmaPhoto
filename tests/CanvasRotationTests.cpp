#include "Document/EditorSession.h"
#include "IO/ImageExporter.h"
#include "SessionFixtures.h"
#include <QSignalSpy>
#include <QtTest>

// Swift's CanvasRotationTests: a quarter turn, everything along.
namespace {
// Reds 10, 20, 30 over 40, 50, 60.
QImage pixels()
{
    QImage image = BrushRaster::context(3, 2, false);
    const int values[] = {10, 20, 30, 40, 50, 60};
    for (int index = 0; index < 6; ++index)
        image.setPixelColor(index % 3, index / 3, QColor(values[index], 0, 0));
    return image;
}

void canvas(EditorSession &session)
{
    session.createDocument(3, 2);
    session.insert(ImportedImage(pixels(), pixels(), QStringLiteral("Pixels")));
}

// The flattened canvas's reds, row by row.
QList<int> reds(const EditorSession &session)
{
    const QImage image = ImageExporter::render(session.projectSnapshot().value()).image;
    QList<int> values;
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            values << image.pixelColor(x, y).red();
    return values;
}
}

class CanvasRotationTests : public QObject {
    Q_OBJECT
private slots:
    void clockwiseTurnsThePixelsExactly();
    void counterclockwiseTurnsThePixelsExactly();
    void turningBackAndUndoingRestoreTheCanvas();
    void guidesAndSelectionTurnWithTheCanvas();
    void turnedLayersAndPlacedMasksTurnAlong();
    void itIsOneStepRefusedWhereLayersCannotChange();
    void everyLayerAndEveryPlacedMaskTurns();
};

void CanvasRotationTests::clockwiseTurnsThePixelsExactly()
{
    EditorSession session;
    canvas(session);
    session.rotateCanvas(true);
    QCOMPARE(session.document().value().size(), QSizeF(2, 3));
    // The left column, bottom to top, is the top row.
    QCOMPARE(reds(session), (QList<int>{40, 10, 50, 20, 60, 30}));
}

void CanvasRotationTests::counterclockwiseTurnsThePixelsExactly()
{
    EditorSession session;
    canvas(session);
    session.rotateCanvas(false);
    QCOMPARE(session.document().value().size(), QSizeF(2, 3));
    // The right column, top to bottom, is the top row.
    QCOMPARE(reds(session), (QList<int>{30, 60, 20, 50, 10, 40}));
}

void CanvasRotationTests::turningBackAndUndoingRestoreTheCanvas()
{
    EditorSession session;
    canvas(session);
    const QList<int> original = reds(session);
    session.rotateCanvas(true);
    session.rotateCanvas(false);
    QCOMPARE(session.document().value().size(), QSizeF(3, 2));
    QCOMPARE(reds(session), original);
    session.rotateCanvas(true);
    session.rotateCanvas(true);
    // Two quarter turns are a half turn.
    QList<int> reversed = original;
    std::reverse(reversed.begin(), reversed.end());
    QCOMPARE(reds(session), reversed);
    QCOMPARE(session.activeLayer().value().transform.rotation, 180.0);
    // Undo fits the view to the canvas it brings back.
    session.zoom(4);
    session.undo();
    QCOMPARE(session.document().value().size(), QSizeF(2, 3));
    const CanvasViewport fitted = [&] {
        CanvasViewport viewport = session.viewport;
        viewport.fit(QSizeF(2, 3));
        return viewport;
    }();
    QCOMPARE(session.viewport, fitted);
    session.undo();
    QCOMPARE(session.document().value().size(), QSizeF(3, 2));
    QCOMPARE(reds(session), original);
}

void CanvasRotationTests::guidesAndSelectionTurnWithTheCanvas()
{
    EditorSession session;
    session.createDocument(100, 60);
    rewrite(session, [](ProjectSnapshot &snapshot) {
        snapshot.manifest.guides = std::vector<CanvasGuide>{{QUuid::createUuid(), CanvasGuide::Axis::vertical, 30},
                                                            {QUuid::createUuid(), CanvasGuide::Axis::horizontal, 10}};
    });
    QPainterPath box;
    box.addRect(0, 0, 20, 10);
    session.setSelection(DocumentSelection{.path = box, .antialiased = false, .feather = 3}, QStringLiteral("Marquee"));
    session.rotateCanvas(true);
    const std::vector<CanvasGuide> &guides = session.document().value().guides;
    // Clockwise: x becomes y; y, the new width less y.
    QVERIFY(guides[0].axis == CanvasGuide::Axis::horizontal && guides[0].position == 30);
    QVERIFY(guides[1].axis == CanvasGuide::Axis::vertical && guides[1].position == 50);
    const DocumentSelection &selection = session.document().value().selection.value();
    QCOMPARE(selection.path.boundingRect(), QRectF(50, 0, 10, 20));
    QVERIFY(!selection.antialiased && selection.feather == 3);
    // Counterclockwise from the start: the other way round.
    session.undo();
    session.rotateCanvas(false);
    QVERIFY(session.document().value().guides[0].axis == CanvasGuide::Axis::horizontal && session.document().value().guides[0].position == 70);
    QVERIFY(session.document().value().guides[1].axis == CanvasGuide::Axis::vertical && session.document().value().guides[1].position == 10);
    QCOMPARE(session.document().value().selection.value().path.boundingRect(), QRectF(0, 80, 10, 20));
}

void CanvasRotationTests::turnedLayersAndPlacedMasksTurnAlong()
{
    EditorSession session;
    session.createDocument(100, 60);
    session.insert(ImportedImage(pixels(), pixels(), QStringLiteral("Pixels")), QPointF(20, 10));
    const QUuid id = session.activeLayerID().value();
    rewrite(session, [id](ProjectSnapshot &snapshot) {
        record(snapshot, id).transform.rotation = 300;
        record(snapshot, id).transform.flipX = true;
        setMask(snapshot, id, coverage());
        record(snapshot, id).maskPlacement = LayerTransform{.origin = QPointF(70, 40), .size = QSizeF(10, 4), .rotation = -30};
        record(snapshot, id).maskLinked = false;
    });
    const QPointF before = session.activeLayer().value().transform.center();
    session.rotateCanvas(true);
    const ImageLayer layer = session.activeLayer().value();
    // A centre (x, y) goes to (60 - y, x).
    QCOMPARE(layer.transform.center(), QPointF(60 - before.y(), before.x()));
    QCOMPARE(layer.transform.size, QSizeF(3, 2));
    QCOMPARE(layer.transform.rotation, 30.0);
    QVERIFY(layer.transform.flipX);
    const LayerTransform placed = layer.mask.value().placement.value();
    QCOMPARE(placed.center(), QPointF(18, 75));
    QCOMPARE(placed.rotation, 60.0);
    // Swift's truncatingRemainder keeps the sign: -60, then -150.
    session.rotateCanvas(false);
    QCOMPARE(session.activeLayer().value().transform.center(), before);
    QCOMPARE(session.activeLayer().value().transform.rotation, -60.0);
    session.rotateCanvas(false);
    QCOMPARE(session.activeLayer().value().transform.rotation, -150.0);
}

void CanvasRotationTests::itIsOneStepRefusedWhereLayersCannotChange()
{
    EditorSession session;
    session.rotateCanvas(true);
    QVERIFY(!session.document());
    canvas(session);
    session.setIsProjectBusy(true);
    QSignalSpy refused(&session, &EditorSession::changed);
    session.rotateCanvas(true);
    QCOMPARE(refused.count(), 0);
    QCOMPARE(session.document().value().size(), QSizeF(3, 2));
    session.setIsProjectBusy(false);
    // A drawn crop frame goes first, as Swift's cancelCrop.
    session.selectTool(NavigationTool::crop);
    QVERIFY(session.cropRect());
    session.zoom(3);
    session.rotateCanvas(false);
    QVERIFY(!session.cropRect());
    QCOMPARE(session.history.undoName(), QString("Rotate Canvas 90° Counterclockwise"));
    // The view fits the turned canvas.
    CanvasViewport fitted = session.viewport;
    fitted.fit(QSizeF(2, 3));
    QCOMPARE(session.viewport, fitted);
    session.undo();
    session.rotateCanvas(true);
    QCOMPARE(session.history.undoName(), QString("Rotate Canvas 90° Clockwise"));
    session.undo();
    QCOMPARE(session.document().value().size(), QSizeF(3, 2));
}

void CanvasRotationTests::everyLayerAndEveryPlacedMaskTurns()
{
    EditorSession session;
    session.createDocument(100, 60);
    session.insert(ImportedImage(pixels(), pixels(), QStringLiteral("Lower")), QPointF(10, 10));
    session.insert(ImportedImage(pixels(), pixels(), QStringLiteral("Middle")), QPointF(50, 30));
    session.insert(ImportedImage(pixels(), pixels(), QStringLiteral("Upper")), QPointF(90, 50));
    const std::vector<ImageLayer> before = session.document().value().layers;
    rewrite(session, [&](ProjectSnapshot &snapshot) {
        for (const ImageLayer &layer : before) {
            setMask(snapshot, layer.id, coverage());
            record(snapshot, layer.id).maskLinked = false;
        }
        record(snapshot, before[1].id).maskPlacement = LayerTransform{.origin = QPointF(30, 20), .size = QSizeF(8, 6)};
        record(snapshot, before[2].id).maskPlacement = LayerTransform{.origin = QPointF(70, 44), .size = QSizeF(10, 4)};
    });
    const std::vector<ImageLayer> placed = session.document().value().layers;
    session.rotateCanvas(true);
    const std::vector<ImageLayer> &turned = session.document().value().layers;
    QCOMPARE(turned.size(), size_t(3));
    for (size_t index = 0; index < 3; ++index) {
        const QPointF centre = placed[index].transform.center();
        QCOMPARE(turned[index].transform.center(), QPointF(60 - centre.y(), centre.x()));
        QCOMPARE(turned[index].transform.rotation, 90.0);
        if (index == 0)
            continue;
        const QPointF maskCentre = placed[index].mask.value().placement.value().center();
        QCOMPARE(turned[index].mask.value().placement.value().center(), QPointF(60 - maskCentre.y(), maskCentre.x()));
        QCOMPARE(turned[index].mask.value().placement.value().rotation, 90.0);
    }
    session.undo();
    QVERIFY(session.document().value().layers == placed);
}

QTEST_GUILESS_MAIN(CanvasRotationTests)
#include "CanvasRotationTests.moc"
