#include "Document/BrushStroke.h"
#include "Document/EditorSession.h"
#include "Document/PixelAdjust.h"
#include "Document/Scanlines.h"
#include <QPainter>
#include <QtTest>
#include <numeric>
extern "C" {
#include "DitherPixels.h"
}

// Swift 1.4.9's Filter › Scanlines, its own filter.
namespace {
QImage filled(int width, int height, QColor colour)
{
    QImage image = BrushRaster::context(width, height, false);
    image.fill(colour);
    return image;
}

std::vector<int> column(const QImage &image, int x, int channel = 0)
{
    std::vector<int> values;
    for (int y = 0; y < image.height(); ++y)
        values.push_back(image.constScanLine(y)[x * 4 + channel]);
    return values;
}

std::vector<int> row(const QImage &image, int y, int channel = 0)
{
    std::vector<int> values;
    for (int x = 0; x < image.width(); ++x)
        values.push_back(image.constScanLine(y)[x * 4 + channel]);
    return values;
}

// Swift's render: no glow unless asked.
QImage render(const QImage &image, const std::function<void(ScanlinesSettings &)> &adjust = {})
{
    ScanlinesSettings settings;
    if (adjust)
        adjust(settings);
    return settings.apply(image);
}

std::vector<int> gray(int level, double spacing, double glow = 0)
{
    return column(render(filled(16, 32, QColor(level, level, level)), [&](ScanlinesSettings &s) { s.lineSpacing = spacing, s.glow = glow; }), 5);
}

// The oracle's picture: premultiplied, some clear, some soft.
QImage pattern(int width, int height)
{
    QImage image = BrushRaster::context(width, height, false);
    for (int y = 0; y < height; ++y) {
        uchar *out = image.scanLine(y);
        for (int x = 0; x < width; ++x) {
            const unsigned alpha = (x * 13 + y * 7) % 4 == 0 ? 0 : 128 + (x * 29 + y * 17) % 128;
            for (int c = 0; c < 3; ++c)
                out[x * 4 + c] = uchar(((x * 37 + y * 11 + c * 53) % 256) * alpha / 255);
            out[x * 4 + 3] = uchar(alpha);
        }
    }
    return image;
}

quint64 fnv(const QImage &image)
{
    quint64 hash = 1469598103934665603ULL;
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width() * 4; ++x)
            hash = (hash ^ image.constScanLine(y)[x]) * 1099511628211ULL;
    return hash;
}

// Swift's params: unset fields zero, white lines unless set.
ScanlinesParams made(const std::function<void(ScanlinesParams &)> &set)
{
    ScanlinesParams params{};
    params.light[0] = params.light[1] = params.light[2] = 255;
    set(params);
    return params;
}

void rgb(uint8_t *to, uint8_t red, uint8_t green, uint8_t blue)
{
    to[0] = red, to[1] = green, to[2] = blue;
}

QImage kernel(int width, int height, const ScanlinesParams &params)
{
    QImage image = pattern(width, height);
    if (scanlines_apply(image.bits(), size_t(width), size_t(height), size_t(image.bytesPerLine()), &params) == 0)
        throw std::runtime_error("no memory");
    return image;
}

}

class ScanlinesTests : public QObject {
    Q_OBJECT
private slots:
    void scanlinesAreLinesOfLightThatBloomWithBrightness();
    void linesShowOnSolidWhiteAndBlack();
    void glowLightsBetweenTheLines();
    void dotsBeadTheLinesBelowTheirLevel();
    void wobbleMovesLinesSideways();
    void displaceLiftsLinesWhereThePictureIsBright();
    void aLiftedLineHidesTheLinesBehindIt();
    void thresholdDarkensWhatsBelowIt();
    void theKernelDrawsSwiftsOwnBytes();
    void theSettingsReachTheKernel();
    void normalizingClampsEverySetting();
    void clearPixelsStayClear();
    void theGlowIsTheBlurredLightEasedNearWhite();
};

