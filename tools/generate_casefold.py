"""Generate identical locale-independent Unicode casefold tables for C and TS.

Run with Python 3.12: uv run --python 3.12 python -I tools/generate_casefold.py
"""
from pathlib import Path
import json
import unicodedata

ROOT = Path(__file__).resolve().parents[1]
assert unicodedata.unidata_version == "15.0.0", unicodedata.unidata_version
folds = [(cp, chr(cp).casefold()) for cp in range(0x110000) if chr(cp).casefold() != chr(cp)]
c = "/* Generated Unicode 15.0.0 full case folding. */\n"
c += "typedef struct { uint32_t codepoint; const char *replacement; } note4_casefold_entry_t;\n"
c += "static const note4_casefold_entry_t NOTE4_CASEFOLD[] = {\n"
for cp, text in folds:
    escaped = "".join(f"\\x{byte:02x}" for byte in text.encode("utf-8"))
    c += f'    {{{cp}U, "{escaped}"}},\n'
c += "};\n"
(ROOT / "main/casefold_data.h").write_text(c, encoding="utf-8")
ts = "// Generated Unicode 15.0.0 full case folding.\n"
ts += "const FOLDS: Record<number, string> = " + json.dumps(dict(folds), ensure_ascii=True, separators=(",", ":")) + ";\n"
ts += "export function casefold(value: string): string { return Array.from(value, scalar => FOLDS[scalar.codePointAt(0)!] ?? scalar).join(''); }\n"
(ROOT / "provisioning-web/src/casefold.ts").write_text(ts, encoding="utf-8")
print(f"Generated {len(folds)} casefold mappings for Unicode {unicodedata.unidata_version}")
