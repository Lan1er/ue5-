# -*- coding: utf-8 -*-
"""整理妻子精灵图（普通 python，不依赖 unreal）。

来源：F:\\乱七八糟的文件\\妻子精灵图\\妻子_{正面,侧面,背面}.zip
目标：SourceArt/Characters/Wife/{Front,Side,Back}/T_Wife_<Dir>_01..NN.png
      以及一张程序生成的柔和阴影贴图 SourceArt/Characters/Wife/T_Wife_Shadow.png

**关键：归一化**（和甲虫序列帧同一套思路）
美术给的画布里有大量空白（人物只占 ~91.6% 高、脚底离画布底还有 ~14px），
如果直接按画布高度缩放，人物就不是"真实身高"了，而且脚会悬空。
所以这里对每个方向：
  1) 求出该方向**所有帧的 alpha 并集包围盒**（同一方向共用，保证动画不抖）；
  2) 把每一帧裁到这个并集框里 -> **人物正好填满画布**（高 = 画布高、脚底 = 画布底）；
  3) 统一缩放到 320px 高（保持宽高比）。
这样游戏里就可以直接：画布世界高度 = WifeHeightCm（1 uu = 1 cm），底部中点贴地。

用法：
  python Tools/stage_wife_sprites.py
"""
import io
import json
import os
import zipfile

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ZIP_DIR = r'F:\乱七八糟的文件\妻子精灵图'
OUT_ROOT = os.path.join(ROOT, 'SourceArt', 'Characters', 'Wife')
REPORT = os.path.join(ROOT, 'Saved', 'wife_sprites_stage.txt')

SETS = [
    ('Front', '妻子_正面.zip', 'Front', 'T_Wife_Front'),
    ('Side', '妻子_侧面.zip', 'Side', 'T_Wife_Side'),
    ('Back', '妻子_背面.zip', 'Back', 'T_Wife_Back'),
]

ALPHA_THRESHOLD = 8     # alpha 低于它算透明（抗锯齿边缘不算内容）
TARGET_HEIGHT = 320     # 归一化后每帧的像素高度
SHADOW_SIZE = 256

# 把"接地阴影"直接烘进每一帧（见 bake_contact_shadow 的注释）。
BAKE_CONTACT_SHADOW = True

BAYER8 = [
    [0, 32, 8, 40, 2, 34, 10, 42],
    [48, 16, 56, 24, 50, 18, 58, 26],
    [12, 44, 4, 36, 14, 46, 6, 38],
    [60, 28, 52, 20, 62, 30, 54, 22],
    [3, 35, 11, 43, 1, 33, 9, 41],
    [51, 19, 59, 27, 49, 17, 57, 25],
    [15, 47, 7, 39, 13, 45, 5, 37],
    [63, 31, 55, 23, 61, 29, 53, 21],
]

report = []


def bake_contact_shadow(img):
    """在人物脚底画一圈"接地阴影"，直接烘进贴图。

    为什么要烘进贴图：
      工程里单独做一块"贴地阴影面片"实测渲染不出来（面片位置/尺寸/可见性/材质都验证过，
      和 UI 交付里那几张"贴图不渲染"同族问题）。而精灵面片本身是能画的，所以把阴影烘进画面里最稳。
    为什么用抖动：
      精灵材质是 Masked（二值 alpha），半透明会被裁掉 -> 用 8x8 Bayer 抖动把"渐变"编码成点阵，
      远看就是一圈柔和的阴影。
    """
    if not BAKE_CONTACT_SHADOW:
        return img

    w, h = img.size
    out = img.copy()
    px = out.load()

    # 阴影椭圆：中心在画面底部稍上方，宽=画面宽的 62%，高=画面高的 7%
    cx = w / 2.0
    cy = h - h * 0.015
    rx = w * 0.31
    ry = h * 0.035

    for y in range(h):
        dy = (y + 0.5 - cy) / ry
        if abs(dy) > 1.0:
            continue
        for x in range(w):
            dx = (x + 0.5 - cx) / rx
            d = (dx * dx + dy * dy) ** 0.5
            if d >= 1.0:
                continue
            density = (1.0 - d) ** 1.3 * 0.8
            threshold = (BAYER8[y % 8][x % 8] + 0.5) / 64.0
            if density <= threshold:
                continue
            r, g, b, a = px[x, y]
            if a <= ALPHA_THRESHOLD:
                # 空白处：直接点一个不透明的黑点（Masked 材质只认二值 alpha，所以用点阵模拟渐变）
                px[x, y] = (0, 0, 0, 255)
            else:
                # 人物像素：压暗（阴影落在鞋/裤脚上）
                k = 0.5
                px[x, y] = (int(r * k), int(g * k), int(b * k), a)
    return out


