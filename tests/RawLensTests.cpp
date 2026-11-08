#include "DNGFixture.h"
#include "IO/LensCorrection.h"
#include "IO/RawImporter.h"
#include "UI/RawDevelopSheet.h"
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QDir>
#include <QTemporaryDir>
#include <QtTest>
#include <cmath>
#include <lensfun.h>
#include <utility>
#include <libraw.h>

// Swift 1.4.10's develop: noise, highlights, the lens (lensfun).
namespace {
// The user's lensfun folder: a camera, lenses of known make.
const char *const profiles = R"(<lensdatabase version="1">
  <mount><name>OmaPhoto Mount</name></mount>
  <camera><maker>OmaPhoto</maker><model>Fixture</model><mount>OmaPhoto Mount</mount><cropfactor>1</cropfactor></camera>
  <lens><maker>OmaPhoto</maker><model>OmaPhoto Test Lens</model><mount>OmaPhoto Mount</mount><cropfactor>1</cropfactor><aspect-ratio>4:3</aspect-ratio>
    <calibration>
      <distortion model="poly3" focal="50" k1="-0.08"/>
      <vignetting model="pa" focal="50" aperture="4" distance="1000" k1="-0.6" k2="0" k3="0"/>
      <vignetting model="pa" focal="50" aperture="8" distance="1000" k1="-0.1" k2="0" k3="0"/>
    </calibration>
  </lens>
  <lens><maker>OmaPhoto</maker><model>OmaPhoto PTLens Lens</model><mount>OmaPhoto Mount</mount><cropfactor>1</cropfactor><aspect-ratio>4:3</aspect-ratio>
    <calibration><distortion model="ptlens" focal="50" a="0.01" b="-0.04" c="0.02"/></calibration>
  </lens>
  <lens><maker>OmaPhoto</maker><model>OmaPhoto Wide Lens</model><mount>OmaPhoto Mount</mount><cropfactor>1</cropfactor><aspect-ratio>4:3</aspect-ratio>
    <calibration><distortion model="ptlens" focal="50" a="0.08" b="-0.3" c="0.1"/></calibration>
  </lens>
  <lens><maker>OmaPhoto</maker><model>OmaPhoto Vignette Lens</model><mount>OmaPhoto Mount</mount><cropfactor>1</cropfactor><aspect-ratio>4:3</aspect-ratio>
    <calibration><vignetting model="pa" focal="50" aperture="4" distance="1000" k1="-0.5" k2="0" k3="0"/></calibration>
  </lens>
  <lens><maker>OmaPhoto</maker><model>OmaPhoto Bare Lens</model><mount>OmaPhoto Mount</mount><cropfactor>1</cropfactor></lens>
  <mount><name>Solo Mount</name></mount>
  <camera><maker>OmaPhoto</maker><model>Solo</model><mount>Solo Mount</mount><cropfactor>1</cropfactor></camera>
  <lens><maker>OmaPhoto</maker><model>OmaPhoto Solo Lens</model><mount>Solo Mount</mount><cropfactor>1</cropfactor>
    <calibration><distortion model="poly3" focal="50" k1="-0.05"/></calibration>
  </lens>
  <lens><maker>OmaPhoto</maker><model>OmaPhoto Own Lens</model><mount>Bundled Mount</mount><cropfactor>1</cropfactor>
    <calibration><distortion model="poly3" focal="50" k1="-0.05"/></calibration>
  </lens>
</lensdatabase>
)";

// A packaged folder, as the AppImage bundles lensfun's data.
const char *const bundled = R"(<lensdatabase version="1">
  <mount><name>Bundled Mount</name></mount>
  <camera><maker>OmaPhoto</maker><model>Bundled</model><mount>Bundled Mount</mount><cropfactor>1</cropfactor></camera>
  <lens><maker>OmaPhoto</maker><model>OmaPhoto Bundled Lens</model><mount>Bundled Mount</mount><cropfactor>1</cropfactor>
    <calibration><distortion model="poly3" focal="50" k1="-0.05"/></calibration>
  </lens>
  <lens><maker>OmaPhoto</maker><model>OmaPhoto Replaced Lens</model><mount>Bundled Mount</mount><cropfactor>1</cropfactor></lens>
  <lens><maker>OmaPhoto</maker><model>OmaPhoto Own Lens</model><mount>Bundled Mount</mount><cropfactor>1</cropfactor></lens>
  <lens><maker>Canon</maker><model>Canon EF 50mm f/1.4 USM</model><mount>Canon EF</mount><cropfactor>1</cropfactor></lens>
