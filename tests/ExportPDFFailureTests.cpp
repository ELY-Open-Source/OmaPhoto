#include "AddressSpaceLimit.h"
#include "IO/ImageExporter.h"
#include <QtEndian>
#include <QtTest>
#include <malloc.h>

// A starved PDF throws; it never comes back broken.
class ExportPDFFailureTests : public QObject {
    Q_OBJECT
private slots:
    void aStarvedPDFThrowsOrIsWhole();
    void aStarvedPNGThrowsOrIsWhole();
};

namespace {
// Each Flate stream's bytes, inflated.
QList<QByteArray> inflated(const QByteArray &pdf)
{
    QList<QByteArray> streams;
    for (qsizetype at = pdf.indexOf("/FlateDecode"); at >= 0; at = pdf.indexOf("/FlateDecode", at + 1)) {
        const qsizetype begin = pdf.indexOf(">>\nstream\n", at) + 10, end = pdf.indexOf("\nendstream", begin);
        QByteArray sized(4, 0);
        qToBigEndian(quint32(64 << 20), sized.data());
        streams << qUncompress(sized + pdf.mid(begin, end - begin));
    }
    return streams;
}
}

void ExportPDFFailureTests::aStarvedPDFThrowsOrIsWhole()
{
    // Noise, so the compressed streams stay near their size.
    QImage noise(360, 360, QImage::Format_RGBA8888_Premultiplied);
    quint32 seed = 7;
    for (int y = 0; y < noise.height(); ++y) {
        auto *row = reinterpret_cast<quint32 *>(noise.scanLine(y));
        for (int x = 0; x < noise.width(); ++x)
            row[x] = (seed = seed * 1664525u + 1013904223u) | 0xff000000u;
    }
    const ExportRaster raster{noise, 72};
    // Under 2^17 pixels Qt converts on this thread alone.
    ImageExporter::pdfData(raster);
    int refused = 0, whole = 0;
    for (qint64 kilobytes = 64; kilobytes <= 4096; kilobytes += 32) {
        QByteArray pdf;
        try {
            const AddressSpaceLimit limit(kilobytes << 10);
            pdf = ImageExporter::pdfData(raster);
        } catch (const ExportError &error) {
            QCOMPARE(error.kind, ExportError::Kind::render);
            ++refused;
            continue;
        }
        const QList<QByteArray> streams = inflated(pdf);
        QCOMPARE(streams.size(), qsizetype(2));
        QVERIFY2(streams[0].size() == 360 * 360 * 3 && streams[1].size() == 360 * 360, qPrintable(QString::number(kilobytes)));
        ++whole;
    }
    // The sweep reaches both sides of the limit.
    QVERIFY(refused > 0 && whole > 0);
}

// Export As encodes PNG on a worker that catches ExportError.
void ExportPDFFailureTests::aStarvedPNGThrowsOrIsWhole()
{
    QImage noise(360, 360, QImage::Format_RGBA8888_Premultiplied);
    quint32 seed = 11;
    for (int y = 0; y < noise.height(); ++y) {
        auto *row = reinterpret_cast<quint32 *>(noise.scanLine(y));
        for (int x = 0; x < noise.width(); ++x)
            row[x] = (seed = seed * 1664525u + 1013904223u) | 0xff000000u;
    }
    const ExportRaster raster{noise, 72};
    const QByteArray full = ImageExporter::pngData(raster);
    int refused = 0, whole = 0;
    for (qint64 kilobytes = 64; kilobytes <= 4096; kilobytes += 32) {
        QByteArray png;
        try {
            const AddressSpaceLimit limit(kilobytes << 10);
            png = ImageExporter::pngData(raster);
        } catch (const ExportError &error) {
            QVERIFY(error.kind == ExportError::Kind::render || error.kind == ExportError::Kind::encode);
            ++refused;
            continue;
        }
        QCOMPARE(png, full);
        ++whole;
    }
    QVERIFY(refused > 0 && whole > 0);
}

int main(int argc, char **argv)
{
    // Large asks map memory the limit counts.
    mallopt(M_MMAP_THRESHOLD, 64 * 1024);
    QCoreApplication app(argc, argv);
    ExportPDFFailureTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "ExportPDFFailureTests.moc"