def log(msg):
    report.append(str(msg))


def alpha_bbox(img):
    alpha = img.getchannel('A')
    mask = alpha.point(lambda v: 255 if v > ALPHA_THRESHOLD else 0)
    return mask.getbbox()


metrics = {}
os.makedirs(OUT_ROOT, exist_ok=True)

for direction, zip_name, sub_dir, prefix in SETS:
    zip_path = os.path.join(ZIP_DIR, zip_name)
    out_dir = os.path.join(OUT_ROOT, sub_dir)
    os.makedirs(out_dir, exist_ok=True)
    if not os.path.exists(zip_path):
        log('!! 缺 zip: {}'.format(zip_path))
        continue

    with zipfile.ZipFile(zip_path) as zf:
        names = sorted([n for n in zf.namelist() if n.lower().endswith('.png')])
        log('=== {} ({}) : {} 帧 ==='.format(direction, zip_name, len(names)))

        frames = []
        for i, name in enumerate(names, start=1):
            img = Image.open(io.BytesIO(zf.read(name))).convert('RGBA')
            frames.append((i, img))

        union = None
        for i, img in frames:
            box = alpha_bbox(img)
            if box is None:
                continue
            if union is None:
                union = list(box)
            else:
                union[0] = min(union[0], box[0])
                union[1] = min(union[1], box[1])
                union[2] = max(union[2], box[2])
                union[3] = max(union[3], box[3])

        if union is None:
            log('  !! {} 全部透明，跳过'.format(direction))
            continue

        src_w, src_h = frames[0][1].size
        crop_w = union[2] - union[0]
        crop_h = union[3] - union[1]
        log('  源画布 {}x{}，内容并集 ({},{})..({},{}) = {}x{}'.format(
            src_w, src_h, union[0], union[1], union[2], union[3], crop_w, crop_h))

        # 归一化缩放：高固定 TARGET_HEIGHT，宽按内容宽高比
        scale = TARGET_HEIGHT / float(crop_h)
        norm_w = max(1, int(round(crop_w * scale)))
        aspect = norm_w / float(TARGET_HEIGHT)
        log('  归一化：{}x{}（宽高比 {:.3f}，人物=画布全高、脚底=画布底）'.format(norm_w, TARGET_HEIGHT, aspect))

        saved = 0
        for i, img in frames:
            cropped = img.crop((union[0], union[1], union[2], union[3]))
            if cropped.size != (norm_w, TARGET_HEIGHT):
                cropped = cropped.resize((norm_w, TARGET_HEIGHT), Image.LANCZOS)
            cropped = bake_contact_shadow(cropped)
            cropped.save(os.path.join(out_dir, '{}_{:02d}.png'.format(prefix, i)))
            saved += 1

        metrics[direction] = {
            'source_canvas': [src_w, src_h],
            'content_aspect': aspect,          # 画布宽 / 画布高（人物宽/人物高）
            'frames': saved,
        }
        log('  输出 {} 张 {}x{} 到 {}'.format(saved, norm_w, TARGET_HEIGHT, out_dir))

