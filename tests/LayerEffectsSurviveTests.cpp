#include "Document/CanvasSize.h"
#include "Document/ImageTrim.h"
#include "IO/CanvasResizer.h"
#include "IO/ImageResizer.h"
#include "SessionFixtures.h"
#include <QtTest>

// Swift 1.4.9's: effects, live text and shapes outlast size changes.
class LayerEffectsSurviveTests : public QObject {
    Q_OBJECT
private slots:
    void canvasSizeKeepsEffectsTextAndShapes();
    void cropKeepsEffectsTextAndShapes();
    void trimKeepsEffectsTextAndShapes();
    void resolutionOnlyImageSizeKeepsEffectsTextAndShapes();
    void imageSizeKeepsEffectsScaledWithTheImage();
    void aStretchScalesByTheFactorsMeanWithinRange();
};

namespace {
LayerEffects effects()
{
    LayerEffects made;
    made.stroke = StrokeEffect{.size = 4};
    made.shadow = ShadowEffect{.distance = 20, .blur = 10};
    made.outerGlow = OuterGlowEffect{.size = 8};
    return made;
}

// What a layer carries beyond its pixels.
struct Extras {
    std::optional<LayerEffects> effects;
    std::optional<LayerTextStyle> text;
    std::optional<LayerShapeStyle> shape;
    friend bool operator==(const Extras &, const Extras &) = default;
};

std::vector<Extras> extras(const EditorSession &session, const std::vector<QUuid> &ids)
{
    std::vector<Extras> found;
    for (const QUuid id : ids) {
        const ImageLayer layer = layerWith(session, id);
        found.push_back({layer.effects, layer.liveText() ? std::optional(layer.liveText().value().style) : std::nullopt,
                         layer.liveShape() ? std::optional(layer.liveShape().value().style) : std::nullopt});
    }
    return found;
}

// A clear canvas holding text and a shape, with effects.
std::unique_ptr<EditorSession> textAndShape(std::vector<QUuid> &ids)
{
    auto session = std::make_unique<EditorSession>();
    session->createDocument(200, 120);
    session->selectTool(NavigationTool::type);
    session->beginText(QPointF(30, 60));
    TextDraft draft = session->textDraft().value();
    draft.style.content = QStringLiteral("Text");
    if (!session->applyText(draft))
        throw std::runtime_error("no text");
    const QUuid text = session->activeLayerID().value();
    session->selectTool(NavigationTool::shape);
    session->beginShape(QPointF(120, 30));
    session->dragShape(QPointF(170, 80), false, false);
    session->finishShape();
    const QUuid shape = session->activeLayerID().value();
    rewrite(*session, [&](ProjectSnapshot &snapshot) { record(snapshot, text).effects = record(snapshot, shape).effects = effects(); });
    ids = {text, shape};
    const std::vector<Extras> before = extras(*session, ids);
    if (shape == text || !before[0].text || !before[1].shape)
        throw std::runtime_error("no live text or shape");
    return session;
}
}

void LayerEffectsSurviveTests::canvasSizeKeepsEffectsTextAndShapes()
{
    std::vector<QUuid> ids;
    const auto session = textAndShape(ids);
    const std::vector<Extras> before = extras(*session, ids);
    session->applyDocumentSize(CanvasResizer::resize(session->projectSnapshot().value(), CanvasSizeOptions{.width = 260, .height = 160, .anchor = 4}),
                               QStringLiteral("Canvas Size"));
    QCOMPARE(session->document().value().width, 260);
    QVERIFY(extras(*session, ids) == before);
}

void LayerEffectsSurviveTests::cropKeepsEffectsTextAndShapes()
{
    std::vector<QUuid> ids;
    const auto session = textAndShape(ids);
    const std::vector<Extras> before = extras(*session, ids);
    session->selectTool(NavigationTool::crop);
    session->setCropRect(QRectF(10, 10, 180, 100));
    bool done = false;
    session->commitCrop([&done] { done = true; });
    QTRY_VERIFY(done);
    QCOMPARE(session->document().value().width, 180);
    QVERIFY(extras(*session, ids) == before);
}

void LayerEffectsSurviveTests::trimKeepsEffectsTextAndShapes()
{
    std::vector<QUuid> ids;
    const auto session = textAndShape(ids);
    const std::vector<Extras> before = extras(*session, ids);
    session->applyDocumentSize(ImageTrim::trim(session->projectSnapshot().value(), TrimOptions()).value(), QStringLiteral("Trim"));
    QVERIFY(session->document().value().width < 200);
    QVERIFY(extras(*session, ids) == before);
}

