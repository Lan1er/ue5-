# -*- coding: utf-8 -*-
"""生成 NoteOnFoil（写了留言的烟纸）的道具图标 —— 第三版（程序化）。

前两版问题：
 v1 大字模糊把质感抹平 -> 像带横线的白纸；
 v2 旋转并集 -> 像一叠纸，且工资条的表格残影还在、笔迹像"CCC"。
第三版从零生成，只对齐现有 10 张的【风格基准】：暖灰冷银、做旧、手绘颗粒、柔和边缘光。
  1) 形状：不规则撕口多边形（四边带锯齿），角落轻微卷边；
  2) 褶皱：三倍频值噪声当高度图 -> 转成冷银明暗（有明有暗 = 金属揉皱感）；
  3) 高光：几条细长的亮折痕 + 一条斜向柔光；
  4) 笔迹：连笔涂写（起伏基线 + 变斜率小环），深浅两层；
  5) 收尾：alpha 包围盒 -> 补方形 -> 5% 边距 -> 256x256（与其它图标同一套参数）。
"""
import os
import math
import random
from PIL import Image, ImageFilter, ImageOps, ImageDraw, ImageChops, ImageEnhance

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, r"SourceArt\UI\Icons\T_Item_NoteOnFoil.png")
SIZE = 256
MARGIN = 0.05
SEED = 20261019
SS = 1024  # 超采样画布


