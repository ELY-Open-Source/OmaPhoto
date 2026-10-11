#include "IO/ImageExporter.h"
#include "RenderFixtures.h"
#include <QtEndian>
#include <QtTest>

// Swift 1.4.8's PDF and Export As's resize, in the exporter.
namespace {
// Swift's fixture: 6 by 6, red left, clear right.
ProjectSnapshot snapshot(double resolution)
{
    QImage image = BrushRaster::context(2, 2, false);
    image.setPixel(0, 0, qRgba(255, 0, 0, 255));
    image.setPixel(0, 1, qRgba(255, 0, 0, 255));
    const QUuid id = QUuid::createUuid();
    const LayerTransform transform{.origin = {1, 1}, .size = {4, 4}, .sampling = LayerSampling::nearest};
    const ProjectLayerRecord record{.id = id, .name = "Red", .isVisible = true, .transform = transform, .imageFile = id.toString(QUuid::WithoutBraces) + ".png"};
    ProjectSnapshot made{.manifest = {.documentID = QUuid::createUuid(), .width = 6, .height = 6, .activeLayerID = id, .layers = {record}},
                         .images = {{id, ImportedImage(image, image, "Red")}}};
    made.manifest.resolution = resolution;
    return made;
}

// Object `number`'s text, found through the cross-reference table.
QByteArray object(const QByteArray &pdf, int number)
{
    const qsizetype table = pdf.lastIndexOf("startxref\n");
    const qsizetype xref = pdf.mid(table + 10).split('\n').value(0).toLongLong();
    const QList<QByteArray> lines = pdf.mid(xref).split('\n');
    const qsizetype offset = lines.value(2 + number).left(10).toLongLong();
    const QByteArray start = QByteArray::number(number) + " 0 obj\n";
    if (pdf.mid(offset, start.size()) != start)
        throw std::runtime_error("cross-reference points elsewhere");
    return pdf.mid(offset, pdf.indexOf("\nendobj\n", offset) - offset);
}

// A stream's bytes, inflated; its /Length must be its size.
QByteArray streamOf(const QByteArray &text)
{
    const qsizetype begin = text.indexOf(">>\nstream\n") + 10, end = text.lastIndexOf("\nendstream");
    const QByteArray body = text.mid(begin, end - begin);
    const QByteArray dictionary = text.left(begin);
    const qsizetype length = dictionary.mid(dictionary.indexOf("/Length ") + 8).split(' ').value(0).toLongLong();
    if (length != body.size())
        throw std::runtime_error("stream length is wrong");
    if (!dictionary.contains("/FlateDecode"))
        return body;
    QByteArray sized(4, 0);
    qToBigEndian(quint32(1 << 20), sized.data());
    return qUncompress(sized + body);
}
}

class ExportPDFTests : public QObject {
    Q_OBJECT
private slots:
    void pdfIsOnePageAtThePrintedSizeWithLosslessPixels();
    void aBackgroundFillsThePageUnderThePicture();
    void anOversizedCanvasIsRefused();
    void resizingKeepsTheResolutionWithinTheLimits();
    void flatteningLaysThePictureOverItsBackground();
};

