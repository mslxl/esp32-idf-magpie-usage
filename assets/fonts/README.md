# NOTE4 UI font

`note4-ui.otf` is a renamed subset of Noto Sans CJK JP Regular, distributed under the SIL Open Font License 1.1 in `OFL.txt`. The source copyright notice is retained in the font's name table: Copyright 2014–2021 Adobe (http://www.adobe.com/).

Source: <https://github.com/notofonts/noto-cjk/blob/f8d157532fbfaeda587e826d4cd5b21a49186f7c/Sans/OTF/Japanese/NotoSansCJKjp-Regular.otf>

Source SHA-256: `68a3fc98800b2a27b371f2fb79991daf3633bd89309d4ffaa6946fd587f375b5`.

Regenerate from the repository root after changing the fixed UI character policy in `fonts.json`:

```powershell
uv run --with fonttools==4.62.1 python -I tools/prepare_ui_font.py
just package-pinned
bun test tests/quota-app.test.ts
```

The script verifies the downloaded source hash, keeps the source copyright/license metadata, and renames the subset to NOTE4 UI. PocketJS bakes the configured characters into the application package. Device text uses the baked cmap for coverage checks; unsupported input scalars are shown as Unicode code labels while the original text remains in the native model.
