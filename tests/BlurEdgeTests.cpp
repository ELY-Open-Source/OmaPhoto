#include "Document/BrushStroke.h"
#include "Document/EditorSession.h"
#include "Document/PixelAdjust.h"
#include "SelectionFixtures.h"
#include "UI/FilterSheet.h"
#include <QCheckBox>
#include <QPainter>
#include <QRandomGenerator>
#include <QtTest>
#include <array>
#include <cmath>
#include <numbers>

// Swift 1.4.10's blurs: transparency kept, canvas edges carried on.
namespace {
// Premultiplied pixels of every alpha, the same each run.
QImage noise(int width, int height, quint32 seed)
{
    QRandomGenerator random(seed);
    QImage image = BrushRaster::context(width, height, false);
    for (int y = 0; y < height; ++y) {
        uchar *row = image.scanLine(y);
        for (int x = 0; x < width; ++x) {
            const int alpha = random.bounded(4) == 0 ? 0 : random.bounded(256);
            for (int c = 0; c < 3; ++c)
                row[x * 4 + c] = uchar(random.bounded(alpha + 1));
            row[x * 4 + 3] = uchar(alpha);
        }
    }
    return image;
}

ImageLayer layer(int width, int height, QPointF origin)
{
    QImage image = BrushRaster::context(width, height, false);
    image.fill(Qt::white);
    return ImageLayer(ImportedImage(image, image, QStringLiteral("Layer")), origin);
}

LayerSides sides(const ImageLayer &placed, QSizeF canvas, FilterKind kind = FilterKind::gaussianBlur)
{
    return FilterEdit(kind, placed, std::nullopt, FilterSettings{}, std::nullopt, canvas).canvasSides;
}

QImage run(const QImage &image, const FilterSettings &settings, FilterKind kind, LayerSides at)
{
    FilterJob job{kind, image, settings, 1, std::nullopt, QTransform()};
    job.canvasSides = at;
    return PixelFilter::run(job);
}
}

class BlurEdgeTests : public QObject {
    Q_OBJECT
private slots:
    void theDefaultsAndTheMargin();
    void colorBlurDividesInFloats();
    void aFaintLonePixelKeepsItsColour();
    void extendingRepeatsEachChosenSide();
    void everySideCarriedIsAClampedBlur();
    void aMotionBlurCarriesEachSideAsFarAsItReaches();
    void oneSideCarriedLeavesTheOthersToFade();
    void theSidesFollowTheCanvas();
    void growthPadsOnlyTheInnerSides();
    void aPreservedCommitKeepsTheLayerAsItWas();
    void thePreviewCarriesTheCanvasSides();
    void aPreservedCommitReadsTheSelectionOnTheLayer();
    void theSheetTogglesPreserveTransparency();
};

void BlurEdgeTests::theDefaultsAndTheMargin()
{
    QCOMPARE(FilterSettings().radius, 5.0);
    QVERIFY(!FilterSettings().preserveTransparency);
    QCOMPARE(FilterSettings{.radius = std::nan("")}.normalized().radius, 5.0);
    QVERIFY(FilterSettings{.preserveTransparency = true} != FilterSettings());
    QCOMPARE(FilterEdit::blurMargin(FilterKind::gaussianBlur, FilterSettings{.radius = 4}), 14.0);
    QCOMPARE(FilterEdit::blurMargin(FilterKind::gaussianBlur, FilterSettings{.radius = 4, .preserveTransparency = true}), 0.0);
}

void BlurEdgeTests::colorBlurDividesInFloats()
{
    const QImage source = noise(23, 17, 7);
    for (const double sigma : {0.7, 2.3, 9.0}) {
        const QImage blurred = PixelAdjust::colorBlur(source, sigma);
        // Three sigmas of normalised weights, clear past the edge.
        const int reach = int(std::ceil(3 * sigma));
        std::vector<double> weights;
        double total = 0;
        for (int i = 0; i <= reach; ++i) {
            weights.push_back(std::exp(-double(i) * i / (2 * sigma * sigma)));
            total += i ? 2 * weights.back() : weights.back();
        }
        for (double &weight : weights)
            weight /= total;
        for (int y = 0; y < 17; ++y)
            for (int x = 0; x < 23; ++x) {
                double sums[4] = {0, 0, 0, 0};
                for (int dy = -reach; dy <= reach; ++dy)
                    for (int dx = -reach; dx <= reach; ++dx) {
                        if (x + dx < 0 || x + dx >= 23 || y + dy < 0 || y + dy >= 17)
                            continue;
                        const uchar *pixel = source.constScanLine(y + dy) + (x + dx) * 4;
                        for (int c = 0; c < 4; ++c)
                            sums[c] += weights[size_t(std::abs(dx))] * weights[size_t(std::abs(dy))] * pixel[c];
                    }
                const int alpha = source.constScanLine(y)[x * 4 + 3];
                const uchar *made = blurred.constScanLine(y) + x * 4;
                QCOMPARE(int(made[3]), alpha);
                for (int c = 0; c < 3; ++c) {
                    const double expected = sums[3] > 0 ? sums[c] / sums[3] * alpha : 0;
                    QVERIFY2(std::abs(made[c] - expected) <= 0.51, qPrintable(QString("%1 %2 %3: %4 for %5").arg(x).arg(y).arg(c).arg(made[c]).arg(expected)));
                }
            }
    }
}

