#include "Document/BrushStroke.h"
#include "Rendering/TiledLayerRenderer.h"
#include <QPainter>
#include <QtTest>

// Compositor 1.4's sharper canvas: upright layers at 1:1 copy across.
namespace {
// Black and white columns, opaque, 100 by 4.
QImage columns()
{
    QImage image = BrushRaster::context(100, 4, false);
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 100; ++x)
            image.setPixel(x, y, x % 2 ? qRgba(255, 255, 255, 255) : qRgba(0, 0, 0, 255));
    }
    return image;
}

using Draw = std::function<void(const LayerTransform &, QPainter &)>;

// Pixels under the layer neither black nor white.
int blurred(double width, const Draw &draw)
{
    QImage surface = BrushRaster::context(120, 4, false);
    QPainter painter(&surface);
    draw(LayerTransform{.origin = {0, 0}, .size = {width, 4}}, painter);
    painter.end();
    int count = 0;
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 100; ++x)
            count += qRed(surface.pixel(x, y)) % 255 != 0;
    }
    return count;
}

// Opaque mid tones of 20 rows of columns, turned 2°.
int turned(const Draw &draw)
{
    QImage surface = BrushRaster::context(120, 30, false);
    QPainter painter(&surface);
    draw(LayerTransform{.origin = {0, 5}, .size = {100, 20}, .rotation = 2}, painter);
    painter.end();
    int count = 0;
    for (int y = 0; y < 30; ++y) {
        for (int x = 0; x < 100; ++x) {
            const QRgb pixel = surface.pixel(x, y);
            count += qAlpha(pixel) == 255 && qRed(pixel) > 30 && qRed(pixel) < 225;
        }
    }
    return count;
}
}

class SharpCanvasTests : public QObject {
    Q_OBJECT
private slots:
    void anUprightLayerPixelForPixelTakesNoFilter();
    void plainMaskedAndTiledLayersCopyAcross();
    void aQuarterTurnOnWholePixelsCopiesAcross();
};

void SharpCanvasTests::anUprightLayerPixelForPixelTakesNoFilter()
{
    for (LayerSampling sampling : {LayerSampling::smooth, LayerSampling::high}) {
        QCOMPARE(LayerRenderer::interpolation(sampling, 1.0009, true), InterpolationQuality::none);
        QCOMPARE(LayerRenderer::interpolation(sampling, 0.9991, true), InterpolationQuality::none);
        QCOMPARE(LayerRenderer::interpolation(sampling, 0.9989, true), InterpolationQuality::low);
        QCOMPARE(LayerRenderer::interpolation(sampling, 1, false), InterpolationQuality::low);
    }
    QCOMPARE(LayerRenderer::interpolation(LayerSampling::high, 1.0011, true), InterpolationQuality::high);
    QCOMPARE(LayerRenderer::interpolation(LayerSampling::high, 1.0009, false), InterpolationQuality::high);
    QCOMPARE(LayerRenderer::interpolation(LayerSampling::nearest, 1.5, true), InterpolationQuality::none);
}

void SharpCanvasTests::plainMaskedAndTiledLayersCopyAcross()
{
    const QImage image = columns();
    QImage white = BrushRaster::context(100, 4, true);
    white.fill(255);
    const auto raster = std::make_shared<const RasterSnapshot>(100, 4, image, QRectF(0, 0, 100, 4), std::vector<BrushPatch>{});
    const std::vector<Draw> draws{
        [&](const LayerTransform &transform, QPainter &painter) { LayerRenderer::draw(image, transform, transform.center(), painter); },
        [&](const LayerTransform &transform, QPainter &painter) {
            LayerRenderer::draw(image, transform, transform.center(), painter, {.mask = white});
        },
        [&](const LayerTransform &transform, QPainter &painter) {
            TiledLayerRenderer::drawRaster(raster, transform, transform.center(), painter);
        },
    };
    for (const Draw &draw : draws) {
        // A twentieth of a pixel over 100: within tolerance, crisp.
        QCOMPARE(blurred(100.05, draw), 0);
        // A fifth: filtered as before.
        QVERIFY(blurred(100.2, draw) > 0);
        // Turned, even pixel for pixel: filtered between columns.
        QVERIFY(turned(draw) > 100);
    }
}

void SharpCanvasTests::aQuarterTurnOnWholePixelsCopiesAcross()
{
    // The columns stood up, centred at (10, 60).
    const auto standing = [](double rotation, double nudge) {
        return LayerTransform{.origin = {-40 + nudge, 58}, .size = {100, 4}, .rotation = rotation};
    };
    QImage surface = BrushRaster::context(20, 120, false);
    QPainter painter(&surface);
    const auto upright = [&](const LayerTransform &transform) {
        QTransform placement;
        placement.translate(transform.center().x(), transform.center().y());
        placement.rotateRadians(transform.radians());
        const QSizeF size = transform.size;
        return LayerRenderer::upright(transform, placement, QRectF(-size.width() / 2, -size.height() / 2, size.width(), size.height()), painter);
    };
    for (double rotation : {90.0, -90.0, 180.0, 270.0, 450.0})
        QVERIFY2(upright(standing(rotation, 0)), qPrintable(QString::number(rotation)));
    QVERIFY(!upright(standing(90, 0.5)) && !upright(standing(90, 0.002)) && upright(standing(90, 0.0009)));
    QVERIFY(!upright(standing(45, 0)) && !upright(standing(90.000001, 0)));
    // Either axis off, or a side stretched: off the pixels.
    LayerTransform low = standing(90, 0);
    low.origin.ry() += 0.25;
    // 4.5 across: one corner on a pixel, one between.
    const LayerTransform stretched{.origin = {-40.25, 57.75}, .size = {100, 4.5}, .rotation = 90};
    QVERIFY(!upright(low) && !upright(stretched));
    // Swift's own upright holds off whole pixels too.
    QVERIFY(upright(standing(0, 0.3)));
    // The painter's own place counts.
    painter.translate(0.5, 0);
    QVERIFY(!upright(standing(90, 0)) && upright(standing(90, 0.5)));
    painter.end();
    const QImage image = columns();
    const auto raster = std::make_shared<const RasterSnapshot>(100, 4, image, QRectF(0, 0, 100, 4), std::vector<BrushPatch>{});
    const auto mids = [&](const LayerTransform &transform, bool tiled) {
        QImage drawn = BrushRaster::context(20, 120, false);
        QPainter onto(&drawn);
        if (tiled)
            TiledLayerRenderer::drawRaster(raster, transform, transform.center(), onto);
        else
            LayerRenderer::draw(image, transform, transform.center(), onto);
        onto.end();
        int count = 0;
        for (int y = 10; y < 110; ++y) {
            for (int x = 8; x < 12; ++x)
                count += qRed(drawn.pixel(x, y)) % 255 != 0 || qAlpha(drawn.pixel(x, y)) != 255;
        }
        return count;
    };
    for (bool tiled : {false, true}) {
        // On whole pixels: each column a row, exact.
        QCOMPARE(mids(standing(90, 0), tiled), 0);
        QCOMPARE(mids(standing(-90, 0), tiled), 0);
        // Half a pixel off: filtered, as CoreGraphics blends it.
        QVERIFY(mids(standing(90, 0.5), tiled) > 100);
    }
}

QTEST_GUILESS_MAIN(SharpCanvasTests)
#include "SharpCanvasTests.moc"
