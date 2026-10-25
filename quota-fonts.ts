import { get } from "./third_party/pocketjs/framework/src/pak";

const heights = new Map<number, number>();
export function quotaLineHeight(slot: number): number {
  let height = heights.get(slot);
  if (height === undefined) {
    const bytes = get(`ui:font.${slot}`, 0, 16);
    height = Math.max(bytes[9], bytes[11]);
    if (!height) throw new Error("Invalid NOTE4 font metrics");
    heights.set(slot, height);
  }
  return height;
}
