#include "ContentView.h"
#include "MenuFixtures.h"
#include "Document/DocumentLimits.h"
#include <QPainter>

// Filter › Last Filter: the last filter again, same settings.
namespace {
// Swift's fixture: 40 by 20, the left half white.
std::unique_ptr<EditorSession> halfWhite()
{
    auto session = std::make_unique<EditorSession>();
    session->createDocument(40, 20);
    QImage image = BrushRaster::context(40, 20, false);
    QPainter(&image).fillRect(QRect(0, 0, 20, 20), Qt::white);
    session->insert(ImportedImage(image, image, QStringLiteral("Half")));
    return session;
}

bool committed(EditorSession &session)
{
    bool done = false;
    session.commitFilter([&done] { done = true; });
    return QTest::qWaitFor([&done] { return done; }, 20000);
}

bool repeated(EditorSession &session)
{
    bool done = false;
    session.repeatLastFilter([&done] { done = true; });
    return QTest::qWaitFor([&done] { return done; }, 20000);
}

ImageLayer active(const EditorSession &session)
{
    return session.activeLayer().value();
}
}

class LastFilterTests : public QObject {
    Q_OBJECT
private slots:
    void runsTheLastFilterAgainWithItsSettingsAsOneUndoStep();
    void aCancelledFilterOrAnImageAdjustmentIsNotTheLastFilter();
    void theGatesAndAPendingGradient();
    void anAutomaticFilterRunsAgainThroughItsPreview();
    void theMenuNamesItAndRunsWithoutThePanel();
    void aRepeatThatCannotBeginOrLandLeavesNothingOpen();
};

void LastFilterTests::runsTheLastFilterAgainWithItsSettingsAsOneUndoStep()
{
    const auto session = halfWhite();
    QVERIFY(!session->canRepeatLastFilter() && !session->lastFilter());
    session->beginFilter(FilterKind::gaussianBlur);
    FilterSettings settings;
    settings.radius = 3;
    session->updateFilter(settings, true);
    QVERIFY(committed(*session));
    const ImageLayer once = active(*session);
    QVERIFY(session->lastFilter() == FilterKind::gaussianBlur && session->canRepeatLastFilter());
    const int count = session->history.undoCount();
    QVERIFY(repeated(*session));
    const ImageLayer twice = active(*session);
    QVERIFY(!session->filterEdit() && session->history.undoCount() == count + 1 && session->history.undoName() == "Gaussian Blur");
    QVERIFY(twice.asset->identity() != once.asset->identity());
    // Blurred by 3 again, it spreads past the edge further.
    QVERIFY(twice.transform.size.width() > once.transform.size.width());
    QCOMPARE(session->filterSettings().radius, 3.0);
    session->undo();
    QVERIFY(active(*session).asset->identity() == once.asset->identity());
}

void LastFilterTests::aCancelledFilterOrAnImageAdjustmentIsNotTheLastFilter()
{
    const auto session = halfWhite();
    session->beginFilter(FilterKind::addNoise);
    QVERIFY(committed(*session));
    session->beginFilter(FilterKind::gaussianBlur);
    session->cancelFilter();
    session->beginFilter(FilterKind::curves);
    session->cancelFilter();
    QCOMPARE(session->lastFilter(), std::optional(FilterKind::addNoise));
    // An Image menu adjustment committed is not recorded either.
    session->beginFilter(FilterKind::exposure);
    FilterSettings brighter = session->filterSettings();
    brighter.exposure.exposure = 1;
    session->updateFilter(brighter, true);
    QVERIFY(committed(*session));
    QCOMPARE(session->lastFilter(), std::optional(FilterKind::addNoise));
    // Nothing to do: no step, and nothing left open.
    QSignalSpy changes(session.get(), &EditorSession::changed);
    session->setLastFilter(FilterKind::lensCorrection);
    QCOMPARE(changes.count(), 1);
    const int count = session->history.undoCount();
    QVERIFY(repeated(*session));
    QVERIFY(!session->filterEdit() && session->history.undoCount() == count && !session->isProjectBusy());
    QVERIFY(!repeatsAsLastFilter(FilterKind::contentAwareFill) && !repeatsAsLastFilter(FilterKind::grain) && repeatsAsLastFilter(FilterKind::dither));
}

