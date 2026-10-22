# -*- coding: utf-8 -*-
import os
import zipfile, io, os
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = r"D:\文件\微信聊天记录\xwechat_files\wxid_0fqvhh03wqta22_cb19\temp\RWTemp\2026-10\39e6e23740a1e0d1ec791f65dfa4bf4c\图标.zip"
OUT = os.path.join(ROOT, "SourceArt", "UI", "Icons")
os.makedirs(OUT, exist_ok=True)
MAP = {
 "工资结算单":"Payslip", "日记本和笔":"HomeworkNotebook", "用药清单":"MedicationList",
 "红糖":"BrownSugar", "药片":"BpPill", "蝴蝶结":"BowHairClip", "降压药":"BpMedicineBottle",
 "面条":"Noodles", "风湿贴":"PainPatch", "麦片":"Cereal",
 "棉签":"SKIP", "棉签罐":"SKIP", "瓜果蔬菜":"SKIP", "铁丝":"SKIP", "长棍发卡":"SKIP",
}
SIZE = 256
MARGIN = 0.05
z = zipfile.ZipFile(SRC)
log=[]
for info in z.infolist():
    if info.is_dir(): continue
    raw = info.filename
    stem = os.path.splitext(os.path.basename(raw))[0]
    item = MAP.get(stem, None)
    if item is None:
        log.append("  ? 未映射: %s -> 跳过" % stem); continue
    if item == "SKIP":
        log.append("  - 跳过（道具已删）: %s" % stem); continue
    im = Image.open(io.BytesIO(z.read(info))).convert("RGBA")
    bbox = im.getchannel("A").getbbox()
    if bbox: im = im.crop(bbox)
    w,h = im.size
    side = max(w,h)
    pad = int(round(side * MARGIN))
    canvas = Image.new("RGBA", (side+2*pad, side+2*pad), (0,0,0,0))
    canvas.paste(im, ((side-w)//2+pad, (side-h)//2+pad), im)
    canvas = canvas.resize((SIZE,SIZE), Image.LANCZOS)
    out = os.path.join(OUT, "T_Item_%s.png" % item)
    canvas.save(out)
    log.append("  ✓ %-18s -> %-18s 裁切 %dx%d 源(bbox后) -> 256x256" % (stem, item, w, h))
open(os.path.join(ROOT, "Saved", "_icons_stage.txt"),"w",encoding="utf-8").write("\n".join(log))
print("ok", len([l for l in log if "✓" in l]))
