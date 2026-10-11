#include "UI/LayersPanel.h"
#include "UI/NativeLayerList.h"
#include <QtTest>

// Swift 1.4.8: the panel grays for what lasts, not moments.
namespace {
std::unique_ptr<EditorSession> withLayer()
{
    auto session = std::make_unique<EditorSession>();
    session->createDocument(20, 20);
    QImage pixels(20, 20, QImage::Format_RGBA8888_Premultiplied);
    pixels.fill(Qt::white);
    session->insert(ImportedImage(pixels, pixels, QStringLiteral("White")));
    return session;
}

using Steps = std::pair<std::function<void(EditorSession &)>, std::function<void(EditorSession &)>>;
}

class LayersLookEditableTests : public QObject {
    Q_OBJECT
private slots:
    void aMomentLooksEditableWhileTheGateStaysShut();
    void whatLastsGraysThePanel();
    void theFooterAndListFollowTheLook();
};

void LayersLookEditableTests::aMomentLooksEditableWhileTheGateStaysShut()
{
    const std::vector<std::pair<const char *, Steps>> moments{
        {"stroke", {[](EditorSession &s) { s.selectTool(NavigationTool::brush); s.beginBrush(QPointF(5, 5)); }, [](EditorSession &s) { s.cancelBrush(); }}},
        {"warp", {[](EditorSession &s) { s.selectTool(NavigationTool::blur); s.setBlurMode(BlurToolMode::liquify); s.beginBrush(QPointF(5, 5)); },
                  [](EditorSession &s) { s.cancelBrush(); }}},
        {"drag", {[](EditorSession &s) { s.selectTool(NavigationTool::move); s.beginTransform(false); }, [](EditorSession &s) { s.cancelTransform(); }}},
        {"pixels", {[](EditorSession &s) { s.selectAll(); s.beginPixelMove(); }, [](EditorSession &s) { s.cancelPixelMove(); s.deselect(); }}},
        {"busy", {[](EditorSession &s) { s.setIsProjectBusy(true); }, [](EditorSession &s) { s.setIsProjectBusy(false); }}}};
    const auto session = withLayer();
    QVERIFY(session->layersLookEditable() && session->canEditLayers());
    for (const auto &[name, steps] : moments) {
        steps.first(*session);
        QVERIFY2(session->layersLookEditable() && !session->canEditLayers(), name);
        steps.second(*session);
        QVERIFY2(session->layersLookEditable() && session->canEditLayers(), name);
    }
    // Busy long enough to show it: then it grays.
    session->setIsProjectBusy(true);
    QTRY_VERIFY(session->showsBusy());
    QVERIFY(!session->layersLookEditable());
    session->setIsProjectBusy(false);
    QVERIFY(session->layersLookEditable());
}

