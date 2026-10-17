#pragma once

#include <QObject>
#include <QHash>
#include <QStringList>

class AppController;
class QAction;
class QMenu;
class QSystemTrayIcon;
class QWindow;

class TrayController final : public QObject {
public:
    TrayController(AppController* controller,
                   QWindow* window,
                   QObject* parent = nullptr);
    ~TrayController() override;

    QMenu* menu() const;
    void hide();

private:
    void ensureTrayIcon();
    void scheduleMenuRebuild();
    void showWindow();
    void rebuildMenu();

    AppController* controller_;
    QWindow* window_;
    QSystemTrayIcon* tray_;
    QMenu* menu_;
    QAction* summaryAction_ = nullptr;
    QAction* sessionsEnd_ = nullptr;
    QHash<QString, QAction*> sessionActions_;
    QHash<QString, QAction*> infoActions_;
    QStringList sessionOrder_;
    bool menuRebuildScheduled_ = false;
    bool menuRebuildPending_ = false;
    bool menuOpen_ = false;
    bool hidden_ = false;
};
