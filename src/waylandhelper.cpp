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
#include <QWidget>
#include <QWindow>
#include <QHash>
#include <QMargins>
#include <qpa/qplatformnativeinterface.h>

#include <LayerShellQt/Window>
#include <wayland-client.h>

#include "treeland-dde-shell-v1-client-protocol.h"
#include "waylandhelper.h"

namespace WaylandHelper {

namespace {

bool isCurPlatWayland() {
    if (!qGuiApp) {
        return qgetenv("XDG_SESSION_TYPE") == "wayland";
    }

    return QGuiApplication::platformName().toLower().contains(
        QLatin1String("wayland"));
}

wl_display* g_display = nullptr;
wl_registry* g_registry = nullptr;
treeland_dde_shell_manager_v1* g_ddeShellManager = nullptr;
bool g_tried = false;

// dde_shell_surface 保活，菜单销毁时释放
QHash<QObject*, treeland_dde_shell_surface_v1*>& ddeShellSurfaces() {
    static QHash<QObject*, treeland_dde_shell_surface_v1*> map;
    return map;
}

void registryGlobal(void*, wl_registry* reg, uint32_t name,
        const char* interface, uint32_t) {
    if (qstrcmp(interface, "treeland_dde_shell_manager_v1") == 0) {
        g_ddeShellManager = static_cast<treeland_dde_shell_manager_v1*>(
            wl_registry_bind(reg, name,
                &treeland_dde_shell_manager_v1_interface, 2));
    }
}

void registryGlobalRemove(void*, wl_registry*, uint32_t) {}

const wl_registry_listener kRegistryListener = {
    registryGlobal,
    registryGlobalRemove
};

void* nativeRes(const QByteArray& name) {
    if (QPlatformNativeInterface* native_int =
            QGuiApplication::platformNativeInterface()) {
        return native_int->nativeResourceForIntegration(name);
    }

    return nullptr;
}

wl_surface* wlSurfaceOf(QWidget* widget) {
    if (!widget) {
        return nullptr;
    }

    widget->createWinId();
    QWindow* win = widget->windowHandle();
    if (!win) {
        return nullptr;
    }

    if (QPlatformNativeInterface* native_int =
            QGuiApplication::platformNativeInterface()) {
        return static_cast<wl_surface*>(
            native_int->nativeResourceForWindow("surface", win));
    }

    return nullptr;
}

bool ensureDdeShell() {
    if (g_tried) {
        return g_ddeShellManager != nullptr;
    }

    g_tried = true;

    g_display = static_cast<wl_display*>(nativeRes("display"));
    if (!g_display) {
        return false;
    }

    g_registry = wl_display_get_registry(g_display);
    if (!g_registry) {
        return false;
    }

    wl_registry_add_listener(g_registry, &kRegistryListener, nullptr);
    wl_display_roundtrip(g_display);
    return g_ddeShellManager != nullptr;
}

// 为 widget 创建(或重建) treeland_dde_shell_surface_v1。
// 子菜单 re-show 时 wl_surface 会重建，所以每次都释放旧对象重新绑定。
treeland_dde_shell_surface_v1* ddeShellSurfaceOf(QWidget* widget) {
    if (!widget) {
        return nullptr;
    }

    if (treeland_dde_shell_surface_v1* old = ddeShellSurfaces().take(widget)) {
        treeland_dde_shell_surface_v1_destroy(old);
    }

    wl_surface* surface = wlSurfaceOf(widget);
    if (!surface) {
        return nullptr;
    }

    treeland_dde_shell_surface_v1* ss =
        treeland_dde_shell_manager_v1_get_shell_surface(g_ddeShellManager,
                                                        surface);
    if (!ss) {
        return nullptr;
    }

    ddeShellSurfaces().insert(widget, ss);
    QObject::connect(widget, &QObject::destroyed, [widget]() {
        if (treeland_dde_shell_surface_v1* s = ddeShellSurfaces().take(widget)) {
            treeland_dde_shell_surface_v1_destroy(s);
        }
    });

    return ss;
}

}  // namespace

bool isWayland() {
    return isCurPlatWayland();
}

void setMenuLayerRole(QWidget* widget, const QPoint& pos) {
    if (widget == nullptr || !isCurPlatWayland()) {
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
    if (widget == nullptr || !isCurPlatWayland()) {
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

bool placeMenuAtCursor(QWidget* widget, int xOffset, int yOffset) {
    if (!widget || !isCurPlatWayland()) {
        return false;
    }

    if (!ensureDdeShell() || !g_ddeShellManager) {
        return false;
    }

    treeland_dde_shell_surface_v1* ss = ddeShellSurfaceOf(widget);
    if (!ss) {
        return false;
    }

    // 合成器把菜单锁存到全局光标处，客户端只需给出 surface 原点相对光标的偏移。
    // 偏移取菜单阴影边距的相反数，使可见面板(而非 surface)精确落在光标上。
    treeland_dde_shell_surface_v1_set_auto_placement(ss, xOffset, yOffset);
    wl_display_flush(g_display);
    return true;
}

bool placeMenuRelativeToWindow(QWidget* widget, int x, int y) {
    if (!widget || !isCurPlatWayland()) {
        return false;
    }

    if (!ensureDdeShell() || !g_ddeShellManager) {
        return false;
    }

    treeland_dde_shell_surface_v1* ss = ddeShellSurfaceOf(widget);
    if (!ss) {
        return false;
    }

    treeland_dde_shell_surface_v1_set_surface_position(ss, x, y);
    wl_display_flush(g_display);
    return true;
}

}  // namespace WaylandHelper
