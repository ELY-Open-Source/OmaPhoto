#include "AddressSpaceLimit.h"
#include "BrushFixtures.h"
#include "Document/ContentFill.h"
#include "Document/EditorSession.h"
#include "Document/PixelAdjust.h"
#include "IO/ImageExporter.h"
#include "IO/ProjectStore.h"
#include "Rendering/RasterSnapshot.h"
#include <QSignalSpy>
#include <QPainterPath>
#include <algorithm>
#include <array>
#include <cmath>
#include <malloc.h>
#include <stdexcept>
extern "C" {
#include "HealPixels.h"
#include "PatchPixels.h"
}

// Swift's SpotHealingTests: a blemish healed from its surroundings.
namespace {
// Gray stripes two pixels wide, a red blemish mid-image.
QImage blemished(int width = 120, int left = 55)
{
    QImage image(width, 80, QImage::Format_RGBA8888_Premultiplied);
    for (int y = 0; y < 80; ++y) {
        uchar *row = image.scanLine(y);
        for (int x = 0; x < width; ++x) {
            const bool red = x >= left && x < left + 10 && y >= 35 && y < 45;
            const uchar gray = x % 4 < 2 ? 100 : 112;
            row[x * 4] = red ? 230 : gray;
            row[x * 4 + 1] = red ? 20 : gray;
            row[x * 4 + 2] = red ? 20 : gray;
            row[x * 4 + 3] = 255;
        }
    }
    return image;
}

// Gray within the stripes' range, the red gone.
bool healedGray(const std::vector<int> &pixel)
{
    return pixel[0] - pixel[1] < 30 && pixel[1] >= 80 && pixel[1] <= 130 && pixel[3] == 255;
}

// One healing dab on the surface, healed: its tile.
QImage healedTile(const QImage &surface, SpotHealingMode mode, double opacity = 1)
{
    BrushSettings settings = brush(24, 1, 0, 0, 0, opacity);
    settings.healing = true;
    settings.healingMode = mode;
    BrushStroke paint(ImageLayer(ImportedImage(surface, surface, QStringLiteral("Surface")), QPointF(0, 0)), false, settings, QSizeF(120, 80));
    paint.append(QPointF(60, 40));
    paint.heal();
    return paint.patches().at(0).image;
}

// Swift's ridge: sky over a curved hill, a disc optional.
QImage ridge(bool disc)
{
    QImage image(320, 200, QImage::Format_RGBA8888_Premultiplied);
    quint32 seed = 7;
    const double top = 110 + 28 * std::sin(160.0 / 37);
    for (int y = 0; y < 200; ++y)
        for (int x = 0; x < 320; ++x) {
            seed = seed * 1664525u + 1013904223u;
            const double grain = double(int(seed >> 28) - 8) * 2;
            const bool sky = y < 110 + 28 * std::sin(x / 37.0);
            const double dx = x - 160, dy = y - top;
            const bool red = disc && dx * dx + dy * dy < 22 * 22;
            const std::array<double, 3> color = red ? std::array<double, 3>{220, 30, 30}
                : sky                             ? std::array<double, 3>{120 + y * 0.5, 160 + y * 0.3, 230}
                                                  : std::array<double, 3>{60 + grain, 110 + grain, 50 + grain};
            uchar *pixel = image.scanLine(y) + x * 4;
            for (int c = 0; c < 3; ++c)
                pixel[c] = uchar(std::clamp(color[size_t(c)], 0.0, 255.0));
            pixel[3] = 255;
        }
    return image;
}

const QPointF ridgeCentre(160, 110 + 28 * std::sin(160.0 / 37));

// Mean level off from the hidden hill, inside the disc.
double ridgeError(const QImage &healed)
{
    const QImage truth = ridge(false);
    double sum = 0, count = 0;
    for (int y = 0; y < 200; ++y)
        for (int x = 0; x < 320; ++x) {
            const double dx = x - ridgeCentre.x(), dy = y - ridgeCentre.y();
            if (dx * dx + dy * dy >= 22 * 22)
                continue;
            for (int c = 0; c < 3; ++c, ++count)
                sum += std::abs(healed.constScanLine(y)[x * 4 + c] - truth.constScanLine(y)[x * 4 + c]);
        }
    return sum / count;
}

// The fill over the disc, at a seed.
QImage ridgeFilled(quint32 seed)
{
    QPainterPath disc;
    disc.addEllipse(ridgeCentre, 22, 22);
    const SelectionClip clip{QRectF(0, 0, 320, 200), DocumentSelection{disc}.coverage(320, 200)};
    return ContentFill::run(FilterJob{FilterKind::contentAwareFill, ridge(true), FilterSettings(), 1, clip, QTransform(), seed});
}

// One stroke of a type over the disc: its error.
double ridgeHealed(SpotHealingMode mode)
{
    EditorSession session;
    session.createDocument(320, 200);
    const QImage damaged = ridge(true);
    session.insert(ImportedImage(damaged, damaged, QStringLiteral("Ridge")));
    session.selectTool(NavigationTool::spotHealing);
    session.setSpotHealingMode(mode);
    session.setBrushSettings(BrushSettings{.diameter = 56, .hardness = 1});
    session.beginBrush(ridgeCentre);
    session.continueBrush(ridgeCentre + QPointF(0.5, 0));
    session.finishBrush();
    if (session.brushError())
        throw std::runtime_error(session.brushError()->toStdString());
    return ridgeError(ImageExporter::render(session.projectSnapshot().value()).image);
}

// The blemished surface in a session, Spot Healing chosen.
void surface(EditorSession &session)
{
    session.createDocument(120, 80);
    const QImage original = blemished();
    session.insert(ImportedImage(original, original, QStringLiteral("Surface")));
    session.selectTool(NavigationTool::spotHealing);
    session.setBrushSettings(BrushSettings{.diameter = 24, .hardness = 1});
}
}