void BlurEdgeTests::aFaintLonePixelKeepsItsColour()
{
    // Its blurred alpha falls under a level; the colour stays.
    QImage image = BrushRaster::context(41, 41, false);
    uchar *pixel = image.scanLine(20) + 20 * 4;
    pixel[0] = 2;
    pixel[1] = 1;
    pixel[3] = 2;
    const QImage blurred = PixelAdjust::colorBlur(image, 9);
    const uchar *made = blurred.constScanLine(20) + 20 * 4;
    QCOMPARE((std::array<int, 4>{made[0], made[1], made[2], made[3]}), (std::array<int, 4>{2, 1, 0, 2}));
}

void BlurEdgeTests::extendingRepeatsEachChosenSide()
{
    const QImage source = noise(3, 2, 3);
    for (int mask = 0; mask < 16; ++mask) {
        const LayerSides at{bool(mask & 1), bool(mask & 2), bool(mask & 4), bool(mask & 8)};
        const PixelFilter::Extended padded = PixelFilter::extending(source, at, 4);
        const int left = at.left ? 4 : 0, top = at.top ? 4 : 0;
        QCOMPARE(padded.inside, QRect(left, top, 3, 2));
        QCOMPARE(padded.image.size(), QSize(3 + left + (at.right ? 4 : 0), 2 + top + (at.bottom ? 4 : 0)));
        for (int y = 0; y < padded.image.height(); ++y)
            for (int x = 0; x < padded.image.width(); ++x)
                QCOMPARE(std::memcmp(padded.image.constScanLine(y) + x * 4, source.constScanLine(std::clamp(y - top, 0, 1)) + std::clamp(x - left, 0, 2) * 4, 4), 0);
    }
}

void BlurEdgeTests::everySideCarriedIsAClampedBlur()
{
    const QImage source = noise(31, 19, 5);
    const LayerSides all{true, true, true, true};
    for (const double radius : {0.4, 2.0, 7.5})
        QCOMPARE(run(source, FilterSettings{.radius = radius}, FilterKind::gaussianBlur, all), PixelAdjust::gaussianBlur(source, radius, true));
}

void BlurEdgeTests::aMotionBlurCarriesEachSideAsFarAsItReaches()
{
    const QImage source = noise(29, 21, 9);
    const LayerSides all{true, true, true, true};
    for (const double angle : {0.0, 30.0, 90.0, -61.0}) {
        const FilterSettings settings{.angle = angle, .distance = 24};
        // Padded far past any reach, then cut back.
        const PixelFilter::Extended far = PixelFilter::extending(source, all, 60);
        const QImage expected = PixelAdjust::motionBlur(far.image, 24 * PixelFilter::motionRadiusPerPixel, angle * std::numbers::pi / 180).copy(far.inside);
        QCOMPARE(run(source, settings, FilterKind::motionBlur, all), expected);
    }
}

void BlurEdgeTests::oneSideCarriedLeavesTheOthersToFade()
{
    // Row 10 lies past the top and bottom's reach.
    QImage block = BrushRaster::context(20, 20, false);
    block.fill(Qt::white);
    const QImage left = run(block, FilterSettings{.radius = 2}, FilterKind::gaussianBlur, LayerSides{.left = true});
    QCOMPARE(int(left.constScanLine(10)[3]), 255);
    QVERIFY(left.constScanLine(10)[19 * 4 + 3] < 200 && left.constScanLine(0)[10 * 4 + 3] < 200);
    // Without sides, as Core Image blurs a finite image.
    QCOMPARE(run(block, FilterSettings{.radius = 2}, FilterKind::gaussianBlur, LayerSides{}), PixelAdjust::gaussianBlur(block, 2, false));
}

