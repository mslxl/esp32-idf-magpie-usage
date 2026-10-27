use crate::{
    i18n::{self, Lang, Msg},
    resources::ZONES,
    Text,
};

/// Supported local-time range: 2000-01-01 to 2100-01-01 UTC.
pub const MIN_EPOCH: i64 = 946_684_800;
const MAX_EPOCH: i64 = 4_102_444_800;
const DAY: i64 = 86_400;

fn days(year: i64, month: i64, day: i64) -> i64 {
    let y = year - i64::from(month <= 2);
    let era = y.div_euclid(400);
    let yo = y - era * 400;
    let m = month + if month > 2 { -3 } else { 9 };
    era * 146097 + yo * 365 + yo / 4 - yo / 100 + (153 * m + 2) / 5 + day - 1 - 719468
}

/// Parses an RFC 3339 timestamp into Unix seconds.
pub fn instant(value: &str) -> Option<i64> {
    let b = value.as_bytes();
    if b.len() < 20 || b[4] != b'-' || b[7] != b'-' || b[10] != b'T' || b[13] != b':' || b[16] != b':' {
        return None;
    }
    let number = |a, z| core::str::from_utf8(&b[a..z]).ok()?.parse::<i64>().ok();
    let (year, month, day) = (number(0, 4)?, number(5, 7)?, number(8, 10)?);
    let (hour, minute, second) = (number(11, 13)?, number(14, 16)?, number(17, 19)?);
    let leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    let max = match month {
        2 => if leap { 29 } else { 28 },
        4 | 6 | 9 | 11 => 30,
        1 | 3 | 5 | 7 | 8 | 10 | 12 => 31,
        _ => return None,
    };
    if year < 100 || day < 1 || day > max || hour > 23 || minute > 59 || second > 59 {
        return None;
    }
    let mut at = 19;
    if b[at] == b'.' {
        at += 1;
        let start = at;
        while at < b.len() && b[at].is_ascii_digit() {
            at += 1;
        }
        if at == start {
            return None;
        }
    }
    let offset = match b.get(at)? {
        b'Z' if at + 1 == b.len() => 0,
        sign @ (b'+' | b'-') if at + 6 == b.len() && b[at + 3] == b':' => {
            let h = number(at + 1, at + 3)?;
            let m = number(at + 4, at + 6)?;
            if h > 23 || m > 59 {
                return None;
            }
            (h * 3600 + m * 60) * if *sign == b'+' { 1 } else { -1 }
        }
        _ => return None,
    };
    Some(days(year, month, day) * DAY + hour * 3600 + minute * 60 + second - offset)
}

fn rules(zone: &str) -> Option<&'static [(i64, i32)]> {
    ZONES.iter().find(|(name, _)| *name == zone).map(|(_, rules)| *rules)
}

/// Index of the first rule that starts after `epoch`.
fn rule_after(rules: &[(i64, i32)], epoch: i64) -> usize {
    rules.partition_point(|(at, _)| *at <= epoch)
}

struct Local {
    year: i64,
    month: i64,
    day: i64,
    /// Minutes since local midnight.
    clock: i64,
    /// Local days since the Unix epoch.
    day_number: i64,
}

fn local(epoch: i64, zone: &str) -> Option<Local> {
    if !(MIN_EPOCH..MAX_EPOCH).contains(&epoch) {
        return None;
    }
    let rules = rules(zone)?;
    let stamp = epoch + rules[rule_after(rules, epoch).saturating_sub(1)].1 as i64;
    let day_number = stamp.div_euclid(DAY);
    let z = day_number + 719468;
    let era = z.div_euclid(146097);
    let doe = z - era * 146097;
    let yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    let doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    let mp = (5 * doy + 2) / 153;
    let day = doy - (153 * mp + 2) / 5 + 1;
    let month = mp + if mp < 10 { 3 } else { -9 };
    let year = yoe + era * 400 + i64::from(month <= 2);
    Some(Local { year, month, day, clock: stamp.rem_euclid(DAY) / 60, day_number })
}

