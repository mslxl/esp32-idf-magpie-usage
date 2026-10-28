const ENDPOINT_MAX: usize = 512;
const KEY_MAX: usize = 256;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) struct Canonical {
    bytes: [u8; ENDPOINT_MAX + 1],
    len: usize,
}

impl Canonical {
    pub(crate) fn as_slice(&self) -> &[u8] {
        &self.bytes[..self.len]
    }
}

fn endpoint_valid(url: &[u8]) -> bool {
    let host_start = if url.starts_with(b"https://") {
        8
    } else if url.starts_with(b"http://") {
        7
    } else {
        return false;
    };
    let end = url[host_start..]
        .iter()
        .position(|&b| b == b'/')
        .map_or(url.len(), |offset| host_start + offset);
    if end == host_start
        || url
            .iter()
            .any(|&b| b <= 32 || b == 127 || matches!(b, b'@' | b'?' | b'#' | b'\\'))
    {
        return false;
    }
    let host = &url[host_start..end];
    let port_start = if host.first() == Some(&b'[') {
        let Some(close_rel) = host.iter().position(|&b| b == b']') else {
            return false;
        };
        let close = host_start + close_rel;
        if close == host_start + 1
            || !url[host_start + 1..close]
                .iter()
                .all(|&b| b.is_ascii_hexdigit() || b == b':' || b == b'.')
        {
            return false;
        }
        if close + 1 != end {
            if url[close + 1] != b':' {
                return false;
            }
            Some(close + 2)
        } else {
            None
        }
    } else {
        let mut index = host_start;
        while index < end && url[index] != b':' {
            if !(url[index].is_ascii_alphanumeric() || matches!(url[index], b'-' | b'.')) {
                return false;
            }
            index += 1;
        }
        if index == host_start {
            return false;
        }
        if index != end {
            Some(index + 1)
        } else {
            None
        }
    };
    if let Some(port) = port_start {
        if port == end {
            return false;
        }
        let mut value = 0u32;
        for &byte in &url[port..end] {
            if !byte.is_ascii_digit() {
                return false;
            }
            value = value * 10 + u32::from(byte - b'0');
            if value > 65535 {
                return false;
            }
        }
        if value == 0 {
            return false;
        }
    }
    true
}

fn decimal(mut value: u32, output: &mut [u8]) -> usize {
    let mut length = 0;
    loop {
        output[output.len() - 1 - length] = b'0' + (value % 10) as u8;
        length += 1;
        value /= 10;
        if value == 0 {
            break;
        }
    }
    for index in 0..length {
        output[index] = output[output.len() - length + index];
    }
    length
}

pub(crate) fn canonicalize(endpoint: &[u8]) -> Option<Canonical> {
    if endpoint.len() > ENDPOINT_MAX {
        return None;
    }
    let mut result = Canonical {
        bytes: [0; ENDPOINT_MAX + 1],
        len: endpoint.len(),
    };
    result.bytes[..endpoint.len()].copy_from_slice(endpoint);
    if endpoint.is_empty() {
        return Some(result);
    }
    if !endpoint_valid(endpoint) {
        return None;
    }
    let scheme = if endpoint.starts_with(b"https://") {
        8
    } else {
        7
    };
    let authority_end = result.bytes[scheme..result.len]
        .iter()
        .position(|&b| b == b'/')
        .map_or(result.len, |offset| scheme + offset);
    for byte in &mut result.bytes[scheme..authority_end] {
        byte.make_ascii_lowercase();
    }
    let port_start = if result.bytes[scheme] == b'[' {
        result.bytes[scheme..authority_end]
            .iter()
            .position(|&b| b == b']')
            .and_then(|close| (scheme + close + 1 < authority_end).then_some(scheme + close + 2))
    } else {
        result.bytes[scheme..authority_end]
            .iter()
            .position(|&b| b == b':')
            .map(|offset| scheme + offset + 1)
    };
    if let Some(port_start) = port_start {
        let mut number = 0u32;
        for &byte in &result.bytes[port_start..authority_end] {
            number = number * 10 + u32::from(byte - b'0');
        }
        let colon = port_start - 1;
        let default = (scheme == 8 && number == 443) || (scheme == 7 && number == 80);
        let mut replacement = [0u8; 6];
        replacement[0] = b':';
        let replacement_len = if default {
            0
        } else {
            1 + decimal(number, &mut replacement[1..])
        };
        let old_len = authority_end - colon;
        if replacement_len > old_len && result.len + replacement_len - old_len > ENDPOINT_MAX {
            return None;
        }
        if replacement_len != old_len {
            let tail_len = result.len - authority_end;
            if replacement_len > old_len {
                let delta = replacement_len - old_len;
                for index in (0..=tail_len).rev() {
                    result.bytes[colon + replacement_len + index] =
                        result.bytes[authority_end + index];
                }
                result.len += delta;
            } else {
                let delta = old_len - replacement_len;
                for index in 0..=tail_len {
                    result.bytes[colon + replacement_len + index] =
                        result.bytes[authority_end + index];
                }
                result.len -= delta;
            }
        }
        result.bytes[colon..colon + replacement_len]
            .copy_from_slice(&replacement[..replacement_len]);
    }
    while result.len > scheme && result.bytes[result.len - 1] == b'/' {
        result.len -= 1;
    }
    if let Some(path) = result.bytes[scheme..result.len]
        .iter()
        .position(|&b| b == b'/')
        .map(|offset| scheme + offset)
    {
        let tail = &result.bytes[path..result.len];
        for (index, &byte) in tail.iter().enumerate() {
            let dot_path = byte == b'/'
                && tail.get(index + 1) == Some(&b'.')
                && matches!(tail.get(index + 2), None | Some(b'/') | Some(b'.'));
            if byte == b'%' || (byte == b'/' && tail.get(index + 1) == Some(&b'/')) || dot_path {
                return None;
            }
        }
    }
    Some(result)
}

