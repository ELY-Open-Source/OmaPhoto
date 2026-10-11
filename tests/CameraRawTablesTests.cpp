#include "CameraRawFixtures.h"
#include "Document/CameraRawTables.h"
#include <QtTest>

// The measured tables as Swift reads them, checked against Python.
using namespace CameraRawFixtures;

class CameraRawTablesTests : public QObject {
    Q_OBJECT
private slots:
    void theTablesDecodeAsWritten();
    void aStageBlendsTheTablesAroundItsValue();
    void eachSliderPicksItsOwnTables();
    void theAdaptiveSlidersBlendAcrossBrightness();
    void neutralizeFindsTheGridsGrayest();
    void identitySlidersComposeNothing();
};

namespace {
const uint8_t *entry(int table, int cell)
{
    constexpr size_t entries = size_t(CameraRawTables::size) * CameraRawTables::size * CameraRawTables::size * 3;
    return CameraRawTables::tables().data() + table * entries + size_t(cell) * 3;
}

int indexOf(const uint8_t *table)
{
    constexpr size_t entries = size_t(CameraRawTables::size) * CameraRawTables::size * CameraRawTables::size * 3;
    return table ? int((table - CameraRawTables::tables().data()) / qsizetype(entries)) : -1;
}
}

void CameraRawTablesTests::theTablesDecodeAsWritten()
{
    const std::vector<uint8_t> &tables = CameraRawTables::tables();
    QCOMPARE(tables.size(), size_t(285) * 17 * 17 * 17 * 3);
    quint64 hash = 0xcbf29ce484222325ULL;
    for (const uint8_t byte : tables)
        hash = (hash ^ byte) * 0x100000001b3ULL;
    QCOMPARE(hash, quint64(0x770d674136320424ULL));
    QCOMPARE(std::vector<int>(entry(40, 2456), entry(40, 2456) + 3), (std::vector<int>{128, 128, 128}));
    QCOMPARE(std::vector<int>(entry(81, 100), entry(81, 100) + 3), (std::vector<int>{0, 4, 32}));
    QCOMPARE(std::vector<int>(entry(146, 4912), entry(146, 4912) + 3), (std::vector<int>{255, 255, 255}));
    QCOMPARE(std::vector<int>(entry(200, 2456), entry(200, 2456) + 3), (std::vector<int>{172, 172, 172}));
    QCOMPARE(std::vector<int>(entry(284, 100), entry(284, 100) + 3), (std::vector<int>{70, 108, 255}));
}

void CameraRawTablesTests::aStageBlendsTheTablesAroundItsValue()
{
    const CameraRawStage balance = CameraRawTables::whiteBalance(30, -60);
    QCOMPARE((std::vector<int>{indexOf(balance.table[0]), indexOf(balance.table[1]), indexOf(balance.table[2]), indexOf(balance.table[3])}),
             (std::vector<int>{46, 47, 55, 56}));
    QCOMPARE((std::vector<float>{balance.weight[0], balance.weight[1], balance.weight[2], balance.weight[3]}),
             (std::vector<float>{0.32f, 0.48f, 0.08f, 0.12f}));
    // At the far corner the last pair takes it all.
    const CameraRawStage corner = CameraRawTables::whiteBalance(100, 100);
    QVERIFY(indexOf(corner.table[0]) == 70 && indexOf(corner.table[3]) == 80 && corner.weight[3] == 1 && corner.weight[0] == 0);
    // Past the range it holds at the end, never beyond.
    const CameraRawStage past = CameraRawTables::whiteBalance(150, 175);
    QVERIFY(indexOf(past.table[3]) == 80 && past.weight[3] == 1 && past.weight[0] == 0);
    const CameraRawStage low = CameraRawTables::whiteBalance(-100, -100);
    QVERIFY(indexOf(low.table[0]) == 0 && low.weight[0] == 1 && low.weight[1] == 0);
}

