/*
 * Copyright (C) 2015 ~ 2018 Deepin Technology Co., Ltd.
 *
 * Author:     Hualet Wang <mr.asianwang@gmail.com
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "ddesktopmenu.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QDebug>
#include <QKeyEvent>
#include <QDBusPendingCall>
#include <QTimer>
#include <QApplication>
#include <QScreen>
#include <QCursor>
#include <QPainter>
#include <QMouseEvent>
#include <QWindow>
#include <functional>
#include <qpa/qplatformscreen.h>

#include "waylandhelper.h"

namespace {

// Wayland下的全屏遮罩：同时作为 xdg_popup 的父 surface。
// 非 grab popup 不会在点击外部时自动关闭，遮罩负责接收菜单外的点击。
// 因为 popup 一定叠在父 surface 之上，遮罩无需额外的 z-order 处理。
class WlMaskWidget : public QWidget {
public:
    std::function<void()> onPress;

protected:
    void mousePressEvent(QMouseEvent *) override {
        if (onPress) {
            onPress();
        }
    }
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.fillRect(rect(), QColor(0, 0, 0, 1));
    }
};

// Wayland下的子菜单：与主菜单同为非 grab xdg_popup。
// QMenu 会自动把子菜单的 transient parent 设为主菜单窗口，
// 这里只需把窗口类型改成 ToolTip 即可走 xdg_popup。
class WlSubMenu : public QMenu {
public:
    explicit WlSubMenu(QWidget *parent) : QMenu(parent) {
        if (WaylandHelper::isWayland()) {
            setWindowFlags(Qt::ToolTip | Qt::FramelessWindowHint);
        }
    }
};

}  // namespace

DDesktopMenu::DDesktopMenu()
    : QMenu()
    , m_monitor(new DRegionMonitor(this))
{
    setAccessibleName("DesktopMenu");

    if (WaylandHelper::isWayland()) {
        // ToolTip 类型 + transient parent 才会被 Qt 当作 xdg_popup 定位。
        setWindowFlags(Qt::ToolTip | Qt::FramelessWindowHint);
    }

    connect(m_monitor, &DRegionMonitor::buttonPress, this, [=] (const QPoint &p) {
        for (auto *menu : m_ownMenus)
            if (menu->geometry().contains(p))
                return;

        QTimer::singleShot(100, this, &DDesktopMenu::hide);
    });
}

DDesktopMenu::~DDesktopMenu()
{
    m_monitor->unregisterRegion();
    releaseKeyboard();
    if (m_wlMask) {
        m_wlMask->deleteLater();
        m_wlMask = nullptr;
    }
}

void DDesktopMenu::setItems(QJsonArray items)
{
    addActionFromJson(this, items);
}

void DDesktopMenu::setItemActivity(const QString &itemId, bool isActive)
{
    QAction *action = this->action(itemId);
    if (action) {
        action->setEnabled(isActive);
    }
}

void DDesktopMenu::setItemChecked(const QString &itemId, bool checked)
{
    QAction *action = this->action(itemId);
    if (action) {
        action->setChecked(checked);
    }
}

void DDesktopMenu::setItemText(const QString &itemId, const QString &text)
{
    QAction *action = this->action(itemId);
    if (action) {
        action->setText(text);
    }
}

void DDesktopMenu::showMenu(const QPoint pos, bool isScaled)
{
    QPoint handlePos = pos;

    if (!WaylandHelper::isWayland()) {
        // X11下走原逻辑
        // 因为.dde_env已经不包含qt的缩放环境变量，所以收到的都是原始坐标
        if (isScaled) {
            handlePos = pos * devicePixelRatioF();
        }
        QList<QScreen *> oldList = qApp->screens();
        for (auto it = oldList.constBegin(); it != oldList.constEnd(); ++it) {
            QScreen const * currentScreen = (*it);
            QRect rect = currentScreen->handle()->geometry();
            const QPoint point = rect.topLeft();

            if (rect.contains(handlePos)) {
                // 保持原始的topleft和在当前屏幕内坐标的偏移就可以正常显示了
                QMenu::popup(QPoint(rect.topLeft() + (handlePos - point) / devicePixelRatioF()));
                break;
            }
        }
        return;
    }

    // Wayland下走标准 xdg_popup。D-Bus 服务拿不到 input serial，
    // 用 ToolTip(非 grab popup) + 隐藏父 surface 的方式精确摆放。
    //
    // 客户端(gxde-terminal 等 GTK 程序)发送的是 GDK「设备像素」坐标：
    //   x = event.x_root * gtk_scale / dde_scale
    // Wayland 下 gtk_scale=2(整数 buffer scale)，而 dde_scale 读的是
    // QT_SCALE_FACTOR/QT_FONT_DPI(本机均未设置，取 1.0)，于是收到的是
    // 逻辑坐标的 2 倍。故除以 devicePixelRatioF()(=2) 换回逻辑坐标。
    handlePos = QPoint(qRound(pos.x() / devicePixelRatioF()),
                       qRound(pos.y() / devicePixelRatioF()));
    ensurePolished();
    const QSize sz = sizeHint();

    QScreen* scr = qApp->screenAt(handlePos);
    if (!scr) {
        scr = qApp->primaryScreen();
    }

    const QMargins menuMargins = contentsMargins();
    QPoint topLeft = handlePos
        - QPoint(menuMargins.left(), menuMargins.top());
    if (scr) {
        const QRect g = scr->geometry();
        topLeft.setX(qBound(g.left(), topLeft.x(), qMax(g.left(),
            g.right() - sz.width() + 1)));
        topLeft.setY(qBound(g.top(), topLeft.y(), qMax(g.top(),
            g.bottom() - sz.height() + 1)));
    }

    // 全屏遮罩兼作父 surface。
    if (!m_wlMask) {
        WlMaskWidget *mask = new WlMaskWidget;
        mask->setAttribute(Qt::WA_TranslucentBackground);
        mask->setWindowFlags(Qt::FramelessWindowHint |
            Qt::WindowDoesNotAcceptFocus);
        mask->onPress = [this] { hide(); };
        m_wlMask = mask;
    }

    if (scr) {
        m_wlMask->createWinId();
        m_wlMask->windowHandle()->setScreen(scr);
    }
    m_wlMask->showFullScreen();

    WaylandHelper::attachAsPopup(this, m_wlMask->windowHandle());

    resize(sz);
    QMenu::popup(topLeft);
}

void DDesktopMenu::showEvent(QShowEvent *e)
{
    QMenu::showEvent(e);

    // Wayland下DRegionMonitor收不到全局点击，由全屏遮罩负责「点击外部关闭」
    if (!WaylandHelper::isWayland()) {
        // X11保持原版逻辑
        m_monitor->registerRegion();
    }

    QTimer::singleShot(100, this, [=] {
        if (!isVisible()) {
            return;
        }
        activateWindow();
        grabKeyboard();
    });
}

void DDesktopMenu::hideEvent(QHideEvent *e)
{
    QMenu::hideEvent(e);

    if (m_wlMask) {
        m_wlMask->hide();
    }
    m_monitor->unregisterRegion();
    releaseKeyboard();
}

void DDesktopMenu::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        hide();
    }

    QMenu::keyPressEvent(event);
}

QAction *DDesktopMenu::action(const QString &id)
{
    for (QAction *action : actions()) {
        if (action->property("itemId") == id) {
            return action;
        }
    }

    return nullptr;
}

void DDesktopMenu::addActionFromJson(QMenu *menu, const QJsonArray &items)
{
    m_ownMenus << menu;

    foreach (QJsonValue item, items) {
        QJsonObject itemObj = item.toObject();
        const QString itemText = itemObj["itemText"].toString().replace("_", QString()).replace(QRegularExpression("\\([^)]+\\)"), QString());
        const QString itemIcon = itemObj["itemIcon"].toString();

        const QJsonObject subMenuJson = itemObj["itemSubMenu"].toObject();
        const QJsonArray subMenuItemsJson = subMenuJson["items"].toArray();

        QAction *action = nullptr;
        if (subMenuItemsJson.count()) {

            // Wayland下用WlSubMenu，X11用普通QMenu
            QMenu *subMenu = WaylandHelper::isWayland()
                ? static_cast<QMenu *>(new WlSubMenu(menu))
                : new QMenu(menu);
            action = menu->addMenu(subMenu);
            addActionFromJson(subMenu, subMenuItemsJson);
        } else if (itemText.isEmpty()) {
            menu->addSeparator();
            continue;
        } else {
            action = new QAction(menu);
            menu->addAction(action);
        }

        action->setText(itemText);
        action->setIcon(QIcon(itemIcon));

        action->setEnabled(itemObj["isActive"].toBool());
        action->setCheckable(itemObj["isCheckable"].toBool());
        action->setChecked(itemObj["checked"].toBool());

        action->setProperty("itemId", itemObj["itemId"].toString());

        connect(action, &QAction::triggered, menu, [=] (const bool checked) {
            const QString id = action->property("itemId").toString();

            releaseFocus();
            releaseMouse();
            releaseKeyboard();
            emit itemClicked(id, checked);

            hide();
        });
    }
}
