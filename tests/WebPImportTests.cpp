#include "Document/EditorSession.h"
#include "IO/ImageFileDrop.h"
#include "IO/ImageImporter.h"
#include <QColorSpace>
#include <QMimeData>
#include <QTemporaryDir>
#include <QtTest>

// Swift 1.4.9's WebP import (`5724099`).
class WebPImportTests : public QObject {
    Q_OBJECT
private slots:
    void webPImportPreservesTransparency_data();
    void webPImportPreservesTransparency();
    void aDroppedPictureTakesWebPAfterTIFF();
};

void WebPImportTests::webPImportPreservesTransparency_data()
{
    // 64 by 32: red left, clear right.
    QTest::addColumn<QByteArray>("encoded");
    QTest::newRow("lossless") << QByteArray("UklGRiYAAABXRUJQVlA4TBkAAAAvP8AHEA8Q8x/zHwyxYDJ/6RB6I/ofVfANAA==");
    QTest::newRow("lossy") << QByteArray("UklGRoYAAABXRUJQVlA4WAoAAAAQAAAAPwAAHwAAQUxQSBQAAAABDzD/ERFCLJjMXzqE3oj+RxV8A1ZQOCBMAAAA8AMAnQEqQAAgAD4xFolDIiEhFgQAIAMEsYBmu0A/AD8AADKMNAliwAD+96KX/9eiUqpf3sP/+Fmfwsz+Fmf/Cc2fv90UD4A2eDkAAA==");
}

void WebPImportTests::webPImportPreservesTransparency()
{
    QFETCH(QByteArray, encoded);
    const QByteArray bytes = QByteArray::fromBase64(encoded);
    QTemporaryDir folder;
    // The bytes decide, as everywhere: no suffix needed.
    const QString path = folder.filePath(QStringLiteral("picture"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size());
    file.close();
    const ImportedImage result = ImageImporter::decode(path);
    const QImage image = result.image();
    QCOMPARE(image.size(), QSize(64, 32));
    QCOMPARE(image.colorSpace(), QColorSpace(QColorSpace::SRgb));
    QCOMPARE(image.format(), QImage::Format_RGBA8888_Premultiplied);
    QVERIFY(result.thumbnail.width() <= 96 && result.thumbnail.height() <= 96);
    const uchar *row = image.constScanLine(0);
    QVERIFY(row[0] >= 250 && row[3] == 255);
    QCOMPARE(int(row[63 * 4 + 3]), 0);
    // Past its budget it is too large.
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("^cannot import "));
    try {
        ImageImporter::decode(path, 10);
        QFAIL("an over-budget WebP decoded");
    } catch (const ImageImportError &error) {
        QCOMPARE(error.kind, ImageImportError::Kind::tooLarge);
    }
    // A dropped WebP picture opens a document of its own.
    EditorSession session;
    QMimeData data;
    data.setData(QStringLiteral("image/webp"), bytes);
    bool done = false;
    ImageFileDrop::importProviders(data, session, std::nullopt, nullptr, std::nullopt, [&done] { done = true; });
    QTRY_VERIFY(done);
    QCOMPARE(session.document().value().size(), QSizeF(64, 32));
    QCOMPARE(session.document().value().layers.size(), size_t(1));
    QVERIFY(!session.importError());
}

void WebPImportTests::aDroppedPictureTakesWebPAfterTIFF()
{
    QMimeData data;
    data.setData(QStringLiteral("image/vnd.adobe.photoshop"), "psd");
    data.setData(QStringLiteral("image/webp"), "webp");
    QCOMPARE(ImageFileDrop::providers(data).front()->formats(), QStringList{"image/webp"});
    data.setData(QStringLiteral("image/tiff"), "tiff");
    QCOMPARE(ImageFileDrop::providers(data).front()->formats(), QStringList{"image/tiff"});
}

int main(int argc, char **argv)
{
    // Its drops copy into its own folder: others count theirs.
    QTemporaryDir own;
    qputenv("TMPDIR", own.path().toUtf8());
    QApplication app(argc, argv);
    WebPImportTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "WebPImportTests.moc"