class SpotHealingTests : public QObject {
    Q_OBJECT
private slots:
    // Large asks map memory, so the address limit counts them.
    void initTestCase() { mallopt(M_MMAP_THRESHOLD, 64 * 1024); }
    void healsTheBlemishUnderTheBrushAndNothingElse_data();
    void healsTheBlemishUnderTheBrushAndNothingElse();
    void aMaskIsNeverHealed();
    void tabCyclesTheTypesAndDigitsSetTheOpacity();
    void aHealingStrokeShowsTheWashThenTheSurface();
    void aMaskStrokeNeverHeals();
    void theSelectionBoundsTheHeal();
    void aPaintedSourceHealsFromItsTiles();
    void aHealThatCannotAllocateIsTooLarge();
    void aHealWithoutRoomForItsCoverageCannotRender();
    void theTypesDifferAndTextureGrainIsRandom();
    void opacityHealsPartWayAndPlainStrokesNever();
    void aSpotAcrossTilesHealsWhole();
    void contentAwareCarriesACurvedEdgeThroughTheSpot();
    void contentAwareFillCarriesTheCurveToo();
    void theFillFollowsItsSeed();
    void theKernelHealsSwiftsOwnBytes();
    void everyBandOfRowsIsFilled();
    void aFillWithoutRoomForItsLevelsCannotRender();
};

void SpotHealingTests::healsTheBlemishUnderTheBrushAndNothingElse_data()
{
    QTest::addColumn<int>("type");
    for (const SpotHealingMode mode : allSpotHealingModes)
        QTest::newRow(qPrintable(rawValue(mode))) << int(mode);
}

