#include "MenuFixtures.h"
#include "SessionFixtures.h"

// Canvas Only's gates: what F and Escape may do.
namespace {
void filled(EditorSession &session)
{
    session.createDocument(20, 20);
    QImage pixels(10, 10, QImage::Format_RGBA8888_Premultiplied);
    pixels.fill(Qt::red);
    session.insert(ImportedImage(pixels, QImage(), "Red"), QPointF(10, 10));
}
}

class CanvasOnlySessionTests : public QObject {
    Q_OBJECT
private slots:
    void theFlagIsAnnounced();
    void fIsRefusedWhileTextOrADialogHoldsIt();
    void escapeWaitsForWhatItCancels();
};

void CanvasOnlySessionTests::theFlagIsAnnounced()
{
    EditorSession session;
    QVERIFY(!session.canvasOnly());
    QSignalSpy changes(&session, &EditorSession::changed);
    session.setCanvasOnly(true);
    QVERIFY(session.canvasOnly() && changes.count() == 1);
    session.setCanvasOnly(false);
    QVERIFY(!session.canvasOnly() && changes.count() == 2);
}

void CanvasOnlySessionTests::fIsRefusedWhileTextOrADialogHoldsIt()
{
    EditorSession session;
    QVERIFY(!session.canToggleCanvasOnly());
    filled(session);
    QVERIFY(session.canToggleCanvasOnly());
    const std::vector<std::pair<const char *, std::pair<std::function<void()>, std::function<void()>>>> holders{
        {"text", {[&] { session.selectTool(NavigationTool::type); session.beginText(QPointF(5, 5), true); }, [&] { session.cancelText(); }}},
        {"filter", {[&] { session.beginFilter(FilterKind::gaussianBlur); }, [&] { session.cancelFilter(); }}},
        {"hue", {[&] { session.beginHueSaturation(); }, [&] { session.cancelHueSaturation(); }}},
        {"levels", {[&] { session.beginLevels(); }, [&] { session.cancelLevels(); }}},
        {"picker", {[&] { session.openColorPicker(false); }, [&] { session.closeColorPicker(false); }}},
        {"adjustment", {[&] { session.addAdjustment(AdjustmentKind::curves); }, [&] { session.setAdjustmentEditingID(std::nullopt); session.undo(); }}},
        {"effects", {[&] { session.setEffectsEditing(LayerEffectSelection{session.activeLayerID().value(), LayerEffectKind::stroke}); },
                     [&] { session.setEffectsEditing(std::nullopt); }}},
        {"range", {[&] { session.beginColorRange(); }, [&] { session.cancelColorRange(); }}},
        {"amount", {[&] { session.setSelectionAmountOperation(SelectionAmountOperation::feather); }, [&] { session.setSelectionAmountOperation(std::nullopt); }}}};
    for (const auto &[name, steps] : holders) {
        steps.first();
        QVERIFY2(!session.canToggleCanvasOnly(), name);
        steps.second();
        QVERIFY2(session.canToggleCanvasOnly(), name);
    }
}

void CanvasOnlySessionTests::escapeWaitsForWhatItCancels()
{
    EditorSession session;
    QVERIFY(session.escapeHasNothingToCancel());
    filled(session);
    QVERIFY(session.escapeHasNothingToCancel());
    const std::vector<std::pair<const char *, std::pair<std::function<void()>, std::function<void()>>>> edits{
        {"text", {[&] { session.selectTool(NavigationTool::type); session.beginText(QPointF(5, 5), true); }, [&] { session.cancelText(); }}},
        {"levels", {[&] { session.beginLevels(); }, [&] { session.cancelLevels(); }}},
        {"stroke", {[&] { session.selectTool(NavigationTool::brush); session.beginBrush(QPointF(10, 10)); }, [&] { session.cancelBrush(); }}},
        {"warp", {[&] { session.selectTool(NavigationTool::blur); session.setBlurMode(BlurToolMode::liquify); session.beginBrush(QPointF(10, 10)); },
                  [&] { session.cancelBrush(); }}},
        {"lasso", {[&] { session.selectTool(NavigationTool::lasso); session.beginLasso(QPointF(2, 2), SelectionMode::replace); }, [&] { session.cancelLasso(); }}},
        {"shape", {[&] { session.selectTool(NavigationTool::shape); session.beginShape(QPointF(2, 2)); }, [&] { session.cancelShape(); }}},
        {"gradient", {[&] { session.selectTool(NavigationTool::gradient); session.beginGradient(QPointF(2, 2)); session.moveGradient(std::nullopt, QPointF(12, 2)); },
                      [&] { session.cancelGradient(); }}},
        {"crop", {[&] { session.selectTool(NavigationTool::crop); }, [&] { session.cancelCrop(); }}},
        {"guide", {[&] { session.beginGuideCreation(CanvasGuide::Axis::vertical, 4); }, [&] { session.finishGuideDrag(true); }}},
        {"transform", {[&] { session.selectTool(NavigationTool::move); session.beginTransform(); }, [&] { session.cancelTransform(); }}},
        {"pixels", {[&] { session.selectAll(); session.beginPixelMove(); }, [&] { session.cancelPixelMove(); session.deselect(); }}}};
    for (const auto &[name, steps] : edits) {
        steps.first();
        QVERIFY2(!session.escapeHasNothingToCancel(), name);
        steps.second();
        QVERIFY2(session.escapeHasNothingToCancel(), name);
    }
}

QTEST_MAIN(CanvasOnlySessionTests)
#include "CanvasOnlySessionTests.moc"
