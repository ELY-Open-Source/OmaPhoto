#include "AddressSpaceLimit.h"
#include "Document/BrushStroke.h"
#include "Document/EditorSession.h"
#include "Document/Filters.h"
#include <QtTest>
#include <malloc.h>

// What a8630e4 spares: a second measure, a second picture.
class CameraRawFoldFailureTests : public QObject {
    Q_OBJECT
private slots:
    void aMeasuredSourceIsNotDrawnAgain();
    void theScopeReadsPremultipliedBytesInPlace();
};

void CameraRawFoldFailureTests::aMeasuredSourceIsNotDrawnAgain()
{
    QImage gray = BrushRaster::context(2048, 2048, false);
    gray.fill(QColor(64, 64, 64));
    EditorSession session;
    session.createDocument(2048, 2048);
    session.insert(ImportedImage(gray, gray.scaled(96, 96), QStringLiteral("Gray")));
    session.beginFilter(FilterKind::cameraRaw);
    const FilterEdit &edit = session.filterEdit().value();
    QCOMPARE(edit.previewSource.size(), QSize(2048, 2048));
    QVERIFY(std::abs(edit.previewBrightness().brightest - 64) < 0.01);
    double again = 0;
    {
        // No room for a 16 MB redraw: the reading stays.
        const AddressSpaceLimit limit(2ll << 20);
        again = edit.previewBrightness().brightest;
    }
    QVERIFY(std::abs(again - 64) < 0.01);
}

void CameraRawFoldFailureTests::theScopeReadsPremultipliedBytesInPlace()
{
    QImage gray = BrushRaster::context(4096, 4096, false);
    gray.fill(QColor(64, 64, 64));
    std::optional<CameraRawScope> scope;
    {
        // A 64 MB copy cannot fit; bytes read in place.
        const AddressSpaceLimit limit(2ll << 20);
        scope = CameraRawScope::make(gray);
    }
    QCOMPARE(scope.value().red[64], 2048.0 * 2048.0);
}

int main(int argc, char **argv)
{
    // Large asks map memory the limit counts.
    mallopt(M_MMAP_THRESHOLD, 64 * 1024);
    QApplication app(argc, argv);
    CameraRawFoldFailureTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "CameraRawFoldFailureTests.moc"