void SpotHealingTests::healsTheBlemishUnderTheBrushAndNothingElse()
{
    QFETCH(int, type);
    EditorSession session;
    surface(session);
    session.setSpotHealingMode(allSpotHealingModes[size_t(type)]);
    const int count = session.history.undoCount();
    session.beginBrush(QPointF(60, 40));
    session.continueBrush(QPointF(60.5, 40));
    session.finishBrush();
    QVERIFY(!session.brushError().has_value());
    QCOMPARE(session.history.undoCount(), count + 1);
    QCOMPARE(session.history.undoName(), QString("Spot Healing"));
    const QImage after = ImageExporter::render(session.projectSnapshot().value()).image;
    for (const auto &[x, y] : {std::pair{60, 40}, std::pair{56, 36}, std::pair{64, 44}}) {
        const std::vector<int> healed = pixel(after, x, y);
        QVERIFY2(healed[0] - healed[1] < 30 && healed[1] >= 80 && healed[1] <= 130 && healed[3] == 255,
                 qPrintable(QString("(%1, %2) is %3 %4 %5 %6").arg(x).arg(y).arg(healed[0]).arg(healed[1]).arg(healed[2]).arg(healed[3])));
    }
    // Away from the brush, nothing moves.
    const QImage original = blemished();
    for (const auto &[x, y] : {std::pair{10, 10}, std::pair{90, 40}, std::pair{30, 40}, std::pair{60, 10}, std::pair{60, 70}})
        QCOMPARE(pixel(after, x, y), pixel(original, x, y));
}

void SpotHealingTests::aMaskIsNeverHealed()
{
    EditorSession session;
    surface(session);
    session.addLayerMask(true);
    QVERIFY(session.isMaskSelected());
    session.beginBrush(QPointF(60, 40));
    QVERIFY(!session.brushStroke());
    // The Brush still paints the mask.
    session.selectTool(NavigationTool::brush);
    session.beginBrush(QPointF(60, 40));
    QVERIFY(session.brushStroke() && session.brushStroke()->isMask && !session.brushStroke()->settings.healing);
    session.cancelBrush();
}

void SpotHealingTests::tabCyclesTheTypesAndDigitsSetTheOpacity()
{
    EditorSession session;
    surface(session);
    QSignalSpy changes(&session, &EditorSession::changed);
    session.setSpotHealingMode(SpotHealingMode::proximityMatch);
    QCOMPARE(changes.count(), 1);
    // Tab walks Swift's order, round to the start.
    session.cycleToolMode();
    QCOMPARE(session.spotHealingMode(), SpotHealingMode::contentAware);
    session.cycleToolMode();
    QCOMPARE(session.spotHealingMode(), SpotHealingMode::createTexture);
    session.cycleToolMode();
    QCOMPARE(session.spotHealingMode(), SpotHealingMode::proximityMatch);
    QCOMPARE(session.brushMode(), BrushToolMode::paint);
    QVERIFY(session.usesOpacityKeys());
    session.typeOpacityDigit(3, 10);
    QCOMPARE(session.brushSettings().opacity, 0.3);
    // The stroke takes the type chosen, and never erases.
    session.setBrushMode(BrushToolMode::erase);
    session.beginBrush(QPointF(60, 40));
    QCOMPARE(session.brushStroke()->settings.healingMode, SpotHealingMode::proximityMatch);
    QVERIFY(session.brushStroke()->settings.healing && !session.brushStroke()->settings.erasing);
    session.cancelBrush();
}

void SpotHealingTests::aHealingStrokeShowsTheWashThenTheSurface()
{
    const QImage original = blemished();
    BrushSettings settings = brush(24, 1, 0, 0, 0);
    settings.healing = true;
    settings.erasing = true;
    BrushStroke paint(ImageLayer(ImportedImage(original, original, QStringLiteral("Surface")), QPointF(0, 0)), false, settings, QSizeF(120, 80));
    paint.append(QPointF(60, 40));
    // While painting, Swift's dark wash covers the pixels at 45%.
    QCOMPARE(pixel(preview(paint, QSizeF(120, 80)), 60, 40), (std::vector<int>{140, 25, 25, 255}));
    paint.heal();
    const QImage healed = preview(paint, QSizeF(120, 80));
    const std::vector<int> centre = pixel(healed, 60, 40);
    QVERIFY(centre[0] - centre[1] < 30 && centre[1] >= 80 && centre[1] <= 130);
    QCOMPARE(pixel(healed, 90, 40), pixel(original, 90, 40));
}