</lensdatabase>
)";

// A later folder on the path; the earlier one overrides.
const char *const older = R"(<lensdatabase version="1">
  <mount><name>Bundled Mount</name></mount>
  <lens><maker>OmaPhoto</maker><model>OmaPhoto Bundled Lens</model><mount>Bundled Mount</mount><cropfactor>1</cropfactor></lens>
  <lens><maker>OmaPhoto</maker><model>OmaPhoto Replaced Lens</model><mount>Bundled Mount</mount><cropfactor>1</cropfactor>
    <calibration><distortion model="poly3" focal="50" k1="-0.05"/></calibration>
  </lens>
</lensdatabase>
)";

DNGFixture::Options shot(const QByteArray &lens)
{
    DNGFixture::Options options;
    options.lensModel = lens;
    options.focal = 50;
    options.aperture = 4;
    return options;
}

// A pattern every pixel of which differs from its neighbours.
std::vector<quint16> pattern(int width, int height)
{
    std::vector<quint16> pixels(size_t(width) * size_t(height) * 3);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            for (int c = 0; c < 3; ++c)
                pixels[(size_t(y) * size_t(width) + size_t(x)) * 3 + size_t(c)] = quint16(9000 + 700 * x + 450 * y + 3000 * c + ((x * 7 + y * 3) % 5) * 900);
    return pixels;
}

// What lensfun asks for, resampled in doubles.
std::vector<double> oracle(const char *lensName, const std::vector<quint16> &pixels, int width, int height)
{
    lfDatabase database;
    database.Load();
    const lfCamera **cameras = database.FindCamerasExt("OmaPhoto", "Fixture");
    const lfLens **lenses = database.FindLenses(cameras[0], nullptr, lensName);
    lfModifier modifier(lenses[0], cameras[0]->CropFactor, width, height);
    const int done = modifier.Initialize(lenses[0], LF_PF_F32, 50, 4, 1000, 1, lenses[0]->Type, LF_MODIFY_VIGNETTING | LF_MODIFY_DISTORTION, false);
    if (done & LF_MODIFY_DISTORTION)
        modifier.AddCoordCallbackScale(0);
    std::vector<float> source(pixels.begin(), pixels.end());
    modifier.ApplyColorModification(source.data(), 0, 0, width, height, LF_CR_3(RED, GREEN, BLUE), width * 3 * int(sizeof(float)));
    std::vector<double> result(source.begin(), source.end());
    if (done & LF_MODIFY_DISTORTION) {
        // Lensfun's plain code: its SSE pairs lanes wrongly.
        std::vector<float> store(size_t(width) * size_t(height) * 2 + 1);
        float *const places = store.data() + ((reinterpret_cast<uintptr_t>(store.data()) & 0xf) ? 0 : 1);
        modifier.ApplyGeometryDistortion(0, 0, width, height, places);
        for (size_t index = 0; index < size_t(width) * size_t(height); ++index) {
            const double x = places[index * 2], y = places[index * 2 + 1];
            for (int c = 0; c < 3; ++c) {
                double sum = 0;
                for (int j = 0; j < 2; ++j)
                    for (int i = 0; i < 2; ++i) {
                        const int column = int(std::floor(x)) + i, row = int(std::floor(y)) + j;
                        if (column >= 0 && column < width && row >= 0 && row < height)
                            sum += (i ? x - std::floor(x) : 1 - (x - std::floor(x))) * (j ? y - std::floor(y) : 1 - (y - std::floor(y)))
                                * source[(size_t(row) * size_t(width) + size_t(column)) * 3 + size_t(c)];
                    }
                result[index * 3 + size_t(c)] = sum;
            }
        }
    }
    lf_free(lenses);
    lf_free(cameras);
    return result;
}

