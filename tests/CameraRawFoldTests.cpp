#include "CameraRawFixtures.h"
#include "Document/CameraRawTables.h"
#include "Document/EditorSession.h"
#include "Document/Filters.h"
#include <QtTest>

// Swift's a8630e4: one table, the brightness once, a sparser scope.
using namespace CameraRawFixtures;

class CameraRawFoldTests : public QObject {
    Q_OBJECT
private slots:
    void curveMixerAndGradingFoldIntoTheTable();
    void aPointColourOrRefineSaturationKeepsTheCurvePass();
    void aKnownBrightnessIsReadBeforeCalibration();
    void aSessionMeasuresItsPreviewSourceOnce();
    void everyBandIsMeasuredAndCounted();
    void calibrationsBlueHueTurnsAndItsSaturationSaturates();
};

namespace {
QImage noise(int side = 32)
{
    QImage picture = BrushRaster::context(side, side, false);
    quint32 state = 11;
    for (int y = 0; y < side; ++y)
        for (int x = 0; x < side; ++x) {
            std::array<int, 3> channel{};
            for (int &value : channel) {
                state = state * 1664525 + 1013904223;
                value = int(state >> 24);
            }
            picture.setPixelColor(x, y, QColor(channel[0], channel[1], channel[2]));
        }
    return picture;
}

QImage kernel(const QImage &picture, const std::vector<float> &table)
{
    QImage copy = picture.copy();
    adjust_camera_raw(copy.bits(), size_t(copy.width()), size_t(copy.height()), size_t(copy.bytesPerLine()), table.data(), CameraRawTables::grid, 0);
    return copy;
}

CameraRawSettings coloured()
{
    CameraRawSettings settings;
    settings.exposure = 0.5;
    settings.curve.rgb = CameraRawCurveSettings::mediumContrast();
    settings.curve.red = {{0, 0}, {0.4, 0.5}, {1, 1}};
    settings.mixer.saturation[1] = -40;
    settings.grading.highlights = {30, 40, 10};
    return settings;
}

int farthest(const QImage &left, const QImage &right)
{
    int most = 0;
    const auto a = pixels(left), b = pixels(right);
    for (size_t i = 0; i < a.size(); ++i)
        for (int c = 0; c < 4; ++c)
            most = std::max(most, std::abs(a[i][size_t(c)] - b[i][size_t(c)]));
    return most;
}
}

void CameraRawFoldTests::curveMixerAndGradingFoldIntoTheTable()
{
    const QImage picture = noise();
    const CameraRawSettings settings = coloured().normalized();
    std::vector<float> table = CameraRawTables::compose(CameraRawTables::stages(settings, {}));
    // The curve, then the Mixer and Grading, over Light's table.
    const CameraRawCurveSettings &curve = settings.curve;
    const std::vector<float> tone = curve.toneTable(), red = curve.channelTable(curve.red), green = curve.channelTable(curve.green),
                             blue = curve.channelTable(curve.blue);
    camera_raw_table_curves(table.data(), CameraRawTables::grid, tone.data(), red.data(), green.data(), blue.data());
    std::vector<CameraRawStage> stages = CameraRawTables::mixerStages(settings.mixer);
    const std::vector<CameraRawStage> graded = CameraRawTables::gradingStages(settings.grading);
    stages.insert(stages.end(), graded.begin(), graded.end());
    camera_raw_compose_onto(table.data(), CameraRawTables::grid, stages.data(), int(stages.size()), CameraRawTables::size);
    QVERIFY(pixels(settings.apply(picture)) == pixels(kernel(picture, table)));
    // Without Light's table the fold starts from no change.
    CameraRawSettings alone;
    alone.curve.red = settings.curve.red;
    const std::vector<float> identity = CameraRawTables::identity();
    QCOMPARE(identity.size(), size_t(33) * 33 * 33 * 3);
    QVERIFY(identity[3 * (16 * 33 * 33 + 4 * 33 + 28)] == 0.5f && identity.back() == 1.0f);
    std::vector<float> curved = identity;
    const std::vector<float> straight = alone.curve.toneTable(), redOnly = alone.curve.channelTable(alone.curve.red);
    camera_raw_table_curves(curved.data(), CameraRawTables::grid, straight.data(), redOnly.data(), straight.data(), straight.data());
    QVERIFY(alone.foldingColor({}) == curved);
    QVERIFY(pixels(alone.apply(picture)) == pixels(kernel(picture, curved)));
}

void CameraRawFoldTests::aPointColourOrRefineSaturationKeepsTheCurvePass()
{
    const QImage picture = noise();
    // A point colour that weighs nothing changes no pixel.
    CameraRawSettings pointed = coloured();
    pointed.mixer.points = {CameraRawPointColor{.hue = 200, .saturation = 0.01, .luminance = 0.99}};
    const QImage folded = coloured().apply(picture), passed = pointed.apply(picture);
    QVERIFY(farthest(folded, passed) <= 3 && pixels(folded) != pixels(passed));
    // Refine Saturation is the curve pass's alone.
    CameraRawSettings refined = coloured();
    refined.curve.refineSaturation = 30;
    const CameraRawSettings light = refined.normalized();
    QImage direct = kernel(picture, CameraRawTables::compose(CameraRawTables::stages(light, {})));
    light.applyCurveColor(direct.bits(), direct.width(), direct.height(), direct.bytesPerLine(), -1);
    QVERIFY(pixels(refined.apply(picture)) == pixels(direct));
    // A point-colour view keeps the pass, with no points too.
    const CameraRawSettings plain = coloured().normalized();
    QImage shown = kernel(picture, CameraRawTables::compose(CameraRawTables::stages(plain, {})));
    plain.applyCurveColor(shown.bits(), shown.width(), shown.height(), shown.bytesPerLine(), 0);
    QVERIFY(pixels(coloured().apply(picture, std::nullopt, 1, 0, 0)) == pixels(shown));
}