void SpotHealingTests::aMaskStrokeNeverHeals()
{
    const QImage original = blemished();
    ImageLayer layer(ImportedImage(original, original, QStringLiteral("Surface")), QPointF(0, 0));
    layer.mask = LayerMask(LayerMask::assetFrom(BrushRaster::context(120, 80, true)));
    BrushSettings settings = brush(24, 1, 1, 1, 1);
    settings.healing = true;
    BrushStroke paint(layer, true, settings, QSizeF(120, 80));
    paint.append(QPointF(60, 40));
    // The mask takes plain white, and heal() leaves it be.
    const QImage painted = paint.patches().at(0).image;
    QCOMPARE(int(painted.constScanLine(40)[60]), 255);
    paint.heal();
    QCOMPARE(paint.patches().at(0).image, painted);
}

void SpotHealingTests::theSelectionBoundsTheHeal()
{
    const QImage original = blemished();
    BrushSettings settings = brush(24, 1, 0, 0, 0);
    settings.healing = true;
    BrushStroke paint(ImageLayer(ImportedImage(original, original, QStringLiteral("Surface")), QPointF(0, 0)), false, settings, QSizeF(120, 80));
    DocumentSelection selection;
    selection.path.addRect(QRectF(0, 0, 60, 80));
    paint.selectionClip = selection.clip(QSizeF(120, 80));
    paint.append(QPointF(60, 40));
    paint.heal();
    // Left of the edge the blemish heals; right, it stays.
    const QImage result = preview(paint, QSizeF(120, 80));
    const std::vector<int> inside = pixel(result, 57, 40);
    QVERIFY(inside[0] - inside[1] < 30 && inside[1] >= 80 && inside[1] <= 130);
    QCOMPARE(pixel(result, 62, 40), pixel(original, 62, 40));
}

void SpotHealingTests::aPaintedSourceHealsFromItsTiles()
{
    // Blue beneath, the blemished stripes painted over it all.
    QImage base = BrushRaster::context(120, 80, false);
    base.fill(QColor(20, 20, 200));
    const QImage original = blemished();
    const auto raster = std::make_shared<const RasterSnapshot>(120, 80, base, QRectF(0, 0, 120, 80),
                                                               std::vector<BrushPatch>{{QRectF(0, 0, 120, 80), original}});
    BrushSettings settings = brush(24, 1, 0, 0, 0);
    settings.healing = true;
    BrushStroke paint(ImageLayer(ImportedImage(raster, PixelAdjust::thumbnail(original), QStringLiteral("Painted")), QPointF(0, 0)), false, settings,
                      QSizeF(120, 80));
    paint.append(QPointF(60, 40));
    paint.heal();
    QVERIFY(!raster->hasMaterializedPixels());
    const QImage result = preview(paint, QSizeF(120, 80));
    for (const QPoint healed : {QPoint(60, 40), QPoint(56, 36), QPoint(64, 44)})
        QVERIFY2(healedGray(pixel(result, healed.x(), healed.y())), qPrintable(QString("(%1, %2)").arg(healed.x()).arg(healed.y())));
    // Painted pixels away from the brush stay the stripes.
    for (const QPoint away : {QPoint(10, 10), QPoint(90, 40), QPoint(30, 40), QPoint(60, 10), QPoint(62, 70)})
        QCOMPARE(pixel(result, away.x(), away.y()), pixel(original, away.x(), away.y()));
}

void SpotHealingTests::aHealThatCannotAllocateIsTooLarge()
{
    if (!placesStarvation())
        QSKIP("the failure point is placed for Qt 6.4's allocations");
    // Room for the region's 7.2 MB, not the kernel's 6.4.
    QImage flat = BrushRaster::context(1200, 1200, false);
    flat.fill(QColor(90, 90, 90));
    BrushSettings settings = brush(600, 1, 0, 0, 0);
    settings.healing = true;
    BrushStroke paint(ImageLayer(ImportedImage(flat, flat, QStringLiteral("Flat")), QPointF(0, 0)), false, settings, QSizeF(1200, 1200));
    paint.append(QPointF(600, 600));
    malloc_trim(0);
    const AddressSpaceLimit limit(qint64(1200) * 1200 * 5 + 2 * 1024 * 1024);
    QVERIFY_THROWS_EXCEPTION(ProjectError, paint.heal());
}