/// First instant after `now` at which a formatted local time can change its
/// wording: local midnight (today/tomorrow/weekday) or a UTC offset change.
pub fn next_boundary(now: i64, zone: &str) -> i64 {
    if now < MIN_EPOCH {
        return MIN_EPOCH;
    }
    let Some(rules) = rules(zone).filter(|_| now < MAX_EPOCH) else {
        return i64::MAX;
    };
    let next = rule_after(rules, now);
    let offset = rules[next.saturating_sub(1)].1 as i64;
    let midnight = ((now + offset).div_euclid(DAY) + 1) * DAY - offset;
    let transition = rules.get(next).map_or(i64::MAX, |(at, _)| *at);
    midnight.min(transition).min(MAX_EPOCH)
}

fn date(out: &mut Text, lang: Lang, at: &Local, current_year: i64) {
    let month = i18n::month(lang, at.month as usize);
    if at.year == current_year {
        i18n::format(out, Msg::Date.text(lang), &[&month, &at.day]);
    } else if lang == Lang::Zh {
        i18n::format(out, Msg::DateWithYear.text(lang), &[&at.year, &month, &at.day]);
    } else {
        i18n::format(out, Msg::DateWithYear.text(lang), &[&month, &at.day, &at.year]);
    }
}

fn clock(out: &mut Text, at: &Local) {
    let _ = core::fmt::Write::write_fmt(out, format_args!("{:02}:{:02}", at.clock / 60, at.clock % 60));
}

/// Reset/expiry time relative to today: "Today 14:05", "Fri 09:00", "Oct 30 08:00".
/// `hh:mm` of `epoch` in the display zone.
pub fn hhmm(epoch: i64, zone: &str) -> Option<Text> {
    let at = local(epoch, zone)?;
    let mut out = Text::new();
    clock(&mut out, &at);
    Some(out)
}
pub fn absolute(lang: Lang, value: &str, now: i64, zone: &str) -> Text {
    let mut out = Text::new();
    let Some(epoch) = instant(value) else {
        out.push(Msg::InvalidTime.text(lang));
        return out;
    };
    let (Some(at), Some(today)) = (local(epoch, zone), local(now, zone)) else {
        out.push(Msg::TimeOutOfRange.text(lang));
        return out;
    };
    let (day, current) = (at.day_number, today.day_number);
    // Weeks start on Monday; day 0 of the epoch was a Thursday.
    let next_monday = current - (current + 3).rem_euclid(7) + 7;
    if day == current {
        out.push(Msg::Today.text(lang));
    } else if day == current + 1 {
        out.push(Msg::Tomorrow.text(lang));
    } else if day > current + 1 && day < next_monday {
        out.push(i18n::weekday(lang, (day + 4).rem_euclid(7) as usize));
    } else {
        date(&mut out, lang, &at, today.year);
    }
    out.push(" ");
    clock(&mut out, &at);
    out
}

/// "Updated 14:05" for readings from today, otherwise with the date. Absolute
/// times keep the card unchanged from minute to minute.
pub fn updated(lang: Lang, value: &str, cached: bool, deferred: bool, now: i64, zone: &str) -> Text {
    let mut when = Text::new();
    match instant(value) {
        None => when.push(Msg::InvalidReadTime.text(lang)),
        Some(_) if now < MIN_EPOCH => when.push(Msg::DeviceTimeUnknown.text(lang)),
        Some(epoch) => match (local(epoch, zone), local(now, zone)) {
            (Some(at), Some(today)) => {
                if at.day_number != today.day_number {
                    date(&mut when, lang, &at, today.year);
                    when.push(" ");
                }
                clock(&mut when, &at);
            }
            _ => when.push(Msg::TimeOutOfRange.text(lang)),
        },
    }
    let marker = if cached {
        Some(Msg::Cached)
    } else if deferred {
        Some(Msg::NotRefreshed)
    } else {
        None
    };
    let mut label = Text::new();
    if let Some(marker) = marker {
        i18n::format(&mut label, marker.text(lang), &[&when.as_str()]);
    } else {
        label.push(when.as_str());
    }
    i18n::message(lang, Msg::Updated, &[&label.as_str()])
}
