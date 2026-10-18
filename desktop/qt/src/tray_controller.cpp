#include "tray_controller.h"

#include "app_controller.h"

#include <QAction>
#include <QColor>
#include <QIcon>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QWindow>
#include <algorithm>

TrayController::TrayController(AppController* controller,
                               QWindow* window,
                               QObject* parent)
    : QObject(parent),
      controller_(controller),
      window_(window),
      tray_(nullptr),
      menu_(new QMenu()) {
    ensureTrayIcon();
    // macOS 菜单栏服务有时要等到事件循环启动后才可用；启动时重试，
    // 避免应用已运行但没有状态栏图标。
    QTimer::singleShot(800, this, &TrayController::ensureTrayIcon);
    QTimer::singleShot(2500, this, &TrayController::ensureTrayIcon);
    connect(menu_, &QMenu::aboutToShow, this, [this] { menuOpen_ = true; });
    connect(menu_, &QMenu::aboutToHide, this, [this] {
        menuOpen_ = false;
        if (menuRebuildPending_) {
            menuRebuildPending_ = false;
            scheduleMenuRebuild();
        }
    });
    connect(controller_->sessionModel(), &QAbstractItemModel::dataChanged,
            this, [this] { scheduleMenuRebuild(); });
    connect(controller_->sessionModel(), &QAbstractItemModel::rowsInserted,
            this, [this] { scheduleMenuRebuild(); });
    connect(controller_->sessionModel(), &QAbstractItemModel::rowsRemoved,
            this, [this] { scheduleMenuRebuild(); });
    connect(controller_->sessionModel(), &QAbstractItemModel::modelReset,
            this, [this] { scheduleMenuRebuild(); });
    connect(controller_->sessionModel(), &SessionListModel::orderChanged,
            this, [this] { scheduleMenuRebuild(); });
    rebuildMenu();
}

TrayController::~TrayController() {
    hide();
    if (tray_) tray_->setContextMenu(nullptr);
    delete menu_;
}

QMenu* TrayController::menu() const {
    return menu_;
}

void TrayController::hide() {
    hidden_ = true;
    if (tray_) tray_->hide();
}

bool TrayController::recoverTrayIcon() {
    if (hidden_ || controller_->quitting()) return false;
    if (menuOpen_ || menu_->isVisible()) {
        QTimer::singleShot(120, this, [this] { recoverTrayIcon(); });
        return false;
    }
    if (tray_ && tray_->isVisible()) {
        controller_->setTrayAvailable(true);
        return true;
    }
    if (tray_) {
        tray_->setContextMenu(nullptr);
        tray_->hide();
        delete tray_;
        tray_ = nullptr;
    }
    controller_->setTrayAvailable(false);
    ensureTrayIcon();
    return tray_ != nullptr;
}

void TrayController::scheduleTrayRecovery() {
    if (hidden_ || controller_->quitting()) return;
    for (const int delay : {0, 250, 1000, 2500}) {
        QTimer::singleShot(delay, this, [this] { recoverTrayIcon(); });
    }
}

void TrayController::ensureTrayIcon() {
    if (hidden_ || controller_->quitting()) return;
    if (tray_) {
        tray_->setVisible(true);
        controller_->setTrayAvailable(true);
        return;
    }
    if (!QSystemTrayIcon::isSystemTrayAvailable()) return;

    tray_ = new QSystemTrayIcon(this);
    QIcon icon(QStringLiteral(":/openvpn/app-icon.png"));
    if (icon.isNull()) icon = QIcon::fromTheme(QStringLiteral("network-vpn"));
    if (icon.isNull()) {
        // 资源未随测试目标或极简安装包复制时，仍提供一个可见的备用图标。
        QPixmap pixmap(32, 32);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(QStringLiteral("#4c76c8")));
        painter.drawRoundedRect(QRectF(2, 2, 28, 28), 8, 8);
        painter.setPen(QPen(Qt::white, 2.5, Qt::SolidLine, Qt::RoundCap,
                            Qt::RoundJoin));
        painter.drawLine(QPointF(10, 21), QPointF(16, 15));
        painter.drawLine(QPointF(16, 15), QPointF(22, 9));
        painter.drawLine(QPointF(16, 15), QPointF(22, 15));
        icon = QIcon(pixmap);
    }
    tray_->setIcon(icon);
    tray_->setToolTip(QStringLiteral("OpenVPN"));
    tray_->setContextMenu(menu_);
    connect(tray_, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
                // macOS 单击由系统打开菜单，不能再抢回主窗口焦点。
                if (reason == QSystemTrayIcon::DoubleClick
#ifndef Q_OS_MACOS
                    || reason == QSystemTrayIcon::Trigger
#endif
                ) {
                    showWindow();
                }
            });
    tray_->show();
    tray_->setVisible(true);
    controller_->setTrayAvailable(true);
}