int differing(const QImage &a, const QImage &b, QRect area)
{
    int count = 0;
    for (int y = area.top(); y <= area.bottom(); ++y)
        for (int x = area.left(); x <= area.right(); ++x)
            count += a.pixel(x, y) != b.pixel(x, y);
    return count;
}
}

class RawLensTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void theCameraStartsWhereLibRawAndLensfunCan();
    void theProfileWantsCameraLensAndCalibration();
    void theDataFoldersAreRead();
    void applyCorrectsAsLensfunAsks();
    void aVignetteAloneKeepsTheGeometry();
    void brightCornersSurviveNegativeExposure();
    void developAppliesTheLensOnlyWhereAllowed();
    void noiseReductionSmoothsGrain();
    void highlightRecoveryRebuildsOnlyClippedPixels();
    void theSheetShowsWhatTheFileAllows();

private:
    QTemporaryDir m_data;
    QTemporaryDir m_shared;
    QTemporaryDir m_older;
};

void RawLensTests::initTestCase()
{
    // Before lensfun first asks glib for the user's folder.
    qputenv("XDG_DATA_HOME", m_data.path().toUtf8());
    QVERIFY(QDir(m_data.path()).mkpath("lensfun"));
    QFile file(QDir(m_data.path()).filePath("lensfun/omaphoto.xml"));
    QVERIFY(file.open(QIODevice::WriteOnly) && file.write(profiles) > 0);
    qputenv("XDG_DATA_DIRS", (m_shared.path() + ':' + m_older.path()).toUtf8());
    for (const auto &[folder, content] : {std::pair{m_shared.path(), bundled}, std::pair{m_older.path(), older}}) {
        QVERIFY(QDir(folder).mkpath("lensfun/version_1"));
        QFile shared(QDir(folder).filePath("lensfun/version_1/profiles.xml"));
        QVERIFY(shared.open(QIODevice::WriteOnly) && shared.write(content) > 0);
    }
}

void RawLensTests::theCameraStartsWhereLibRawAndLensfunCan()
{
    const QString path = DNGFixture::write(m_data.filePath("lens.dng"), shot("OmaPhoto Test Lens"));
    const RawDevelopSettings settings = RawImporter::asShot(path).value();
    QVERIFY(settings.supportsNoiseReduction && settings.supportsHighlightRecovery && settings.supportsLensCorrection);
    QVERIFY(settings.lensCorrection && settings.asShotLensCorrection);
    QVERIFY(settings.noiseReduction == 0 && settings.asShotNoiseReduction == 0 && !settings.highlightRecovery && !settings.asShotHighlightRecovery);
    QVERIFY(settings.isAsShot());
    // Each control counts against the shot, and Reset returns it.
    for (const auto &change : std::vector<std::function<void(RawDevelopSettings &)>>{[](RawDevelopSettings &s) { s.noiseReduction = 0.3f; },
                                                                                     [](RawDevelopSettings &s) { s.lensCorrection = false; },
                                                                                     [](RawDevelopSettings &s) { s.highlightRecovery = true; }}) {
        RawDevelopSettings changed = settings;
        change(changed);
        QVERIFY(!changed.isAsShot());
        changed.reset();
        QVERIFY(changed == settings);
    }
}

