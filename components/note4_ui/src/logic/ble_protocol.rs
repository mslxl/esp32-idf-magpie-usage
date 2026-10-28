pub(crate) const VERSION: u8 = 1;
pub(crate) const MAX_PAYLOAD: usize = 192;
const HEADER_SIZE: usize = 10;
const CRC_SIZE: usize = 2;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) struct Frame<'a> {
    pub version: u8,
    pub opcode: u8,
    pub request_id: u16,
    pub slot: u8,
    pub sequence: u8,
    pub total_sequences: u8,
    pub payload: &'a [u8],
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) enum DecodeError {
    Length = 2,
    Version = 1,
    Crc = 3,
    Sequence = 4,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) enum PasswordMode {
    Unchanged = 0,
    Set = 1,
    Clear = 2,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub(crate) struct SetSlot {
    pub enabled: bool,
    pub password_mode: PasswordMode,
    pub ssid: [u8; 33],
    pub ssid_length: u8,
    pub password: [u8; 65],
    pub password_length: u8,
}

pub(crate) fn crc16(data: &[u8]) -> u16 {
    let mut crc = 0xffffu16;
    for &byte in data {
        crc ^= u16::from(byte);
        for _ in 0..8 {
            crc = (crc >> 1) ^ (0xa001 & 0u16.wrapping_sub(crc & 1));
        }
    }
    crc
}

pub(crate) fn encoded_size(payload_length: usize) -> usize {
    if payload_length > MAX_PAYLOAD {
        0
    } else {
        HEADER_SIZE + payload_length + CRC_SIZE
    }
}

pub(crate) fn encode(frame: &Frame<'_>, output: &mut [u8]) -> Option<usize> {
    if frame.version != VERSION
        || frame.payload.len() > MAX_PAYLOAD
        || (frame.total_sequences == 0 || frame.sequence >= frame.total_sequences)
    {
        return None;
    }
    let size = encoded_size(frame.payload.len());
    if output.len() < size {
        return None;
    }
    output[0] = frame.version;
    output[1] = frame.opcode;
    output[2..4].copy_from_slice(&frame.request_id.to_le_bytes());
    output[4] = frame.slot;
    output[5] = frame.sequence;
    output[6] = frame.total_sequences;
    output[7..9].copy_from_slice(&(frame.payload.len() as u16).to_le_bytes());
    output[9] = 0;
    output[HEADER_SIZE..HEADER_SIZE + frame.payload.len()].copy_from_slice(frame.payload);
    let crc = crc16(&output[..size - CRC_SIZE]);
    output[size - CRC_SIZE..size].copy_from_slice(&crc.to_le_bytes());
    Some(size)
}

pub(crate) fn decode<'a>(input: &[u8], payload: &'a mut [u8]) -> Result<Frame<'a>, DecodeError> {
    if input.len() < HEADER_SIZE + CRC_SIZE {
        return Err(DecodeError::Length);
    }
    if input[0] != VERSION {
        return Err(DecodeError::Version);
    }
    let length = u16::from_le_bytes([input[7], input[8]]) as usize;
    let expected = encoded_size(length);
    if expected == 0 || input.len() != expected || payload.len() < length {
        return Err(DecodeError::Length);
    }
    let actual = u16::from_le_bytes([input[expected - 2], input[expected - 1]]);
    if crc16(&input[..expected - CRC_SIZE]) != actual {
        return Err(DecodeError::Crc);
    }
    if input[6] == 0 || input[5] >= input[6] {
        return Err(DecodeError::Sequence);
    }
    payload[..length].copy_from_slice(&input[HEADER_SIZE..HEADER_SIZE + length]);
    Ok(Frame {
        version: input[0],
        opcode: input[1],
        request_id: u16::from_le_bytes([input[2], input[3]]),
        slot: input[4],
        sequence: input[5],
        total_sequences: input[6],
        payload: &payload[..length],
    })
}

pub(crate) fn decode_set_slot(input: &[u8]) -> Option<SetSlot> {
    if input.len() < 3 {
        return None;
    }
    let enabled = input[0] != 0;
    let password_mode = match input[1] {
        0 => PasswordMode::Unchanged,
        1 => PasswordMode::Set,
        2 => PasswordMode::Clear,
        _ => return None,
    };
    let ssid_length = input[2] as usize;
    if ssid_length > 32
        || 3 + ssid_length > input.len()
        || (!enabled && ssid_length != 0)
        || (enabled && ssid_length == 0)
    {
        return None;
    }
    let password_offset = 3 + ssid_length;
    let password_length = match password_mode {
        PasswordMode::Set => {
            if password_offset >= input.len() {
                return None;
            }
            let length = input[password_offset] as usize;
            if length == 0 || length > 64 || password_offset + 1 + length != input.len() {
                return None;
            }
            length
        }
        PasswordMode::Unchanged | PasswordMode::Clear => {
            if password_offset != input.len() {
                return None;
            }
            0
        }
    };
    let mut slot = SetSlot {
        enabled,
        password_mode,
        ssid: [0; 33],
        ssid_length: ssid_length as u8,
        password: [0; 65],
        password_length: password_length as u8,
    };
    slot.ssid[..ssid_length].copy_from_slice(&input[3..password_offset]);
    if password_length != 0 {
        slot.password[..password_length].copy_from_slice(&input[password_offset + 1..]);
    }
    Some(slot)
}