void TrayController::scheduleMenuRebuild() {
    if (menuOpen_ || menu_->isVisible()) {
        menuRebuildPending_ = true;
        return;
    }
    if (menuRebuildScheduled_) return;
    menuRebuildScheduled_ = true;
    QTimer::singleShot(0, this, [this] {
        menuRebuildScheduled_ = false;
        if (menuOpen_ || menu_->isVisible()) {
            menuRebuildPending_ = true;
            return;
        }
        rebuildMenu();
    });
}

void TrayController::showWindow() {
    if (!window_) return;
    window_->show();
    window_->raise();
    window_->requestActivate();
}

void TrayController::rebuildMenu() {
    if (!summaryAction_) {
    QAction* open = menu_->addAction(QStringLiteral("打开 OpenVPN"));
    connect(open, &QAction::triggered, this, [this] { showWindow(); });
    menu_->addSeparator();
    summaryAction_ = menu_->addAction(QStringLiteral("当前没有活动连接"));
    summaryAction_->setEnabled(false);
    sessionsEnd_ = menu_->addSeparator();
    QAction* refresh = menu_->addAction(QStringLiteral("刷新服务状态"));
    connect(refresh, &QAction::triggered, controller_, &AppController::refreshService);
    QAction* settings = menu_->addAction(QStringLiteral("桌面设置"));
    connect(settings, &QAction::triggered, this, [this] { showWindow(); controller_->showSettings(); });
    QAction* about = menu_->addAction(QStringLiteral("关于"));
    connect(about, &QAction::triggered, this, [this] { showWindow(); controller_->showAbout(); });
    QAction* quit = menu_->addAction(QStringLiteral("退出"));
    connect(quit, &QAction::triggered, this, [this] { showWindow(); controller_->quitApplication(); });
    }

    const QList<QJsonObject> sessions = controller_->sessionModel()->sessions();
    const int activeCount = std::count_if(sessions.cbegin(), sessions.cend(), [](const auto& session) {
        return session.value(QStringLiteral("state")) == QStringLiteral("connected");
    });
    summaryAction_->setText(activeCount
        ? QStringLiteral("%1 个连接活动中").arg(activeCount)
        : QStringLiteral("当前没有活动连接"));
    QStringList order;
    if (sessions.isEmpty()) {
        summaryAction_->setText(QStringLiteral("暂无已导入配置"));
    } else {
        for (const QJsonObject& session : sessions) {
            const QString id = session.value(QStringLiteral("sessionId")).toString();
            const QString name = session.value(QStringLiteral("name"))
                                     .toString(QStringLiteral("未命名配置"));
            const QString state = session.value(QStringLiteral("state")).toString();
            QString label;
            if (state == QStringLiteral("connected")) {
                label = QStringLiteral("断开：") + name;
            } else if (state == QStringLiteral("connecting")) {
                label = QStringLiteral("取消连接：") + name;
            } else if (state == QStringLiteral("disconnecting")) {
                label = QStringLiteral("处理中：") + name;
            } else {
                label = QStringLiteral("连接：") + name;
            }
            order.append(id);
            QAction* action = sessionActions_.value(id);
            if (!action) {
                action = new QAction(menu_);
                sessionActions_.insert(id, action);
                auto* info = new QAction(menu_);
                info->setEnabled(false);
                infoActions_.insert(id, info);
                connect(action, &QAction::triggered, this, [this, id] {
                    controller_->toggleConnection(id);
                });
            }
            action->setText(label);
            action->setData(id);
            action->setEnabled(state != QStringLiteral("disconnecting"));
            auto* info = infoActions_.value(id);
            info->setVisible(state == QStringLiteral("connected"));
            if (state == QStringLiteral("connected")) {
                const auto bytes = [](double value) {
                    return value < 1048576 ? QString::number(value / 1024, 'f', 1) + QStringLiteral(" KB")
                                          : QString::number(value / 1048576, 'f', 1) + QStringLiteral(" MB");
                };
                const int seconds = session.value(QStringLiteral("durationSeconds")).toInt();
                info->setText(QStringLiteral("    %1:%2  ↓ %3  ↑ %4")
                    .arg(seconds / 60).arg(seconds % 60, 2, 10, QLatin1Char('0'))
                    .arg(bytes(session.value(QStringLiteral("downloadBytes")).toDouble()),
                         bytes(session.value(QStringLiteral("uploadBytes")).toDouble())));
            }
        }
    }

    // NSMenu 先关闭，再异步投递选择动作。流量或状态刷新只能更新现有
    // QAction；即便菜单已经关闭，也不能销毁仍可能待触发的动作对象。
    for (const auto& id : sessionActions_.keys()) {
        if (order.contains(id)) continue;
        auto* action = sessionActions_.take(id);
        auto* info = infoActions_.take(id);
        menu_->removeAction(action);
        menu_->removeAction(info);
        action->deleteLater();
        info->deleteLater();
    }
    if (order != sessionOrder_) {
        for (const auto& id : order) {
            menu_->removeAction(sessionActions_.value(id));
            menu_->removeAction(infoActions_.value(id));
            menu_->insertAction(sessionsEnd_, sessionActions_.value(id));
            menu_->insertAction(sessionsEnd_, infoActions_.value(id));
        }
        sessionOrder_ = order;
    }
}
