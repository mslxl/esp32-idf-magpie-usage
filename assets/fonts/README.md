# NOTE4 UI font

`note4-ui.otf` is a renamed subset of Noto Sans CJK JP Regular, distributed under the SIL Open Font License 1.1 in `OFL.txt`. The source copyright notice is retained in the font's name table: Copyright 2014–2021 Adobe (http://www.adobe.com/).

Source: <https://github.com/notofonts/noto-cjk/blob/f8d157532fbfaeda587e826d4cd5b21a49186f7c/Sans/OTF/Japanese/NotoSansCJKjp-Regular.otf>

Source SHA-256: `68a3fc98800b2a27b371f2fb79991daf3633bd89309d4ffaa6946fd587f375b5`.

Regenerate from the repository root after changing the fixed UI character policy in `fonts.json`:

```powershell
uv run --with fonttools==4.62.1 python -I tools/prepare_ui_font.py
just prepare
just test-ui
```

Latin text uses Arimo Regular and Bold, an Arial-metric-compatible family under SIL OFL 1.1 (`Arimo-OFL.txt`). Sources are pinned to <https://github.com/googlefonts/Arimo/tree/4a6255f269916ae7ad3fc2706b0935e7621396b8/fonts/ttf>. SHA-256: Regular `41b22bc8f0b51f932825d37bc55b5eb6ba67dfe599a626e4aff2b43b624f9f8c`, Bold `d7a8b187cf8444d4cfee102e8eae9e3043682fd5106d5d33ed677fe268a0e2ba`. Both source fonts are tracked for reproducible offline baking. CJK bold uses a one-pixel expansion of the licensed Noto bitmap.

The script verifies the downloaded source hash, keeps the source copyright/license metadata, and renames the subset to NOTE4 UI. The native resource generator bakes the configured characters into monochrome Rust font tables. Device text uses the baked cmap for coverage checks; unsupported input scalars are shown as Unicode code labels while the original text remains in the native model.