void RawLensTests::theProfileWantsCameraLensAndCalibration()
{
    for (const QByteArray &lens : {QByteArray(), QByteArray("Another Lens"), QByteArray("OmaPhoto Bare Lens")}) {
        const QString path = DNGFixture::write(m_data.filePath("other.dng"), shot(lens));
        const RawDevelopSettings settings = RawImporter::asShot(path).value();
        QVERIFY2(!settings.supportsLensCorrection && !settings.lensCorrection && !settings.asShotLensCorrection, lens.constData());
        QVERIFY(settings.supportsNoiseReduction && settings.supportsHighlightRecovery);
    }
    // No lens named: none, though the mount holds one.
    DNGFixture::Options solo = shot(QByteArray());
    solo.model = "Solo";
    QVERIFY(!RawImporter::asShot(DNGFixture::write(m_data.filePath("solo.dng"), solo)).value().supportsLensCorrection);
    solo.lensModel = "OmaPhoto Solo Lens";
    QVERIFY(RawImporter::asShot(DNGFixture::write(m_data.filePath("solo.dng"), solo)).value().supportsLensCorrection);
    // A lens's maker must agree, when the file names one.
    DNGFixture::Options made = shot("OmaPhoto Test Lens");
    made.lensMake = "Other";
    QVERIFY(!RawImporter::asShot(DNGFixture::write(m_data.filePath("made.dng"), made)).value().supportsLensCorrection);
    made.lensMake = "OmaPhoto";
    QVERIFY(RawImporter::asShot(DNGFixture::write(m_data.filePath("made.dng"), made)).value().supportsLensCorrection);
    // A profile wanted and missing is a bug.
    LibRaw raw;
    QCOMPARE(raw.open_file(QFile::encodeName(m_data.filePath("other.dng")).constData()), int(LIBRAW_SUCCESS));
    const std::vector<quint16> pixels = pattern(8, 6);
    QVERIFY_THROWS_EXCEPTION(std::logic_error, LensCorrection::apply(raw, pixels.data(), 8, 6));
}

void RawLensTests::theDataFoldersAreRead()
{
    // Only the shared folder knows this camera and lens.
    DNGFixture::Options options = shot("OmaPhoto Bundled Lens");
    options.model = "Bundled";
    QVERIFY(RawImporter::asShot(DNGFixture::write(m_data.filePath("bundled.dng"), options)).value().supportsLensCorrection);
    options.lensModel = "OmaPhoto Test Lens";
    QVERIFY(!RawImporter::asShot(DNGFixture::write(m_data.filePath("bundled.dng"), options)).value().supportsLensCorrection);
    // The earlier folder on the path wins, either way round.
    options.lensModel = "OmaPhoto Replaced Lens";
    QVERIFY(!RawImporter::asShot(DNGFixture::write(m_data.filePath("bundled.dng"), options)).value().supportsLensCorrection);
    // The user's lensfun folder overrides the data path.
    options.lensModel = "OmaPhoto Own Lens";
    QVERIFY(RawImporter::asShot(DNGFixture::write(m_data.filePath("bundled.dng"), options)).value().supportsLensCorrection);
    // The path overrides lensfun's compiled folder, calibrated there.
    lfDatabase own;
    QCOMPARE(own.Load(), LF_NO_ERROR);
    const lfCamera **cameras = own.FindCamerasExt("Canon", "Canon EOS 5D Mark II");
    QVERIFY(cameras);
    const lfLens **lenses = own.FindLenses(cameras[0], "Canon", "Canon EF 50mm f/1.4 USM");
    QVERIFY(lenses && lenses[0]->CalibDistortion && lenses[0]->CalibDistortion[0]);
    lf_free(lenses);
    lf_free(cameras);
    options.make = "Canon";
    options.model = "Canon EOS 5D Mark II";
    options.lensMake = "Canon";
    options.lensModel = "Canon EF 85mm f/1.8 USM";
    QVERIFY(RawImporter::asShot(DNGFixture::write(m_data.filePath("canon.dng"), options)).value().supportsLensCorrection);
    options.lensModel = "Canon EF 50mm f/1.4 USM";
    QVERIFY(!RawImporter::asShot(DNGFixture::write(m_data.filePath("canon.dng"), options)).value().supportsLensCorrection);
}

