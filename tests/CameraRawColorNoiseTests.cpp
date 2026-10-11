#include "CameraRawFixtures.h"
#include <QtTest>
#include <cmath>
#include <malloc.h>

// Swift 1.4.6's Color noise reduction: color smoothed, brightness kept.
using namespace CameraRawFixtures;

namespace {
struct Measured {
    double color;
    double brightness;
};

// Swift's measure: mean spread and brightness inside a margin.
Measured measure(const QImage &image, int margin)
{
    const std::vector<std::array<int, 4>> read = pixels(image);
    double color = 0, brightness = 0;
    int counted = 0;
    for (int y = margin; y < image.height() - margin; ++y)
        for (int x = margin; x < image.width() - margin; ++x) {
            const std::array<int, 4> &p = read[size_t(y * image.width() + x)];
            color += chroma(p);
            brightness += 0.2126 * p[0] + 0.7152 * p[1] + 0.0722 * p[2];
            ++counted;
        }
    return {color / counted, brightness / counted};
}

CameraRawSettings colorNoise(double amount, double detail = 50)
{
    CameraRawSettings settings;
    settings.detail.noiseColor = amount;
    settings.detail.noiseColorDetail = detail;
    return settings;
}
}

class CameraRawColorNoiseTests : public QObject {
    Q_OBJECT
private slots:
    void colorNoiseReductionRemovesColorGrainNotBrightness();
    void colorNoiseReductionIgnoresWhatClearPixelsHeld();
    void clearPixelsPullNoColorTowardGray();
    void colorDetailKeepsARealColorEdge();
    void aSpeckTakesTheColorAroundItAndKeepsItsBrightness();
    void translucentPixelsKeepTheirColor();
};

void CameraRawColorNoiseTests::colorNoiseReductionRemovesColorGrainNotBrightness()
{
    QImage grain = BrushRaster::context(120, 120, false);
    quint32 seed = 12345;
    for (int y = 0; y < 120; ++y) {
        uchar *row = grain.scanLine(y);
        for (int x = 0; x < 120; ++x) {
            for (int c = 0; c < 3; ++c) {
                seed = seed * 1664525u + 1013904223u;
                row[x * 4 + c] = uchar(64 + int(seed >> 25));
            }
            row[x * 4 + 3] = 255;
        }
    }
    const Measured before = measure(CameraRawSettings().apply(grain), 10);
    const Measured after = measure(colorNoise(100).apply(grain), 10);
    QVERIFY2(after.color < before.color * 0.25, qPrintable(QStringLiteral("%1 → %2").arg(before.color).arg(after.color)));
    QVERIFY2(std::abs(after.brightness - before.brightness) < 1, qPrintable(QStringLiteral("%1 → %2").arg(before.brightness).arg(after.brightness)));
    // Half the amount takes about half the speckle.
    const Measured half = measure(colorNoise(50).apply(grain), 10);
    QVERIFY(half.color < before.color * 0.75 && half.color > after.color * 1.5);
}

void CameraRawColorNoiseTests::colorNoiseReductionIgnoresWhatClearPixelsHeld()
{
    const CameraRawSettings settings = colorNoise(60);
    QImage picture = BrushRaster::context(97, 61, false);
    for (int y = 0; y < 61; ++y) {
        uchar *row = picture.scanLine(y);
        for (int x = 0; x < 97; ++x) {
            const int alpha = x < 30 ? 0 : 255;
            uchar *p = row + x * 4;
            p[0] = uchar((x * 7 + y * 3) % 256 * alpha / 255);
            p[1] = uchar((x * x + y * 5) % 256 * alpha / 255);
            p[2] = uchar((x * 2 + y * y) % 256 * alpha / 255);
            p[3] = uchar(alpha);
        }
    }
    // Planes on the heap; handed-out memory holds bytes.
    mallopt(M_MMAP_THRESHOLD, 64 << 20);
    const std::vector<std::array<int, 4>> first = pixels(settings.apply(picture));
    mallopt(M_PERTURB, 0x5a);
    const QImage saturated = image(97, 61, 1, 0, 0.2);
    for (int run = 0; run < 4; ++run)
        settings.apply(saturated);
    const std::vector<std::array<int, 4>> again = pixels(settings.apply(picture));
    mallopt(M_PERTURB, 0);
    QVERIFY(again == first);
}

