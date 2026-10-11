#include "CameraRawFixtures.h"
#include <QtTest>
#include <cmath>

// Swift 1.4.9: Shadows and Highlights keep tones in order.
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
}

class CameraRawToneOrderTests : public QObject {
    Q_OBJECT
private slots:
    void shadowsAndHighlightsKeepTonesInOrder();
};

void CameraRawToneOrderTests::shadowsAndHighlightsKeepTonesInOrder()
{
    for (const auto &[shadows, highlights] : std::vector<std::pair<double, double>>{{100, 0}, {-100, 0}, {0, 100}, {0, -100}}) {
        const std::vector<int> out = levels(shadows, highlights);
        QVERIFY2(std::ranges::is_sorted(out), qPrintable(QStringLiteral("shadows %1, highlights %2").arg(shadows).arg(highlights)));
        // Photoshop's Highlights −100 brings white to 239 here.
        QVERIFY2(out[0] <= 2 && out[255] >= (highlights < 0 ? 235 : 253), qPrintable(QStringLiteral("black %1, white %2").arg(out[0]).arg(out[255])));
    }
    QVERIFY(levels(100, 0)[45] >= 45 + 12);
}

QTEST_MAIN(CameraRawToneOrderTests)
#include "CameraRawToneOrderTests.moc"
