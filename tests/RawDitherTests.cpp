#include "DNGFixture.h"
#include "IO/RawImporter.h"
#include <QTemporaryDir>
#include <QtTest>
#include <set>
extern "C" {
#include "DitherPixels.h"
}

// Swift's RawDitherTests: 16 bits rounded to 8 with fine noise.
namespace {
// Premultiplied RGBA at 16 bits, one value a row.
std::vector<quint16> wide(int width, int height, const std::function<double(int row)> &tone)
{
    std::vector<quint16> values(size_t(width) * size_t(height) * 4);
    for (int y = 0; y < height; ++y) {
        const quint16 value = quint16(std::lround(tone(y) * 65535));
        for (int x = 0; x < width; ++x) {
            quint16 *pixel = values.data() + (size_t(y) * size_t(width) + size_t(x)) * 4;
            pixel[0] = pixel[1] = pixel[2] = value;
            pixel[3] = 65535;
        }
    }
    return values;
}

QImage quantized(const std::vector<quint16> &values, int width, int height)
{
    QImage image(width, height, QImage::Format_RGBA8888_Premultiplied);
    dither_quantize16(values.data(), image.bits(), size_t(width), size_t(height), size_t(image.bytesPerLine()));
    return image;
}
}

class RawDitherTests : public QObject {
    Q_OBJECT
private slots:
    void ditheredGradientFollowsTheTonesNotTheSteps();
    void ditherIsFineAndRepeatable();
    void theNoiseNeverPassesAlpha();
    void theDevelopIsDithered();
};

void RawDitherTests::ditheredGradientFollowsTheTonesNotTheSteps()
{
    // From 0.30 at the top to 0.32: five steps.
    const auto tone = [](int row) { return 0.30 + 0.02 * (double(row) + 0.5) / 400; };
    const QImage image = quantized(wide(300, 400, tone), 300, 400);
    double worst = 0, plain = 0;
    std::vector<double> means;
    std::set<int> steps;
    for (int y = 0; y < 400; ++y) {
        const uchar *row = image.constScanLine(y);
        double sum = 0;
        for (int x = 0; x < 300; ++x)
            sum += row[x * 4];
        means.push_back(sum / 300);
        const double ideal = tone(y) * 255;
        worst = std::max(worst, std::abs(means.back() - ideal));
        plain = std::max(plain, std::abs(std::round(ideal) - ideal));
        steps.insert(int(std::lround(ideal)));
    }
    QVERIFY2(worst < 0.25 && plain > 0.45, qPrintable(QStringLiteral("%1 %2").arg(worst).arg(plain)));
    QVERIFY(means.front() < means.back());
    QVERIFY(steps.size() <= 6);
}

void RawDitherTests::ditherIsFineAndRepeatable()
{
    const std::vector<quint16> flat = wide(64, 64, [](int) { return 0.5; });
    const QImage first = quantized(flat, 64, 64), second = quantized(flat, 64, 64);
    QCOMPARE(first, second);
    std::set<int> seen;
    double sum = 0;
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 64; ++x) {
            const uchar *pixel = first.constScanLine(y) + x * 4;
            seen.insert(pixel[0]);
            sum += pixel[0];
            QCOMPARE(int(pixel[3]), 255);
        }
    }
    QVERIFY(*seen.begin() >= 126 && *seen.rbegin() <= 129 && seen.size() > 1);
    QVERIFY2(std::abs(sum / 4096 - 127.5) < 0.1, qPrintable(QString::number(sum / 4096)));
    // Two draws: three in four stay on a level.
    const QImage level = quantized(wide(64, 64, [](int) { return 128.0 / 255; }), 64, 64);
    int kept = 0;
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 64; ++x)
            kept += level.constScanLine(y)[x * 4] == 128;
    }
    QVERIFY2(std::abs(kept / 4096.0 - 0.75) < 0.03, qPrintable(QString::number(kept / 4096.0)));
}

void RawDitherTests::theNoiseNeverPassesAlpha()
{
    // Premultiplied colour at its alpha: noise up is clamped.
    std::vector<quint16> values(16 * 16 * 4);
    for (size_t index = 0; index < values.size(); index += 4) {
        values[index] = values[index + 1] = values[index + 2] = 32896;
        values[index + 3] = 32896;
    }
    const QImage image = quantized(values, 16, 16);
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            const uchar *pixel = image.constScanLine(y) + x * 4;
            QCOMPARE(int(pixel[3]), 128);
            QVERIFY(pixel[0] <= 128 && pixel[1] <= 128 && pixel[2] <= 128);
        }
    }
}

void RawDitherTests::theDevelopIsDithered()
{
    const QTemporaryDir folder;
    const QString path = DNGFixture::write(folder.filePath("gray.dng"));
    RawDevelopSettings settings = RawImporter::asShot(path).value();
    settings.boost = 0;
    const QImage developed = RawImporter::develop(path, settings);
    QCOMPARE(developed, RawImporter::develop(path, settings));
    std::set<int> reds;
    for (int y = 0; y < developed.height(); ++y) {
        for (int x = 0; x < developed.width(); ++x)
            reds.insert(developed.pixelColor(x, y).red());
    }
    // 20000 of 65535 through sRGB's curve is 149.94.
    QCOMPARE(reds, (std::set<int>{149, 150, 151}));
}

QTEST_GUILESS_MAIN(RawDitherTests)
#include "RawDitherTests.moc"
