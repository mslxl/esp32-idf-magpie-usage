mod ble_protocol;
mod bytes;
mod magpie_config;

#[cfg(test)]
mod tests {
    use super::{ble_protocol, bytes, magpie_config};

    #[test]
    fn crc_vectors_match_standard_values() {
        assert_eq!(bytes::crc32(b"123456789"), 0xcbf4_3926);
        assert_eq!(ble_protocol::crc16(b"123456789"), 0x4b37);
    }

    #[test]
    fn ble_round_trip() {
        let payload = [0x01, 0x02, 0xa5, 0xff];
        let mut encoded = [0u8; 204];
        let frame = ble_protocol::Frame {
            version: 1,
            opcode: 4,
            request_id: 0x1234,
            slot: 2,
            sequence: 0,
            total_sequences: 1,
            payload: &payload,
        };
        let size = ble_protocol::encode(&frame, &mut encoded).unwrap();
        assert_eq!(size, 16);
        let mut decoded_payload = [0u8; 192];
        let decoded = ble_protocol::decode(&encoded[..size], &mut decoded_payload).unwrap();
        assert_eq!(decoded.request_id, frame.request_id);
        assert_eq!(decoded.payload, payload);
    }

    #[test]
    fn endpoint_normalization_matches_contract() {
        assert_eq!(
            magpie_config::canonicalize(b"https://MAGPIE.TEST:443/")
                .unwrap()
                .as_slice(),
            b"https://magpie.test"
        );
        assert_eq!(
            magpie_config::canonicalize(b"http://host:008080/proxy")
                .unwrap()
                .as_slice(),
            b"http://host:8080/proxy"
        );
        assert!(magpie_config::canonicalize(b"https://host/../proxy").is_none());
        assert!(magpie_config::canonicalize(b"http://user@host").is_none());
    }
}
