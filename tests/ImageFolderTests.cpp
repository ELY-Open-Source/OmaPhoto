#include "Document/EditorSession.h"
#include "Document/ProjectWorkspace.h"
#include "IO/ImageImporter.h"
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

// Swift 1.4.10: a folder imports the images directly inside it.
namespace {
void picture(const QString &path, const char *format = "PNG")
{
    QImage image(8, 4, QImage::Format_RGBA8888);
    image.fill(Qt::red);
    if (!image.save(path, format))
        throw std::runtime_error("cannot write the fixture");
}

bool imported(EditorSession &session, const QList<QUrl> &urls)
{
    bool done = false;
    session.importImages(urls, std::nullopt, [&] { done = true; });
    return QTest::qWaitFor([&] { return done && !session.isImporting(); }, 10'000);
}

QStringList names(const EditorSession &session)
{
    QStringList found;
    for (const ImageLayer &layer : session.document().value().layers)
        found << layer.name;
    return found;
}
}

class ImageFolderTests : public QObject {
    Q_OBJECT
private slots:
    void folderImportsTheImagesDirectlyInsideIt();
    void theNameOrderKeepsNumbersInEveryLocale();
    void folderWithoutImagesSaysSo();
    void aFolderAndAFileImportTogether();
    void theWorkspaceOpensAFolderInOneTab();
};

void ImageFolderTests::folderImportsTheImagesDirectlyInsideIt()
{
    QTemporaryDir root;
    QDir folder(root.path());
    QVERIFY(folder.mkpath("More") && folder.mkpath("Project.comp") && folder.mkpath("Other.COMP") && folder.mkpath("Folder.png"));
    picture(folder.filePath("10 Ten.png"));
    picture(folder.filePath("2 Two.jpeg"), "JPEG");
    picture(folder.filePath("3 Three.jpe"), "JPEG");
    picture(folder.filePath("b Bee.PNG"));
    picture(folder.filePath("A Ay.png"));
    picture(folder.filePath(".Hidden.png"));
    picture(folder.filePath("More/Nested.png"));
    QVERIFY(QFile::link(folder.filePath("A Ay.png"), folder.filePath("Link.png")));
    QFile notes(folder.filePath("Notes.txt"));
    QVERIFY(notes.open(QIODevice::WriteOnly) && notes.write("notes") == 5);
    notes.close();
    QVERIFY(QFile::link(folder.filePath("More"), folder.filePath("Linked folder")));
    QVERIFY(ImageFolder::matches(folder.path()));
    QVERIFY(ImageFolder::matches(folder.filePath("More")));
    QVERIFY(!ImageFolder::matches(folder.filePath("Project.comp")));
    QVERIFY(!ImageFolder::matches(folder.filePath("Other.COMP")));
    QVERIFY(!ImageFolder::matches(folder.filePath("Linked folder")));
    QVERIFY(!ImageFolder::matches(folder.filePath("Linked folder/")));
    QVERIFY(!ImageFolder::matches(folder.filePath("Project.comp/")));
    QVERIFY(ImageFolder::matches(folder.filePath("More/")));
    QVERIFY(!ImageFolder::matches(folder.filePath("10 Ten.png")));
    QVERIFY(!ImageFolder::matches(folder.filePath("Missing")));
    EditorSession session;
    QVERIFY(imported(session, {QUrl::fromLocalFile(folder.path())}));
    // Name order: numbers by value, letters in any case.
    QCOMPARE(names(session), (QStringList{"2 Two", "3 Three", "10 Ten", "A Ay", "b Bee"}));
    QCOMPARE(session.importError(), std::nullopt);
    QCOMPARE(session.history.undoCount(), 1);
    QCOMPARE(session.history.undoName(), QString("Import Images"));
}

void ImageFolderTests::theNameOrderKeepsNumbersInEveryLocale()
{
    QTemporaryDir root;
    QDir folder(root.path());
    for (const char *name : {"10", "002", "9", "2", "B", "02", "a", "x2y02", "x02y2", "2A", "02a", "a02", "A2", "A", "b", "é", "E", "a2x", "A02x"})
        picture(folder.filePath(QString::fromUtf8(name) + ".png"));
    for (const QLocale::Language language : {QLocale::C, QLocale::German}) {
        QLocale::setDefault(QLocale(language));
        QStringList found;
        for (const QString &path : ImageFolder::images(folder.path()))
            found << QFileInfo(path).fileName();
        // Swift 6.0.3's localizedStandardCompare sorts them so.
        QCOMPARE(found.join(' '), QString::fromUtf8("2.png 02.png 002.png 2A.png 02a.png 9.png 10.png a.png A.png a02.png A2.png a2x.png A02x.png b.png B.png E.png é.png x2y02.png x02y2.png"));
    }
    QLocale::setDefault(QLocale::c());
}

void ImageFolderTests::folderWithoutImagesSaysSo()
{
    QTemporaryDir root;
    QDir folder(root.path());
    QVERIFY(folder.mkpath("Empty") && folder.mkpath("Notes"));
    QFile notes(folder.filePath("Notes/Notes.txt"));
    QVERIFY(notes.open(QIODevice::WriteOnly) && notes.write("notes") == 5);
    notes.close();
    EditorSession session;
    QVERIFY(imported(session, {QUrl::fromLocalFile(folder.filePath("Notes/"))}));
    QCOMPARE(session.document(), std::nullopt);
    QCOMPARE(session.importError(), QString("“Notes” has no images to import."));
    // Two more: one line each, after what was said before.
    QVERIFY(imported(session, {QUrl::fromLocalFile(folder.filePath("Empty")), QUrl::fromLocalFile(folder.filePath("Notes"))}));
    QCOMPARE(session.importError(),
             QString("“Notes” has no images to import.\n\n“Empty” has no images to import.\n“Notes” has no images to import."));
    session.setImportError(std::nullopt);
    QVERIFY(imported(session, {QUrl::fromLocalFile(folder.filePath("Empty")), QUrl::fromLocalFile(folder.filePath("Notes"))}));
    QCOMPARE(session.importError(), QString("“Empty” has no images to import.\n“Notes” has no images to import."));
    QCOMPARE(session.document(), std::nullopt);
}

void ImageFolderTests::aFolderAndAFileImportTogether()
{
    QTemporaryDir root;
    QDir folder(root.path());
    QVERIFY(folder.mkpath("Empty") && folder.mkpath("Full"));
    picture(folder.filePath("Full/Inside.png"));
    picture(folder.filePath("Alone.png"));
    EditorSession session;
    QVERIFY(imported(session, {QUrl::fromLocalFile(folder.filePath("Alone.png")), QUrl::fromLocalFile(folder.filePath("Empty")),
                               QUrl::fromLocalFile(folder.filePath("Full"))}));
    QCOMPARE(names(session), (QStringList{"Alone", "Inside"}));
    QCOMPARE(session.importError(), QString("“Empty” has no images to import."));
    QCOMPARE(session.history.undoCount(), 1);
}

void ImageFolderTests::theWorkspaceOpensAFolderInOneTab()
{
    QTemporaryDir root;
    QDir folder(root.path());
    QVERIFY(folder.mkpath("Shots"));
    picture(folder.filePath("Shots/1.png"));
    picture(folder.filePath("Shots/2.png"));
    ProjectWorkspace workspace;
    workspace.current().session.createDocument(10, 10);
    const size_t before = workspace.tabs().size();
    bool done = false;
    workspace.receive({QUrl::fromLocalFile(folder.filePath("Shots"))}, std::nullopt, std::nullopt, [&] { done = true; });
    QTRY_VERIFY(done);
    QCOMPARE(workspace.tabs().size(), before + 1);
    QCOMPARE(names(workspace.current().session), (QStringList{"1", "2"}));
}

QTEST_MAIN(ImageFolderTests)
#include "ImageFolderTests.moc"
