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

#ifndef SRC_WAYLANDHELPER_H_
#define SRC_WAYLANDHELPER_H_

#include <QPoint>
#include <QRect>

class QWidget;

namespace WaylandHelper {

bool isWayland();
bool isTreeland();
bool isGLWorking();
void setMenuLayerRole(QWidget* widget, const QPoint& pos);
void setFullscreenMaskRole(QWidget* widget);
bool enableBlur(QWidget* widget, const QRect& region);

}  // namespace WaylandHelper

#endif  // SRC_WAYLANDHELPER_H_