void LayerEffectsSurviveTests::resolutionOnlyImageSizeKeepsEffectsTextAndShapes()
{
    std::vector<QUuid> ids;
    const auto session = textAndShape(ids);
    const std::vector<Extras> before = extras(*session, ids);
    session->applyImageSize(ImageResizer::resize(session->projectSnapshot().value(), ImageSizeOptions{.width = 200, .height = 120, .resolution = 300}));
    QCOMPARE(session->document().value().resolution, 300.0);
    QVERIFY(extras(*session, ids) == before);
}

// Resampled pixels take their effects with them, as Scale Styles.
void LayerEffectsSurviveTests::imageSizeKeepsEffectsScaledWithTheImage()
{
    std::vector<QUuid> ids;
    const auto session = textAndShape(ids);
    session->applyImageSize(ImageResizer::resize(session->projectSnapshot().value(), ImageSizeOptions{.width = 100, .height = 60, .resolution = 72}));
    QCOMPARE(session->document().value().width, 100);
    for (const Extras &kept : extras(*session, ids)) {
        QVERIFY(!kept.text && !kept.shape);
        const LayerEffects scaled = kept.effects.value();
        QVERIFY(scaled.stroke.value().size == 2 && scaled.outerGlow.value().size == 4);
        QVERIFY(scaled.shadow.value().distance == 10 && scaled.shadow.value().blur == 5 && scaled.shadow.value().angle == effects().shadow.value().angle);
    }
}

void LayerEffectsSurviveTests::aStretchScalesByTheFactorsMeanWithinRange()
{
    LayerEffects made;
    made.stroke = StrokeEffect{.size = 300};
    made.shadow = ShadowEffect{.distance = 3000, .blur = 300, .opacity = 0.4};
    made.innerShadow = InnerShadowEffect{.distance = 10, .blur = 4};
    made.innerGlow = InnerGlowEffect{.size = 6};
    made.colorOverlay = ColorOverlayEffect{.opacity = 0.3};
    const LayerEffects doubled = made.scaled(2);
    // Each held to the range the effect accepts.
    QVERIFY(doubled.stroke.value().size == 500 && doubled.shadow.value().distance == 5000 && doubled.shadow.value().blur == 500);
    QVERIFY(doubled.innerShadow.value().distance == 20 && doubled.innerShadow.value().blur == 8 && doubled.innerGlow.value().size == 12);
    QVERIFY(doubled.shadow.value().opacity == 0.4 && doubled.colorOverlay == made.colorOverlay && !doubled.outerGlow && doubled.isValid());
    // The rest to 500, the inner distance to 5,000.
    LayerEffects large;
    large.innerShadow = InnerShadowEffect{.distance = 2600, .blur = 260};
    large.outerGlow = OuterGlowEffect{.size = 260};
    large.innerGlow = InnerGlowEffect{.size = 260};
    const LayerEffects capped = large.scaled(2);
    QVERIFY(capped.innerShadow.value().distance == 5000 && capped.innerShadow.value().blur == 500);
    QVERIFY(capped.outerGlow.value().size == 500 && capped.innerGlow.value().size == 500 && capped.isValid());
    const LayerEffects under = large.scaled(1.5);
    QVERIFY(under.innerShadow.value().distance == 3900 && under.outerGlow.value().size == 390 && under.innerGlow.value().size == 390);
    // Stretched 4 by 1: the mean factor is 2.
    EditorSession session;
    session.createDocument(20, 20);
    QImage pixels(20, 20, QImage::Format_RGBA8888_Premultiplied);
    pixels.fill(Qt::red);
    session.insert(ImportedImage(pixels, pixels, QStringLiteral("Red")));
    const QUuid id = session.activeLayerID().value();
    rewrite(session, [&](ProjectSnapshot &snapshot) { record(snapshot, id).effects = effects(); });
    session.applyImageSize(ImageResizer::resize(session.projectSnapshot().value(), ImageSizeOptions{.width = 80, .height = 20, .resolution = 72}));
    QVERIFY(layerWith(session, id).effects == effects().scaled(2));
    QCOMPARE(layerWith(session, id).effects.value().stroke.value().size, 8.0);
}

QTEST_MAIN(LayerEffectsSurviveTests)
#include "LayerEffectsSurviveTests.moc"