void CameraRawTablesTests::eachSliderPicksItsOwnTables()
{
    CameraRawSettings settings;
    settings.exposure = 1.25;
    settings.temperature = 30;
    settings.whites = 100;
    settings.blacks = 12.5;
    settings.saturation = -62.5;
    settings.vibrance = 80;
    const std::vector<CameraRawStage> stages = CameraRawTables::stages(settings, {});
    QCOMPARE(stages.size(), size_t(6));
    const auto pair = [](const CameraRawStage &stage) {
        return std::vector<double>{double(indexOf(stage.table[0])), double(indexOf(stage.table[1])), stage.weight[0], stage.weight[1]};
    };
    QCOMPARE(pair(stages[0]), (std::vector<double>{93, 94, 0.5, 0.5}));
    QCOMPARE(indexOf(stages[1].table[0]), 5 * 9 + 4);
    QCOMPARE(pair(stages[2]), (std::vector<double>{109, 110, 0, 1}));
    QCOMPARE(pair(stages[3]), (std::vector<double>{115, 116, 0.5, 0.5}));
    QCOMPARE(pair(stages[4]), (std::vector<double>{121, 122, 0.5, 0.5}));
    QVERIFY(indexOf(stages[5].table[0]) == 136 && std::abs(stages[5].weight[1] - 0.2f) < 1e-6);
    // Tint alone still makes the white balance stage.
    CameraRawSettings tint;
    tint.tint = -25;
    const std::vector<CameraRawStage> tinted = CameraRawTables::stages(tint, {});
    QVERIFY(tinted.size() == 1 && indexOf(tinted[0].table[0]) == 4 * 9 + 3);
}

// Seven brightnesses down, seven values across, each read alone.
void CameraRawTablesTests::theAdaptiveSlidersBlendAcrossBrightness()
{
    const auto four = [](const CameraRawStage &stage) {
        return std::vector<double>{double(indexOf(stage.table[0])), double(indexOf(stage.table[1])), double(indexOf(stage.table[2])),
                                   double(indexOf(stage.table[3])), stage.weight[0], stage.weight[1], stage.weight[2], stage.weight[3]};
    };
    CameraRawSettings settings;
    settings.contrast = 10;
    settings.highlights = -100;
    settings.shadows = 100;
    settings.saturation = 25;
    const std::vector<CameraRawStage> stages = CameraRawTables::stages(settings, {120, 250, 30});
    QCOMPARE(stages.size(), size_t(4));
    QCOMPARE(four(stages[0]), (std::vector<double>{155, 156, 162, 163, 0.23076923191547394, 0.1538461595773697, 0.3692307770252228, 0.2461538463830948}));
    QCOMPARE(four(stages[1]), (std::vector<double>{187, 188, 194, 195, 1, 0, 0, 0}));
    QCOMPARE(four(stages[2]), (std::vector<double>{276, 277, 283, 284, 0, 0, 0, 1}));
    QCOMPARE(indexOf(stages[3].table[0]), 125);
    // Contrast reads the brightest channel, Highlights the log-average.
    settings = CameraRawSettings();
    settings.contrast = -37.5;
    settings.highlights = 50;
    const std::vector<CameraRawStage> low = CameraRawTables::stages(settings, {53, 0, 224});
    QCOMPARE(four(low[0]), (std::vector<double>{139, 140, 146, 147, 0.5, 0.5, 0, 0}));
    QCOMPARE(four(low[1]), (std::vector<double>{226, 227, 233, 234, 0, 0, 0, 1}));
    // On a measured point the lower pair takes it whole.
    settings = CameraRawSettings();
    settings.shadows = 25;
    const std::vector<CameraRawStage> on = CameraRawTables::stages(settings, {0, 104, 0});
    QCOMPARE(four(on[0]), (std::vector<double>{246, 247, 253, 254, 0, 0, 0, 1}));
    // Two colours: Python's own reading of each measure.
    QImage two = BrushRaster::context(2, 1, false);
    two.setPixelColor(0, 0, QColor(200, 40, 20));
    two.setPixelColor(1, 0, QColor(20, 100, 180));
    const CameraRawTables::Brightness colours = CameraRawTables::brightness(two.constBits(), 2, 1, two.bytesPerLine());
    QVERIFY(std::abs(colours.brightest - 190.342675743) < 1e-6 && std::abs(colours.luminance - 101.694057272) < 1e-6);
    QVERIFY(std::abs(colours.logBrightest - 189.755128630) < 1e-6);
    // A mid-gray picture reads 128 by every measure.
    const QImage mid = gray(8, 8);
    const CameraRawTables::Brightness read = CameraRawTables::brightness(mid.constBits(), 8, 8, mid.bytesPerLine());
    QVERIFY(std::abs(read.brightest - 128) < 0.01 && std::abs(read.luminance - 128) < 0.01 && std::abs(read.logBrightest - 128) < 0.01);
}