void SpotHealingTests::aHealWithoutRoomForItsCoverageCannotRender()
{
    if (!placesStarvation())
        QSKIP("the failure point is placed for Qt 6.4's allocations");
    // Room for the region's pixels, 5.8 MB, not its coverage.
    QImage flat = BrushRaster::context(1200, 1200, false);
    flat.fill(QColor(90, 90, 90));
    BrushSettings settings = brush(600, 1, 0, 0, 0);
    settings.healing = true;
    BrushStroke paint(ImageLayer(ImportedImage(flat, flat, QStringLiteral("Flat")), QPointF(0, 0)), false, settings, QSizeF(1200, 1200));
    paint.append(QPointF(600, 600));
    malloc_trim(0);
    const AddressSpaceLimit limit(qint64(1200) * 1200 * 4 + 1200 * 1200 / 2);
    QVERIFY_THROWS_EXCEPTION(ExportError, paint.heal());
}

void SpotHealingTests::theTypesDifferAndTextureGrainIsRandom()
{
    // Content-Aware copies a patch; Create Texture adds random grain.
    const QImage surface = blemished();
    const QImage copied = healedTile(surface, SpotHealingMode::contentAware);
    QVERIFY(healedGray(pixel(copied, 60, 40)));
    QCOMPARE(healedTile(surface, SpotHealingMode::contentAware), copied);
    const QImage textured = healedTile(surface, SpotHealingMode::createTexture);
    QVERIFY(textured != copied);
    QVERIFY(healedTile(surface, SpotHealingMode::createTexture) != textured);
}

void SpotHealingTests::opacityHealsPartWayAndPlainStrokesNever()
{
    // At half opacity the red is half gone.
    const std::vector<int> half = pixel(healedTile(blemished(), SpotHealingMode::contentAware, 0.5), 60, 40);
    QVERIFY2(half[0] - half[1] > 60 && half[0] < 200, qPrintable(QString("%1 %2").arg(half[0]).arg(half[1])));
    // heal() leaves a stroke that is no healing one.
    const QImage original = blemished();
    BrushStroke paint(ImageLayer(ImportedImage(original, original, QStringLiteral("Surface")), QPointF(0, 0)), false, brush(24, 1, 0, 0, 1), QSizeF(120, 80));
    paint.append(QPointF(60, 40));
    const QImage painted = paint.patches().at(0).image;
    paint.heal();
    QCOMPARE(paint.patches().at(0).image, painted);
}

void SpotHealingTests::aSpotAcrossTilesHealsWhole()
{
    // Odd width, the blemish astride the first tile's edge.
    const QImage original = blemished(301, 251);
    BrushSettings settings = brush(24, 1, 0, 0, 0);
    settings.healing = true;
    BrushStroke paint(ImageLayer(ImportedImage(original, original, QStringLiteral("Surface")), QPointF(0, 0)), false, settings, QSizeF(301, 80));
    paint.append(QPointF(256, 40));
    paint.heal();
    const QImage result = preview(paint, QSizeF(301, 80));
    QVERIFY(healedGray(pixel(result, 253, 40)) && healedGray(pixel(result, 259, 40)));
    QCOMPARE(pixel(result, 5, 40), pixel(original, 5, 40));
}

void SpotHealingTests::contentAwareCarriesACurvedEdgeThroughTheSpot()
{
    const double contentAware = ridgeHealed(SpotHealingMode::contentAware), proximity = ridgeHealed(SpotHealingMode::proximityMatch);
    // Swift's 12; x86-64 reaches 12.8 over 300 seeds.
    QVERIFY2(contentAware < 14, qPrintable(QString("Content-Aware is off by %1 levels").arg(contentAware)));
    QVERIFY2(contentAware < proximity / 2, qPrintable(QString("%1 against %2").arg(contentAware).arg(proximity)));
}