void RawLensTests::applyCorrectsAsLensfunAsks()
{
    // The wide lens reaches just past the top and bottom.
    for (const char *lens : {"OmaPhoto Test Lens", "OmaPhoto PTLens Lens", "OmaPhoto Wide Lens"})
    for (const QSize size : {QSize(64, 48), QSize(31, 23)}) {
        LibRaw raw;
        QCOMPARE(raw.open_file(QFile::encodeName(DNGFixture::write(m_data.filePath("apply.dng"), shot(lens))).constData()), int(LIBRAW_SUCCESS));
        const std::vector<quint16> pixels = pattern(size.width(), size.height());
        const std::unique_ptr<float[]> corrected = LensCorrection::apply(raw, pixels.data(), size.width(), size.height());
        const std::vector<double> expected = oracle(lens, pixels, size.width(), size.height());
        int moved = 0;
        for (size_t index = 0; index < expected.size(); ++index) {
            // Float sums against double ones: well under a level.
            QVERIFY2(std::abs(corrected[index] - expected[index]) <= 1, qPrintable(QString("%1: %2 vs %3 (was %4)").arg(index).arg(corrected[index]).arg(expected[index]).arg(pixels[index])));
            moved += corrected[index] != pixels[index];
        }
        QVERIFY(moved > int(expected.size()) / 2);
    }
}

void RawLensTests::aVignetteAloneKeepsTheGeometry()
{
    LibRaw raw;
    QCOMPARE(raw.open_file(QFile::encodeName(DNGFixture::write(m_data.filePath("vignette.dng"), shot("OmaPhoto Vignette Lens"))).constData()), int(LIBRAW_SUCCESS));
    const std::vector<quint16> pixels = pattern(64, 48);
    const std::unique_ptr<float[]> corrected = LensCorrection::apply(raw, pixels.data(), 64, 48);
    const std::vector<double> expected = oracle("OmaPhoto Vignette Lens", pixels, 64, 48);
    for (size_t index = 0; index < expected.size(); ++index)
        QCOMPARE(double(corrected[index]), expected[index]);
    // Corners brighten; the middle hardly moves.
    QVERIFY(corrected[0] > pixels[0] + 1000);
    const size_t middle = (24 * 64 + 32) * 3;
    QVERIFY(std::abs(corrected[middle] - pixels[middle]) < 20);
    // Past 16 bits nothing clips: exposure brings it back.
    const std::vector<quint16> bright(64 * 48 * 3, 60000);
    const std::unique_ptr<float[]> lifted = LensCorrection::apply(raw, bright.data(), 64, 48);
    QVERIFY2(lifted[0] > 100000, qPrintable(QString::number(lifted[0])));
}

void RawLensTests::brightCornersSurviveNegativeExposure()
{
    // Clipped before exposure, both frames' corners would match.
    const auto corner = [&](quint16 level) {
        DNGFixture::Options options = shot("OmaPhoto Vignette Lens");
        options.value = [level](int, int, int) { return level; };
        const QString path = DNGFixture::write(m_data.filePath("bright.dng"), options);
        RawDevelopSettings settings = RawImporter::asShot(path).value();
        settings.exposure = -2;
        const QImage image = RawImporter::develop(path, settings);
        return std::pair{qGreen(image.pixel(0, 0)), qGreen(image.pixel(image.width() / 2, image.height() / 2))};
    };
    const auto [dimCorner, dimMiddle] = corner(40000);
    const auto [brightCorner, brightMiddle] = corner(60000);
    QVERIFY2(brightCorner > dimCorner + 20, qPrintable(QString("%1 %2").arg(brightCorner).arg(dimCorner)));
    QVERIFY(brightCorner > brightMiddle + 20 && dimCorner > dimMiddle + 20);
}

void RawLensTests::developAppliesTheLensOnlyWhereAllowed()
{
    DNGFixture::Options options = shot("OmaPhoto Test Lens");
    options.value = [](int channel, int x, int y) { return quint16(6000 + 400 * x + 300 * y + 2000 * channel); };
    const QString path = DNGFixture::write(m_data.filePath("develop.dng"), options);
    RawDevelopSettings settings = RawImporter::asShot(path).value();
    const QImage corrected = RawImporter::develop(path, settings);
    settings.lensCorrection = false;
    const QImage plain = RawImporter::develop(path, settings);
    QVERIFY(differing(corrected, plain, corrected.rect()) > corrected.width() * corrected.height() / 2);
    // A file without a profile ignores the wish.
    RawDevelopSettings unsupported = settings;
    unsupported.supportsLensCorrection = false;
    unsupported.lensCorrection = true;
    QCOMPARE(RawImporter::develop(path, unsupported), plain);
}

