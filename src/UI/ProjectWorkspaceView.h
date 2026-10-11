#pragma once
#include "ContentView.h"
#include "Document/ProjectWorkspace.h"
#include "UI/CompositorMenus.h"
#include "UI/ProjectTabs.h"
#include <QMainWindow>
#include <QToolBar>

// The window: the front tab's editor under the tab strip.
class ProjectWorkspaceView : public QMainWindow {
    Q_OBJECT
public:
    explicit ProjectWorkspaceView(ProjectWorkspace &workspace, QWidget *parent = nullptr);
    ~ProjectWorkspaceView() override;

    ContentView *content() const { return m_content; }
    ProjectTabStrip *tabs() const { return m_tabs; }
    // Canvas Only: the window fills the screen, bars away.
    void toggleCanvasOnly();

protected:
    void closeEvent(QCloseEvent *event) override;
    // Escape leaves fullscreen once nothing is left to cancel.
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void synchronize();
    void synchronizeSession();
    void watchFront();

    ProjectWorkspace &m_workspace;
    QToolBar *const m_toolbar;
    ProjectTabStrip *const m_tabs;
    QAction *m_newCanvas = nullptr;
    QAction *m_fit = nullptr;
    QAction *m_actualPixels = nullptr;
    QAction *m_zoomIn = nullptr;
    QAction *m_zoomOut = nullptr;
    ContentView *m_content = nullptr;
    // Held until its editor goes, as Swift's views hold sessions.
    std::shared_ptr<ProjectTab> m_shownTab;
    // The window's state before Canvas Only, to put back.
    std::optional<Qt::WindowStates> m_beforeCanvasOnly;
    QMetaObject::Connection m_sessionWatch;
};