# ---- 阴影贴图：中心不透明、边缘渐隐的椭圆（贴在正方形面片上，再压扁成椭圆） ----
shadow = Image.new('RGBA', (SHADOW_SIZE, SHADOW_SIZE), (0, 0, 0, 0))
px = shadow.load()
cx = cy = SHADOW_SIZE / 2.0
for y in range(SHADOW_SIZE):
    for x in range(SHADOW_SIZE):
        dx = (x + 0.5 - cx) / (SHADOW_SIZE / 2.0)
        dy = (y + 0.5 - cy) / (SHADOW_SIZE / 2.0)
        d = (dx * dx + dy * dy) ** 0.5
        if d >= 1.0:
            continue
        a = (1.0 - d) ** 1.6 * 0.62
        px[x, y] = (0, 0, 0, int(max(0.0, min(1.0, a)) * 255))
shadow.save(os.path.join(OUT_ROOT, 'T_Wife_Shadow.png'))
log('阴影贴图：{}x{}（中心 0.62 不透明度，边缘渐隐）'.format(SHADOW_SIZE, SHADOW_SIZE))

# ---- 抖动版阴影贴图（给 **Masked** 材质用）----
# 为什么需要它：本工程里"运行时用 Python 建的 Translucent 材质"实测渲染不出来
# （和 UI 交付里那几张"贴图不渲染"同族问题）。而甲虫的 M_BeetleSprite 是 **Masked** 且已验证能画，
# 所以阴影也走 Masked：把"渐隐"用 8x8 Bayer 抖动编码成二值 alpha，视觉上就是一圈柔和的渐变阴影。
BAYER8 = [
    [0, 32, 8, 40, 2, 34, 10, 42],
    [48, 16, 56, 24, 50, 18, 58, 26],
    [12, 44, 4, 36, 14, 46, 6, 38],
    [60, 28, 52, 20, 62, 30, 54, 22],
    [3, 35, 11, 43, 1, 33, 9, 41],
    [51, 19, 59, 27, 49, 17, 57, 25],
    [15, 47, 7, 39, 13, 45, 5, 37],
    [63, 31, 55, 23, 61, 29, 53, 21],
]
dither = Image.new('RGBA', (SHADOW_SIZE, SHADOW_SIZE), (0, 0, 0, 0))
dpx = dither.load()
for y in range(SHADOW_SIZE):
    for x in range(SHADOW_SIZE):
        dx = (x + 0.5 - cx) / (SHADOW_SIZE / 2.0)
        dy = (y + 0.5 - cy) / (SHADOW_SIZE / 2.0)
        d = (dx * dx + dy * dy) ** 0.5
        if d >= 1.0:
            continue
        a = (1.0 - d) ** 1.6 * 0.85
        threshold = (BAYER8[y % 8][x % 8] + 0.5) / 64.0
        if a > threshold:
            dpx[x, y] = (0, 0, 0, 255)
dither.save(os.path.join(OUT_ROOT, 'T_Wife_ShadowDither.png'))
log('抖动阴影贴图：{}x{}（Bayer 8x8，给 Masked 材质用，二值 alpha 但视觉上连续）'.format(SHADOW_SIZE, SHADOW_SIZE))

with open(os.path.join(ROOT, 'Saved', 'wife_sprites_metrics.json'), 'w', encoding='utf-8') as f:
    json.dump(metrics, f, ensure_ascii=False, indent=2)

log('')
log('=== 归一化后汇总 ===')
for direction, m in metrics.items():
    log('  {:<6} 宽高比 {:.3f}  帧数 {}（源画布 {}x{}）'.format(
        direction, m['content_aspect'], m['frames'], m['source_canvas'][0], m['source_canvas'][1]))

with open(REPORT, 'w', encoding='utf-8') as f:
    f.write('\n'.join(report))
print('\n'.join(report))
