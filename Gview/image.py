import cv2
import numpy as np
import re

def parse_gcode(filename, z_threshold=0.2):
    """
    解析 GCode 文件，提取运动轨迹点
    :param filename: GCode 文件路径
    :param z_threshold: Z 轴落笔阈值（小于该值视为落笔）
    :return: 列表，每个元素为字典 {'x':x, 'y':y, 'z':z, 'f':f, 'pen':bool}
    """
    moves = []
    current_x = current_y = current_z = 0.0
    current_f = 0.0
    pen_down = False

    with open(filename, 'r') as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith(';'):
                continue
            parts = line.split()
            cmd = parts[0]

            if cmd in ('G0', 'G1'):
                x = y = z = None
                f_val = None
                for p in parts[1:]:
                    if p.startswith('X'):
                        x = float(p[1:])
                    elif p.startswith('Y'):
                        y = float(p[1:])
                    elif p.startswith('Z'):
                        z = float(p[1:])
                    elif p.startswith('F'):
                        f_val = float(p[1:])

                # 更新当前位置
                if x is not None:
                    current_x = x
                if y is not None:
                    current_y = y
                if z is not None:
                    current_z = z
                if f_val is not None:
                    current_f = f_val

                # 根据 Z 值判断落笔状态
                # 如果 Z 小于阈值，认为笔落下；否则抬起
                # 注意：有些 GCode 使用 M03/M05 控制抬落笔，此处简化处理
                if current_z < z_threshold:
                    pen_down = True
                    moves.append({
                        'x': current_y,
                        'y': current_x,
                        'z': current_z,
                        'f': current_f,
                        'pen': pen_down
                    })
                else:
                    pen_down = False

            # 可选：如果 GCode 使用 M03 落下、M05 抬起，可以加入以下处理
            elif cmd.startswith('M'):
                if 'M03' in line:  # 假设 M03 表示落笔
                    pen_down = True
                elif 'M05' in line:  # M05 表示抬笔
                    pen_down = False

    return moves


