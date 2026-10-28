pub(crate) fn crc32(data: &[u8]) -> u32 {
    let mut crc = u32::MAX;
    for &byte in data {
        crc ^= u32::from(byte);
        for _ in 0..8 {
            crc = (crc >> 1) ^ (0xedb8_8320 & 0u32.wrapping_sub(crc & 1));
        }
    }
    !crc
}

#[unsafe(no_mangle)]
pub extern "C" fn note4_rs_crc32(data: *const u8, length: usize) -> u32 {
    if data.is_null() && length != 0 {
        return 0;
    }
    let bytes = if length == 0 {
        &[]
    } else {
        unsafe { core::slice::from_raw_parts(data, length) }
    };
    crc32(bytes)
}
