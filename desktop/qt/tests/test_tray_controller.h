#pragma once

#include <QObject>

class TrayControllerTest final : public QObject {
    Q_OBJECT

private slots:
    void sessionActionTriggersControllerBeforeMenuRefresh();
    void nativeMenuKeepsActionsAliveUntilItCloses();
    void secondSessionAndWindowActionsWork();
    void recreatesTrayItemAfterMenuBarRefresh();
};
