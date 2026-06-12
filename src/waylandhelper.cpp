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
#include <QHash>
#include <qpa/qplatformnativeinterface.h>

#include <LayerShellQt/Window>
#include <wayland-client.h>

#include "waylandhelper.h"
#include "kde-blur-client-protocol.h"

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

// Wayland下的模糊协议，由KDE提供
namespace {

struct wl_display* g_display = nullptr;
struct wl_registry* g_registry = nullptr;
struct org_kde_kwin_blur_manager* g_blurManager = nullptr;
bool g_blurTried = false;

// 用哈希表追踪每个widget对应的blur对象，便于确保在菜单销毁时全部释放
QHash<QObject*, org_kde_kwin_blur*>& getBlurObjects() {
    static QHash<QObject*, org_kde_kwin_blur*> map;
    return map;
}

// 注册Cllback
void registryGlobal(void* data, struct wl_registry* reg, uint32_t name,
        const char* interface, uint32_t version) {
    Q_UNUSED(data);
    Q_UNUSED(version);

    // interface类型为const char*，必须用strcmp
    if (qstrcmp(interface, "org_kde_kwin_blur_manager") == 0) {
        g_blurManager = static_cast<struct org_kde_kwin_blur_manager*>(
            wl_registry_bind(reg, name, &org_kde_kwin_blur_manager_interface,
                1));
    }
}

// 移除使用默认行为
void registryGlobalRemove(void*, struct wl_registry*, uint32_t) {}

const struct wl_registry_listener kRegistryListener = {
    registryGlobal,
    registryGlobalRemove,
};

void* nativeRes(const QByteArray& name) {
    QPlatformNativeInterface* ni = QGuiApplication::platformNativeInterface();
    if (ni) {
        return ni->nativeResourceForIntegration(name);
    } else {
        return nullptr;
    }
}

// 绑定blur manager
bool ensureBlurMan() {
    if (g_blurTried) {
        return g_blurManager != nullptr;
    }
    g_blurTried = true;

    g_display = static_cast<struct wl_display*>(nativeRes("display"));
    if (!g_display) {
        return false;
    }

    g_registry = wl_display_get_registry(g_display);
    if (!g_registry) {
        return false;
    }

    wl_registry_add_listener(g_registry, &kRegistryListener, nullptr);
    wl_display_roundtrip(g_display);
    return g_blurManager != nullptr;
}

struct wl_surface* wlSurfaceOf(QWidget* widget) {
    if (!widget) {
        return nullptr;
    }

    widget->createWinId();
    QWindow* win = widget->windowHandle();
    if (!win) {
        return nullptr;
    }

    QPlatformNativeInterface* ni = QGuiApplication::platformNativeInterface();
    if (!ni) {
        return nullptr;
    }

    return static_cast<struct wl_surface*>(
        ni->nativeResourceForWindow("surface", win));
}

}  // namespace

bool enableBlur(QWidget* widget, const QRect& region)
{
    if (!widget || !isWayland() || region.isEmpty()) {
        return false;
    }
    if (!ensureBlurMan() || !g_blurManager) {
        return false;
    }

    struct wl_compositor* compositor =
        static_cast<struct wl_compositor*>(nativeRes("compositor"));
    struct wl_surface* surface = wlSurfaceOf(widget);
    if (!compositor || !surface) {
        return false;
    }

    // 能复用blur时复用blur
    org_kde_kwin_blur* blur = getBlurObjects().value(widget, nullptr);
    if (!blur) {
        blur = org_kde_kwin_blur_manager_create(g_blurManager, surface);
        if (!blur) {
            return false;
        }
        getBlurObjects().insert(widget, blur);

        // 菜单销毁时释放blur对象
        QObject::connect(widget, &QObject::destroyed, [widget]() {
            org_kde_kwin_blur* b = getBlurObjects().take(widget);
            if (b) {
                org_kde_kwin_blur_release(b);
            }
        });
    }

    struct wl_region* reg = wl_compositor_create_region(compositor);
    wl_region_add(reg, region.x(), region.y(), region.width(), region.height());
    org_kde_kwin_blur_set_region(blur, reg);

    org_kde_kwin_blur_set_strength(blur, 2600);
    org_kde_kwin_blur_commit(blur);
    wl_region_destroy(reg);

    // release会让合成器移除模糊 (见kylin-wlcom kde_blur.c)
    wl_display_flush(g_display);
    return true;
}

}  // namespace WaylandHelper
