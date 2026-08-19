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

#include "waylandhelper.h"

namespace WaylandHelper {

bool isWayland() {
    if (!qGuiApp) {
        return false;
    }
    return QGuiApplication::platformName().toLower().contains(
        QLatin1String("wayland"));
}

void attachAsPopup(QWidget *popup, QWindow *anchorWindow) {
    if (popup == nullptr || !isWayland()) {
        return;
    }

    popup->setWindowFlags(Qt::ToolTip | Qt::FramelessWindowHint);
    popup->createWinId();

    QWindow *window = popup->windowHandle();
    if (window && anchorWindow) {
        window->setTransientParent(anchorWindow);
    }
}

}  // namespace WaylandHelper
