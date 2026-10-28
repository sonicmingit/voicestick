"""Original code-drawn device icon; no SayAll brand artwork is reused."""
from pathlib import Path
from PIL import Image, ImageDraw
import struct

root = Path(__file__).resolve().parents[1]
def icon(size, blue=False):
    image = Image.new("RGBA", (256,256))
    draw = ImageDraw.Draw(image)
    draw.rounded_rectangle((0,0,255,255), radius=50, fill="#142332")
    draw.rounded_rectangle((76,30,180,229), radius=24, fill="#55b8e8" if blue else "#ff942f")
    draw.rounded_rectangle((87,47,169,159), radius=12, fill="#142332")
    draw.rounded_rectangle((116,70,140,111), radius=12, fill="#ffffff")
    draw.arc((106,85,150,126),0,180,fill="#ffffff",width=5)
    draw.line((128,126,128,138),fill="#ffffff",width=5)
    draw.line((116,138,140,138),fill="#ffffff",width=5)
    draw.ellipse((112,179,144,211),fill="#142332")
    return image.resize((size,size), Image.Resampling.LANCZOS)

icons = root / "src-tauri/icons"
for path in icons.rglob("*.png"):
    # Existing names are retained to keep native resource references compatible.
    with Image.open(path) as old: size = old.width
    icon(size, "faceted" in path.name).save(path)
for path in icons.rglob("*.ico"):
    icon(256, "faceted" in path.name).save(path, sizes=[(16,16),(32,32),(48,48),(64,64),(128,128),(256,256)])
    # Tauri takes the first ICO directory entry as its default window image.
    data = bytearray(path.read_bytes())
    count = struct.unpack_from("<H", data, 4)[0]
    entries = [data[6 + index*16:22 + index*16] for index in range(count)]
    entries.sort(key=lambda entry: (entry[0] or 256)*(entry[1] or 256), reverse=True)
    data[6:6 + count*16] = b"".join(entries)
    path.write_bytes(data)
for name, blue in [("app-logo.png",False),("favicon.png",False),("app-icon-faceted-duck.png",True)]:
    icon(256,blue).save(root / "public" / name)
(root / "public/sticks3-logo.svg").write_text('''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 256 256"><rect x="8" y="8" width="240" height="240" rx="56" fill="#142332"/><rect x="76" y="30" width="104" height="199" rx="24" fill="#ff942f"/><rect x="87" y="47" width="82" height="112" rx="12" fill="#142332"/><rect x="116" y="70" width="24" height="41" rx="12" fill="white"/><path d="M106 106a22 22 0 0 0 44 0m-22 22v10m-12 0h24" fill="none" stroke="white" stroke-width="5"/><circle cx="128" cy="195" r="16" fill="#142332"/></svg>''', encoding="utf-8")
print("Original StickS3 icons generated.")