pub(crate) fn valid_config(endpoint: &[u8], key: &[u8]) -> bool {
    if endpoint.len() > ENDPOINT_MAX || key.len() > KEY_MAX {
        return false;
    }
    if endpoint.is_empty() {
        return key.is_empty();
    }
    endpoint_valid(endpoint)
        && (key.is_empty()
            || (key.len() >= 16
                && key
                    .iter()
                    .all(|&b| b.is_ascii_alphanumeric() || matches!(b, b'-' | b'.' | b'_' | b'~'))))
}

#[repr(C)]
pub(crate) struct CConfig {
    endpoint: [u8; ENDPOINT_MAX + 1],
    key: [u8; KEY_MAX + 1],
}

const _: () = {
    assert!(core::mem::size_of::<CConfig>() == 770);
    assert!(core::mem::offset_of!(CConfig, key) == 513);
};

fn nul_string(bytes: &[u8]) -> Option<&[u8]> {
    let end = bytes.iter().position(|&b| b == 0)?;
    Some(&bytes[..end])
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn note4_rs_magpie_endpoint_canonicalize(
    endpoint: *const u8,
    length: usize,
    output: *mut u8,
    capacity: usize,
) -> i32 {
    if (endpoint.is_null() && length != 0)
        || output.is_null()
        || capacity == 0
        || length > ENDPOINT_MAX
    {
        return 0;
    }
    let input = if length == 0 {
        &[]
    } else {
        core::slice::from_raw_parts(endpoint, length)
    };
    let Some(value) = canonicalize(input) else {
        return 0;
    };
    if value.len + 1 > capacity {
        return 0;
    }
    core::ptr::copy_nonoverlapping(value.as_slice().as_ptr(), output, value.len);
    *output.add(value.len) = 0;
    1
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn note4_rs_magpie_config_validate(config: *const CConfig) -> i32 {
    if config.is_null() {
        return 0;
    }
    let config = &*config;
    let Some(endpoint) = nul_string(&config.endpoint) else {
        return 0;
    };
    let Some(key) = nul_string(&config.key) else {
        return 0;
    };
    valid_config(endpoint, key) as i32
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn note4_rs_magpie_config_apply(
    target: *mut CConfig,
    saved: *const CConfig,
    payload: *const u8,
    length: usize,
) -> i32 {
    if target.is_null() || saved.is_null() || payload.is_null() || length < 5 {
        return 0;
    }
    let input = core::slice::from_raw_parts(payload, length);
    let mode = input[0];
    let endpoint_length = u16::from_le_bytes([input[1], input[2]]) as usize;
    let key_length = u16::from_le_bytes([input[3], input[4]]) as usize;
    if mode > 2
        || endpoint_length > ENDPOINT_MAX
        || key_length > KEY_MAX
        || length != 5 + endpoint_length + key_length
        || (mode != 1 && key_length != 0)
        || (mode == 1 && key_length == 0)
        || input[5..].contains(&0)
    {
        return 0;
    }
    let mut next = CConfig {
        endpoint: [0; ENDPOINT_MAX + 1],
        key: [0; KEY_MAX + 1],
    };
    next.endpoint[..endpoint_length].copy_from_slice(&input[5..5 + endpoint_length]);
    let saved_endpoint = match nul_string(&(*saved).endpoint) {
        Some(value) => value,
        None => return 0,
    };
    let saved_key = match nul_string(&(*saved).key) {
        Some(value) => value,
        None => return 0,
    };
    if mode == 0 {
        if saved_endpoint != &next.endpoint[..endpoint_length] {
            return 0;
        }
        next.key[..saved_key.len()].copy_from_slice(saved_key);
    } else if mode == 1 {
        next.key[..key_length].copy_from_slice(&input[5 + endpoint_length..]);
    }
    if !valid_config(
        &next.endpoint[..endpoint_length],
        nul_string(&next.key).unwrap_or(&[]),
    ) {
        return 0;
    }
    next.endpoint[endpoint_length] = 0;
    *target = next;
    1
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn note4_rs_magpie_config_metadata(
    config: *const CConfig,
    output: *mut u8,
    capacity: usize,
) -> usize {
    if config.is_null() || output.is_null() {
        return 0;
    }
    let config = &*config;
    let Some(endpoint) = nul_string(&config.endpoint) else {
        return 0;
    };
    let Some(key) = nul_string(&config.key) else {
        return 0;
    };
    if !valid_config(endpoint, key) || capacity < endpoint.len() + 3 {
        return 0;
    }
    *output = endpoint.len() as u8;
    *output.add(1) = (endpoint.len() >> 8) as u8;
    *output.add(2) = (!key.is_empty()) as u8;
    core::ptr::copy_nonoverlapping(endpoint.as_ptr(), output.add(3), endpoint.len());
    endpoint.len() + 3
}
