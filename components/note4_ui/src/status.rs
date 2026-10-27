use crate::{
    battery_flags, cstr,
    font::{self, Canvas},
    i18n::{self, Msg},
    phase, result, state, storage, Status, Text,
};
use core::fmt::Write;

const LEFT: i16 = 24;
const HEADER_Y: i16 = 20;
const TITLE_Y: i16 = 78;
const DETAIL_Y: i16 = 126;
const TEXT_WIDTH: i16 = 352;
const TEXT_SIZE: u8 = 16;
const LINE_GAP: i16 = 5;
const TEXT_BOTTOM: i16 = 262;

/// Title and explanation of the full-screen status page for `status`.
pub fn content(status: &Status) -> (&'static str, Text) {
    let lang = status.lang();
    let mut detail = Text::new();
    let mut put = |msg: Msg, args: &[&dyn core::fmt::Display]| i18n::format(&mut detail, msg.text(lang), args);
    let title = if status.storage_error == storage::NEEDS_ERASE {
        put(Msg::StorageErase, &[]);
        Msg::StorageDamaged
    } else if status.storage_error != storage::OK {
        put(Msg::StorageRetry, &[]);
        Msg::StorageUnavailable
    } else if status.pairing != 0 {
        let mut code = Text::new();
        let _ = write!(code, "{:06}", status.passkey);
        put(Msg::PairingCode, &[&code.as_str()]);
        Msg::BluetoothPairing
    } else if status.state == state::DISCARD_DIALOG {
        put(Msg::DiscardChanges, &[]);
        Msg::ExitSetup
    } else if status.state == state::CONFIGURING {
        put(Msg::OpenSetup, &[&unsafe { cstr(status.ble_name) }]);
        Msg::DeviceSettings
    } else if status.state == state::APPLYING {
        put(Msg::PleaseWait, &[]);
        Msg::SavingConfiguration
    } else if status.phase == phase::FAILED {
        match status.result {
            result::CONFIG_REQUIRED => {
                put(Msg::ConfigureMagpie, &[]);
                Msg::MagpieSetupRequired
            }
            result::AUTH_REQUIRED => {
                put(Msg::SaveWebKey, &[]);
                Msg::MagpieLoginExpired
            }
            result::FORBIDDEN => {
                put(Msg::CheckPermissions, &[]);
                Msg::AccessDenied
            }
            result::CERTIFICATE => {
                put(Msg::CheckCertificate, &[]);
                Msg::CertificateError
            }
            other => {
                put(
                    match other {
                        result::TIMEOUT => Msg::RequestTimedOut,
                        result::INVALID_RESPONSE => Msg::InvalidResponse,
                        result::RESOURCE_EXHAUSTED => Msg::LowMemory,
                        result::RATE_LIMITED => Msg::RateLimited,
                        _ => Msg::ConnectionError,
                    },
                    &[],
                );
                if status.state != state::CONNECTED {
                    put(Msg::ReconnectingWifi, &[]);
                }
                if status.retry_seconds > 0 {
                    put(Msg::RetryIn, &[&status.retry_seconds]);
                }
                Msg::QuotaReadingFailed
            }
        }
    } else if status.state == state::CONNECTING {
        put(Msg::NetworkRound, &[&(status.slot + 1), &status.round]);
        Msg::ConnectingWifi
    } else if status.state == state::FAILURE {
        if status.wifi_retry_seconds > 0 {
            put(Msg::WifiFailedRetry, &[&status.wifi_retry_seconds.div_ceil(60)]);
        } else {
            put(Msg::WifiFailed, &[]);
        }
        Msg::ConnectionFailed
    } else if status.state == state::CONNECTED {
        match status.phase {
            phase::CLOCK_SYNC => {
                put(Msg::SettingTime, &[]);
                Msg::SyncingClock
            }
            phase::SIGNING_IN => {
                put(Msg::SigningIn, &[]);
                Msg::MagpieSignIn
            }
            _ => {
                put(Msg::WaitingReadings, &[]);
                Msg::ReadingQuotas
            }
        }
    } else {
        put(Msg::StartingDetail, &[]);
        Msg::Starting
    };
    (title.text(lang), detail)
}