void ScanlinesTests::scanlinesAreLinesOfLightThatBloomWithBrightness()
{
    const std::vector<int> white = gray(255, 8), dim = gray(89, 8), black = gray(0, 8);
    for (int band = 0; band < 4; ++band) {
        const std::vector<int> rows(white.begin() + band * 8, white.begin() + band * 8 + 8);
        QVERIFY(rows[3] == 255 && rows[4] == 255);
        QVERIFY(std::any_of(rows.begin(), rows.end(), [](int value) { return value < 40; }));
    }
    QVERIFY(std::accumulate(dim.begin(), dim.end(), 0) * 2 < std::accumulate(white.begin(), white.end(), 0));
    QVERIFY(std::all_of(black.begin(), black.end(), [](int value) { return value == 0; }));
}

void ScanlinesTests::linesShowOnSolidWhiteAndBlack()
{
    const std::vector<int> white = gray(255, 8, 100);
    QVERIFY(*std::max_element(white.begin(), white.end()) - *std::min_element(white.begin(), white.end()) > 40);
    const std::vector<int> black = column(render(filled(16, 32, Qt::black), [](ScanlinesSettings &s) { s.lineSpacing = 8, s.blackLevel = 40; }), 8);
    const std::vector<int> band(black.begin(), black.begin() + 8);
    QVERIFY(*std::max_element(band.begin(), band.end()) > 60 && *std::min_element(band.begin(), band.end()) < 10);
}

void ScanlinesTests::glowLightsBetweenTheLines()
{
    const std::vector<int> plain = gray(255, 8), glowing = gray(255, 8, 100);
    QVERIFY2(glowing[0] > plain[0] + 40, qPrintable(QString::number(glowing[0])));
}

void ScanlinesTests::dotsBeadTheLinesBelowTheirLevel()
{
    const auto middle = [](int level) {
        return row(render(filled(64, 16, QColor(level, level, level)), [](ScanlinesSettings &s) { s.lineSpacing = 8, s.dots = 70; }), 4);
    };
    QCOMPARE(middle(255), std::vector<int>(64, 255));
    const std::vector<int> grey = middle(102);
    QVERIFY(grey[3] > 100 && grey[4] > 100 && grey[0] < 40 && grey[8] < 40);
}

void ScanlinesTests::wobbleMovesLinesSideways()
{
    QImage edge = filled(64, 64, Qt::black);
    QPainter(&edge).fillRect(QRect(32, 0, 32, 64), Qt::white);
    const auto edges = [&edge](double wobble) {
        const QImage result = render(edge, [wobble](ScanlinesSettings &s) { s.lineSpacing = 8, s.wobble = wobble; });
        std::set<int> found;
        for (int line = 0; line < 8; ++line) {
            const std::vector<int> middle = row(result, line * 8 + 4);
            found.insert(int(std::find_if(middle.begin(), middle.end(), [](int value) { return value > 128; }) - middle.begin()));
        }
        return found;
    };
    QCOMPARE(edges(0), std::set<int>{32});
    QVERIFY(edges(12).size() >= 3);
}

void ScanlinesTests::displaceLiftsLinesWhereThePictureIsBright()
{
    QImage half = filled(64, 64, Qt::black);
    QPainter(&half).fillRect(QRect(32, 0, 32, 64), Qt::white);
    const QImage result = render(half, [](ScanlinesSettings &s) { s.lineSpacing = 16, s.displace = 6, s.colors = DitherColors::original, s.blackLevel = 50; });
    const auto brightest = [&result](int x) {
        const std::vector<int> values = column(result, x);
        return int(std::max_element(values.begin() + 16, values.begin() + 32) - values.begin());
    };
    QVERIFY(brightest(48) < 24);
    // Over black the line keeps its middle, row 24.
    const std::vector<int> still = column(result, 8);
    QVERIFY(still[24] > 0 && still[24] == *std::max_element(still.begin() + 16, still.begin() + 32) && still[16] < still[24]);
}

void ScanlinesTests::aLiftedLineHidesTheLinesBehindIt()
{
    QImage band = filled(64, 80, QColor(64, 64, 64));
    QPainter(&band).fillRect(QRect(16, 32, 32, 16), Qt::white);
    const QImage result = render(band, [](ScanlinesSettings &s) { s.lineSpacing = 16, s.displace = 40, s.smoothness = 0; });
    QVERIFY(result.constScanLine(14)[4 * 4] > 20);
    QVERIFY(result.constScanLine(14)[32 * 4] < 8);
    QVERIFY(result.constScanLine(2)[32 * 4] > 200);
}

