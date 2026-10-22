# -*- coding: utf-8 -*-
"""生成剧情对话框的「预合成人物卡片层」（普通 python，需要 Pillow + numpy）。

为什么不用"UImage + 旋转矩形"贴头像：
    美术那张人物卡片是**透视倾斜的四边形 + 纸框**，用旋转的矩形去贴永远贴不齐（用户两次反馈）。
    所以这里用 PIL 做透视变换，把 512x512 头像"烤"进卡片内层画面，再叠上纸框像素，
    输出整张 800x320 的 RGBA 层（只含卡片区域，其余透明）-> 控件整张画上去即可，像素级贴合、无需旋转。

用法：
    python Tools/make_story_cards.py
    # 然后在 UE 里跑：UnrealEditor-Cmd.exe <uproject> -run=PythonScript -Script=".../Tools/import_story_cards.py"

卡片四角（800x320 设计空间，用 Saved/card_measure.png 的网格标尺量出来的）：
    纸框外框: (34,37) (173,25) (192,167) (41,189)
    内层画面 = 外框绕中心缩放 0.93（略小于实际内沿 -> 留一圈原图的暗边，读作阴影，不会啃到纸框）
"""
import os
import numpy as np
from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FRAME = os.path.join(ROOT, 'SourceArt', 'UI', 'Dialogue', 'Seq', 'DLG_Idle_00.png')
PORTRAITS = os.path.join(ROOT, 'SourceArt', 'UI', 'Dialogue', 'Portraits')
OUT = os.path.join(ROOT, 'SourceArt', 'UI', 'Dialogue', 'Cards')

W, H = 800, 320
OUTER = [(34, 37), (173, 25), (192, 167), (41, 189)]
INNER_SCALE = 0.93
# 2026-10-21：旁白改用"风衣礼帽剪影"（T_Portrait_Narrator）；bumy（主控）改用甲虫（T_Portrait_BumyBeetle）。
# 老素材 T_Portrait_Bumy（戴眼镜叼烟斗的老人）按用户要求"剧情里用不到"，不再生成卡片，但文件保留。
NAMES = ['Bumy', 'BumyBeetle', 'Narrator', 'Su', 'Sam', 'Sooy', 'Kim']


def center(pts):
    return sum(p[0] for p in pts) / 4.0, sum(p[1] for p in pts) / 4.0


def scaled(pts, k):
    cx, cy = center(pts)
    return [(cx + (x - cx) * k, cy + (y - cy) * k) for x, y in pts]


def expanded(pts, d):
    cx, cy = center(pts)
    out = []
    for x, y in pts:
        dx, dy = x - cx, y - cy
        L = max((dx * dx + dy * dy) ** 0.5, 1e-6)
        out.append((x + dx / L * d, y + dy / L * d))
    return out


def homography(dst, src):
    """求把 dst 四角映射到 src 四角的单应矩阵（PIL PERSPECTIVE 要的是"输出->输入"的逆映射）。"""
    A, b = [], []
    for (x, y), (u, v) in zip(dst, src):
        A.append([x, y, 1, 0, 0, 0, -u * x, -u * y]); b.append(u)
        A.append([0, 0, 0, x, y, 1, -v * x, -v * y]); b.append(v)
    h = np.linalg.solve(np.array(A, dtype=np.float64), np.array(b, dtype=np.float64))
    return h


def warp_to_quad(pimg, quad):
    """把 pimg 整张透视映射到 quad 里（落在 800x320 透明画布上）。"""
    pw, ph = pimg.size
    h = homography(quad, [(0, 0), (pw, 0), (pw, ph), (0, ph)])
    coeffs = (h[0], h[1], h[2], h[3], h[4], h[5], h[6], h[7])
    return pimg.transform((W, H), Image.PERSPECTIVE, coeffs, Image.BICUBIC)


def main():
    os.makedirs(OUT, exist_ok=True)
    art = Image.open(FRAME).convert('RGBA')
    if art.size != (W, H):
        raise SystemExit('美术帧尺寸不是 800x320：%s' % (art.size,))

    inner = scaled(OUTER, INNER_SCALE)

    # 纸框层：只保留卡片像素（外框外扩 3px 以保住纸边/阴影），并把内层画面掏空
    keep = Image.new('L', (W, H), 0)
    ImageDraw.Draw(keep).polygon([tuple(map(int, p)) for p in expanded(OUTER, 3)], fill=255)
    hole = Image.new('L', (W, H), 255)
    ImageDraw.Draw(hole).polygon([tuple(map(int, p)) for p in inner], fill=0)
    ring = Image.composite(Image.new('L', (W, H), 255), Image.new('L', (W, H), 0), keep)
    ring = Image.composite(Image.new('L', (W, H), 0), ring, Image.eval(hole, lambda v: 255 - v))

    frame_layer = art.copy()
    frame_layer.putalpha(Image.composite(frame_layer.split()[3], Image.new('L', (W, H), 0), ring))

    made = []
    for name in NAMES:
        src = os.path.join(PORTRAITS, 'T_Portrait_%s.png' % name)
        if not os.path.exists(src):
            print('缺头像，跳过：%s' % src)
            continue
        pimg = Image.open(src).convert('RGBA')
        card = Image.new('RGBA', (W, H), (0, 0, 0, 0))
        card.alpha_composite(warp_to_quad(pimg, inner))
        card.alpha_composite(frame_layer)
        dst = os.path.join(OUT, 'DLG_Card_%s.png' % name)
        card.save(dst)
        made.append(dst)
        print('已生成 %s' % dst)

    # 预览：每张卡叠回原始面板（方便肉眼验收）
    if made:
        prev = Image.new('RGBA', (W, H * len(made)), (0, 0, 0, 255))
        for i, dst in enumerate(made):
            base = art.copy()
            base.alpha_composite(Image.open(dst).convert('RGBA'))
            prev.alpha_composite(base, (0, H * i))
        prev.convert('RGB').save(os.path.join(ROOT, 'Saved', 'cards_preview.png'))
        print('预览：Saved/cards_preview.png')


if __name__ == '__main__':
    main()
