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
#include <QPointer>
#include <QPlatformSurfaceEvent>
#include <functional>
#include <qpa/qplatformscreen.h>

#include "waylandhelper.h"

namespace {

// Wayland下的全屏遮罩：layer-shell「top」层，接收菜单外的点击以关闭菜单。
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

// Wayland下的子菜单：与主菜单同为 layer-shell 表面。
// 定位由 treeland_dde_shell 按「父菜单落点 + 偏移」在合成器端计算。
class WlSubMenu : public QMenu {
public:
    explicit WlSubMenu(QWidget *parent) : QMenu(parent) {
        if (WaylandHelper::isWayland()) {
            setWindowFlags(windowFlags() | Qt::FramelessWindowHint);
            setAttribute(Qt::WA_TranslucentBackground);
        }
    }

protected:
    void showEvent(QShowEvent *e) override {
        QMenu::showEvent(e);
        if (WaylandHelper::isWayland()) {
            scheduleSetup(0);
        }
    }

private:
    // 子菜单定位拿不到 Qt 的真实 global geometry，由合成器按偏移计算。
    bool setupWayland() {
        WaylandHelper::setMenuLayerRole(this, QPoint(0, 0));

        QPoint off(width(), 0);  // 兜底：贴在父菜单右侧顶部
        if (QMenu *pm = qobject_cast<QMenu *>(parentWidget())) {
            QRect ar;
            for (QAction *a : pm->actions()) {
                if (a->menu() == this) {
                    ar = pm->actionGeometry(a);
                    break;
                }
            }
            if (!ar.isNull()) {
                off = QPoint(pm->width() - pm->contentsMargins().right(),
                             ar.top());
            }
        }

        return WaylandHelper::placeMenuRelativeToWindow(this, off.x(), off.y());
    }

    // re-show 时 wl_surface 在 show 之后才就绪，逐帧重试直到拿到 surface。
    void scheduleSetup(int attempt) {
        if (attempt > 8) {
            return;
        }
        QPointer<WlSubMenu> self(this);
        QTimer::singleShot(0, this, [self, attempt]() {
            if (!self || !self->isVisible()) {
                return;
            }
            if (!self->setupWayland()) {
                self->scheduleSetup(attempt + 1);
            }
        });
    }
};

}  // namespace

DDesktopMenu::DDesktopMenu()
    : QMenu()
    , m_monitor(new DRegionMonitor(this))
{
    setAccessibleName("DesktopMenu");

    if (WaylandHelper::isWayland()) {
        // layer-shell 只作用于 toplevel；Qt6 下普通 Tool 会被加 SSD 边框，
        // 故显式去掉边框。不要改成 ToolTip，那会变成 popup 走不了 layer-shell。
        setWindowFlags(Qt::WindowStaysOnTopHint | Qt::Tool |
                       Qt::FramelessWindowHint);
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

    // Wayland下用 layer-shell 把菜单做成非 toplevel 表面(不出现在任务栏)，
    // 定位交给 treeland_dde_shell_v1：合成器直接按全局光标摆放右键菜单。
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
            g.right() - sz.width() + 1)));
        topLeft.setY(qBound(g.top(), topLeft.y(), qMax(g.top(),
            g.bottom() - sz.height() + 1)));
    }

    // 全屏遮罩兼作「点击外部关闭」。
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

    createWinId();
    resize(sz);
    WaylandHelper::setMenuLayerRole(this, topLeft);

    // 顶层菜单定位：锁存到全局光标处。DTK 的 DMenuEffect 会给菜单加 18px 阴影边距，
    // surface 原点比可见面板左上角偏左/偏上 18px，这里传阴影边距的相反数作偏移，
    // 让合成器把可见面板(而非 surface 原点)精确摆到光标处。
    WaylandHelper::placeMenuAtCursor(this,
                                     -contentsMargins().left(),
                                     -contentsMargins().top());

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
