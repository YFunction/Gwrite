import numpy as np
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d import Axes3D
import sys
import re

def parse_gcode(filename):
    """
    解析 GCode 文件，提取所有 G0/G1 移动的点。
    返回一个列表，每个元素为 (x, y, z, move_type)，
    其中 move_type 为 'G0' 或 'G1'。
    坐标基于 G90/G91 进行绝对/相对转换。
    """
    points = []
    current_x, current_y, current_z = 0.0, 0.0, 0.0
    absolute_mode = True  # G90 绝对，G91 相对

    # 正则表达式匹配常见的坐标和命令
    g_pattern = re.compile(r'G([0-9]+)')
    x_pattern = re.compile(r'X([-+]?[0-9]*\.?[0-9]+)')
    y_pattern = re.compile(r'Y([-+]?[0-9]*\.?[0-9]+)')
    z_pattern = re.compile(r'Z([-+]?[0-9]*\.?[0-9]+)')

    with open(filename, 'r') as f:
        for line in f:
            # 移除注释（分号之后）
            if ';' in line:
                line = line[:line.index(';')]
            line = line.strip()
            if not line:
                continue

            # 检测 G90 / G91
            g_match = g_pattern.search(line)
            if g_match:
                g_code = int(g_match.group(1))
                if g_code == 90:
                    absolute_mode = True
                    continue
                elif g_code == 91:
                    absolute_mode = False
                    continue
                # 其他 G 代码忽略

            # 检测 G0 / G1 移动
            if line.startswith('G0') or line.startswith('G1'):
                parts = line.split()
                cmd = parts[0]  # 'G0' 或 'G1'
                move_type = cmd

                # 提取 X, Y, Z 值
                new_x = current_x
                new_y = current_y
                new_z = current_z

                for part in parts[1:]:
                    if part.startswith('X'):
                        val = float(part[1:])
                        new_x = val if absolute_mode else current_x + val
                    elif part.startswith('Y'):
                        val = float(part[1:])
                        new_y = val if absolute_mode else current_y + val
                    elif part.startswith('Z'):
                        val = float(part[1:])
                        new_z = val if absolute_mode else current_z + val

                # 记录上一个点（如果当前点与上一个点不同）
                if (new_x, new_y, new_z) != (current_x, current_y, current_z):
                    points.append((current_x, current_y, current_z, move_type))  # 起点
                    points.append((new_x, new_y, new_z, move_type))              # 终点

                # 更新当前位置
                current_x, current_y, current_z = new_x, new_y, new_z

    return points

def plot_gcode(points):
    """
    绘制 3D 轨迹，支持鼠标交互和键盘平移。
    """
    if not points:
        print("没有找到任何移动指令。")
        return

    fig = plt.figure(figsize=(12, 8))
    ax = fig.add_subplot(111, projection='3d')

    # 分离 G0 和 G1 线段
    # points 列表是成对出现的：(起点, 终点, 起点, 终点, ...)
    g0_lines = []
    g1_lines = []

    for i in range(0, len(points), 2):
        start = points[i][:3]
        end = points[i+1][:3]
        move_type = points[i][3]  # 起点和终点的类型相同
        if move_type == 'G0':
            g0_lines.append([start, end])
        else:
            g1_lines.append([start, end])

    # 绘制 G1 为蓝色实线，G0 为红色虚线
    if g1_lines:
        for seg in g1_lines:
            xs, ys, zs = zip(seg[0], seg[1])
            ax.plot(xs, ys, zs, color='blue', linewidth=1, label='G1' if seg is g1_lines[0] else "")
    if g0_lines:
        for seg in g0_lines:
            xs, ys, zs = zip(seg[0], seg[1])
            ax.plot(xs, ys, zs, color='red', linestyle='--', linewidth=0.8, label='G0' if seg is g0_lines[0] else "")

    # 设置图例（避免重复）
    handles = []
    if g1_lines:
        handles.append(plt.Line2D([0], [0], color='blue', lw=1, label='G1 (切削)'))
    if g0_lines:
        handles.append(plt.Line2D([0], [0], color='red', linestyle='--', lw=0.8, label='G0 (快速移动)'))
    if handles:
        ax.legend(handles=handles)

    # 自动设置坐标轴范围，留一点边距
    all_x = [p[0] for p in points]
    all_y = [p[1] for p in points]
    all_z = [p[2] for p in points]
    margin = 1.0
    ax.set_xlim(min(all_x) - margin, max(all_x) + margin)
    ax.set_ylim(min(all_y) - margin, max(all_y) + margin)
    ax.set_zlim(min(all_z) - margin, max(all_z) + margin)

    ax.set_xlabel('X')
    ax.set_ylabel('Y')
    ax.set_zlabel('Z')
    ax.set_title('GCode 3D 轨迹')

    # 键盘事件处理（平移）
    def on_key(event):
        step_ratio = 0.1  # 每次移动范围的比例
        if event.key == 'w':
            # 向前移动（增加Y）
            ylim = ax.get_ylim()
            dy = (ylim[1] - ylim[0]) * step_ratio
            ax.set_ylim(ylim[0] + dy, ylim[1] + dy)
        elif event.key == 's':
            # 向后移动（减少Y）
            ylim = ax.get_ylim()
            dy = (ylim[1] - ylim[0]) * step_ratio
            ax.set_ylim(ylim[0] - dy, ylim[1] - dy)
        elif event.key == 'a':
            # 向左移动（减少X）
            xlim = ax.get_xlim()
            dx = (xlim[1] - xlim[0]) * step_ratio
            ax.set_xlim(xlim[0] - dx, xlim[1] - dx)
        elif event.key == 'd':
            # 向右移动（增加X）
            xlim = ax.get_xlim()
            dx = (xlim[1] - xlim[0]) * step_ratio
            ax.set_xlim(xlim[0] + dx, xlim[1] + dx)
        elif event.key == 'q':
            # 向上移动（增加Z）
            zlim = ax.get_zlim()
            dz = (zlim[1] - zlim[0]) * step_ratio
            ax.set_zlim(zlim[0] + dz, zlim[1] + dz)
        elif event.key == 'e':
            # 向下移动（减少Z）
            zlim = ax.get_zlim()
            dz = (zlim[1] - zlim[0]) * step_ratio
            ax.set_zlim(zlim[0] - dz, zlim[1] - dz)
        else:
            return
        plt.draw()

    fig.canvas.mpl_connect('key_press_event', on_key)

    plt.show()

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print("用法: python gcode_viewer.py <GCode文件>")
        sys.exit(1)

    filename = sys.argv[1]
    try:
        points = parse_gcode(filename)
        plot_gcode(points)
    except FileNotFoundError:
        print(f"错误：文件 '{filename}' 未找到。")
        sys.exit(1)
    except Exception as e:
        print(f"解析错误：{e}")
        sys.exit(1)