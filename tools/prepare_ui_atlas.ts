import { readFileSync, writeFileSync } from "node:fs";
import { resolve } from "node:path";
import opentype from "../third_party/pocketjs/node_modules/opentype.js";
import { bakeSlot, DEFAULT_REGULAR } from "../third_party/pocketjs/framework/compiler/bake-font";
import { pack, unpack } from "../third_party/pocketjs/framework/compiler/pak";
import { decodePocketPackage, encodePocketPackage, POCKET_SECTION } from "../third_party/pocketjs/contracts/spec/pocket-package";

const directory = resolve(import.meta.dir, "../.pocket-build/package");
const packagePath = resolve(directory, "note4-hello.pocket");
const product = decodePocketPackage(readFileSync(packagePath));
const variant = product.variants.find(value => value.target === "note4-hello");
const section = variant?.sections.find(value => value.kind === POCKET_SECTION.pak);
if (!section) throw new Error("NOTE4 package asset section missing");
const blobs = unpack(section.bytes);
const base = blobs.find(blob => blob.key === "ui:font.2");
if (!base) throw new Error("NOTE4 base font missing");
const header = new DataView(base.data.buffer, base.data.byteOffset, base.data.byteLength);
const count = header.getUint16(6, true);
const codepoints = Array.from({ length: count }, (_, index) => header.getUint32(16 + index * 8, true));
const load = (path: string) => {
  const bytes = readFileSync(path);
  return opentype.parse(bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength));
};
const atlas = bakeSlot(load(DEFAULT_REGULAR), 21, 15, false, codepoints, 1,
  [load(resolve(import.meta.dir, "../assets/fonts/note4-ui.otf"))]);
const icons: Record<string, string[]> = JSON.parse(readFileSync(resolve(import.meta.dir, "../assets/provider-icons.json"), "utf8"));
const images = Object.entries(icons).map(([name, rows]) => {
  if (rows.length !== 16 || rows.some(row => !/^[.#]{16}$/.test(row))) throw new Error(`Invalid NOTE4 icon: ${name}`);
  const data = new Uint8Array(8 + 16 * 16 * 4), view = new DataView(data.buffer);
  view.setUint16(0, 16, true); view.setUint16(2, 16, true); data[4] = 3;
  rows.forEach((row, y) => Array.from(row).forEach((pixel, x) => {
    const offset = 8 + (y * 16 + x) * 4;
    data[offset] = data[offset + 1] = data[offset + 2] = pixel === "#" ? 0 : 255;
    data[offset + 3] = 255;
  }));
  return { key: `ui:img.note4-${name}`, dtype: 0, data };
});
section.bytes = pack([...blobs.filter(blob => blob.key !== "ui:font.21" && !blob.key.startsWith("ui:img.note4-")),
  { key: "ui:font.21", dtype: base.dtype, data: atlas.bytes }, ...images]);
const encoded = encodePocketPackage(product);
// Validate the final package, including its recomputed identity hashes, before publishing either output.
decodePocketPackage(encoded);
writeFileSync(resolve(directory, "note4-hello.pak"), section.bytes);
writeFileSync(packagePath, encoded);
console.log(`NOTE4 15px atlas: slot 21, ${atlas.glyphCount} glyphs, ${atlas.cellW}x${atlas.cellH}, ${atlas.bytes.length} bytes`);