pub fn draw(status: &Status, canvas: &mut Canvas<'_>) {
    canvas.text(Msg::Title.text(status.lang()), LEFT, HEADER_Y, TEXT_SIZE, true);
    battery(status, canvas, LEFT);
    let (title, detail) = content(status);
    paragraph(canvas, title, TITLE_Y, true);
    paragraph(canvas, detail.as_str(), DETAIL_Y, false);
}

/// Right-aligned 30x14 battery outline with a 14px percentage on its left.
/// Returns the label's left edge so the page number can sit beside it.
pub fn battery(status: &Status, canvas: &mut Canvas<'_>, y: i16) -> i16 {
    let valid = status.battery_flags & battery_flags::VALID != 0;
    let charging = status.battery_flags & battery_flags::CHARGING != 0;
    let percent = status.battery_percent.min(100);
    let mut label = Text::new();
    if valid {
        let _ = write!(label, "{}%", percent);
    } else {
        label.push("--");
    }
    let left = 354 - font::measure(label.as_str(), 14);
    canvas.text(label.as_str(), left, y - 2, 14, false);
    // Clipped corners and a small terminal keep the 30x14 silhouette light.
    let x = 362;
    canvas.rect(x + 1, y, 25, 1);
    canvas.rect(x + 1, y + 13, 25, 1);
    canvas.rect(x, y + 1, 1, 12);
    canvas.rect(x + 26, y + 1, 1, 12);
    canvas.rect(x + 28, y + 4, 2, 6);
    if valid {
        let width = if percent == 0 { 0 } else { ((percent * 21 + 99) / 100) as i16 };
        canvas.rect(x + 3, y + 3, width, 8);
        if percent <= 10 && !charging {
            canvas.rect(x + 12, y + 3, 2, 5);
            canvas.rect(x + 12, y + 9, 2, 2);
        }
    } else if !charging {
        canvas.text("?", x + 10, y - 1, 14, false);
    }
    if charging {
        // White inset separates the bolt from a dark battery fill.
        canvas.erase(x + 8, y + 1, 11, 11);
        let bolt = [(14, 3), (13, 3), (12, 3), (11, 6), (10, 6), (13, 3), (12, 3), (11, 3), (10, 3)];
        for (dy, (offset, width)) in bolt.into_iter().enumerate() {
            canvas.rect(x + offset, y + 2 + dy as i16, width, 1);
        }
    }
    left
}

/// Wraps at spaces where possible so English words stay whole; text without
/// spaces (Chinese) wraps at any character.
fn paragraph(canvas: &mut Canvas<'_>, value: &str, mut y: i16, bold: bool) {
    let size = TEXT_SIZE;
    let mut line = Text::new();
    let mut used = 0;
    for cp in value.chars() {
        let advance = font::advance_weight(cp, size, bold);
        if used + advance > TEXT_WIDTH && line.len > 0 {
            let full = line.as_str();
            let (head, tail) = match full.rfind(' ') {
                Some(at) if at > 0 => (&full[..at], &full[at + 1..]),
                _ => (full, ""),
            };
            canvas.text(head, LEFT, y, size, bold);
            y += size as i16 + LINE_GAP;
            if y + size as i16 > TEXT_BOTTOM {
                return;
            }
            let mut next = Text::new();
            next.push(tail);
            used = font::measure_weight(tail, size, bold);
            line = next;
            if cp == ' ' && line.len == 0 {
                continue;
            }
        }
        let _ = line.write_char(cp);
        used += advance;
    }
    canvas.text(line.as_str(), LEFT, y, size, bold);
}
