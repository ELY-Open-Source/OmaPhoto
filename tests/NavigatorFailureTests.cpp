#include "AddressSpaceLimit.h"
#include "Document/EditorSession.h"
#include "Rendering/RasterSnapshot.h"
#include "UI/NavigatorMinimap.h"
#include <QSettings>
#include <QStandardPaths>
#include <QtTest>
#include <malloc.h>

// The Navigator when a painted layer cannot flatten.
class NavigatorFailureTests : public QObject {
    Q_OBJECT
private slots:
    void aFlattenThatFindsNoMemoryDrawsNoPicture();
};

void NavigatorFailureTests::aFlattenThatFindsNoMemoryDrawsNoPicture()
{
    QStandardPaths::setTestModeEnabled(true);
    NavigatorMinimap::setVisibleSetting(true);
    // 4000 by 3000 painted from two pixels: 48 MB flat.
    QImage base(2, 2, QImage::Format_RGBA8888_Premultiplied);
    base.fill(Qt::red);
    EditorSession session;
    session.createDocument(4000, 3000);
    session.insert(ImportedImage(std::make_shared<const RasterSnapshot>(4000, 3000, base, QRectF(0, 0, 2, 2), std::vector<BrushPatch>{}), QImage(),
                                 QStringLiteral("Painted")));
    session.viewport.resize(QSizeF(800, 600), 1, QSizeF(4000, 3000));
    std::optional<QImage> starved;
    {
        const AddressSpaceLimit limit(8ll << 20);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("the navigator's picture could not be made"));
        starved = session.navigatorImage(300);
        // The minimap shows without a picture, never throwing.
        NavigatorMinimap minimap(session);
        session.zoom(4, session.viewport.center());
        QVERIFY(!minimap.isHidden() && minimap.picture().isNull());
    }
    QVERIFY(!starved);
    // With room again it draws: the base's corner, then clear.
    const QImage drawn = session.navigatorImage(300).value();
    QVERIFY(drawn.size() == QSize(300, 225) && drawn.pixelColor(150, 100).alpha() == 0);
    QSettings().remove(QLatin1String(NavigatorMinimap::visibleKey));
}

int main(int argc, char **argv)
{
    // Large asks map memory the limit counts.
    mallopt(M_MMAP_THRESHOLD, 64 * 1024);
    QApplication app(argc, argv);
    NavigatorFailureTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "NavigatorFailureTests.moc"
