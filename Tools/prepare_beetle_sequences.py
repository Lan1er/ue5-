# -*- coding: utf-8 -*-
"""甲虫序列图预处理：把美术给的原始帧整理成可以直接导入 UE 的成品帧。

用法（普通 Python，需要 Pillow；不需要引擎）:
    python Tools/prepare_beetle_sequences.py

做三件事（**只平移/等比缩放，不拉伸**）：
  1. 每个序列按"内容高度 = 目标占比 × 画布"统一缩放 —— 美术各序列的画布留白不一样
     （走路 337² 内容占 57%、12形态待机 404² 内容占 70%），不统一的话游戏里换动作会突然变大变小；
  2. 把**躯干质心**（深色区域腐蚀掉细腿后的质心）平移到画布中心 —— 换动作/换形态时身体不会跳位；
  3. 统一到 384x384 画布输出。

输入 : SourceArt/Character/Beetle_Sequences/<序列名>/frame_XX.png
输出 : SourceArt/Character/Beetle_Sequences_Ready/<Tag>/<Tag>_XX.png
       Tag 映射见下面的 SETS（游戏代码里按这个 Tag 找贴图）。
"""
import os
import sys

try:
    from PIL import Image
    import numpy as np
except ImportError:
    print('需要 Pillow 与 numpy：pip install pillow numpy')
    sys.exit(1)

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC_DIR = os.path.join(ROOT, 'SourceArt', 'Character', 'Beetle_Sequences')
OUT_DIR = os.path.join(ROOT, 'SourceArt', 'Character', 'Beetle_Sequences_Ready')

CANVAS = 384
# 目标内容高度占画布比例（游戏里 SpriteContentHeightRatio 用同一个数）
TARGET_CONTENT_RATIO = 0.57

# 序列名（美术文件夹） -> (输出 Tag, 归一化方式, 参照 Tag)
#   content : 按"整体轮廓高度"对齐（适合无翅的走路/待机组；这两套用哪个指标结果都一样）
#   torso   : 按"躯干面积开方"对齐（适合带翅膀的一组 —— 振翅时轮廓高度变化很大，
#             若按轮廓对齐，会让待机的身体比飞行时大 ~10%，停下来时会看到体型一跳）
#   参照设为 None 表示它就是基准，不缩放
SETS = [
    ('1形态走路',  'Walk',   None,    None),
    ('3形态飞行1', 'Fly',    None,    None),
    ('12形态待机', 'Idle12', 'content', 'Walk'),
    ('3形态待机',  'Idle3',  'torso',   'Fly'),
]


def content_height(path):
    a = np.asarray(Image.open(path).convert('RGBA'))[..., 3] > 128
    ys, xs = np.nonzero(a)
    return float(ys.max() - ys.min() + 1)


def torso_area(path, lum_thr=95, erode=3):
    """深色躯干（腐蚀掉细腿/触角）的像素面积；开方即"身体线性尺寸"的代理，抗姿态变化"""
    a = np.asarray(Image.open(path).convert('RGBA')).astype(np.float32)
    alpha = a[..., 3] > 128
    lum = 0.299 * a[..., 0] + 0.587 * a[..., 1] + 0.114 * a[..., 2]
    m = alpha & (lum < lum_thr)
    for _ in range(erode):
        e = m.copy()
        e[1:, :] &= m[:-1, :]
        e[:-1, :] &= m[1:, :]
        e[:, 1:] &= m[:, :-1]
        e[:, :-1] &= m[:, 1:]
        m = e
    return float(m.sum())


def torso_centroid(path, lum_thr=95, erode=3):
    """深色躯干（腐蚀掉细腿/触角）的质心；用来对齐身体"""
    a = np.asarray(Image.open(path).convert('RGBA')).astype(np.float32)
    alpha = a[..., 3] > 128
    lum = 0.299 * a[..., 0] + 0.587 * a[..., 1] + 0.114 * a[..., 2]
    m = alpha & (lum < lum_thr)
    for _ in range(erode):
        e = m.copy()
        e[1:, :] &= m[:-1, :]
        e[:-1, :] &= m[1:, :]
        e[:, 1:] &= m[:, :-1]
        e[:, :-1] &= m[:, 1:]
        m = e
    ys, xs = np.nonzero(m)
    if len(xs) == 0:
        ys, xs = np.nonzero(alpha)          # 兜底：整体轮廓
    return float(xs.mean()), float(ys.mean())


def main():
    report = []
    for seq_name, tag, mode, ref_tag in SETS:
        src = os.path.join(SRC_DIR, seq_name)
        if not os.path.isdir(src):
            report.append('!! 缺少序列目录：{}'.format(src))
            continue

        files = sorted(f for f in os.listdir(src) if f.lower().endswith('.png'))
        if not files:
            report.append('!! 序列里没有 png：{}'.format(src))
            continue

        # 该序列自身的内容高度中位值 / 躯干面积中位值
        median_h = float(np.median([content_height(os.path.join(src, f)) for f in files]))
        median_area = float(np.median([torso_area(os.path.join(src, f)) for f in files]))

        # 参照序列的对应数值
        if mode is None:
            scale = 1.0                       # 本身就是基准
        else:
            ref_dir = os.path.join(SRC_DIR, {t: s for s, t, _, _ in SETS}[ref_tag])
            ref_files = sorted(f for f in os.listdir(ref_dir) if f.lower().endswith('.png'))
            if mode == 'content':
                ref_val = float(np.median([content_height(os.path.join(ref_dir, f)) for f in ref_files]))
                scale = ref_val / median_h
            else:                              # torso：面积 -> 线性尺寸取开方
                ref_val = float(np.median([torso_area(os.path.join(ref_dir, f)) for f in ref_files]))
                scale = (ref_val / median_area) ** 0.5

        # 锚点：躯干质心（第 1 帧）-> 画布中心
        cx, cy = torso_centroid(os.path.join(src, files[0]))

        outdir = os.path.join(OUT_DIR, tag)
        os.makedirs(outdir, exist_ok=True)
        for i, f in enumerate(files, 1):
            im = Image.open(os.path.join(src, f)).convert('RGBA')
            if abs(scale - 1.0) > 1e-3:
                im = im.resize((max(1, int(round(im.width * scale))),
                                max(1, int(round(im.height * scale)))), Image.LANCZOS)
            canvas = Image.new('RGBA', (CANVAS, CANVAS), (0, 0, 0, 0))
            offx = CANVAS / 2 - cx * scale
            offy = CANVAS / 2 - cy * scale
            canvas.alpha_composite(im, (int(round(offx)), int(round(offy))))
            canvas.save(os.path.join(outdir, '{}_{:02d}.png'.format(tag, i)))

        report.append('{:<8} 帧数={:<3} 内容高中位={:.0f}px 躯干面积中位={:.0f} 方式={:<8} 缩放={:.3f} -> {}'
                      .format(tag, len(files), median_h, median_area, str(mode), scale, outdir))

    print('\n'.join(report))
    print('输出目录：{}'.format(OUT_DIR))


if __name__ == '__main__':
    main()