void CameraRawFoldTests::aKnownBrightnessIsReadBeforeCalibration()
{
    // Darker noise, where Shadows Tint turns many hues.
    QImage picture = noise();
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x) {
            const QColor colour = picture.pixelColor(x, y);
            picture.setPixelColor(x, y, QColor(colour.red() / 2, colour.green() / 2, colour.blue() / 2));
        }
    CameraRawSettings settings;
    settings.shadows = 100;
    settings.calibration.shadowTint = 100;
    // Measured on the pixels before Shadows Tint changes them.
    const CameraRawTables::Brightness before = CameraRawTables::brightness(picture);
    QImage direct = picture.copy();
    settings.normalized().applyCalibration(direct.bits(), direct.width(), direct.height(), direct.bytesPerLine());
    const CameraRawTables::Brightness after = CameraRawTables::brightness(direct);
    const QImage expected = kernel(direct, CameraRawTables::compose(CameraRawTables::stages(settings.normalized(), before)));
    const QImage late = kernel(direct, CameraRawTables::compose(CameraRawTables::stages(settings.normalized(), after)));
    QVERIFY(pixels(settings.apply(picture)) == pixels(expected));
    QVERIFY(pixels(late) != pixels(expected));
    // A brightness handed in is used as it is.
    const CameraRawTables::Brightness dim{60, 60, 60};
    const QImage handed = kernel(direct, CameraRawTables::compose(CameraRawTables::stages(settings.normalized(), dim)));
    QVERIFY(pixels(settings.apply(picture, std::nullopt, 1, 0, -1, false, dim)) == pixels(handed) && pixels(handed) != pixels(expected));
}

void CameraRawFoldTests::aSessionMeasuresItsPreviewSourceOnce()
{
    const QImage picture = noise();
    EditorSession session;
    session.createDocument(32, 32);
    session.insert(ImportedImage(picture, picture, QStringLiteral("Picture")));
    session.beginFilter(FilterKind::cameraRaw);
    const FilterEdit &edit = session.filterEdit().value();
    const CameraRawTables::Brightness measured = CameraRawTables::brightness(edit.previewSource);
    const CameraRawTables::Brightness carried = edit.previewJob().cameraRawBrightness.value();
    QVERIFY(carried.brightest == measured.brightest && carried.luminance == measured.luminance && carried.logBrightest == measured.logBrightest);
    QCOMPARE(edit.previewBrightness().brightest, measured.brightest);
    session.cancelFilter();
    session.beginFilter(FilterKind::gaussianBlur);
    QVERIFY(!session.filterEdit().value().previewJob().cameraRawBrightness);
}

// Forty rows of gray: every band's rows reach the sums.
void CameraRawFoldTests::everyBandIsMeasuredAndCounted()
{
    QImage ramp = BrushRaster::context(1, 40, false);
    for (int y = 0; y < 40; ++y)
        ramp.setPixelColor(0, y, QColor(y * 6, y * 6, y * 6));
    // Python's reading of all forty rows.
    const CameraRawTables::Brightness read = CameraRawTables::brightness(ramp);
    QVERIFY(std::abs(read.brightest - 139.556468482) < 1e-6 && std::abs(read.logBrightest - 91.779144447) < 1e-6);
    // Sixteen bands of three rows, every other row within each.
    const CameraRawScope scope = CameraRawScope::make(ramp).value();
    std::array<double, 256> expected{};
    for (int band = 0; band < 16; ++band)
        for (int y = band * 3; y < std::min(40, band * 3 + 3); y += 2)
            expected[size_t(y * 6)] += 1;
    for (size_t level = 0; level < 256; ++level)
        QVERIFY2(scope.red[level] == expected[level] && scope.blue[level] == expected[level], qPrintable(QString::number(level)));
}

// The blue primary: hue turns the hue, saturation saturates.
void CameraRawFoldTests::calibrationsBlueHueTurnsAndItsSaturationSaturates()
{
    const auto hsl = [](const QImage &picture) {
        const QColor colour = picture.pixelColor(0, 0).toHsl();
        return std::array<int, 3>{colour.hslHue(), colour.hslSaturation(), colour.lightness()};
    };
    QImage blue = BrushRaster::context(1, 1, false);
    blue.setPixelColor(0, 0, QColor(60, 80, 200));
    const std::array<int, 3> before = hsl(blue);
    QImage turned = blue.copy(), saturated = blue.copy();
    adjust_camera_raw_calibration(turned.bits(), 1, 1, size_t(turned.bytesPerLine()), 0, 0, 0, 0, 0, 80, 0, 6);
    adjust_camera_raw_calibration(saturated.bits(), 1, 1, size_t(saturated.bytesPerLine()), 0, 0, 0, 0, 0, 0, -80, 6);
    QVERIFY(std::abs(hsl(turned)[0] - before[0]) > 5 && std::abs(hsl(turned)[1] - before[1]) <= 3);
    QVERIFY(std::abs(hsl(saturated)[0] - before[0]) <= 2 && hsl(saturated)[1] < before[1] - 40);
}

QTEST_GUILESS_MAIN(CameraRawFoldTests)
#include "CameraRawFoldTests.moc"
