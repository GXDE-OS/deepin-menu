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

#include <QPainter>
#include <QPainterPath>
#include <QRect>
#include <QPen>
#include <QBrush>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QDebug>
#include <QJsonArray>
#include <QRegularExpression>
#include <QApplication>
#include <QScreen>
#include <QWidget>
#include <QMouseEvent>
#include <QTimer>
#include <QPointer>
#include <QPlatformSurfaceEvent>
#include <functional>
#include <DBlurEffectWidget>

#include "ddockmenu.h"
#include "dmenucontent.h"
#include "utils.h"
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

DDockMenu::DDockMenu(DDockMenu *parent)
    : DArrowRectangle(DArrowRectangle::ArrowBottom, parent)
    , m_menuContent(new DMenuContent(this))
    , m_monitor(new DRegionMonitor(this))
{
    setAttribute(Qt::WA_InputMethodEnabled, false);

    setMouseTracking(true);

    m_wmHelper = DWindowManagerHelper::instance();

    connect(m_wmHelper, &DWindowManagerHelper::hasCompositeChanged, this, &DDockMenu::onWMCompositeChanged);

    onWMCompositeChanged();

    setAccessibleName("DockMenu");
    setBackgroundColor(DBlurEffectWidget::DarkColor);
    setMargin(0);
    setArrowWidth(18);
    setArrowHeight(10);

    normalStyle = ItemStyle{Qt::transparent,
            Qt::white,
            QColor("#646464"),
            ":/images/check_dark_normal.png",
            ":/images/arrow-dark.png"};
    hoverStyle = ItemStyle{QColor("#2ca7f8"),
            Qt::white,
            QColor("#646464"),
            ":/images/check_dark_hover.png",
            ":/images/arrow-dark.png"};
    inactiveStyle = ItemStyle{Qt::transparent,
            QColor("#646464"),
            QColor("#646464"),
            ":/images/check_dark_inactive.png",
            ":/images/arrow-dark.png"};

    connect(m_monitor, &DRegionMonitor::buttonPress, this, [=] (const QPoint &p) {
        if (geometry().contains(p)) {
            // The action performed is not from QAction and needs to be postponed because the menu requires a hover style.
            QTimer::singleShot(100, this, [=] {
                m_menuContent->processButtonClick(p);
            });
        } else {
            qDebug() << "window deactivate, destroy menu";
            destroyAll();
        }
    });

    // treeland 不认 DBlurEffectWidget 走的 KDE blur 协议(它用 personalization), 所以在 treeland 上
    // DBlurEffectWidget 的半透明背景拿不到合成器模糊, 会渲染成白框。与 GL 不可用/无 blur 窗口一样,
    // treeland 下也走"不透明深色平背景"兜底 —— 即 wlcom 上那个正常黑菜单的观感。
    if (WaylandHelper::isWayland()
        && (!WaylandHelper::isGLWorking() || !m_wmHelper->hasBlurWindow()
            || WaylandHelper::isTreeland())) {
        setBackgroundColor(QColor(20, 20, 20));  // 不透明深色,等价于DarkColor的观感
        // 关掉箭头与阴影:GL 不可用时它们会画成白色直角晕
        setArrowHeight(0);
        setArrowWidth(0);
        setShadowBlurRadius(0);
        setShadowXOffset(0);
        setShadowYOffset(0);
        setRadius(8);
        m_wlFlatBg = true;
    }
}

DDockMenu::~DDockMenu()
{
    m_monitor->unregisterRegion();
    setVisible(false);
    releaseFocus();
    releaseKeyboard();
    if (m_wlMask) {
        m_wlMask->deleteLater();
        m_wlMask = nullptr;
    }
}