void BlurEdgeTests::theSidesFollowTheCanvas()
{
    QCOMPARE(sides(layer(40, 20, QPointF(0, 0)), QSizeF(80, 20)), (LayerSides{true, false, true, true}));
    QCOMPARE(sides(layer(40, 20, QPointF(0, 0)), QSizeF(80, 20), FilterKind::motionBlur), (LayerSides{true, false, true, true}));
    QCOMPARE(sides(layer(40, 20, QPointF(0, 0)), QSizeF(80, 20), FilterKind::bloomGlow), LayerSides{});
    ImageLayer flipped = layer(40, 20, QPointF(0, 0));
    flipped.transform.flipX = true;
    QCOMPARE(sides(flipped, QSizeF(80, 20)), (LayerSides{false, true, true, true}));
    ImageLayer turned = layer(40, 20, QPointF(0, 0));
    turned.transform.rotation = 90;
    QCOMPARE(sides(turned, QSizeF(80, 20)), LayerSides{});
    // Past the canvas everywhere: every side is at its edge.
    QCOMPARE(sides(layer(40, 20, QPointF(-5, -5)), QSizeF(20, 10)), (LayerSides{true, true, true, true}));
    // Within half a pixel of the edge counts.
    QCOMPARE(sides(layer(40, 20, QPointF(0.4, 2)), QSizeF(80, 30)).left, true);
    QCOMPARE(sides(layer(40, 20, QPointF(0.6, 2)), QSizeF(80, 30)).left, false);
    QCOMPARE(sides(layer(40, 20, QPointF(10, 0.4)), QSizeF(80, 30)).top, true);
    QCOMPARE(sides(layer(40, 20, QPointF(10, 0.6)), QSizeF(80, 30)).top, false);
    QCOMPARE(sides(layer(40, 20, QPointF(10, 2)), QSizeF(50.4, 22.4)), (LayerSides{false, true, false, true}));
    QCOMPARE(sides(layer(40, 20, QPointF(10, 2)), QSizeF(50.6, 22.6)), LayerSides{});
    QCOMPARE(FilterEdit(FilterKind::gaussianBlur, layer(40, 20, QPointF(0, 0)), std::nullopt, FilterSettings{}, std::nullopt).canvasSides, LayerSides{});
}

void BlurEdgeTests::growthPadsOnlyTheInnerSides()
{
    FilterEdit edit(FilterKind::gaussianBlur, layer(40, 20, QPointF(0, 0)), std::nullopt, FilterSettings{.radius = 5}, std::nullopt, QSizeF(80, 20));
    QCOMPARE(edit.grownImage.value().size(), QSize(57, 20));
    QCOMPARE(edit.grownMargin, 17.0);
    QCOMPARE(edit.grownTransform.value().origin, QPointF(0, 0));
    const qint64 grid = edit.grownImage.value().cacheKey();
    edit.growForBlur();
    QCOMPARE(edit.grownImage.value().cacheKey(), grid);
    edit.settings.radius = 10;
    edit.growForBlur();
    QCOMPARE(edit.grownImage.value().size(), QSize(72, 20));
    QCOMPARE(edit.grownMargin, 32.0);
    // The room rounds up; a margin within it grows nothing.
    FilterEdit half(FilterKind::gaussianBlur, layer(40, 20, QPointF(0, 0)), std::nullopt, FilterSettings{.radius = 4.5}, std::nullopt, QSizeF(80, 20));
    QCOMPARE(half.grownMargin, 16.0);
    const qint64 halfGrid = half.grownImage.value().cacheKey();
    half.settings.radius = 4.6;
    half.growForBlur();
    QCOMPARE(half.grownImage.value().cacheKey(), halfGrid);
    // Every side at the canvas's edge: no grid, no regrowth.
    FilterEdit filling(FilterKind::gaussianBlur, layer(40, 20, QPointF(0, 0)), std::nullopt, FilterSettings{.radius = 5}, std::nullopt, QSizeF(40, 20));
    QVERIFY(!filling.grownImage);
    QCOMPARE(filling.grownMargin, 17.0);
}