void RawLensTests::noiseReductionSmoothsGrain()
{
    // LibRaw denoises frames past 130 photosites a side.
    DNGFixture::Options options;
    options.width = 200;
    options.height = 160;
    options.value = [](int channel, int x, int y) { return quint16(20000 + channel * 1000 + ((x * 7919 + y * 104729) % 97) * 60); };
    const QString path = DNGFixture::write(m_data.filePath("grain.dng"), options);
    RawDevelopSettings settings = RawImporter::asShot(path).value();
    // The spread of green in the middle, off the edges.
    const auto spread = [](const QImage &image) {
        double sum = 0, squares = 0, count = 0;
        for (int y = 8; y < image.height() - 8; ++y)
            for (int x = 8; x < image.width() - 8; ++x) {
                const double green = qGreen(image.pixel(x, y));
                sum += green;
                squares += green * green;
                ++count;
            }
        return std::sqrt(squares / count - (sum / count) * (sum / count));
    };
    const QImage grainy = RawImporter::develop(path, settings);
    settings.noiseReduction = 1;
    const QImage smooth = RawImporter::develop(path, settings);
    QVERIFY2(spread(smooth) < spread(grainy) * 0.6, qPrintable(QString("%1 %2").arg(spread(smooth)).arg(spread(grainy))));
    settings.supportsNoiseReduction = false;
    QCOMPARE(RawImporter::develop(path, settings), grainy);
}

void RawLensTests::highlightRecoveryRebuildsOnlyClippedPixels()
{
    // Green clips first; red, the largest multiplier, holds.
    DNGFixture::Options options;
    options.asShotNeutral = {0.5, 1, 0.7};
    options.value = [](int channel, int x, int) {
        const std::array<quint16, 3> left{30000, 40000, 30000}, right{60000, 65535, 35000};
        return x < 32 ? left[size_t(channel)] : right[size_t(channel)];
    };
    const QString path = DNGFixture::write(m_data.filePath("clipped.dng"), options);
    RawDevelopSettings settings = RawImporter::asShot(path).value();
    // Recovery shows once exposure brings white down.
    settings.exposure = -2;
    const QImage clipped = RawImporter::develop(path, settings);
    settings.highlightRecovery = true;
    const QImage recovered = RawImporter::develop(path, settings);
    // Per channel: the mean change over an area.
    const auto change = [&](QRect area, int shift8) {
        double sum = 0;
        for (int y = area.top(); y <= area.bottom(); ++y)
            for (int x = area.left(); x <= area.right(); ++x)
                sum += int((recovered.pixel(x, y) >> shift8) & 0xff) - int((clipped.pixel(x, y) >> shift8) & 0xff);
        return sum / (area.width() * area.height());
    };
    // Unclipped, as it was within the dither's level.
    for (const int shift8 : {16, 8, 0})
        QVERIFY2(std::abs(change(QRect(0, 0, 24, 48), shift8)) < 0.05, qPrintable(QString::number(change(QRect(0, 0, 24, 48), shift8))));
    // Clipped, red and green meet at white; rebuilt, green drops.
    const auto mean = [](const QImage &image, int shift8) {
        double sum = 0;
        for (int y = 8; y < 40; ++y)
            for (int x = 40; x < 60; ++x)
                sum += (image.pixel(x, y) >> shift8) & 0xff;
        return sum / (32 * 20);
    };
    QVERIFY2(std::abs(mean(clipped, 16) - mean(clipped, 8)) < 2, qPrintable(QString("%1 %2").arg(mean(clipped, 16)).arg(mean(clipped, 8))));
    QVERIFY2(mean(recovered, 8) < mean(recovered, 16) - 20, qPrintable(QString("%1 %2").arg(mean(recovered, 16)).arg(mean(recovered, 8))));
    settings.supportsHighlightRecovery = false;
    QCOMPARE(RawImporter::develop(path, settings), clipped);
}