void ScanlinesTests::thresholdDarkensWhatsBelowIt()
{
    const QImage grey = filled(32, 32, QColor(77, 77, 77));
    const std::vector<int> shown = column(render(grey, [](ScanlinesSettings &s) { s.lineSpacing = 8; }), 16);
    const std::vector<int> hidden = column(render(grey, [](ScanlinesSettings &s) { s.lineSpacing = 8, s.threshold = 60; }), 16);
    QVERIFY(std::any_of(shown.begin(), shown.end(), [](int value) { return value > 40; }));
    QVERIFY(std::none_of(hidden.begin(), hidden.end(), [](int value) { return value > 10; }));
}

// Swift's GCD kernel, built with clang's blocks, gave these.
void ScanlinesTests::theKernelDrawsSwiftsOwnBytes()
{
    const QImage a = kernel(37, 53, made([](ScanlinesParams &p) { p.spacing = 4; p.thickness = 0.7f; p.displace = 20; p.smoothness = 0.5f; p.originalColors = 1; }));
    QCOMPARE(fnv(a), 14473728681183016108ULL);
    QCOMPARE(QByteArray(reinterpret_cast<const char *>(a.constScanLine(2)) + 4, 4), QByteArray("\xbd\xbf\x25\xbf", 4));
    const QImage b = kernel(37, 53, made([](ScanlinesParams &p) { p.spacing = 6; p.thickness = 0.9f; p.dots = 0.6f; p.wobble = 6; p.displace = -15; p.threshold = 0.3f; p.split = 3; p.density = 0.2f; p.contrast = -0.3f; p.blackLevel = 0.25f; p.smoothness = 0.8f; }));
    QCOMPARE(fnv(b), 3381563246094821038ULL);
    const QImage c = kernel(37, 53, made([](ScanlinesParams &p) { p.spacing = 5; p.thickness = 0.4f; p.contrast = 0.6f; p.smoothness = 0.5f; rgb(p.dark, 10, 20, 200); rgb(p.light, 250, 200, 10); }));
    QCOMPARE(fnv(c), 11894396318800434257ULL);
    QCOMPARE(QByteArray(reinterpret_cast<const char *>(c.constScanLine(2)) + 4, 4), QByteArray("\x55\x4d\x8f\xbf", 4));
    // Wide and tall: 32 bands of columns, lines and rows.
    QCOMPARE(fnv(kernel(70, 300, made([](ScanlinesParams &p) { p.spacing = 3; p.thickness = 0.7f; p.dots = 0.3f; p.displace = 30; p.split = 2; p.blackLevel = 0.1f; p.smoothness = 1; p.originalColors = 1; }))),
             5889846669877741430ULL);
    QCOMPARE(fnv(kernel(70, 300, made([](ScanlinesParams &p) { p.spacing = 2; p.thickness = 1; p.wobble = 9; p.displace = -40; p.threshold = 0.5f; p.smoothness = 0.25f; }))),
             1843572660750656508ULL);
    // The glow eases near full brightness.
    QImage lit = pattern(41, 29), glow = pattern(41, 29);
    for (int y = 0; y < 29; ++y)
        for (int x = 0; x < 41 * 4; ++x)
            glow.scanLine(y)[x] = uchar(255 - glow.constScanLine(y)[x]);
    dither_glow(lit.bits(), glow.constBits(), 41, 29, size_t(lit.bytesPerLine()), 1.3f);
    QCOMPARE(fnv(lit), 14026896863046063157ULL);
}