void DDockMenu::setItems(QJsonArray items)
{
    m_menuContent->setCurrentIndex(-1);
    m_menuContent->clearActions();

    foreach (QJsonValue item, items) {
        QJsonObject itemObj = item.toObject();

        QAction *action = new QAction(m_menuContent);
        QString itemText = itemObj["itemText"].toString().replace("_", QString()).replace(QRegularExpression("\\([^)]+\\)"), QString());

        action->setText(itemText);
        action->setEnabled(itemObj["isActive"].toBool());
        action->setCheckable(itemObj["isCheckable"].toBool() || Utils::menuItemCheckableFromId(itemObj["itemId"].toString()));
        action->setChecked(itemObj["checked"].toBool());
        action->setProperty("itemId", itemObj["itemId"].toString());
        action->setProperty("itemIcon", itemObj["itemIcon"].toString());
        action->setProperty("itemIconHover", itemObj["itemIconHover"].toString());
        action->setProperty("itemIconInactive", itemObj["itemIconInactive"].toString());
        action->setProperty("itemSubMenu", itemObj["itemSubMenu"].toObject());

        m_menuContent->addAction(action);
    }

    // adjust its size according to its content.
    m_menuContent->setFixedSize(m_menuContent->contentWidth(),
                                m_menuContent->contentHeight());


    setContent(m_menuContent);

    resizeWithContent();
}

DDockMenu *DDockMenu::getRootMenu()
{
    return this;
}

void DDockMenu::showSubMenu(int, int, const QJsonObject &)
{

}

void DDockMenu::paintEvent(QPaintEvent *event)
{
    if (!m_wlFlatBg) {
        DArrowRectangle::paintEvent(event);
        return;
    }

    // 自绘圆角背景, 菜单项由子widget DMenuContent 在其上绘制。
    // 开启合成器模糊时背景画成半透明深色(透出模糊); 否则不透明深色兜底。
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    QPainterPath path;
    path.addRoundedRect(rect(), radius(), radius());
    QColor bg = backgroundColor();
    if (m_wlBlur) {
        bg = QColor(20, 20, 20);
        bg.setAlphaF(0.6);
    }
    painter.fillPath(path, QBrush(bg));
}

bool DDockMenu::event(QEvent *event)
{
    if (event->type() == QEvent::PlatformSurface) {
        auto *se = static_cast<QPlatformSurfaceEvent *>(event);
        if (se->surfaceEventType() == QPlatformSurfaceEvent::SurfaceCreated) {
            scheduleBlur(0); // surface 首次创建时给平背景菜单挂模糊
        }
    }

    if (event->type() == QEvent::WindowDeactivate) {
        // NOTE(sbw): test if we have mouse handle
        if (rect().contains(mapFromGlobal(QCursor::pos())))
        {
            activateWindow();
        } else {
            qDebug() << "window deactivate, destroy menu";
            destroyAll();
        }
    }

    if (event->type() == QEvent::Move) {
        qDebug() << pos();
    }

    return DArrowRectangle::event(event);
}

void DDockMenu::show(int x, int y)
{
    if (!WaylandHelper::isWayland()) {
        // X11走原来的逻辑
        DArrowRectangle::show(x, y);
        return;
    }

    const QSize sz = size();
    QPoint topLeft;
    switch (arrowDirection()) {
        case ArrowBottom: {
            topLeft = QPoint(x - sz.width() / 2, y - sz.height());
            break;
        }

        case ArrowTop: {
            topLeft = QPoint(x - sz.width() / 2, y);
            break;
        }

        case ArrowLeft: {
            topLeft = QPoint(x, y - sz.height() / 2);
            break;
        }

        case ArrowRight: {
            topLeft = QPoint(x - sz.width(), y - sz.height() / 2);
            break;
        }
    }

    const QScreen* scr = qApp->primaryScreen();
    if (scr) {
        const QRect g = scr->geometry();
        topLeft.setX(qBound(g.left(), topLeft.x(), g.right() - sz.width()));
        topLeft.setY(qBound(g.top(), topLeft.y(), g.bottom() - sz.height()));
    }

    WaylandHelper::setMenuLayerRole(this, topLeft);

    if (!m_wlMask) {
        WlMaskWidget *mask = new WlMaskWidget;
        mask->setAttribute(Qt::WA_TranslucentBackground);
        mask->setWindowFlags(Qt::FramelessWindowHint
                             | Qt::WindowDoesNotAcceptFocus);
        mask->onPress = [this] { destroyAll(); };
        m_wlMask = mask;
    }

    if (scr) {
        m_wlMask->setGeometry(scr->geometry());
    }
    WaylandHelper::setFullscreenMaskRole(m_wlMask);
    m_wlMask->show();

    DArrowRectangle::show(x, y);
}