def gcode_to_image(gcode_file, output_image, params):
    """
    将 GCode 轨迹渲染为毛笔笔迹图片
    :param gcode_file: GCode 文件路径
    :param output_image: 输出图片路径
    :param params: 参数字典，包含：
        - height, width: 画布尺寸（像素）
        - base_radius: 基础笔刷半径（像素）
        - radius_range: 压力变化范围（像素）
        - step_size: 插值步长（像素单位）
        - speed_factor: 速度影响系数
        - max_speed: 最大速度（mm/s），用于速度归一化
        - apply_paper_texture: 是否应用纸张纹理
        - paper_texture: 纸张纹理图片路径（可选）
        - z_threshold: 解析 GCode 时的落笔阈值（可选）
    """
    # 1. 解析 GCode
    moves = parse_gcode(gcode_file, params.get('z_threshold', 0.2))
    if not moves:
        print("警告：未解析到任何运动点，请检查 GCode 文件格式或落笔阈值。")
        return

    # 在解析 moves 之后，提取所有落笔点坐标
    pen_points = [(m['x'], m['y']) for m in moves if m['pen']]
    if pen_points:
        xs = [p[0] for p in pen_points]
        ys = [p[1] for p in pen_points]
        min_x, max_x = min(xs), max(xs)
        min_y, max_y = min(ys), max(ys)

        # 计算缩放因子，保留一定边距
        margin = 50  # 边距像素
        scale_x = (params['width'] - 2*margin) / (max_x - min_x) if max_x > min_x else 1
        scale_y = (params['height'] - 2*margin) / (max_y - min_y) if max_y > min_y else 1
        scale = min(scale_x, scale_y)  # 等比例缩放

        # 对每个点进行坐标变换
        for m in moves:
            if m['pen']:
                m['x'] = margin + (max_x-m['x'] + min_x) * scale
                m['y'] = margin + (m['y'] - min_y) * scale
        
        for m in moves:
            if m['pen']:
                temp=m['x']
                m['x']=m['y']
                m['y']=temp
        
    # 2. 创建画布（白底）
    canvas = np.ones((params['height'], params['width']), dtype=np.uint8) * 255

    # 3. 计算压力范围
    pen_moves = [m for m in moves if m['pen']]
    if not pen_moves:
        print("警告：没有落笔状态的点，无法生成笔画。")
        return
    z_min = min(m['z'] for m in pen_moves)
    z_max = max(m['z'] for m in pen_moves)
    if z_max == z_min:
        z_max = z_min + 1  # 避免除零

    base_radius = params['base_radius']
    radius_range = params['radius_range']

    # 4. 遍历轨迹点，渲染笔画
    for i in range(1, len(moves)):
        if not moves[i]['pen']:
            continue
        p1 = moves[i-1]
        p2 = moves[i]
        if not p1['pen']:  # 前一点未落笔，跳过（无法绘制线段）
            continue

        # 计算压力（Z 越小压力越大）
        pressure = (z_max - p2['z']) / (z_max - z_min)
        radius = base_radius + pressure * radius_range

        # 速度影响：速度越慢，墨越多，半径稍大
        if 'speed_factor' in params and params['speed_factor'] > 0:
            # 计算两点间距离和速度
            dist = np.hypot(p2['x'] - p1['x'], p2['y'] - p1['y'])
            # 假设进给率 F 的单位是 mm/min，转换为 mm/s
            speed = dist / (1.0 / (p2['f'] / 60)) if p2['f'] > 0 else params.get('max_speed', 50)
            speed_factor = np.clip(1 - speed / params['max_speed'], 0, 1)
            radius *= (1 + params['speed_factor'] * speed_factor)

        # 两点之间插值绘制多个圆，使笔画连续
        dist = np.hypot(p2['x'] - p1['x'], p2['y'] - p1['y'])
        steps = max(int(dist / params['step_size']), 1)
        for t in np.linspace(0, 1, steps):
            x = int(p1['x'] + t * (p2['x'] - p1['x']))
            y = int(p1['y'] + t * (p2['y'] - p1['y']))
            # 确保坐标在画布内
            if 0 <= x < params['width'] and 0 <= y < params['height']:
                cv2.circle(canvas, (x, y), int(radius), 0, -1)

    # 5. 后处理：应用纸张纹理
    if params.get('apply_paper_texture') and params.get('paper_texture'):
        paper = cv2.imread(params['paper_texture'], cv2.IMREAD_GRAYSCALE)
        if paper is not None:
            paper = cv2.resize(paper, (params['width'], params['height']))
            # 将笔画（黑色）与纸张纹理融合：canvas 为黑色笔画（0），纸张为灰度
            # 使用乘法混合：笔画区域保留黑色，背景保留纸张纹理
            # 先将 canvas 转为浮点数 0~1，其中 0 为黑，1 为白
            canvas_norm = canvas.astype(np.float32) / 255.0
            paper_norm = paper.astype(np.float32) / 255.0
            # 笔画区域：canvas_norm 接近 0（黑），乘 paper_norm 仍接近 0；背景区域 canvas_norm 接近 1，乘 paper_norm 得到纸张纹理
            result = canvas_norm * paper_norm
            canvas = (result * 255).astype(np.uint8)
        else:
            print(f"警告：无法加载纸张纹理 {params['paper_texture']}，跳过纹理应用。")

    # 6. 保存图片
    cv2.imwrite(output_image, canvas)
    print(f"图片已保存至 {output_image}")


if __name__ == "__main__":
    # 使用示例
    params2 = {
        'height': 800,
        'width': 800,
        'base_radius': 2,
        'radius_range': 0,       # 关闭压力变化
        'step_size': 1.0,
        'speed_factor': 0.0,     # 关闭速度影响
        'max_speed': 50,
        'z_threshold':60.0,
        'apply_paper_texture': False
    }
    params = {
        'height': 800,               # 画布高度（像素）
        'width': 800,                # 画布宽度（像素）
        'base_radius': 10,             # 基础笔刷半径（像素）
        'radius_range': 5,            # 压力变化范围（像素）
        'step_size': 0.5,             # 插值步长（像素单位）
        'speed_factor': 0.3,           # 速度影响系数（0~1）
        'max_speed': 1000,               # 最大速度（mm/s），用于归一化
        'z_threshold': 60.0,            # 落笔 Z 阈值（mm）
        'apply_paper_texture': True,   # 是否应用纸张纹理
        'paper_texture': './paper.jpg'   # 纸张纹理图片路径（请替换为实际路径）
    }

    # 调用函数，生成图片
    gcode_to_image('G.gcode', 'output.png', params)