// Each setting reaches its own field, scaled as Swift's.
void ScanlinesTests::theSettingsReachTheKernel()
{
    ScanlinesSettings settings;
    settings.lineSpacing = 6, settings.thickness = 90, settings.dots = 60, settings.wobble = 6, settings.displace = -15, settings.threshold = 30;
    settings.split = 3, settings.density = 20, settings.contrast = -30, settings.blackLevel = 25, settings.smoothness = 80;
    QCOMPARE(settings.apply(pattern(37, 53)), kernel(37, 53, made([](ScanlinesParams &p) { p.spacing = 6; p.thickness = 0.9f; p.dots = 0.6f; p.wobble = 6; p.displace = -15; p.threshold = 0.3f; p.split = 3; p.density = 0.2f; p.contrast = -0.3f; p.blackLevel = 0.25f; p.smoothness = 0.8f; })));
    settings = ScanlinesSettings();
    settings.lineSpacing = 5, settings.thickness = 40, settings.contrast = 60, settings.colors = DitherColors::twoColors;
    settings.dark = AdjustmentColor(10 / 255.0, 20 / 255.0, 200 / 255.0), settings.light = AdjustmentColor(250 / 255.0, 200 / 255.0, 10 / 255.0);
    QCOMPARE(settings.apply(pattern(37, 53)), kernel(37, 53, made([](ScanlinesParams &p) { p.spacing = 5; p.thickness = 0.4f; p.contrast = 0.6f; p.smoothness = 0.5f; rgb(p.dark, 10, 20, 200); rgb(p.light, 250, 200, 10); })));
    // Half a channel rounds to 128, lit at 1.35 times.
    ScanlinesSettings halves;
    halves.colors = DitherColors::twoColors, halves.dark = AdjustmentColor(0.5, 0.5, 0.5), halves.light = AdjustmentColor(0.5, 0.5, 0.5);
    const QImage greyLines = halves.apply(filled(8, 8, Qt::white));
    const int lit = int(std::lround(128 / 255.0f * 1.35f * 255));
    const auto pixel = [&greyLines](int y) {
        const uchar *at = greyLines.constScanLine(y);
        return std::array<int, 4>{at[0], at[1], at[2], at[3]};
    };
    QCOMPARE(pixel(1), (std::array<int, 4>{lit, lit, lit, 255}));
    QCOMPARE(pixel(0)[0], int(std::lround(128 + (lit - 128) * 0.4f)));
    // Picked colours count only for Two Colors.
    settings.colors = DitherColors::blackWhite;
    QCOMPARE(settings.apply(pattern(37, 53)), kernel(37, 53, made([](ScanlinesParams &p) { p.spacing = 5; p.thickness = 0.4f; p.contrast = 0.6f; p.smoothness = 0.5f; })));
    settings.colors = DitherColors::original, settings.displace = 20, settings.lineSpacing = 4, settings.thickness = 70, settings.contrast = 0;
    QCOMPARE(fnv(settings.apply(pattern(37, 53))), 14473728681183016108ULL);
}

