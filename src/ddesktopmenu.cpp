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
#include <QPainterPath>
#include <QImage>
#include <QMouseEvent>
#include <QStyle>
#include <QStyleOptionMenuItem>
#include <QPlatformSurfaceEvent>
#include <QPointer>
#include <functional>
#include <qpa/qplatformscreen.h>

#include "waylandhelper.h"

// 阴影和圆角在Wayland下不工作，XCB的方案走了qt_blurImage，是QtWidgets导出的内部函数
// header根本找不着，只能前向声明了
QT_BEGIN_NAMESPACE
void qt_blurImage(QPainter* p, QImage& blurImage, qreal radius, bool quality,
    bool alphaOnly, int transposed = 0);
QT_END_NAMESPACE

namespace {

// Wayland下手动处理圆角与阴影
const int kWlRadius = 8;
const int kWlShadowBlur = 8;
const int kWlShadowOffsetY = 1;
const int kWlShadowMargin = 16;

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

// 非Treeland下的妥协方案
// 样式/源码参考自X11下的样式
void drawWlMenuDecoration(QWidget *w, QPainter &painter, bool blurOn) {
    const QRect content =
        w->rect().adjusted(0, 0, -kWlShadowMargin, -kWlShadowMargin);
    QPainterPath bgPath;
    bgPath.addRoundedRect(content, kWlRadius, kWlRadius);

    painter.setRenderHint(QPainter::Antialiasing);

    QImage shadow(w->size(), QImage::Format_ARGB32_Premultiplied);
    shadow.fill(Qt::transparent);
    {
        QPainter sp(&shadow);
        sp.setRenderHint(QPainter::Antialiasing);
        sp.fillPath(bgPath.translated(0, kWlShadowOffsetY), QColor(0, 0, 0));
    }
    painter.save();
    painter.setOpacity(0.18);
    qt_blurImage(&painter, shadow, kWlShadowBlur * 2.0, true, true);
    painter.restore();

    QColor bgColor = w->palette().color(QPalette::Window);
    if (blurOn) {
        bgColor.setAlphaF(0.6);
    }
    painter.fillPath(bgPath, bgColor);
    painter.strokePath(bgPath, QPen(QColor(0, 0, 0, 20), 1));
    painter.setClipPath(bgPath);
}

// Wayland下的子菜单如果还是普通QMenu就很乱飞了，而且样式也不对
// Wayland下使用这个打补丁的WlSubMenu类
class WlSubMenu : public QMenu {
public:
    explicit WlSubMenu(QWidget *parent) : QMenu(parent) {
        if (WaylandHelper::isWayland()) {
            setWindowFlags(windowFlags() | Qt::FramelessWindowHint);
            setAttribute(Qt::WA_TranslucentBackground);
            setContentsMargins(0, 0, kWlShadowMargin, kWlShadowMargin);
        }
    }

protected:
    bool event(QEvent *e) override {
        if (e->type() == QEvent::PlatformSurface) {
            auto *se = static_cast<QPlatformSurfaceEvent *>(e);
            if (se->surfaceEventType() ==
                    QPlatformSurfaceEvent::SurfaceCreated) {
                setupWayland();
            }
        }
        return QMenu::event(e);
    }

    void showEvent(QShowEvent *e) override {
        QMenu::showEvent(e);
        // re-show时QtWayland不重建QPlatformWindow
        scheduleSetup(0);
    }

