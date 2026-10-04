#include "BrushFixtures.h"
#include "Document/EditorSession.h"
#include "SelectionFixtures.h"

// Swift resumes after await in one job: nothing lands between.
namespace {
std::unique_ptr<EditorSession> halves()
{
    auto session = std::make_unique<EditorSession>();
    session->createDocument(100, 40, true);
    QImage image = BrushRaster::context(100, 40, false);
    QPainter painter(&image);
    painter.fillRect(QRect(0, 0, 50, 40), Qt::red);
    painter.fillRect(QRect(50, 0, 50, 40), Qt::blue);
    painter.end();
    session->insert(ImportedImage(image, image, "Halves"));
    session->deleteLayer(session->document().value().layers[0].id);
    return session;
}

// Runs `look` at the first event after the project frees.
void atTheNextEvent(EditorSession &session, std::function<void()> look)
{
    auto busy = std::make_shared<bool>(false), armed = std::make_shared<bool>(false);
    QObject::connect(&session, &EditorSession::changed, &session, [&session, busy, armed, look] {
        if (session.isProjectBusy())
            *busy = true;
        if (!*busy || session.isProjectBusy() || *armed)
            return;
        *armed = true;
        QTimer::singleShot(0, &session, look);
    });
}

void pendingGradient(EditorSession &session)
{
    session.selectTool(NavigationTool::gradient);
    session.beginGradient(QPointF(0.5, 2));
    session.moveGradient(std::nullopt, QPointF(99.5, 2));
    session.endGradientDrag();
}

bool done(const std::function<void(std::function<void()>)> &call)
{
    bool finished = false;
    call([&] { finished = true; });
    return QTest::qWaitFor([&] { return finished; }, 10000);
}
}

class CommitContinuationTests : public QObject {
    Q_OBJECT
private slots:
    void theProjectFreesWithTheGradientGone();
    void theProjectFreesWithTheMoveGone();
    void theFloatingEditIsReadyAtTheNextEvent();
    void aToolChosenAfterTheLiftStaysChosen();
    void aChainedEditKeepsItsLayer_data();
    void aChainedEditKeepsItsLayer();
};

void CommitContinuationTests::theProjectFreesWithTheGradientGone()
{
    const auto session = halves();
    pendingGradient(*session);
    std::optional<bool> pending, editable;
    atTheNextEvent(*session, [&] {
        pending = session->gradientEdit().has_value();
        editable = session->canEditLayers();
    });
    QVERIFY(done([&](auto finished) { session->commitGradient(finished); }));
    QTRY_VERIFY(pending.has_value());
    QCOMPARE(pending.value(), false);
    QCOMPARE(editable.value(), true);
}

void CommitContinuationTests::theProjectFreesWithTheMoveGone()
{
    const auto session = halves();
    session->applySelection(rectPath(QRectF(0, 0, 50, 40)), SelectionMode::replace, "Select");
    QVERIFY(session->beginPixelMove(false));
    session->movePixels(QSizeF(10, 0));
    std::optional<bool> moving, editable;
    atTheNextEvent(*session, [&] {
        moving = session->pixelMove() != nullptr;
        editable = session->canEditLayers();
    });
    QVERIFY(done([&](auto finished) { session->finishPixelMove(finished); }));
    QTRY_VERIFY(moving.has_value());
    QCOMPARE(moving.value(), false);
    QCOMPARE(editable.value(), true);
}

void CommitContinuationTests::theFloatingEditIsReadyAtTheNextEvent()
{
    const auto session = halves();
    session->applySelection(rectPath(QRectF(10, 10, 10, 10)), SelectionMode::replace, "Select");
    std::optional<bool> floating, editable;
    atTheNextEvent(*session, [&] {
        floating = session->transformEdit().has_value();
        editable = session->canEditLayers();
    });
    QVERIFY(done([&](auto finished) { session->beginSelectionTransform(finished); }));
    QTRY_VERIFY(floating.has_value());
    QCOMPARE(floating.value(), true);
    QCOMPARE(editable.value(), false);
}

void CommitContinuationTests::aToolChosenAfterTheLiftStaysChosen()
{
    const auto session = halves();
    session->applySelection(rectPath(QRectF(10, 10, 10, 10)), SelectionMode::replace, "Select");
    bool chosen = false;
    atTheNextEvent(*session, [&] {
        session->selectTool(NavigationTool::brush);
        chosen = true;
    });
    QVERIFY(done([&](auto finished) { session->beginSelectionTransform(finished); }));
    QTRY_VERIFY(chosen);
    // The choice applied the floating edit and stayed.
    QCOMPARE(session->tool(), NavigationTool::brush);
    QVERIFY(!session->transformEdit().has_value());
}

void CommitContinuationTests::aChainedEditKeepsItsLayer_data()
{
    QTest::addColumn<int>("command");
    QTest::newRow("filter") << 0;
    QTest::newRow("levels") << 1;
    QTest::newRow("hue") << 2;
    QTest::newRow("invert") << 3;
}

void CommitContinuationTests::aChainedEditKeepsItsLayer()
{
    QFETCH(int, command);
    const auto session = halves();
    const QUuid target = session->activeLayerID().value();
    QImage green = BrushRaster::context(100, 40, false);
    green.fill(QColor(0, 255, 0));
    session->insert(ImportedImage(green, green, "Other"));
    const QUuid other = session->activeLayerID().value();
    session->selectLayer(target);
    pendingGradient(*session);
    // Another layer chosen once the gradient lands, if allowed.
    atTheNextEvent(*session, [&] {
        if (session->canEditLayers())
            session->selectLayer(other);
    });
    if (command == 0) {
        session->beginFilter(FilterKind::gaussianBlur);
        QTRY_VERIFY(session->filterEdit().has_value());
        QCOMPARE(session->filterEdit().value().layerID, target);
    } else if (command == 1) {
        session->beginLevels();
        QTRY_VERIFY(session->levels().has_value());
        QCOMPARE(session->levels().value().layerID, target);
    } else if (command == 2) {
        session->beginHueSaturation();
        QTRY_VERIFY(session->hueSaturation().has_value());
        QCOMPARE(session->hueSaturation().value().layerID, target);
    } else {
        QVERIFY(done([&](auto finished) { session->invertPixels(finished); }));
        QCOMPARE(session->history.undoName(), QString("Invert"));
        QCOMPARE(session->activeLayerID().value(), target);
        const ImageLayer kept = session->document().value().layers.back();
        QCOMPARE(kept.id, other);
        QCOMPARE(kept.asset.value().image().pixelColor(5, 5), QColor(0, 255, 0));
    }
}

QTEST_GUILESS_MAIN(CommitContinuationTests)
#include "CommitContinuationTests.moc"