void ScanlinesTests::normalizingClampsEverySetting()
{
    const ScanlinesSettings defaults;
    QVERIFY(defaults.lineSpacing == 4 && defaults.thickness == 70 && defaults.glow == 0 && defaults.dots == 0 && defaults.wobble == 0);
    QVERIFY(defaults.displace == 0 && defaults.smoothness == 50 && defaults.threshold == 0 && defaults.split == 0 && defaults.blackLevel == 0);
    QVERIFY(defaults.colors == DitherColors::blackWhite && defaults.dark == AdjustmentColor(0, 0, 0) && defaults.light == AdjustmentColor(1, 1, 1));
    using Field = double ScanlinesSettings::*;
    // Below, above, a fraction, not a number.
    const std::tuple<Field, double, double, double, double, double> fields[] = {
        {&ScanlinesSettings::lineSpacing, 2, 32, 5.5, 6, 4}, {&ScanlinesSettings::thickness, 5, 100, 33.3, 33.3, 70},
        {&ScanlinesSettings::glow, 0, 100, 12.5, 12.5, 0},   {&ScanlinesSettings::dots, 0, 100, 33.3, 33.3, 0},
        {&ScanlinesSettings::wobble, 0, 64, 3.5, 3.5, 0},    {&ScanlinesSettings::displace, -100, 100, -7.5, -7.5, 0},
        {&ScanlinesSettings::smoothness, 0, 100, 12.5, 12.5, 50}, {&ScanlinesSettings::threshold, 0, 100, 12.5, 12.5, 0},
        {&ScanlinesSettings::split, 0, 16, 2.5, 3, 0},       {&ScanlinesSettings::density, -100, 100, 12.5, 12.5, 0},
        {&ScanlinesSettings::contrast, -100, 100, -12.5, -12.5, 0}, {&ScanlinesSettings::blackLevel, 0, 100, 12.5, 12.5, 0}};
    for (const auto &[field, low, high, typed, kept, fallback] : fields) {
        ScanlinesSettings settings;
        settings.*field = low - 1000;
        QCOMPARE(settings.normalized().*field, low);
        settings.*field = high + 1000;
        QCOMPARE(settings.normalized().*field, high);
        settings.*field = typed;
        QCOMPARE(settings.normalized().*field, kept);
        settings.*field = std::nan("");
        QCOMPARE(settings.normalized().*field, fallback);
    }
    // Applying normalizes first: past the ends draws as the ends.
    ScanlinesSettings wild;
    wild.lineSpacing = 5.6, wild.thickness = 500, wild.split = 2.4, wild.displace = 300;
    QCOMPARE(wild.apply(pattern(37, 53)), wild.normalized().apply(pattern(37, 53)));
    QCOMPARE(fnv(wild.apply(pattern(37, 53))), fnv(kernel(37, 53, made([](ScanlinesParams &p) { p.spacing = 6; p.thickness = 1; p.split = 2; p.displace = 100; p.smoothness = 0.5f; }))));
    // The filter's settings normalize Scanlines with the rest.
    FilterSettings filter;
    filter.scanlines = wild;
    QVERIFY(filter.normalized().scanlines == wild.normalized());
    QVERIFY(filter.normalized().scanlines.lineSpacing == 6 && filter.normalized().scanlines.thickness == 100 && filter.normalized().scanlines.split == 2);
    ScanlinesSettings colours;
    colours.dark = AdjustmentColor(-1, 2, 0.5), colours.light = AdjustmentColor(3, -2, 0.25);
    QVERIFY(colours.normalized().dark == AdjustmentColor(0, 1, 0.5) && colours.normalized().light == AdjustmentColor(1, 0, 0.25));
}

void ScanlinesTests::clearPixelsStayClear()
{
    QImage image = filled(8, 16, Qt::white);
    QPainter painter(&image);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.fillRect(QRect(0, 0, 8, 8), Qt::transparent);
    painter.end();
    const QImage result = render(image, [](ScanlinesSettings &s) { s.lineSpacing = 8, s.glow = 100; });
    QCOMPARE(column(result, 2, 3), (std::vector<int>{0, 0, 0, 0, 0, 0, 0, 0, 255, 255, 255, 255, 255, 255, 255, 255}));
    QCOMPARE(column(result, 2, 0)[4], 0);
}

// Swift's Core Image blur: the twin's within a few levels.
void ScanlinesTests::theGlowIsTheBlurredLightEasedNearWhite()
{
    QImage image = filled(64, 160, Qt::black);
    QPainter(&image).fillRect(QRect(0, 0, 32, 40), Qt::white);
    for (const double glow : {20.0, 100.0}) {
        const QImage lines = render(image, [](ScanlinesSettings &s) { s.lineSpacing = 8; });
        const QImage result = render(image, [glow](ScanlinesSettings &s) { s.lineSpacing = 8, s.glow = glow; });
        const QImage blurred = PixelAdjust::gaussianBlur(lines, 8 * 3 + 3, true);
        const float amount = float(glow / 100 * 2.5);
        int worst = 0;
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                float value = lines.constScanLine(y)[x * 4];
                const float added = blurred.constScanLine(y)[x * 4] * amount, room = (255 - value) * 0.7f;
                if (room > 0)
                    value += room * (1 - std::exp(-added / room));
                worst = std::max(worst, std::abs(result.constScanLine(y)[x * 4] - int(std::lround(std::min(255.0f, value)))));
            }
        }
        QVERIFY2(worst <= 8, qPrintable(QString::number(worst)));
    }
}

QTEST_MAIN(ScanlinesTests)
#include "ScanlinesTests.moc"