void DDockMenu::showEvent(QShowEvent *e)
{
    // Wayland下原逻辑不可靠
    // 改由全屏遮罩负责，X11走原逻辑
    if (!WaylandHelper::isWayland()) {
        Q_ASSERT(!m_monitor->registered());
        m_monitor->registerRegion();
    }

    QTimer::singleShot(100, this, [=] {
        if (!isVisible())
            return;
        if (!isActiveWindow())
            activateWindow();

        grabKeyboard();
    });

    // re-show 时 QtWayland 不重建 QPlatformWindow(无 SurfaceCreated 事件), wl_surface 在 show 之后
    // 才懒重建, 故延迟重试挂模糊直到 surface 就绪。
    scheduleBlur(0);

    DArrowRectangle::showEvent(e);
}

bool DDockMenu::setupBlur()
{
    if (!WaylandHelper::isWayland() || !m_wlFlatBg) {
        return true; // 非 Wayland / 非平背景路径无需处理
    }
    bool ok = false;
    if (WaylandHelper::isTreeland()) {
        // treeland: personalization 做背景模糊 + 圆角裁剪(平背景无阴影边距, 整面即菜单, 不会溢出)。
        ok = WaylandHelper::applyTreelandMenuStyle(this, radius());
    } else {
        // 通用 wlroots(kylin-wlcom): KDE blur, 区域为整面菜单矩形。
        ok = WaylandHelper::enableBlur(this, QRect(0, 0, width(), height()));
    }
    if (ok && !m_wlBlur) {
        m_wlBlur = true;
        update(); // 改用半透明背景重绘, 透出模糊
    }
    return ok;
}

void DDockMenu::scheduleBlur(int attempt)
{
    if (!WaylandHelper::isWayland() || !m_wlFlatBg || m_wlBlur || attempt > 8) {
        return;
    }
    QPointer<DDockMenu> self(this);
    QTimer::singleShot(0, this, [self, attempt]() {
        if (!self || !self->isVisible()) {
            return;
        }
        if (!self->setupBlur()) {
            self->scheduleBlur(attempt + 1);
        }
    });
}

void DDockMenu::hideEvent(QHideEvent *event)
{
    DArrowRectangle::hideEvent(event);

    if (m_wlMask) {
        m_wlMask->hide();
    }
    m_monitor->unregisterRegion();
    releaseKeyboard();
}

void DDockMenu::mouseMoveEvent(QMouseEvent *event)
{
    DArrowRectangle::mouseMoveEvent(event);

    m_menuContent->processCursorMove(mapToGlobal(event->pos()));
}

void DDockMenu::mouseReleaseEvent(QMouseEvent *event)
{
    DArrowRectangle::mouseReleaseEvent(event);

    // Wayland下禁用了DRegionMonitor
    // 关闭交由遮罩负责
    if (WaylandHelper::isWayland()) {
        m_menuContent->processCursorMove(mapToGlobal(event->pos()));
        m_menuContent->doCurrentAction();
    }
}

void DDockMenu::keyPressEvent(QKeyEvent *event)
{
    DArrowRectangle::keyPressEvent(event);

    switch (event->key()) {
    case Qt::Key_Escape:
        destroyAll();
        break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        m_menuContent->doCurrentAction();
        break;
    case Qt::Key_Up:
        m_menuContent->selectPrevious();
        break;
    case Qt::Key_Down:
        m_menuContent->selectNext();
        break;
    default:
        break;
    }
}

/**
 * @brief DDockMenu::menuUnderPoint
 * @param point is a global position.
 * @return
 */
DDockMenu *DDockMenu::menuUnderPoint(const QPoint point)
{
    return geometry().contains(point) ? this : nullptr;
}

void DDockMenu::releaseFocus()
{
    qDebug() << Q_FUNC_INFO << this;

    releaseMouse();
    releaseKeyboard();
}

void DDockMenu::destroyAll()
{
    // NOTE(hualet): the events processed by this menu is actually delivered by
    // xmousearea which is xrecord backed, so if we destroy this window too
    // early, say immediately after mouse clicks, the actual events will go to
    // the window behide the menu(desktop for example).
    QTimer::singleShot(100, this, [this] {
        qApp->processEvents();
        deleteLater();
    });
}

void DDockMenu::onWMCompositeChanged()
{
    if (m_wmHelper->hasComposite())
        setBorderColor(QColor(255, 255, 255, 0));
    else
        setBorderColor(QColor("#2C3238"));
}
