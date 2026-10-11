#include "Document/BrushStroke.h"
#include "Document/LayerEffects+Renderer.h"
#include <QtTest>

// Swift's ColorOverlayTests, and the inner effects alike.
namespace {
QImage square(const QColor &colour, int size = 20)
{
    QImage image = BrushRaster::context(size, size, false);
    image.fill(colour);
    return image;
}

// The premultiplied RGBA bytes at the image's middle.
std::array<int, 4> middle(const QImage &image)
{
    const uchar *pixel = image.constScanLine(image.height() / 2) + (image.width() / 2) * 4;
    return {pixel[0], pixel[1], pixel[2], pixel[3]};
}

std::array<int, 4> rendered(const QImage &source, const LayerEffects &effects)
{
    const QImage image = LayerEffectsRenderer::render(source, std::nullopt, effects).image;
    if (image.format() != QImage::Format_RGBA8888_Premultiplied)
        qFatal("the renderer's format changed");
    return middle(image);
}

bool near(const std::array<int, 4> &pixel, const std::array<int, 4> &expected, int tolerance)
{
    for (int channel = 0; channel < 4; ++channel) {
        if (std::abs(pixel[channel] - expected[channel]) > tolerance)
            return false;
    }
    return true;
}

QString text(const std::array<int, 4> &pixel)
{
    return QStringLiteral("%1 %2 %3 %4").arg(pixel[0]).arg(pixel[1]).arg(pixel[2]).arg(pixel[3]);
}
}

class ColorOverlayTests : public QObject {
    Q_OBJECT
private slots:
    void overlayRecolorsTranslucentPixelsAndKeepsTheirAlpha();
    void overlayOpacityMixesWithTheLayersOwnColor();
    void overlayLeavesTheShadowBeneathTranslucentPixels();
    void innerEffectsRecolourTranslucentPixelsAndKeepTheirAlpha();
};

void ColorOverlayTests::overlayRecolorsTranslucentPixelsAndKeepsTheirAlpha()
{
    // Half-clear black under white: half-clear white, no grey band.
    LayerEffects effects;
    effects.colorOverlay = ColorOverlayEffect{.red = 1, .green = 1, .blue = 1, .opacity = 1};
    const std::array<int, 4> pixel = rendered(square(QColor(0, 0, 0, 128)), effects);
    QVERIFY2(near(pixel, {128, 128, 128, 128}, 1), qPrintable(text(pixel)));
}

void ColorOverlayTests::overlayOpacityMixesWithTheLayersOwnColor()
{
    LayerEffects effects;
    effects.colorOverlay = ColorOverlayEffect{.red = 0, .green = 0, .blue = 1, .opacity = 0.5};
    const std::array<int, 4> pixel = rendered(square(QColor(255, 0, 0)), effects);
    QVERIFY2(near(pixel, {128, 0, 128, 255}, 1), qPrintable(text(pixel)));
}

void ColorOverlayTests::overlayLeavesTheShadowBeneathTranslucentPixels()
{
    // A red shadow under half-clear black stays red beneath.
    LayerEffects effects;
    effects.colorOverlay = ColorOverlayEffect{.red = 1, .green = 1, .blue = 1, .opacity = 1};
    effects.shadow = ShadowEffect{.angle = 90, .distance = 0, .blur = 0, .red = 1, .green = 0, .blue = 0, .opacity = 1};
    const std::array<int, 4> pixel = rendered(square(QColor(0, 0, 0, 128)), effects);
    QVERIFY2(near(pixel, {192, 128, 128, 192}, 1), qPrintable(text(pixel)));
}

void ColorOverlayTests::innerEffectsRecolourTranslucentPixelsAndKeepTheirAlpha()
{
    const QImage half = square(QColor(0, 0, 0, 128));
    // A glow a hair wide falls by half.
    LayerEffects shadowed;
    shadowed.innerShadow = InnerShadowEffect{.angle = 0, .distance = 60, .blur = 0.01, .red = 1, .green = 0, .blue = 0, .opacity = 1};
    std::array<int, 4> pixel = rendered(half, shadowed);
    QVERIFY2(near(pixel, {128, 0, 0, 128}, 1), qPrintable(text(pixel)));
    // A hair-wide glow falls by what the shape lacks: half.
    LayerEffects glowing;
    glowing.innerGlow = InnerGlowEffect{.size = 0.01, .red = 1, .green = 1, .blue = 1, .opacity = 1};
    pixel = rendered(half, glowing);
    QVERIFY2(near(pixel, {64, 64, 64, 128}, 1), qPrintable(text(pixel)));
    // A shadow beneath stays untinted.
    shadowed.shadow = ShadowEffect{.angle = 90, .distance = 0, .blur = 0, .red = 0, .green = 0, .blue = 1, .opacity = 1};
    pixel = rendered(half, shadowed);
    QVERIFY2(near(pixel, {128, 0, 64, 192}, 1), qPrintable(text(pixel)));
}

QTEST_GUILESS_MAIN(ColorOverlayTests)
#include "ColorOverlayTests.moc"
