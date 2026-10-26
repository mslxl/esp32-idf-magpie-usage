use crate::resources::*;

const F14: &[u8] = include_bytes!("../../../.note4-build/native-assets/font14.bin");
const F15: &[u8] = include_bytes!("../../../.note4-build/native-assets/font15.bin");
const F16: &[u8] = include_bytes!("../../../.note4-build/native-assets/font16.bin");
const F18: &[u8] = include_bytes!("../../../.note4-build/native-assets/font18.bin");
fn u32_at(bytes: &[u8], at: usize) -> usize {
    u32::from_le_bytes(bytes[at..at + 4].try_into().unwrap()) as usize
}
const B14: &[u8] = include_bytes!("../../../.note4-build/native-assets/font14-bold.bin");
const B15: &[u8] = include_bytes!("../../../.note4-build/native-assets/font15-bold.bin");
const B16: &[u8] = include_bytes!("../../../.note4-build/native-assets/font16-bold.bin");
const B18: &[u8] = include_bytes!("../../../.note4-build/native-assets/font18-bold.bin");
fn data(size: u8, bold: bool) -> &'static [u8] {
    match (size,bold) { (14,false)=>F14,(15,false)=>F15,(18,false)=>F18,(_,false)=>F16,
        (14,true)=>B14,(15,true)=>B15,(18,true)=>B18,(_,true)=>B16 }
}
fn glyph(cp: char, size: u8, bold: bool) -> Option<(&'static [u8], usize, usize)> {
    let bytes = data(size,bold);
    let count = u32_at(bytes, 0);
    let mut lo = 0;
    let mut hi = count;
    while lo < hi {
        let mid = (lo + hi) / 2;
        let at = 4 + mid * 16;
        match u32_at(bytes, at).cmp(&(cp as usize)) {
            core::cmp::Ordering::Less => lo = mid + 1,
            core::cmp::Ordering::Greater => hi = mid,
            core::cmp::Ordering::Equal => return Some((bytes, at, 4 + count * 16)),
        }
    }
    None
}
pub fn covered(cp: char, size: u8) -> bool { glyph(cp, size, false).is_some() }
pub fn advance(cp: char, size: u8) -> i16 { advance_weight(cp,size,false) }
pub fn advance_weight(cp:char,size:u8,bold:bool)->i16 {
    glyph(cp,size,bold).map(|(b,at,_)|b[at+12] as i16).unwrap_or(size as i16)
}
pub fn measure(text: &str, size: u8) -> i16 { measure_weight(text,size,false) }
pub fn measure_weight(text:&str,size:u8,bold:bool)->i16 {
    text.chars().map(|c|advance_weight(c,size,bold) as i32).sum::<i32>().min(32767) as i16
}
pub struct Canvas<'a>(pub &'a mut [u16]);
impl Canvas<'_> {
    pub fn pixel(&mut self, x: i16, y: i16) {
        if (0..400).contains(&x) && (0..300).contains(&y) { self.0[y as usize * 400 + x as usize] = 0; }
    }
    pub fn rect(&mut self, x: i16, y: i16, w: i16, h: i16) {
        for py in y.max(0)..(y + h).min(300) {
            for px in x.max(0)..(x + w).min(400) { self.pixel(px, py); }
        }
    }
    pub fn text(&mut self, text: &str, mut x: i16, y: i16, size: u8, bold: bool) {
        for cp in text.chars() {
            if let Some((bytes, at, base)) = glyph(cp, size, bold) {
                let offset = base + u32_at(bytes, at + 4);
                let w = bytes[at + 8] as usize;
                let h = bytes[at + 9] as usize;
                let left = bytes[at + 10] as i8 as i16;
                let top = bytes[at + 11] as i8 as i16;
                for py in 0..h {
                    for px in 0..w {
                        let bit = py * w + px;
                        if bytes[offset + bit / 8] & (128 >> (bit % 8)) != 0 {
                            let gx = x + left + px as i16;
                            let gy = y + size as i16 - top + py as i16;
                            self.pixel(gx, gy);
                        }
                    }
                }
            }
            x += advance_weight(cp,size,bold);
        }
    }
    pub fn icon(&mut self, id: u8, x: i16, y: i16) {
        let rows = match id { 1 => &ICON_CODEX, 2 => &ICON_CLAUDE, 3 => &ICON_COPILOT, 4 => &ICON_DEEPSEEK, _ => &ICON_GENERIC };
        for (py, row) in rows.iter().enumerate() {
            for px in 0..16 { if row & (0x8000 >> px) != 0 { self.pixel(x + px, y + py as i16 + 1); } }
        }
    }
}
pub fn icon(provider: &str) -> u8 {
    fn contains(value: &str, token: &str) -> bool {
        value.as_bytes().windows(token.len()).any(|part| part.eq_ignore_ascii_case(token.as_bytes()))
    }
    if contains(provider, "codex") { 1 } else if contains(provider, "claude") { 2 }
    else if contains(provider, "copilot") { 3 } else if contains(provider, "deepseek") { 4 } else { 0 }
}
