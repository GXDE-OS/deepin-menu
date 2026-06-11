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
#include <QDebug>
#include <QKeyEvent>
#include <QDBusPendingCall>
#include <QTimer>
#include <QApplication>
#include <QScreen>
#include <QPainter>
#include <QMouseEvent>
#include <functional>
#include <qpa/qplatformscreen.h>

#include "waylandhelper.h"

namespace {

// Wayland下的全屏遮罩
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

}  // namespace

DDesktopMenu::DDesktopMenu()
    : QMenu()
    , m_monitor(new DRegionMonitor(this))
{
    setAccessibleName("DesktopMenu");

    // NOTE(hualet): don't change those window flags, if you delete below line, deepin-menu
    // won't even show working with deepin-terminal2 and dde-launcher.
    setWindowFlags(Qt::WindowStaysOnTopHint | Qt::Tool);

    // Wayland下调整半透明并设置圆角
    // DDockMenu通过DMenuBase已设置WA_TranslucentBackground
    // 而DDesktopMenu缺少此属性导致Wayland buffer为XRGB格式
    // 缺少的alpha通道似乎导致四角无法透明
    if (WaylandHelper::isWayland()) {
        setAttribute(Qt::WA_TranslucentBackground);
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
    if (isScaled) {
        handlePos = pos * devicePixelRatioF();
    }

    if (!WaylandHelper::isWayland()) {
        // X11下走原逻辑
        // 因为.dde_env已经不包含qt的缩放环境变量，所以收到的都是原始坐标
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

    // Wayland下没有父surface的QMenu，xdg_popup没法用（定位错乱问题）
    // 备用workaround: 用layer-shell把菜单锚定到(x,y)，并加全屏遮罩
    // 遮罩视为菜单外部，点击遮罩等效于「点击菜单外部关闭」
    // 菜单外观一律交给Chameleon/「云璃」平台样式
    ensurePolished();
    const QSize sz = sizeHint();

    QScreen* scr = qApp->screenAt(handlePos);
    if (!scr) {
        scr = qApp->primaryScreen();
    }

    QPoint topLeft = handlePos;
    if (scr) {
        const QRect g = scr->geometry();
        topLeft.setX(qBound(g.left(), topLeft.x(), qMax(g.left(),
            g.right() - sz.width())));
        topLeft.setY(qBound(g.top(), topLeft.y(), qMax(g.top(),
            g.bottom() - sz.height())));
    }

    // 全屏遮罩先于菜单建好显示，防止层级错位
    if (!m_wlMask) {
        WlMaskWidget *mask = new WlMaskWidget;
        mask->setAttribute(Qt::WA_TranslucentBackground);
        mask->setWindowFlags(Qt::FramelessWindowHint |
            Qt::WindowDoesNotAcceptFocus);
        mask->onPress = [this] { hide(); };
        m_wlMask = mask;
    }

    if (scr) {
        m_wlMask->setGeometry(scr->geometry());
    }

    WaylandHelper::setFullscreenMaskRole(m_wlMask);
    m_wlMask->show();

    // 先用非零尺寸建好带anchor的layer表面，再处理弹出菜单
    createWinId();
    resize(sz);
    WaylandHelper::setMenuLayerRole(this, topLeft);

    QMenu::popup(topLeft);
}

void DDesktopMenu::showEvent(QShowEvent *e)
{
    QMenu::showEvent(e);

    // Wayland(下DRegionMonitor收不到全局点击，由全屏遮罩负责「点击外部关闭」
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
        const QString itemText = itemObj["itemText"].toString().replace("_", QString()).replace(QRegExp("\\([^)]+\\)"), QString());
        const QString itemIcon = itemObj["itemIcon"].toString();

        const QJsonObject subMenuJson = itemObj["itemSubMenu"].toObject();
        const QJsonArray subMenuItemsJson = subMenuJson["items"].toArray();

        QAction *action = nullptr;
        if (subMenuItemsJson.count()) {

            QMenu *subMenu = new QMenu(menu);
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
