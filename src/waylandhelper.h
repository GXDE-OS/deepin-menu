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
class QPoint;

namespace WaylandHelper {

bool isWayland();

// 把菜单设成 layer-shell「overlay」表面并锚定到所在屏幕。
// layer 表面不是 toplevel，天然不会出现在任务栏，顺带解决残留 icon 的问题。
// 真正的落点由 treeland_dde_shell_v1 的 set_auto_placement/set_surface_position
// 在合成器端计算，这里的 pos 只是 layer 边距兜底。
void setMenuLayerRole(QWidget *widget, const QPoint &pos);

// 全屏遮罩：layer-shell「top」层，接收「菜单外点击」以关闭菜单。
void setFullscreenMaskRole(QWidget *widget);

// 通过 treeland_dde_shell_v1 让合成器把菜单摆到全局光标处(右键上下文菜单)。
// xOffset/yOffset 为 surface 原点相对光标的偏移；传阴影边距的相反数可使可见面板对准光标。
bool placeMenuAtCursor(QWidget *widget, int xOffset, int yOffset);

// 通过 treeland_dde_shell_v1 按相对偏移定位(子菜单相对父菜单)。
bool placeMenuRelativeToWindow(QWidget *widget, int x, int y);

}  // namespace WaylandHelper

#endif  // SRC_WAYLANDHELPER_H_