void ExportPDFTests::pdfIsOnePageAtThePrintedSizeWithLosslessPixels()
{
    const QByteArray pdf = ImageExporter::pdfData(snapshot(150));
    QVERIFY(pdf.startsWith("%PDF-1.4\n") && pdf.endsWith("%%EOF\n"));
    QCOMPARE(pdf.count("/Type /Page "), qsizetype(1));
    // 6 pixels at 150 per inch: 0.04 in, 2.88 points.
    QVERIFY(object(pdf, 3).contains("/MediaBox [0 0 2.8800 2.8800]"));
    QVERIFY(pdf.contains("/FlateDecode") && !pdf.contains("/DCTDecode"));
    // The pixels as they were: red, then clear beside it.
    const QByteArray colour = streamOf(object(pdf, 5)), alpha = streamOf(object(pdf, 6));
    QVERIFY(object(pdf, 5).contains("/Width 6 /Height 6") && object(pdf, 5).contains("/SMask 6 0 R"));
    QCOMPARE(colour.size(), qsizetype(6 * 6 * 3));
    QCOMPARE(alpha.size(), qsizetype(6 * 6));
    const auto at = [&](int x, int y) { return std::array<int, 4>{uchar(colour[(y * 6 + x) * 3]), uchar(colour[(y * 6 + x) * 3 + 1]), uchar(colour[(y * 6 + x) * 3 + 2]), uchar(alpha[y * 6 + x])}; };
    QCOMPARE(at(1, 1), (std::array<int, 4>{255, 0, 0, 255}));
    QCOMPARE(at(4, 1)[3], 0);
    QCOMPARE(at(0, 0)[3], 0);
    // The page draws the image over it, nothing beneath.
    QCOMPARE(streamOf(object(pdf, 4)), QByteArray("q\n2.8800 0 0 2.8800 0 0 cm /Im0 Do\nQ"));
    // Rows top first, each channel its own, as drawn.
    QImage grid = BrushRaster::context(2, 3, false);
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 2; ++x)
            grid.setPixel(x, y, qRgba(10 + 40 * y + 20 * x, 100 + 30 * y - 10 * x, 200 - 50 * y + 5 * x, 255));
    const QByteArray rows = ImageExporter::pdfData(ExportRaster{grid, 72});
    QCOMPARE(streamOf(object(rows, 5)), QByteArray("\x0a\x64\xc8\x1e\x5a\xcd\x32\x82\x96\x46\x78\x9b\x5a\xa0\x64\x6e\x96\x69", 18));
    QVERIFY(object(rows, 5).contains("/Width 2 /Height 3"));
    // A soft edge goes straight: colour whole, alpha apart.
    QImage soft = BrushRaster::context(1, 1, false);
    soft.setPixelColor(0, 0, QColor(255, 0, 0, 128));
    const QByteArray edge = ImageExporter::pdfData(ExportRaster{soft, 72});
    QCOMPARE(streamOf(object(edge, 5)), QByteArray("\xff\x00\x00", 3));
    QCOMPARE(streamOf(object(edge, 6)), QByteArray("\x80", 1));
    // At 72 per inch a pixel is a point.
    QVERIFY(object(ImageExporter::pdfData(snapshot(72)), 3).contains("/MediaBox [0 0 6.0000 6.0000]"));
    QVERIFY(object(ImageExporter::pdfData(snapshot(0)), 3).contains("/MediaBox [0 0 6.0000 6.0000]"));
}

void ExportPDFTests::aBackgroundFillsThePageUnderThePicture()
{
    const ExportRaster raster = ImageExporter::render(snapshot(300));
    const QByteArray pdf = ImageExporter::pdfData(raster, QColor(255, 128, 0));
    QCOMPARE(streamOf(object(pdf, 4)), QByteArray("q\n1.0000 0.5020 0.0000 rg 0 0 1.4400 1.4400 re f\n1.4400 0 0 1.4400 0 0 cm /Im0 Do\nQ"));
}

void ExportPDFTests::anOversizedCanvasIsRefused()
{
    const ProjectSnapshot huge{.manifest = {.documentID = QUuid::createUuid(), .width = 30'000, .height = 30'000, .activeLayerID = std::nullopt, .layers = {}}, .images = {}};
    QVERIFY_THROWS_EXCEPTION(ExportError, ImageExporter::pdfData(huge));
}

void ExportPDFTests::resizingKeepsTheResolutionWithinTheLimits()
{
    QImage pair = BrushRaster::context(2, 1, false);
    pair.setPixelColor(0, 0, Qt::black);
    pair.setPixelColor(1, 0, Qt::white);
    const ExportRaster raster{pair, 300};
    // The same size is the same raster.
    QCOMPARE(ImageExporter::resized(raster, 2, 1).image.cacheKey(), pair.cacheKey());
    const ExportRaster half = ImageExporter::resized(raster, 1, 1);
    QVERIFY(half.image.size() == QSize(1, 1) && half.resolution == 300);
    QVERIFY(std::abs(half.image.pixelColor(0, 0).red() - 128) <= 1);
    QCOMPARE(ImageExporter::resized(raster, 8, 3).image.size(), QSize(8, 3));
    for (const QSize wrong : {QSize(0, 1), QSize(1, 0), QSize(30'001, 1), QSize(1, 30'001), QSize(20'000, 20'000)})
        QVERIFY_THROWS_EXCEPTION(ExportError, ImageExporter::resized(raster, wrong.width(), wrong.height()));
}

void ExportPDFTests::flatteningLaysThePictureOverItsBackground()
{
    QImage half = BrushRaster::context(1, 1, false);
    half.setPixelColor(0, 0, QColor(255, 0, 0, 128));
    const QImage flat = ImageExporter::flattened({half, 72}, QColor(0, 0, 255));
    QVERIFY(!flat.hasAlphaChannel());
    const QColor mixed = flat.pixelColor(0, 0);
    QVERIFY2(std::abs(mixed.red() - 128) <= 1 && mixed.green() == 0 && std::abs(mixed.blue() - 127) <= 1, qPrintable(mixed.name()));
}

QTEST_MAIN(ExportPDFTests)
#include "ExportPDFTests.moc"