#[repr(C)]
pub(crate) struct CFrame {
    version: u8,
    opcode: u8,
    request_id: u16,
    slot: u8,
    sequence: u8,
    total_sequences: u8,
    payload_length: u16,
    payload: *const u8,
}

#[repr(C)]
pub(crate) struct CSlot {
    enabled: u8,
    password_mode: u32,
    ssid_length: u8,
    ssid: [u8; 33],
    password_length: u8,
    password: [u8; 65],
}

const _: () = {
    const POINTER_ALIGN: usize = core::mem::align_of::<*const u8>();
    const POINTER_OFFSET: usize = (10 + POINTER_ALIGN - 1) / POINTER_ALIGN * POINTER_ALIGN;
    assert!(core::mem::size_of::<CFrame>() == POINTER_OFFSET + core::mem::size_of::<*const u8>());
    assert!(core::mem::offset_of!(CFrame, payload_length) == 8);
    assert!(core::mem::offset_of!(CFrame, payload) == POINTER_OFFSET);
    assert!(core::mem::size_of::<CSlot>() == 108);
    assert!(core::mem::offset_of!(CSlot, password) == 43);
};

#[unsafe(no_mangle)]
pub extern "C" fn note4_rs_ble_crc16(data: *const u8, size: usize) -> u16 {
    if data.is_null() && size != 0 {
        return 0;
    }
    if size == 0 {
        return crc16(&[]);
    }
    crc16(unsafe { core::slice::from_raw_parts(data, size) })
}

#[unsafe(no_mangle)]
pub extern "C" fn note4_rs_ble_frame_encoded_size(payload_length: usize) -> usize {
    encoded_size(payload_length)
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn note4_rs_ble_frame_encode(
    frame: *const CFrame,
    output: *mut u8,
    output_size: usize,
    encoded_size_out: *mut usize,
) -> i32 {
    if frame.is_null() || output.is_null() || encoded_size_out.is_null() {
        return 0;
    }
    let frame = &*frame;
    let payload = if frame.payload_length == 0 {
        &[]
    } else if frame.payload.is_null() {
        return 0;
    } else {
        core::slice::from_raw_parts(frame.payload, frame.payload_length as usize)
    };
    let mut target = core::slice::from_raw_parts_mut(output, output_size);
    let Some(size) = encode(
        &Frame {
            version: frame.version,
            opcode: frame.opcode,
            request_id: frame.request_id,
            slot: frame.slot,
            sequence: frame.sequence,
            total_sequences: frame.total_sequences,
            payload,
        },
        &mut target,
    ) else {
        return 0;
    };
    *encoded_size_out = size;
    1
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn note4_rs_ble_frame_decode(
    input: *const u8,
    input_size: usize,
    frame: *mut CFrame,
    payload: *mut u8,
    payload_size: usize,
) -> i32 {
    if input.is_null() || frame.is_null() || (payload.is_null() && payload_size != 0) {
        return DecodeError::Length as i32;
    }
    let input = core::slice::from_raw_parts(input, input_size);
    let payload_slice = if payload_size == 0 {
        &mut []
    } else {
        core::slice::from_raw_parts_mut(payload, payload_size)
    };
    match decode(input, payload_slice) {
        Ok(decoded) => {
            (*frame).version = decoded.version;
            (*frame).opcode = decoded.opcode;
            (*frame).request_id = decoded.request_id;
            (*frame).slot = decoded.slot;
            (*frame).sequence = decoded.sequence;
            (*frame).total_sequences = decoded.total_sequences;
            (*frame).payload_length = decoded.payload.len() as u16;
            (*frame).payload = payload;
            0
        }
        Err(error) => error as i32,
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn note4_rs_ble_decode_set_slot(
    payload: *const u8,
    payload_length: usize,
    slot: *mut CSlot,
) -> i32 {
    if (payload.is_null() && payload_length != 0) || slot.is_null() {
        return 0;
    }
    let input = if payload_length == 0 {
        &[]
    } else {
        core::slice::from_raw_parts(payload, payload_length)
    };
    let Some(decoded) = decode_set_slot(input) else {
        return 0;
    };
    (*slot).enabled = decoded.enabled as u8;
    (*slot).password_mode = decoded.password_mode as u32;
    (*slot).ssid_length = decoded.ssid_length;
    (*slot).ssid = decoded.ssid;
    (*slot).password_length = decoded.password_length;
    (*slot).password = decoded.password;
    1
}