void CameraRawTablesTests::neutralizeFindsTheGridsGrayest()
{
    // Python's own search over the same tables found these.
    const CameraRawSettings::Balance warm = CameraRawTables::neutralize(160 / 255.0, 140 / 255.0, 120 / 255.0).value();
    QVERIFY(warm.temperature == -19 && warm.tint == -7);
    const CameraRawSettings::Balance blue = CameraRawTables::neutralize(0.2, 0.45, 0.8).value();
    QVERIFY(blue.temperature == 100 && blue.tint == 25.5);
    const CameraRawSettings::Balance green = CameraRawTables::neutralize(0.3, 0.5, 0.25).value();
    QVERIFY(green.temperature == 5 && green.tint == 90.5);
    // White stays white anywhere: the first of the ties wins.
    const CameraRawSettings::Balance white = CameraRawTables::neutralize(1, 1, 1).value();
    QVERIFY(white.temperature == 0 && white.tint == 0);
    // A refinement past −100 keeps to the range.
    const CameraRawSettings::Balance edge = CameraRawTables::neutralize(84 / 255.0, 1 / 255.0, 97 / 255.0).value();
    QVERIFY(edge.temperature == -14.5 && edge.tint == -100);
    const CameraRawSettings::Balance hot = CameraRawTables::neutralize(240 / 255.0, 80 / 255.0, 40 / 255.0).value();
    QVERIFY(hot.temperature == -100 && hot.tint == -13.5);
    // Ties among searched points: tint rising within each temperature.
    const CameraRawSettings::Balance tie = CameraRawTables::neutralize(144 / 255.0, 216 / 255.0, 47 / 255.0).value();
    QVERIFY(tie.temperature == -100 && tie.tint == 60);
    // Any channel missing leaves nothing to balance from.
    QVERIFY(!CameraRawTables::neutralize(0.5, 0.0001, 0.5));
    QVERIFY(!CameraRawTables::neutralize(0, 128 / 255.0, 128 / 255.0));
    QVERIFY(!CameraRawTables::neutralize(128 / 255.0, 128 / 255.0, 0));
    QVERIFY(CameraRawTables::neutralize(0.5, 0.00011, 0.5));
}

void CameraRawTablesTests::identitySlidersComposeNothing()
{
    const std::vector<CameraRawStage> none = CameraRawTables::stages(CameraRawSettings(), {});
    QVERIFY(none.empty() && CameraRawTables::compose(none).empty());
    CameraRawSettings exposure;
    exposure.exposure = 0.5;
    const std::vector<float> composed = CameraRawTables::compose(CameraRawTables::stages(exposure, {}));
    QCOMPARE(composed.size(), size_t(33) * 33 * 33 * 3);
    // The corners are its table's own black and white.
    QVERIFY(composed[0] == entry(92, 0)[0] / 255.0f && composed.back() == entry(92, 4912)[2] / 255.0f);
    // Three stages in their order, against Python's own run.
    CameraRawSettings three;
    three.exposure = 1;
    three.temperature = 40;
    three.contrast = 50;
    const std::vector<float> run = CameraRawTables::compose(CameraRawTables::stages(three, {}));
    const auto at = [&](int r, int g, int b) {
        const size_t index = ((size_t(r) * 33 + g) * 33 + b) * 3;
        return std::array<float, 3>{run[index], run[index + 1], run[index + 2]};
    };
    const auto near = [](std::array<float, 3> got, std::array<float, 3> expected) {
        return std::abs(got[0] - expected[0]) < 1e-6f && std::abs(got[1] - expected[1]) < 1e-6f && std::abs(got[2] - expected[2]) < 1e-6f;
    };
    QVERIFY(near(at(16, 16, 16), {0.94272572f, 0.86208665f, 0.74603087f}));
    QVERIFY(near(at(16, 4, 28), {0.86772656f, 0.55830115f, 0.98769760f}));
}

QTEST_GUILESS_MAIN(CameraRawTablesTests)
#include "CameraRawTablesTests.moc"