    void paintEvent(QPaintEvent *e) override {
        if (!WaylandHelper::isWayland()) {
            QMenu::paintEvent(e);
            return;
        }
        QPainter painter(this);
        drawWlMenuDecoration(this, painter, m_wlBlur);
        for (QAction *action : actions()) {
            const QRect g = actionGeometry(action);
            if (g.isEmpty() || !e->rect().intersects(g)) {
                continue;
            }
            QStyleOptionMenuItem opt;
            initStyleOption(&opt, action);
            opt.rect = g;
            style()->drawControl(QStyle::CE_MenuItem, &opt, &painter, this);
        }
    }

private:
    // 应用layer角色 + dde-shell按光标定位 + blur
    bool setupWayland() {
        if (!WaylandHelper::isWayland()) {
            return true;
        }
        WaylandHelper::setMenuLayerRole(this, QPoint(0, 0));

        // Treeland优先用personalization，非Treeland则回退到KDE blur
        if (WaylandHelper::isTreeland()) {
            m_wlBlur = WaylandHelper::applyTreelandMenuStyle(this, kWlRadius);
        } else {
            const QRect blurRegion(0, 0, width() - kWlShadowMargin,
                height() - kWlShadowMargin);
            m_wlBlur = WaylandHelper::enableBlur(this, blurRegion);
        }

        // 子菜单定位不能用Qt的global geometry，拿不到真实坐标, 是垃圾
        // 自行计算
        QPoint off(width() - kWlShadowMargin, 0);  // 兜底
        if (QMenu *pm = qobject_cast<QMenu *>(parentWidget())) {
            QRect ar;
            for (QAction *a : pm->actions()) {
                if (a->menu() == this) {
                    ar = pm->actionGeometry(a);
                    break;
                }
            }
            off = QPoint(pm->width() - pm->contentsMargins().right(), ar.top());
        }
        const bool placed = WaylandHelper::placeMenuRelativeToWindow(this,
                off.x(), off.y());
        if (placed) {
            update();
        }
        return placed;
    }

    // re-show时surface在show之后才就绪, 逐帧重试直到拿到surface(最多若干次)。
    void scheduleSetup(int attempt) {
        if (!WaylandHelper::isWayland() || attempt > 8) {
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

    bool m_wlBlur = false;
};

}  // namespace

DDesktopMenu::DDesktopMenu()
    : QMenu()
    , m_monitor(new DRegionMonitor(this))
{
    setAccessibleName("DesktopMenu");

    // Wayland下调整半透明并设置圆角
    // DDockMenu通过DMenuBase已设置WA_TranslucentBackground
    // 而DDesktopMenu缺少此属性导致Wayland buffer为XRGB格式
    // 缺少的alpha通道似乎导致四角无法透明
    if (WaylandHelper::isWayland()) {
        // Qt6 Wayland下 Qt::Tool 不再自动处理为无边框
        // compositor 会给普通 Tool 窗口加 SSD 装饰（方形边框）
        // 加 FramelessWindowHint 禁止 WM 加边框
        setWindowFlags(Qt::WindowStaysOnTopHint | Qt::Tool | Qt::FramelessWindowHint);
        setAttribute(Qt::WA_TranslucentBackground);
        m_treeland = WaylandHelper::isTreeland();
        setContentsMargins(0, 0, kWlShadowMargin, kWlShadowMargin);
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
    // 用layer-shell把菜单锚定，并加全屏遮罩负责「点击外部关闭」
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

    const QPoint anchor = topLeft;

    createWinId();
    resize(sz);
    WaylandHelper::setMenuLayerRole(this, anchor);

    // 顶层菜单定位: treeland用绝对，其它用相对
    if (m_treeland) {
        WaylandHelper::placeMenuAtCursor(this, 0);
    } else {
        WaylandHelper::placeMenuRelativeToWindow(this, handlePos.x(), handlePos.y());
    }

    if (m_treeland) {
        m_wlBlur = WaylandHelper::applyTreelandMenuStyle(this, kWlRadius);
    } else {
        const QRect blurRegion(0, 0, sz.width() - kWlShadowMargin, sz.height() - kWlShadowMargin);
        m_wlBlur = WaylandHelper::enableBlur(this, blurRegion);
    }

    QMenu::popup(anchor);

    QMenu::popup(anchor);
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

void DDesktopMenu::paintEvent(QPaintEvent* event) {
    if (!WaylandHelper::isWayland()) {
        // X11下圆角与阴影由dxcb设置
        QMenu::paintEvent(event);
        return;
    }

    QPainter painter(this);
    drawWlMenuDecoration(this, painter, m_wlBlur);

    for (QAction *action : actions()) {
        const QRect g = actionGeometry(action);
        if (g.isEmpty() || !event->rect().intersects(g)) {
            continue;
        }
        QStyleOptionMenuItem opt;
        initStyleOption(&opt, action);
        opt.rect = g;
        style()->drawControl(QStyle::CE_MenuItem, &opt, &painter, this);
    }
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
