"""Bake monochrome fonts, provider icons and timezone rules for the Rust UI.

Run with uv run --with freetype-py==2.5.1 python -I tools/prepare_native_ui.py.
Generated resources live outside Git.
"""
import json
from pathlib import Path
import re
import struct
import freetype

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / '.note4-build' / 'native-assets'


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    cjk = freetype.Face(str(ROOT / 'assets/fonts/note4-ui.otf'))
    latin = [freetype.Face(str(ROOT / f'assets/fonts/arimo-{weight}.ttf')) for weight in ('regular', 'bold')]
    policy = json.loads((ROOT / 'fonts.json').read_text(encoding='utf-8'))
    chars = sorted(set(map(ord, policy['characters'])) | set(range(32, 127)))
    for size in (14, 15, 16, 18):
        for face in [cjk, *latin]:
            face.set_pixel_sizes(0, size)
        for bold in (False, True):
            records, pixels = bytearray(), bytearray()
            count = 0
            for cp in chars:
                face = latin[int(bold)] if cp < 127 else cjk
                if not face.get_char_index(cp):
                    continue
                face.load_char(chr(cp), freetype.FT_LOAD_RENDER | freetype.FT_LOAD_TARGET_MONO)
                glyph = face.glyph
                bitmap = glyph.bitmap
                embolden = int(bold and cp >= 127 and bitmap.width > 0)
                width = bitmap.width + embolden
                bits = bytearray((width * bitmap.rows + 7) // 8)
                for y in range(bitmap.rows):
                    for x in range(bitmap.width):
                        if bitmap.buffer[y * abs(bitmap.pitch) + x // 8] & (128 >> (x % 8)):
                            for dx in range(embolden + 1):
                                index = y * width + x + dx
                                bits[index // 8] |= 128 >> (index % 8)
                records.extend(struct.pack('<IIBBbbB3x', cp, len(pixels), width, bitmap.rows,
                                           glyph.bitmap_left, glyph.bitmap_top, round(glyph.advance.x / 64)))
                pixels.extend(bits)
                count += 1
            suffix = '-bold' if bold else ''
            (OUT / f'font{size}{suffix}.bin').write_bytes(struct.pack('<I', count) + records + pixels)
    icons = json.loads((ROOT / 'assets/provider-icons.json').read_text())
    rust = []
    for name, rows in icons.items():
        rust.append(f'pub const ICON_{name.upper()}: [u16; 16] = ' + repr([int(row.replace('.', '0').replace('#', '1'), 2) for row in rows]) + ';')
    data = (ROOT / 'timezone-data.ts').read_text(encoding='utf-8')
    rules = json.loads(re.search(r'= (\{.*\});', data)[1])
    rust.append('pub const ZONES: &[(&str, &[(i64, i32)])] = &[')
    for name, transitions in rules.items():
        rust.append(f'({json.dumps(name)}, &[' + ','.join(f'({at},{offset})' for at, offset in transitions) + ']),')
    rust.append('];')
    (OUT / 'resources.rs').write_text('\n'.join(rust), encoding='utf-8')
    print(f'Rust UI resources: {count} glyphs per font, {len(icons)} icons, {len(rules)} timezones')


if __name__ == '__main__':
    main()