void LayersLookEditableTests::whatLastsGraysThePanel()
{
    QVERIFY(!EditorSession().layersLookEditable());
    const std::vector<std::pair<const char *, Steps>> lasting{
        {"amount", {[](EditorSession &s) { s.setSelectionAmountOperation(SelectionAmountOperation::feather); },
                    [](EditorSession &s) { s.setSelectionAmountOperation(std::nullopt); }}},
        {"range", {[](EditorSession &s) { s.beginColorRange(); }, [](EditorSession &s) { s.cancelColorRange(); }}},
        {"text", {[](EditorSession &s) { s.selectTool(NavigationTool::type); s.beginText(QPointF(5, 5), true); }, [](EditorSession &s) { s.cancelText(); }}},
        {"importing", {[](EditorSession &s) { s.setIsImporting(true); }, [](EditorSession &s) { s.setIsImporting(false); }}},
        {"sheet", {[](EditorSession &s) { s.setShowsNewDocument(true); }, [](EditorSession &s) { s.setShowsNewDocument(false); }}},
        {"importer", {[](EditorSession &s) { s.setShowsImporter(true); }, [](EditorSession &s) { s.setShowsImporter(false); }}},
        {"rename", {[](EditorSession &s) { s.setRenamingLayerID(s.activeLayerID()); }, [](EditorSession &s) { s.setRenamingLayerID(std::nullopt); }}},
        {"transform", {[](EditorSession &s) { s.selectTool(NavigationTool::move); s.beginTransform(); }, [](EditorSession &s) { s.cancelTransform(); }}},
        {"crop", {[](EditorSession &s) { s.selectTool(NavigationTool::crop); }, [](EditorSession &s) { s.cancelCrop(); }}},
        {"gradient", {[](EditorSession &s) { s.selectTool(NavigationTool::gradient); s.beginGradient(QPointF(2, 2)); s.moveGradient(std::nullopt, QPointF(12, 2)); },
                      [](EditorSession &s) { s.cancelGradient(); }}},
        {"hue", {[](EditorSession &s) { s.beginHueSaturation(); }, [](EditorSession &s) { s.cancelHueSaturation(); }}},
        {"levels", {[](EditorSession &s) { s.beginLevels(); }, [](EditorSession &s) { s.cancelLevels(); }}},
        {"filter", {[](EditorSession &s) { s.beginFilter(FilterKind::gaussianBlur); }, [](EditorSession &s) { s.cancelFilter(); }}},
        {"adjustment", {[](EditorSession &s) { s.setAdjustmentEditingID(s.activeLayerID()); }, [](EditorSession &s) { s.setAdjustmentEditingID(std::nullopt); }}}};
    const auto session = withLayer();
    for (const auto &[name, steps] : lasting) {
        steps.first(*session);
        QVERIFY2(!session->layersLookEditable(), name);
        steps.second(*session);
        QVERIFY2(session->layersLookEditable(), name);
    }
}

void LayersLookEditableTests::theFooterAndListFollowTheLook()
{
    const auto session = withLayer();
    LayersPanel panel(*session);
    panel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel));
    auto *add = panel.findChild<QToolButton *>("addBlankLayer");
    auto *effects = panel.findChild<QToolButton *>("layerEffects");
    auto *mask = panel.findChild<QToolButton *>("addLayerMask");
    auto *eye = panel.list().cells().at(0)->findChild<QToolButton *>("layerEye");
    auto *adjustments = panel.findChild<QToolButton *>("addAdjustment");
    session->addEffect(LayerEffectKind::stroke);
    const auto effectEye = [&] { return panel.list().cells().at(0)->findChild<LayerEffectRow *>()->findChild<QToolButton *>(); };
    QVERIFY(effectEye()->isEnabled());
    // Mid-stroke nothing flashes, and a click changes nothing.
    session->selectTool(NavigationTool::brush);
    session->beginBrush(QPointF(5, 5));
    QVERIFY(session->brushStroke());
    QVERIFY(add->isEnabled() && effects->isEnabled() && mask->isEnabled() && eye->isEnabled() && adjustments->isEnabled());
    // An effect's eye keeps Swift's own gate: the session's.
    QVERIFY(!effectEye()->isEnabled());
    QTest::mouseClick(add, Qt::LeftButton);
    QCOMPARE(session->document().value().layers.size(), size_t(1));
    session->cancelBrush();
    // A pending transform grays them.
    session->selectTool(NavigationTool::move);
    session->beginTransform();
    QVERIFY(!add->isEnabled() && !effects->isEnabled() && !mask->isEnabled() && !eye->isEnabled() && !adjustments->isEnabled());
    session->cancelTransform();
    // Effects want pixels: no folder, no adjustment, no layer.
    session->addGroup();
    QVERIFY(add->isEnabled() && !effects->isEnabled());
    session->addAdjustment(AdjustmentKind::invert);
    QVERIFY(!effects->isEnabled());
    session->selectLayer(std::nullopt);
    QVERIFY(!effects->isEnabled() && !mask->isEnabled() && !panel.findChild<QToolButton *>("deleteLayer")->isEnabled());
    // The mask button takes one layer, without a mask.
    const QUuid white = session->document().value().layers.front().id;
    session->selectLayers({white, session->document().value().layers.back().id}, white);
    QVERIFY(!mask->isEnabled());
    session->selectLayer(white);
    QVERIFY(mask->isEnabled() && effects->isEnabled());
    session->addMask();
    QVERIFY(!mask->isEnabled());
}

QTEST_MAIN(LayersLookEditableTests)
#include "LayersLookEditableTests.moc"