def value_noise(size, cell, rnd):
    """小随机图 -> 双三次放大 = 平滑噪声。"""
    g = Image.new("L", (max(2, size // cell), max(2, size // cell)))
    g.putdata([rnd.randint(0, 255) for _ in range(g.width * g.height)])
    return g.resize((size, size), Image.BICUBIC)


def fbm(size, rnd):
    """三倍频噪声（0..255）当褶皱高度图。"""
    acc = Image.new("L", (size, size), 0)
    total = 0.0
    for cell, weight in ((70, 1.00), (34, 0.75), (15, 0.55), (7, 0.35)):
        n = value_noise(size, cell, rnd)
        acc = ImageChops.add(acc, n.point(lambda v, w=weight: int(v * w)))
        total += weight
    blur = acc.filter(ImageFilter.GaussianBlur(6))
    return ImageChops.blend(acc, blur, 0.55).point(lambda v: int(150 + (v - 128) * 0.62))


def torn_polygon(w, h, rnd):
    """不规则撕口四边形：每条边上打锯齿。"""
    inset = int(w * 0.06)
    # 四个粗略角点（不完全对称）
    corners = [
        (inset + rnd.randint(-8, 14), inset + rnd.randint(-6, 12)),
        (w - inset + rnd.randint(-16, 6), inset + rnd.randint(-8, 14)),
        (w - inset + rnd.randint(-10, 12), h - inset + rnd.randint(-14, 8)),
        (inset + rnd.randint(-12, 10), h - inset + rnd.randint(-10, 12)),
    ]
    pts = []
    for i in range(4):
        x0, y0 = corners[i]
        x1, y1 = corners[(i + 1) % 4]
        steps = 9
        for s in range(steps):
            t = s / steps
            x = x0 + (x1 - x0) * t
            y = y0 + (y1 - y0) * t
            # 沿法线抖动，形成毛边（越靠近角越小）
            nx, ny = -(y1 - y0), (x1 - x0)
            ln = math.hypot(nx, ny) or 1.0
            nx, ny = nx / ln, ny / ln
            amp = rnd.uniform(-32, 32) * (0.35 + 0.65 * math.sin(math.pi * t))
            pts.append((x + nx * amp, y + ny * amp))
    return pts


def make_foil(rnd):
    w = h = SS
    # 1) 高度图（褶皱）
    height = value_noise(w, 420, rnd).filter(ImageFilter.GaussianBlur(60))
    # 轻微"揉皱"方向感：叠一层斜向渐变
    diag = Image.linear_gradient("L").resize((w, h)).rotate(34, resample=Image.BICUBIC)
    height = ImageChops.blend(height, diag, 0.55)
    height = ImageOps.autocontrast(height, cutoff=1)

    # 2) 高度 -> 【暖灰锡箔】灰度
    #    对齐现有 10 张美术图标：色相 H≈37°、饱和度 S≈15~20%、明度 V≈88~92%（工资条实测 mean≈(243,231,211)）
    #    烟纸是铝箔 -> 明暗层次比纸更硬（高光更亮、暗部更深），但整体色温必须留在暖褐家族里，不能偏蓝。
    lo = (188, 168, 134)
    mid = (240, 228, 202)
    hi = (253, 246, 226)
    silver = ImageOps.colorize(height, black=lo, mid=mid, white=hi).convert("RGBA")

    # 2b) 纸/箔纤维颗粒：高频低对比噪点，破掉"光滑矢量渐变"的塑料感
    fiber_small = value_noise(w, 2, rnd)                    # 极细颗粒
    fiber_mid = value_noise(w, 6, rnd)                     # 稍大一点的纤维团
    fiber = ImageChops.blend(fiber_small, fiber_mid, 0.35)
    fiber = fiber.point(lambda v: int(128 + (v - 128) * 0.55))
    grain = Image.composite(Image.new("RGBA", (w, h), (255, 250, 238, 255)),
                            Image.new("RGBA", (w, h), (78, 62, 44, 255)), fiber)
    grain.putalpha(fiber.point(lambda v: int(abs(v - 128) * 0.55)))   # 只做微弱的正负偏差
    silver = Image.alpha_composite(silver, grain)

    # 2c) 做旧：边缘污渍斑 + 折痕处压深（和工资条的旧纸感对齐）
    stains = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    ds = ImageDraw.Draw(stains)
    for _ in range(9):
        cx = rnd.choice([rnd.randint(0, int(w * 0.18)), rnd.randint(int(w * 0.82), w),
                         rnd.randint(0, w)])
        cy = rnd.choice([rnd.randint(0, int(h * 0.18)), rnd.randint(int(h * 0.82), h),
                         rnd.randint(0, h)])
        r = rnd.randint(int(w * 0.03), int(w * 0.10))
        ds.ellipse([cx - r, cy - r, cx + r, cy + r],
                   fill=(120, 98, 70, rnd.randint(14, 30)))
    stains = stains.filter(ImageFilter.GaussianBlur(int(w * 0.012)))
    silver = Image.alpha_composite(silver, stains)

    # 3) 细长亮折痕（金属反光的"筋"）
    creases = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(creases)
    for _ in range(7):
        x1 = rnd.randint(int(w * 0.05), int(w * 0.75))
        y1 = rnd.randint(int(h * 0.05), int(h * 0.95))
        ln = rnd.randint(int(w * 0.18), int(w * 0.55))
        ang = rnd.uniform(-0.5, 0.5)
        x2 = x1 + ln * math.cos(ang)
        y2 = y1 + ln * math.sin(ang)
        d.line([(x1, y1), (x2, y2)], fill=(255, 248, 230, rnd.randint(55, 110)), width=3)
        d.line([(x1, y1 + 1), (x2, y2 + 1)], fill=(96, 74, 52, rnd.randint(36, 76)), width=3)
    creases = creases.filter(ImageFilter.GaussianBlur(0.7))
    silver = Image.alpha_composite(silver, creases)

    # 4) 斜向柔光（箔面的整体反光）
    grad = diag.filter(ImageFilter.GaussianBlur(40)).point(lambda v: int(v * 0.30))
    gloss = Image.composite(Image.new("RGBA", (w, h), (255, 255, 255, 255)),
                            Image.new("RGBA", (w, h), (0, 0, 0, 0)), grad)
    silver = Image.alpha_composite(silver, gloss)
    # 竖向金属渐变（上亮下略暗）
    vg = Image.linear_gradient("L").resize((w, h))
    silver = Image.alpha_composite(silver, Image.composite(
        Image.new("RGBA", (w, h), (255, 255, 255, 255)),
        Image.new("RGBA", (w, h), (0, 0, 0, 0)), vg.point(lambda v: int(v * 0.18))))

    # 5) 撕口轮廓（带 2px 羽化，边缘自然）
    mask = Image.new("L", (w, h), 0)
    ImageDraw.Draw(mask).polygon(torn_polygon(w, h, rnd), fill=255)
    mask = mask.filter(ImageFilter.GaussianBlur(1.6))

    # 6) 边缘亮边（箔片裁边的金属反光）
    rim = ImageChops.subtract(mask.filter(ImageFilter.MaxFilter(5)), mask)
    rim = rim.filter(ImageFilter.GaussianBlur(2.0)).point(lambda v: int(v * 0.55))
    silver = Image.alpha_composite(silver, Image.composite(
        Image.new("RGBA", (w, h), (250, 252, 255, 255)),
        Image.new("RGBA", (w, h), (0, 0, 0, 0)), rim))

    # 7) 连笔涂写（深浅两层，像锡箔上写的日记）
    #    线条粗细要有变化（压力感）：每段落随机 3~6 px，并在起笔/收笔处更细
    ink_layer = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(ink_layer)
    for layer_i, (color, wd) in enumerate((((58, 40, 26, 118), 5), ((40, 28, 18, 172), 4))):
        for row in range(3):
            base_y = int(h * (0.30 + 0.18 * row)) + rnd.randint(-6, 6)
            x = int(w * rnd.uniform(0.14, 0.20))
            end = int(w * rnd.uniform(0.66, 0.88))
            phase = rnd.uniform(0, math.tau)
            while x < end:
                # 连笔：小环 + 上扬的尾巴，基线有起伏
                gw = rnd.randint(7, 13)
                gh = rnd.randint(9, 20)
                yy = base_y + int(3.0 * math.sin(phase + x * 0.05))
                # 压力变化：同一段落内横向"由粗到细"，避免机械等宽
                w_now = max(1, wd + rnd.choice((-2, -1, 0, 0, 1)))
                d.arc([x, yy - gh // 2, x + gw, yy + gh // 2],
                      rnd.randint(160, 210), rnd.randint(320, 380), fill=color, width=w_now)
                if rnd.random() < 0.85:
                    tail_w = max(1, w_now - rnd.randint(0, 2))
                    d.line([(x + gw // 2, yy - gh // 2),
                            (x + gw // 2 + rnd.randint(-2, 2), yy + gh // 2)],
                           fill=color, width=tail_w)
                x += gw + rnd.randint(1, 3)
    ink_layer = ink_layer.filter(ImageFilter.GaussianBlur(0.5))
    silver = Image.alpha_composite(silver, ink_layer)

    # 8) 整体轻微暗角（和现有图标一样：中心略亮）
    vign = Image.new("L", (w, h), 0)
    dv = ImageDraw.Draw(vign)
    dv.ellipse([-int(w * 0.15), -int(h * 0.15), int(w * 1.15), int(h * 1.15)], fill=38)
    vign = vign.filter(ImageFilter.GaussianBlur(120))
    silver = Image.alpha_composite(silver, Image.composite(
        Image.new("RGBA", (w, h), (0, 0, 0, 255)),
        Image.new("RGBA", (w, h), (0, 0, 0, 0)), vign))

    # 8b) 铅笔勾线（现有 10 张都有手绘深色轮廓；这是最后一块"像手绘"的关键）
    edge = ImageChops.subtract(mask.filter(ImageFilter.MaxFilter(3)), mask)
    edge = edge.filter(ImageFilter.GaussianBlur(0.8))
    # 让勾线不均匀（手绘感）：叠一层低频噪声调制浓淡
    jitter = value_noise(w, 26, rnd).point(lambda v: int(120 + (v - 128) * 0.9))
    edge = ImageChops.multiply(edge, jitter).point(lambda v: int(v * 1.5))
    pencil = Image.composite(Image.new("RGBA", (w, h), (86, 68, 48, 255)),
                             Image.new("RGBA", (w, h), (0, 0, 0, 0)),
                             edge.point(lambda v: min(255, int(v * 1.1))))
    silver = Image.alpha_composite(silver, pencil)
    # 9) 最终整体提亮：对齐现有图标的明度（实测工资条 V≈95、风湿贴 V≈92；本图做旧后偏暗）
    silver = ImageEnhance.Brightness(silver).enhance(1.34)
    silver = ImageEnhance.Contrast(silver).enhance(0.96)
    silver.putalpha(mask)
    return silver


def resize_to_icon(img):
    bbox = img.getchannel("A").getbbox()
    if bbox:
        img = img.crop(bbox)
    w, h = img.size
    side = max(w, h)
    pad = int(round(side * MARGIN))
    canvas = Image.new("RGBA", (side + 2 * pad, side + 2 * pad), (0, 0, 0, 0))
    canvas.paste(img, ((side - w) // 2 + pad, (side - h) // 2 + pad), img)
    return canvas.resize((SIZE, SIZE), Image.LANCZOS)


def make_sheet():
    icons_dir = os.path.join(ROOT, r"SourceArt\UI\Icons")
    names = [n for n in sorted(os.listdir(icons_dir)) if n.lower().endswith(".png")]
    cell, gap, cols = SIZE, 10, 4
    rows = (len(names) + cols - 1) // cols
    sheet = Image.new("RGBA", (cols * (cell + gap) + gap, rows * (cell + gap) + gap), (24, 24, 26, 255))
    for i, n in enumerate(names):
        im = Image.open(os.path.join(icons_dir, n)).convert("RGBA").resize((cell, cell), Image.LANCZOS)
        sheet.alpha_composite(im, (gap + (i % cols) * (cell + gap), gap + (i // cols) * (cell + gap)))
    path = os.path.join(ROOT, r"Saved\icons_sheet_with_foil.png")
    sheet.save(path)
    return path, names


if __name__ == "__main__":
    rnd = random.Random(SEED)
    icon = resize_to_icon(make_foil(rnd))
    icon.save(OUT)
    print("saved:", OUT, icon.size)
    p, names = make_sheet()
    print("sheet:", p)
    print("order:", names)







