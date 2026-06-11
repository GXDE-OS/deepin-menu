/*
 * Copyright (C) 2026 CharOfString <markus_verify@126.com>
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

#include <QGuiApplication>
#include <QMargins>
#include <QWidget>
#include <QWindow>
#include <QOpenGLContext>
#include <QOffscreenSurface>

#include <LayerShellQt/Window>

#include "waylandhelper.h"

namespace WaylandHelper {

bool isWayland() {
    if (!qGuiApp) {
        return false;
    }
    return QGuiApplication::platformName().toLower().contains(
        QLatin1String("wayland"));
}

bool isTreeland() {
    if (!isWayland()) {
        return false;
    }

    return qEnvironmentVariable("XDG_SESSION_DESKTOP").toLower().contains(
               QLatin1String("treeland"))
        || qEnvironmentVariable("DESKTOP_SESSION").toLower().contains(
               QLatin1String("treeland"))
        || qEnvironmentVariable("XDG_CURRENT_DESKTOP").toLower().contains(
               QLatin1String("treeland"))
        || qEnvironmentVariable("GDMSESSION").toLower().contains(
               QLatin1String("treeland"));
}

bool isGLWorking() {
    static int cached = -1;
    if (cached != -1) {
        return cached == 1;
    }

    bool ok = false;
    QOffscreenSurface surface;
    surface.create();
    if (surface.isValid()) {
        QOpenGLContext ctx;
        if (ctx.create() && ctx.makeCurrent(&surface)) {
            ok = true;
            ctx.doneCurrent();
        }
    }

    cached = ok ? 1 : 0;
    return ok;
}

void setMenuLayerRole(QWidget* widget, const QPoint& pos) {
    if (widget == nullptr || !isWayland()) {
        return;
    }

    widget->createWinId();
    QWindow* window = widget->windowHandle();
    if (!window) {
        return;
    }

    LayerShellQt::Window* layer = LayerShellQt::Window::get(window);
    if (!layer) {
        return;
    }

    LayerShellQt::Window::Anchors anchors;
    anchors |= LayerShellQt::Window::AnchorTop;
    anchors |= LayerShellQt::Window::AnchorLeft;
    layer->setAnchors(anchors);
    layer->setMargins(QMargins(pos.x(), pos.y(), 0, 0));
    layer->setLayer(LayerShellQt::Window::LayerOverlay);
    layer->setExclusiveZone(0);
    layer->setKeyboardInteractivity(
        LayerShellQt::Window::KeyboardInteractivityOnDemand);
    layer->setScope(QStringLiteral("dde-shell/menu"));
}

void setFullscreenMaskRole(QWidget* widget) {
    if (widget == nullptr || !isWayland()) {
        return;
    }

    widget->createWinId();
    QWindow* window = widget->windowHandle();
    if (!window) {
        return;
    }

    LayerShellQt::Window* layer = LayerShellQt::Window::get(window);
    if (!layer) {
        return;
    }

    LayerShellQt::Window::Anchors anchors;
    anchors |= LayerShellQt::Window::AnchorTop;
    anchors |= LayerShellQt::Window::AnchorBottom;
    anchors |= LayerShellQt::Window::AnchorLeft;
    anchors |= LayerShellQt::Window::AnchorRight;

    layer->setAnchors(anchors);
    layer->setMargins(QMargins(0, 0, 0, 0));
    layer->setLayer(LayerShellQt::Window::LayerTop);
    layer->setExclusiveZone(0);
    layer->setKeyboardInteractivity(
        LayerShellQt::Window::KeyboardInteractivityNone);
    layer->setScope(QStringLiteral("dde-shell/menu"));
}

}  // namespace WaylandHelper