void SpotHealingTests::contentAwareFillCarriesTheCurveToo()
{
    const double fill = ridgeError(ridgeFilled(11)), proximity = ridgeHealed(SpotHealingMode::proximityMatch);
    QVERIFY2(fill < proximity / 2, qPrintable(QString("%1 against %2").arg(fill).arg(proximity)));
}

void SpotHealingTests::theFillFollowsItsSeed()
{
    const QImage first = ridgeFilled(3);
    QCOMPARE(ridgeFilled(3), first);
    QVERIFY(ridgeFilled(4) != first);
}

void SpotHealingTests::theKernelHealsSwiftsOwnBytes()
{
    // Swift's PatchPixels.c, built with blocks and libdispatch, hashed so.
    std::vector<uint8_t> cover(320 * 200);
    for (int y = 0; y < 200; ++y)
        for (int x = 0; x < 320; ++x) {
            const double dx = x + 0.5 - 160, dy = y + 0.5 - ridgeCentre.y();
            cover[size_t(y) * 320 + size_t(x)] = dx * dx + dy * dy < 28 * 28 ? 255 : 0;
        }
    for (const auto &[seed, hash] : {std::pair{1u, 0xe9b4a0af0dd8061full}, std::pair{13u, 0x012e0dbcdccce91bull}}) {
        QImage image = ridge(true);
        QCOMPARE(image.bytesPerLine(), 1280);
        QCOMPARE(spot_heal(image.bits(), cover.data(), 320, 200, 1280, 1, 0, seed), 0);
        quint64 fnv = 1469598103934665603ull;
        for (qsizetype index = 0; index < image.sizeInBytes(); ++index)
            fnv = (fnv ^ image.constBits()[index]) * 1099511628211ull;
        QCOMPARE(fnv, hash);
    }
}

void SpotHealingTests::everyBandOfRowsIsFilled()
{
    // Holes in the first and last of sixteen bands.
    QImage image(320, 200, QImage::Format_RGBA8888_Premultiplied);
    std::vector<uint8_t> hole(320 * 200, 0);
    for (int y = 0; y < 200; ++y)
        for (int x = 0; x < 320; ++x) {
            const bool marked = x >= 140 && x < 180 && (y < 6 || y >= 194);
            hole[size_t(y) * 320 + size_t(x)] = marked ? 255 : 0;
            image.setPixelColor(x, y, marked ? QColor(255, 0, 255) : QColor(x * 255 / 319, y * 255 / 199, 90));
        }
    QCOMPARE(patch_fill(image.bits(), size_t(image.bytesPerLine()), hole.data(), 320, 320, 200, 7), 1);
    // Swift's file, built with blocks and libdispatch, hashed so.
    quint64 fnv = 1469598103934665603ull;
    for (qsizetype index = 0; index < image.sizeInBytes(); ++index)
        fnv = (fnv ^ image.constBits()[index]) * 1099511628211ull;
    QCOMPARE(fnv, 0x47b860d173d05f20ull);
}

void SpotHealingTests::aFillWithoutRoomForItsLevelsCannotRender()
{
    if (!placesStarvation())
        QSKIP("the failure point is placed for Qt 6.4's allocations");
    // Room for the copy and mask, not PatchMatch's levels.
    QImage flat = BrushRaster::context(1200, 1200, false);
    flat.fill(QColor(90, 90, 90));
    QPainterPath square;
    square.addRect(300, 300, 600, 600);
    const SelectionClip clip{QRectF(0, 0, 1200, 1200), DocumentSelection{square}.coverage(1200, 1200)};
    const FilterJob job{FilterKind::contentAwareFill, flat, FilterSettings(), 1, clip, QTransform(), 1};
    malloc_trim(0);
    const AddressSpaceLimit limit(qint64(1200) * 1200 * 5 + 2 * 1024 * 1024);
    QVERIFY_THROWS_EXCEPTION(ExportError, ContentFill::run(job));
}

QTEST_GUILESS_MAIN(SpotHealingTests)
#include "SpotHealingTests.moc"
