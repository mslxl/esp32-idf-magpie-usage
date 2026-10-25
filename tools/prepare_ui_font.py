"""Create the product's licensed Chinese UI subset, with a pinned source hash.

uv run --with fonttools==4.62.1 python -I tools/prepare_ui_font.py
"""
from pathlib import Path
import hashlib
import json
import shutil
import tempfile
import urllib.request
from fontTools import subset
from fontTools.ttLib import TTFont

ROOT = Path(__file__).resolve().parents[1]
URL = "https://raw.githubusercontent.com/notofonts/noto-cjk/f8d157532fbfaeda587e826d4cd5b21a49186f7c/Sans/OTF/Japanese/NotoSansCJKjp-Regular.otf"
SHA = "68a3fc98800b2a27b371f2fb79991daf3633bd89309d4ffaa6946fd587f375b5"
with tempfile.TemporaryDirectory(prefix="note4-font-") as temporary:
    source = Path(temporary) / "source.otf"
    urllib.request.urlretrieve(URL, source)
    if hashlib.sha256(source.read_bytes()).hexdigest() != SHA:
        raise SystemExit("Font source hash mismatch")
    font = TTFont(source)
    policy = json.loads((ROOT / "fonts.json").read_text(encoding="utf-8"))
    points = {ord(c) for c in policy["characters"]} | set(range(32, 127))
    options = subset.Options()
    options.name_IDs = ["*"]
    options.name_legacy = True
    options.name_languages = ["*"]
    cutter = subset.Subsetter(options=options)
    cutter.populate(unicodes=points)
    cutter.subset(font)
    missing = points - set(font.getBestCmap())
    if missing:
        raise SystemExit(f"UI font missing scalars: {sorted(missing)}")
    for record in font["name"].names:
        if record.nameID in (1, 4, 6):
            record.string = "NOTE4 UI".encode(record.getEncoding())
    target = ROOT / "assets" / "fonts"
    target.mkdir(parents=True, exist_ok=True)
    font.save(target / "note4-ui.otf")
    shutil.copyfile(ROOT / "third_party/pocketjs/assets/fonts/LICENSE-NotoSansCJK.txt", target / "OFL.txt")
    print(f"Prepared NOTE4 UI: {len(points)} scalars")