void RawLensTests::theSheetShowsWhatTheFileAllows()
{
    const QString path = DNGFixture::write(m_data.filePath("sheet.dng"), shot("OmaPhoto Test Lens"));
    std::optional<RawDevelopSettings> answer;
    RawDevelopSheet sheet({path, RawImporter::asShot(path).value()}, [&answer](std::optional<RawDevelopSettings> settings) { answer = settings; });
    sheet.show();
    QVERIFY(QTest::qWaitForWindowExposed(&sheet));
    auto *lens = sheet.findChild<QCheckBox *>("rawLensCorrection");
    auto *highlights = sheet.findChild<QCheckBox *>("rawHighlightRecovery");
    auto *noise = sheet.findChild<QSlider *>("rawNoise Reduction");
    auto *reset = sheet.findChild<QPushButton *>("rawReset");
    QVERIFY(lens && highlights && noise && reset);
    QVERIFY(lens->isChecked() && lens->isEnabled() && !highlights->isChecked() && highlights->isEnabled() && noise->isEnabled());
    QCOMPARE(lens->toolTip(), QString("Removes the lens's distortion and vignetting"));
    QCOMPARE(highlights->toolTip(), QString("Brings back detail in bright areas the camera partly clipped"));
    QCOMPARE(noise->toolTip(), QString("Smooths grain, at the cost of fine detail"));
    QCOMPARE(noise->value(), 0);
    QCOMPARE(sheet.findChild<QLabel *>("rawNoise ReductionValue")->text(), QString("0.00"));
    // Swift's titles, 110 wide; the toggles 120 in, 20 apart.
    for (const QLabel *label : sheet.findChildren<QLabel *>())
        if (label->buddy() == noise)
            QCOMPARE(label->width(), 110);
    QCOMPARE(lens->x(), 24 + 120);
    QCOMPARE(highlights->x(), lens->x() + lens->width() + 20);
    QVERIFY(!reset->isEnabled());
    highlights->click();
    noise->setValue(250);
    QVERIFY(highlights->isChecked() && reset->isEnabled());
    QCOMPARE(sheet.findChild<QLabel *>("rawNoise ReductionValue")->text(), QString("0.25"));
    lens->click();
    sheet.findChild<QPushButton *>("rawImport")->click();
    QVERIFY(answer && answer->highlightRecovery && !answer->lensCorrection && std::abs(answer->noiseReduction - 0.25f) < 1e-6f);
    reset->click();
    QVERIFY(lens->isChecked() && !highlights->isChecked() && noise->value() == 0 && !reset->isEnabled());
    // Importing locks them; done, they follow the file again.
    sheet.setImporting(true);
    QVERIFY(!lens->isEnabled() && !highlights->isEnabled() && !noise->isEnabled());
    sheet.setImporting(false);
    QVERIFY(lens->isEnabled() && highlights->isEnabled() && noise->isEnabled());
    // What a file cannot do rests, with Swift's words.
    RawDevelopSettings bare = RawImporter::asShot(DNGFixture::write(m_data.filePath("bare.dng"), shot(QByteArray()))).value();
    bare.supportsNoiseReduction = bare.supportsHighlightRecovery = false;
    RawDevelopSheet plain({m_data.filePath("bare.dng"), bare}, [](std::optional<RawDevelopSettings>) {});
    auto *plainLens = plain.findChild<QCheckBox *>("rawLensCorrection");
    auto *plainHighlights = plain.findChild<QCheckBox *>("rawHighlightRecovery");
    auto *plainNoise = plain.findChild<QSlider *>("rawNoise Reduction");
    QVERIFY(!plainLens->isEnabled() && !plainLens->isChecked() && !plainHighlights->isEnabled() && !plainNoise->isEnabled());
    QCOMPARE(plainLens->toolTip(), QString("This file has no lens profile"));
    QCOMPARE(plainHighlights->toolTip(), QString("Not available for this camera"));
    QCOMPARE(plainNoise->toolTip(), QString("Not available for this camera"));
}

QTEST_MAIN(RawLensTests)
#include "RawLensTests.moc"
