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

class QWidget;
class QWindow;

namespace WaylandHelper {

bool isWayland();

// 将 popup 挂到 anchor 的窗口下并设为 ToolTip 类型。
// Qt6 Wayland 中只有 Qt::ToolTip(非 grab popup) 不需要 input serial
// 就能走 xdg_popup + xdg_positioner 精确定位；D-Bus 服务拿不到 serial，
// 所以这是唯一能精确摆放的方式。必须在 popup 显示前调用。
void attachAsPopup(QWidget *popup, QWindow *anchorWindow);

}  // namespace WaylandHelper

#endif  // SRC_WAYLANDHELPER_H_