void CameraRawColorNoiseTests::clearPixelsPullNoColorTowardGray()
{
    // Red beside clear pixels stays red: they weigh nothing.
    QImage picture = image(40, 20, 0.9, 0.2, 0.2);
    for (int y = 0; y < 20; ++y)
        for (int x = 0; x < 20; ++x)
            picture.setPixelColor(x, y, Qt::transparent);
    const std::vector<std::array<int, 4>> smoothed = pixels(colorNoise(100).apply(picture));
    const std::array<int, 4> beside = smoothed[size_t(10 * 40 + 20)];
    QVERIFY2(std::abs(beside[0] - 230) <= 1 && std::abs(beside[1] - 51) <= 1 && std::abs(beside[2] - 51) <= 1,
             qPrintable(QStringLiteral("%1 %2 %3").arg(beside[0]).arg(beside[1]).arg(beside[2])));
    QCOMPARE(smoothed[size_t(10 * 40 + 5)][3], 0);
}

void CameraRawColorNoiseTests::colorDetailKeepsARealColorEdge()
{
    // Red meets blue at one brightness; Detail keeps the edge.
    QImage picture = image(60, 20, 0.8, 0.1, 0.1);
    QPainter(&picture).fillRect(QRect(30, 0, 30, 20), QColor::fromRgbF(0.1f, 0.1f, 0.8f));
    const auto redness = [](const QImage &smoothed) { return pixels(smoothed)[size_t(10 * 60 + 29)][0]; };
    const int sharp = redness(colorNoise(100, 100).apply(picture)), soft = redness(colorNoise(100, 0).apply(picture));
    QVERIFY2(sharp > soft + 20, qPrintable(QStringLiteral("%1 %2").arg(sharp).arg(soft)));
    // Far from the edge both leave the red alone.
    QCOMPARE(pixels(colorNoise(100, 0).apply(picture))[size_t(10 * 60 + 2)][0], 204);
    // The same edge across, read down the picture.
    const QImage across = picture.transformed(QTransform().rotate(90));
    const auto acrossRedness = [](const QImage &smoothed) { return pixels(smoothed)[size_t(29 * 20 + 10)][0]; };
    const int acrossSharp = acrossRedness(colorNoise(100, 100).apply(across)), acrossSoft = acrossRedness(colorNoise(100, 0).apply(across));
    QVERIFY2(acrossSharp > acrossSoft + 20, qPrintable(QStringLiteral("%1 %2").arg(acrossSharp).arg(acrossSoft)));
}

void CameraRawColorNoiseTests::aSpeckTakesTheColorAroundItAndKeepsItsBrightness()
{
    QImage picture = image(21, 21, 0.6, 0.3, 0.3);
    picture.setPixelColor(10, 10, QColor::fromRgbF(0.3f, 0.3f, 0.6f));
    const std::array<int, 4> before = pixels(picture)[size_t(10 * 21 + 10)];
    const std::array<int, 4> after = pixels(colorNoise(100, 0).apply(picture))[size_t(10 * 21 + 10)];
    const auto luma = [](const std::array<int, 4> &p) { return 0.2126 * p[0] + 0.7152 * p[1] + 0.0722 * p[2]; };
    QVERIFY2(after[0] > after[2] + 40, qPrintable(QStringLiteral("%1 %2 %3").arg(after[0]).arg(after[1]).arg(after[2])));
    QVERIFY2(std::abs(luma(after) - luma(before)) < 1, qPrintable(QStringLiteral("%1 → %2").arg(luma(before)).arg(luma(after))));
}

void CameraRawColorNoiseTests::translucentPixelsKeepTheirColor()
{
    // Coverage weighs the color in and divides it out again.
    const QImage picture = image(30, 30, 0.8, 0.2, 0.2, 0.5);
    const std::array<int, 4> before = pixels(picture)[size_t(15 * 30 + 15)];
    const std::array<int, 4> after = pixels(colorNoise(100).apply(picture))[size_t(15 * 30 + 15)];
    for (size_t channel = 0; channel < 4; ++channel)
        QVERIFY2(std::abs(after[channel] - before[channel]) <= 2, qPrintable(QStringLiteral("%1: %2 → %3").arg(channel).arg(before[channel]).arg(after[channel])));
}

QTEST_MAIN(CameraRawColorNoiseTests)
#include "CameraRawColorNoiseTests.moc"