void BlurEdgeTests::aPreservedCommitKeepsTheLayerAsItWas()
{
    EditorSession session;
    session.createDocument(80, 60);
    QImage image = BrushRaster::context(40, 20, false);
    QPainter(&image).fillRect(0, 0, 20, 20, Qt::red);
    session.insert(ImportedImage(image, image, QStringLiteral("Half")));
    // A mask on the layer's grid, which growth would carry.
    session.applySelection(rectPath(QRectF(20, 20, 30, 20)), SelectionMode::replace, "Select");
    session.addMask(true);
    session.deselect();
    const ImageLayer before = session.activeLayer().value();
    QCOMPARE(before.mask.value().asset.size(), QSize(40, 20));
    session.selectLayerTarget(before.id, false);
    // Opened plain, the grid grew; preserved, the commit ignores it.
    session.beginFilter(FilterKind::gaussianBlur);
    QVERIFY(session.filterEdit().value().grownImage);
    session.updateFilter(FilterSettings{.radius = 5, .preserveTransparency = true}, false);
    bool done = false;
    session.commitFilter([&done] { done = true; });
    QTRY_VERIFY(done);
    const ImageLayer after = session.activeLayer().value();
    QCOMPARE(after.transform, before.transform);
    QCOMPARE(after.asset.value().size(), QSize(40, 20));
    QVERIFY(after.mask.value().asset.identity() == before.mask.value().asset.identity());
    QCOMPARE(int(after.asset.value().image().constScanLine(10)[19 * 4 + 3]), 255);
    QCOMPARE(int(after.asset.value().image().constScanLine(10)[20 * 4 + 3]), 0);
}

void BlurEdgeTests::aPreservedCommitReadsTheSelectionOnTheLayer()
{
    // Swift's selection case, on a grid the panel grew.
    EditorSession session;
    session.createDocument(80, 60);
    QImage image = BrushRaster::context(40, 20, false);
    QPainter painter(&image);
    painter.fillRect(0, 0, 10, 20, Qt::red);
    painter.fillRect(10, 0, 10, 20, Qt::green);
    painter.end();
    session.insert(ImportedImage(image, image, QStringLiteral("Halves")));
    session.applySelection(rectPath(QRectF(30, 20, 10, 20)), SelectionMode::replace, "Green");
    session.beginFilter(FilterKind::gaussianBlur);
    QVERIFY(session.filterEdit().value().grownImage);
    session.updateFilter(FilterSettings{.radius = 3, .preserveTransparency = true}, false);
    bool done = false;
    session.commitFilter([&done] { done = true; });
    QTRY_VERIFY(done);
    const QImage result = session.activeLayer().value().asset.value().image();
    const uchar *row = result.constScanLine(10);
    QVERIFY(row[9 * 4] >= 250 && row[9 * 4 + 1] <= 5);
    QVERIFY(row[10 * 4] > 30);
}

void BlurEdgeTests::thePreviewCarriesTheCanvasSides()
{
    EditorSession session;
    session.createDocument(40, 20);
    QImage image = BrushRaster::context(40, 20, false);
    image.fill(Qt::red);
    session.insert(ImportedImage(image, image, QStringLiteral("Red")));
    const ImageLayer filling = session.activeLayer().value();
    for (const FilterKind kind : {FilterKind::gaussianBlur, FilterKind::motionBlur}) {
        session.beginFilter(kind);
        session.updateFilter(FilterSettings{.radius = 4, .distance = 30}, true);
        QVERIFY(QTest::qWaitFor([&] { return !session.filterEdit().value().preparing; }, 20000));
        const QImage preview = session.filterEdit().value().previewImage(filling.id).value();
        QCOMPARE(preview.size(), QSize(40, 20));
        QCOMPARE(session.displayedTransform(filling), filling.transform);
        for (int y = 0; y < 20; ++y)
            for (int x = 0; x < 40; ++x)
                QVERIFY2(preview.constScanLine(y)[x * 4 + 3] == 255, qPrintable(rawValue(kind)));
        session.cancelFilter();
    }
}

void BlurEdgeTests::theSheetTogglesPreserveTransparency()
{
    EditorSession session;
    session.createDocument(8, 8);
    QImage image = BrushRaster::context(8, 8, false);
    image.fill(Qt::red);
    session.insert(ImportedImage(image, image, QStringLiteral("Red")));
    session.beginFilter(FilterKind::gaussianBlur);
    FilterSheet sheet(session);
    QCheckBox *box = nullptr;
    for (QCheckBox *each : sheet.findChildren<QCheckBox *>())
        if (each->text() == "Preserve Transparency")
            box = each;
    QVERIFY(box && !box->isChecked());
    QCOMPARE(box->toolTip(), QString("Blur only the colors, keeping the layer's outline and transparency as they are"));
    box->click();
    QVERIFY(session.filterEdit().value().settings.preserveTransparency);
    session.updateFilter(FilterSettings{.radius = 5}, true);
    QVERIFY(!box->isChecked());
    session.cancelFilter();
}

QTEST_MAIN(BlurEdgeTests)
#include "BlurEdgeTests.moc"