void LastFilterTests::theGatesAndAPendingGradient()
{
    const auto session = halfWhite();
    session->setLastFilter(FilterKind::addNoise);
    QVERIFY(session->canRepeatLastFilter());
    session->beginFilter(FilterKind::gaussianBlur);
    QVERIFY(!session->canRepeatLastFilter());
    // A refused run warns and calls back, changing nothing.
    QTest::ignoreMessage(QtWarningMsg, "no last filter to run here");
    QVERIFY(repeated(*session));
    QCOMPARE(session->filterEdit()->kind, FilterKind::gaussianBlur);
    session->cancelFilter();
    session->beginHueSaturation();
    QVERIFY(session->hueSaturation() && !session->canRepeatLastFilter());
    session->cancelHueSaturation();
    // Vignette asks Vignette's gate: an empty layer takes it.
    session->addBlankLayer();
    QVERIFY(!session->canRepeatLastFilter());
    session->setLastFilter(FilterKind::vignette);
    QVERIFY(session->canRepeatLastFilter());
    // A pending gradient lands first, then the filter: two steps.
    session->setLastFilter(FilterKind::addNoise);
    session->selectLayer(session->document()->layers.front().id);
    session->selectTool(NavigationTool::gradient);
    session->beginGradient(QPointF(2, 2));
    session->moveGradient(std::nullopt, QPointF(30, 2));
    session->endGradientDrag();
    QVERIFY(session->gradientEdit());
    const int count = session->history.undoCount();
    QVERIFY(repeated(*session));
    QVERIFY(!session->gradientEdit() && session->history.undoCount() == count + 2 && session->history.undoName() == "Add Noise");
}

void LastFilterTests::anAutomaticFilterRunsAgainThroughItsPreview()
{
    // Remove Background commits what its preview made.
    auto session = std::make_unique<EditorSession>();
    session->createDocument(400, 300);
    QImage image = BrushRaster::context(400, 300, false);
    image.fill(Qt::white);
    QPainter painter(&image);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(217, 26, 26));
    painter.drawEllipse(QPointF(200, 150), 80, 80);
    painter.end();
    session->insert(ImportedImage(image, image, QStringLiteral("Disc")));
    session->setLastFilter(FilterKind::removeBackground);
    const int count = session->history.undoCount();
    QVERIFY(repeated(*session));
    QVERIFY(session->history.undoCount() == count + 1 && session->history.undoName() == "Remove Background");
    QVERIFY(active(*session).mask && !session->filterEdit());
}

void LastFilterTests::theMenuNamesItAndRunsWithoutThePanel()
{
    Bar bar;
    bar.window.show();
    QVERIFY(QTest::qWaitForWindowActive(&bar.window));
    QAction &entry = bar.action("lastFilter");
    QVERIFY(entry.text() == "Last Filter" && !entry.isEnabled());
    QCOMPARE(entry.shortcut(), QKeySequence(Qt::CTRL | Qt::META | Qt::Key_F));
    bar.session().createDocument(40, 20);
    bar.session().insert(white(), QPointF(20, 10));
    bar.session().beginFilter(FilterKind::addNoise);
    QTRY_VERIFY(bar.window.findChild<QWidget *>("filterPanel") && bar.window.findChild<QWidget *>("filterPanel")->isVisible());
    QVERIFY(committed(bar.session()));
    QTRY_VERIFY(!bar.window.findChild<QWidget *>("filterPanel")->isVisible());
    QVERIFY(entry.text() == "Last Filter: Add Noise" && entry.isEnabled());
    // Its key runs it at once; the panel never shows.
    bool panelShown = false;
    QObject::connect(&bar.session(), &EditorSession::changed, &bar.window, [&] {
        panelShown = panelShown || bar.window.findChild<QWidget *>("filterPanel")->isVisible();
    });
    const int count = bar.session().history.undoCount();
    QTest::keyClick(&bar.window, Qt::Key_F, Qt::ControlModifier | Qt::MetaModifier);
    QTRY_COMPARE(bar.session().history.undoCount(), count + 1);
    QTRY_VERIFY(!bar.session().isProjectBusy());
    QVERIFY(!panelShown && !bar.session().filterEdit());
    // While a filter is open it rests.
    bar.session().beginFilter(FilterKind::gaussianBlur);
    QVERIFY(!entry.isEnabled());
    bar.session().cancelFilter();
    QVERIFY(entry.isEnabled());
}

void LastFilterTests::aRepeatThatCannotBeginOrLandLeavesNothingOpen()
{
    // A repeat begins without a preview, unless automatic.
    const auto session = halfWhite();
    session->beginFilter(FilterKind::gaussianBlur, true);
    QVERIFY(session->filterEdit()->repeating && !session->filterEdit()->preview);
    session->cancelFilter();
    session->beginFilter(FilterKind::removeBackground, true);
    QVERIFY(session->filterEdit()->repeating && session->filterEdit()->preview);
    session->cancelFilter();
    // No subject: the preview fails, and the repeat closes it.
    session->setLastFilter(FilterKind::removeBackground);
    const int count = session->history.undoCount();
    QVERIFY(repeated(*session));
    QVERIFY(!session->filterEdit() && session->history.undoCount() == count && !session->isProjectBusy());
    // A blur that cannot grow begins nothing: an error.
    EditorSession wide;
    wide.createDocument(100, 10);
    QImage line = BrushRaster::context(DocumentLimits::maxSide - 4, 1, false);
    line.fill(Qt::white);
    wide.insert(ImportedImage(line, QImage(), QStringLiteral("Line")));
    wide.setLastFilter(FilterKind::gaussianBlur);
    QVERIFY(repeated(wide));
    QVERIFY(!wide.filterEdit() && wide.brushError() && !wide.isProjectBusy());
}

QTEST_MAIN(LastFilterTests)
#include "LastFilterTests.moc"
