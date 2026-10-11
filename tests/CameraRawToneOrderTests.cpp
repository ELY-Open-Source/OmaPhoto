#include "CameraRawFixtures.h"
#include <QtTest>
#include <cmath>

// Swift 1.4.8: Shadows and Highlights keep tones in order.
using namespace CameraRawFixtures;

namespace {
// A gray ramp, black to white, one pixel a level.
QImage ramp()
{
    QImage picture = BrushRaster::context(256, 1, false);
    for (int x = 0; x < 256; ++x)
        picture.setPixelColor(x, 0, QColor(x, x, x));
    return picture;
}

std::vector<int> levels(double shadows, double highlights)
{
    CameraRawSettings settings;
    settings.shadows = shadows;
    settings.highlights = highlights;
    std::vector<int> out;
    for (const std::array<int, 4> &pixel : pixels(settings.apply(ramp())))
        out.push_back(pixel[1]);
    return out;
}

// Swift's bump, u(1 - 2u)² to a half, apart.
double bump(double u)
{
    return u <= 0 || u >= 0.5 ? 0 : u * (1 - 2 * u) * (1 - 2 * u);
}
}

class CameraRawToneOrderTests : public QObject {
    Q_OBJECT
private slots:
    void shadowsAndHighlightsKeepTonesInOrder();
    void eachToneFollowsSwiftsCurve();
};

void CameraRawToneOrderTests::shadowsAndHighlightsKeepTonesInOrder()
{
    for (const auto &[shadows, highlights] : std::vector<std::pair<double, double>>{{100, 0}, {-100, 0}, {0, 100}, {0, -100}}) {
        const std::vector<int> out = levels(shadows, highlights);
        QVERIFY2(std::ranges::is_sorted(out), qPrintable(QStringLiteral("shadows %1, highlights %2").arg(shadows).arg(highlights)));
        QVERIFY2(out[0] <= 2 && out[255] >= 253, qPrintable(QStringLiteral("black %1, white %2").arg(out[0]).arg(out[255])));
    }
    QVERIFY(levels(100, 0)[45] >= 45 + 20);
}

void CameraRawToneOrderTests::eachToneFollowsSwiftsCurve()
{
    // Gray stays gray; Highlights first, then Shadows.
    for (const auto &[shadows, highlights] : std::vector<std::pair<double, double>>{{100, 0}, {-100, 0}, {60, 0}, {-50, 0}, {0, 100}, {0, -100}, {0, -40}, {0, 50}, {60, -40}, {-50, 50}}) {
        const std::vector<int> out = levels(shadows, highlights);
        const double low = shadows / 100, high = highlights / 100;
        for (int level = 0; level < 256; ++level) {
            const double y = level / 255.0;
            const double lit = std::clamp(y + high * (high >= 0 ? 1.0 : 2.5) * bump(1 - y), 0.0, 1.0);
            const double lifted = std::clamp(lit + low * (low >= 0 ? 2.5 : 1.0) * bump(lit), 0.0, 1.0);
            QVERIFY2(std::abs(out[size_t(level)] - std::lround(lifted * 255)) <= 1,
                     qPrintable(QStringLiteral("%1, %2 at %3: %4").arg(shadows).arg(highlights).arg(level).arg(out[size_t(level)])));
        }
        // Black, middle gray and white stay where they were.
        QVERIFY(out[0] <= 1 && std::abs(out[128] - 128) <= 1 && out[255] >= 254);
    }
}

QTEST_MAIN(CameraRawToneOrderTests)
#include "CameraRawToneOrderTests.moc"
