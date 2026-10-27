#![cfg_attr(feature = "firmware", no_std)]

mod font;
mod i18n;
mod status;
mod time;
mod resources {
    include!("../../../.note4-build/native-assets/resources.rs");
}

use core::{
    ffi::{c_char, c_void, CStr},
    fmt::{self, Write},
};
use font::{measure, measure_weight, Canvas, FRAME_BYTES, WIDTH};
use i18n::{Lang, Msg};

/// Arena capacity for one complete layout (all pages).
const ROWS: usize = 4096;
const BYTES: usize = 262_144;
/// Scratch capacity of one formatted line.
const TEXT: usize = 512;
/// Visible cards that a layout can order.
const MAX_CARDS: usize = 1024;

// Page geometry.
const MARGIN: i16 = 8;
const COLUMN_GAP: i16 = 8;
const FULL_WIDTH: i16 = WIDTH - 2 * MARGIN;
const HALF_WIDTH: i16 = (FULL_WIDTH - COLUMN_GAP) / 2;
const COLUMN_STEP: i16 = HALF_WIDTH + COLUMN_GAP;
/// Inverted header band; state messages replace the title inside it.
const HEADER_HEIGHT: i16 = 28;
/// Baseline of header text.
const HEADER_BASELINE: i16 = 23;
const CARD_TOP: i16 = HEADER_HEIGHT + 4;
const CARD_BOTTOM: i16 = font::HEIGHT - MARGIN;
const EMPTY_MESSAGE_Y: i16 = 100;

// Card geometry.
const PADDING: i16 = 3;
const CARD_SPACING: i16 = 3;
/// Room for the provider icon before a heading.
const ICON_SPACE: i16 = 20;
/// White space after the provider name inside its inverted label.
const CHIP_PAD: i16 = 4;
/// Provider and account rows repeated at the top of a continued card.
const CONTINUATION_RESERVE: i16 = 48;
const BAR_HEIGHT: i16 = 8;
const BAR_TOP: i16 = 1;
/// Narrowest bar drawn between a window name and its quota.
const INLINE_BAR_MIN: i16 = 32;
/// Gap around inline bars and right-aligned overlays.
const GAP: i16 = 6;

/// Page indicator: dots up to this many pages, then "n / m".
const MAX_PAGE_DOTS: u32 = 10;
const PAGE_DOT: i16 = 7;
const PAGE_DOT_STEP: i16 = 11;

// Type sizes.
const TITLE_SIZE: u8 = 18;
const PAGE_SIZE: u8 = 16;
/// Remaining percentage beside the quota bar.
const QUOTA_SIZE: u8 = 18;
const META_SIZE: u8 = 14;
const BODY_SIZES: [u8; 3] = [16, 15, 14];

/// Remaining quota at or below this percentage is drawn inverted.
const LOW_REMAINING: f32 = 15.0;
const CREDENTIAL_LABELS: [&str; 4] = ["api key", "api_key", "api-key", "token"];

/// `value` shortened with "..." to fit `limit` in bold `size`.
fn fit(value: &str, size: u8, limit: i16) -> Text {
    let mut out = Text::new();
    if measure_weight(value, size, true) <= limit {
        out.push(value);
        return out;
    }
    for ch in value.chars() {
        let mut scalar = [0; 4];
        let next = ch.encode_utf8(&mut scalar);
        if measure_weight(out.as_str(), size, true) + measure_weight(next, size, true) + measure_weight("...", size, true) > limit {
            break;
        }
        out.push(next);
    }
    out.push("...");
    out
}

/// Mirrors `note4_provisioning_state_t`.
mod state {
    pub const CONFIGURING: u32 = 1;
    pub const DISCARD_DIALOG: u32 = 2;
    pub const APPLYING: u32 = 3;
    pub const CONNECTING: u32 = 4;
    pub const CONNECTED: u32 = 5;
    pub const FAILURE: u32 = 6;
}
/// Mirrors `note4_magpie_phase_t`.
mod phase {
    pub const CLOCK_SYNC: u32 = 1;
    pub const SIGNING_IN: u32 = 2;
    pub const FAILED: u32 = 6;
}
/// Mirrors `note4_magpie_result_t`.
mod result {
    pub const CONFIG_REQUIRED: u32 = 1;
    pub const AUTH_REQUIRED: u32 = 2;
    pub const FORBIDDEN: u32 = 3;
    pub const CERTIFICATE: u32 = 4;
    pub const TIMEOUT: u32 = 6;
    pub const INVALID_RESPONSE: u32 = 8;
    pub const RESOURCE_EXHAUSTED: u32 = 9;
    pub const RATE_LIMITED: u32 = 10;
}
/// Values of `storage_error`.
mod storage {
    pub const OK: u32 = 0;
    pub const NEEDS_ERASE: u32 = 2;
}
/// Values of `display_layout`.
mod layout {
    pub const TWO_COLUMNS: u32 = 1;
}
/// Bits of `battery_flags`.
mod battery_flags {
    pub const VALID: u32 = 1;
    pub const CHARGING: u32 = 2;
}

pub struct Text {
    bytes: [u8; TEXT],
    len: usize,
    overflow: bool,
}
impl Text {
    fn new() -> Self {
        Self { bytes: [0; TEXT], len: 0, overflow: false }
    }
    fn push(&mut self, value: &str) {
        let _ = self.write_str(value);
    }
    fn as_str(&self) -> &str {
        core::str::from_utf8(&self.bytes[..self.len]).unwrap()
    }
}
impl Write for Text {
    fn write_str(&mut self, value: &str) -> fmt::Result {
        if self.len + value.len() > TEXT {
            self.overflow = true;
            return Err(fmt::Error);
        }
        self.bytes[self.len..self.len + value.len()].copy_from_slice(value.as_bytes());
        self.len += value.len();
        Ok(())
    }
}

/// Borrowed card fields; every pointer may be NULL and stays valid during build.
#[repr(C)]
pub struct CardFields {
    provider: *const c_char,
    name: *const c_char,
    user: *const c_char,
    plan: *const c_char,
    alias: *const c_char,
    seat: *const c_char,
    from: *const c_char,
    kind: *const c_char,
    balance: *const c_char,
    until: *const c_char,
    renew: *const c_char,
    as_of: *const c_char,
    read_at: *const c_char,
    read_deferred: u32,
    failed: u32,
    windows: usize,
    balances: usize,
}
#[repr(C)]
pub struct WindowFields {
    name: *const c_char,
    display: *const c_char,
    used: *const c_char,
    remaining: *const c_char,
    resets_at: *const c_char,
    reset_secs: *const c_char,
    amount: *const c_char,
    limit: *const c_char,
    unit: *const c_char,
    pool: *const c_char,
    holds_why: *const c_char,
    unlimited: u32,
}
#[repr(C)]
pub struct BalanceFields {
    label: *const c_char,
    text: *const c_char,
    failed: u32,
}
#[repr(C)]
pub struct Source {
    context: *const c_void,
    count: usize,
    card: extern "C" fn(*const c_void, usize, *mut CardFields),
    window: extern "C" fn(*const c_void, usize, usize, *mut WindowFields),
    balance: extern "C" fn(*const c_void, usize, usize, *mut BalanceFields),
    visible: extern "C" fn(*const c_void, usize) -> u32,
    rank: extern "C" fn(*const c_void, usize) -> usize,
}

/// Borrowed source text; invalid UTF-8 is shown rather than dropped.
fn source_text<'a>(p: *const c_char) -> &'a str {
    if p.is_null() {
        ""
    } else {
        unsafe { CStr::from_ptr(p) }.to_str().unwrap_or("[invalid UTF-8]")
    }
}

struct Card<'a> {
    provider: &'a str,
    name: &'a str,
    user: &'a str,
    plan: &'a str,
    alias: &'a str,
    seat: &'a str,
    from: &'a str,
    kind: &'a str,
    balance: &'a str,
    until: &'a str,
    renew: &'a str,
    as_of: &'a str,
    read_at: &'a str,
    read_deferred: bool,
    failed: bool,
    windows: usize,
    balances: usize,
}
struct Window<'a> {
    name: &'a str,
    display: &'a str,
    used: &'a str,
    remaining: &'a str,
    resets_at: &'a str,
    reset_secs: &'a str,
    amount: &'a str,
    limit: &'a str,
    unit: &'a str,
    pool: &'a str,
    holds_why: &'a str,
    unlimited: bool,
}
struct Balance<'a> {
    label: &'a str,
    text: &'a str,
    failed: bool,
}
impl Source {
    fn card(&self, index: usize) -> Card<'_> {
        // SAFETY: all-zero is a valid value (null pointers and zero counts).
        let mut raw: CardFields = unsafe { core::mem::zeroed() };
        (self.card)(self.context, index, &mut raw);
        Card {
            provider: source_text(raw.provider),
            name: source_text(raw.name),
            user: source_text(raw.user),
            plan: source_text(raw.plan),
            alias: source_text(raw.alias),
            seat: source_text(raw.seat),
            from: source_text(raw.from),
            kind: source_text(raw.kind),
            balance: source_text(raw.balance),
            until: source_text(raw.until),
            renew: source_text(raw.renew),
            as_of: source_text(raw.as_of),
            read_at: source_text(raw.read_at),
            read_deferred: raw.read_deferred != 0,
            failed: raw.failed != 0,
            windows: raw.windows,
            balances: raw.balances,
        }
    }
    fn window(&self, card: usize, child: usize) -> Window<'_> {
        // SAFETY: as in `card`.
        let mut raw: WindowFields = unsafe { core::mem::zeroed() };
        (self.window)(self.context, card, child, &mut raw);
        Window {
            name: source_text(raw.name),
            display: source_text(raw.display),
            used: source_text(raw.used),
            remaining: source_text(raw.remaining),
            resets_at: source_text(raw.resets_at),
            reset_secs: source_text(raw.reset_secs),
            amount: source_text(raw.amount),
            limit: source_text(raw.limit),
            unit: source_text(raw.unit),
            pool: source_text(raw.pool),
            holds_why: source_text(raw.holds_why),
            unlimited: raw.unlimited != 0,
        }
    }
    fn balance(&self, card: usize, part: usize) -> Balance<'_> {
        // SAFETY: as in `card`.
        let mut raw: BalanceFields = unsafe { core::mem::zeroed() };
        (self.balance)(self.context, card, part, &mut raw);
        Balance { label: source_text(raw.label), text: source_text(raw.text), failed: raw.failed != 0 }
    }
}

/// Mirrors `note4_ui_status_t`; keep the field order identical.
#[repr(C)]
pub struct Status {
    state: u32,
    slot: u32,
    round: u32,
    storage_error: u32,
    pairing: u32,
    passkey: u32,
    phase: u32,
    result: u32,
    error_page: u32,
    reading: u32,
    retry_seconds: u32,
    layout: u32,
    adaptive: u32,
    now: i64,
    timezone: *const c_char,
    ble_name: *const c_char,
    battery_percent: u32,
    battery_flags: u32,
    language: u32,
    wifi_retry_seconds: u32,
}
impl Status {
    fn zone(&self) -> &str {
        unsafe { cstr(self.timezone) }
    }
    fn lang(&self) -> Lang {
        Lang::from_code(self.language)
    }
    /// Cached quotas stay visible while the header reports the offline or failing state.
    fn banner(&self) -> bool {
        self.state != state::CONNECTED || self.phase == phase::FAILED
    }
    /// Whether quota pages (rather than a full-screen status page) are shown.
    fn quotas(&self) -> bool {
        let setup = matches!(self.state, state::CONFIGURING | state::DISCARD_DIALOG | state::APPLYING);
        let blocking_failure = self.phase == phase::FAILED
            && ((result::CONFIG_REQUIRED..=result::CERTIFICATE).contains(&self.result) || self.error_page != 0);
        self.storage_error == storage::OK && self.pairing == 0 && !setup && !blocking_failure
    }
}
unsafe fn cstr<'a>(p: *const c_char) -> &'a str {
    if p.is_null() {
        ""
    } else {
        CStr::from_ptr(p).to_str().unwrap_or("")
    }
}

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
#[repr(u8)]
enum Kind {
    Body,
    Provider,
    Bar,
    Window,
    /// Right-aligned quota, drawn on the preceding window row.
    Right,
    /// Quota bar between the window name and its quota, on the same row.
    InlineBar,
    InlineUnknownBar,
    /// Right-aligned update time on the card's first row.
    Stamp,
    Account,
    /// Plan drawn after the account name on the same row.
    Plan,
    Meta,
    Rule,
    Frame,
    UnknownBar,
    Bold,
    Reset,
}
impl Kind {
    /// Overlay rows share the vertical position of the row before them.
    fn overlay(self) -> bool {
        matches!(self, Kind::Right | Kind::Plan | Kind::InlineBar | Kind::InlineUnknownBar | Kind::Stamp)
    }
    fn takes_space(self) -> bool {
        !self.overlay() && self != Kind::Frame
    }
    fn bold(self) -> bool {
        matches!(self, Kind::Provider | Kind::Account | Kind::Right | Kind::Bold)
    }
    fn height(self, size: u8) -> u8 {
        match self {
            Kind::Right | Kind::Plan | Kind::InlineBar | Kind::InlineUnknownBar | Kind::Stamp | Kind::Frame => 0,
            Kind::Bar | Kind::UnknownBar => (BAR_TOP + BAR_HEIGHT + 1) as u8,
            Kind::Rule => 3,
            Kind::Provider => 17,
            Kind::Account => line_height(size),
            Kind::Meta | Kind::Reset => line_height(META_SIZE),
            Kind::Body | Kind::Window | Kind::Bold => line_height(size),
        }
    }
}
/// Top of a quota bar; inline bars are positioned during placement.
fn bar_y(row: &Row) -> i16 {
    match row.kind {
        Kind::InlineBar | Kind::InlineUnknownBar => row.y,
        _ => row.y + BAR_TOP,
    }
}
/// Baseline offset below the `y` passed to text drawing (glyphs are drawn at `row.y - 1`).
fn text_top(size: u8) -> i16 {
    size as i16 - 1
}
fn line_height(size: u8) -> u8 {
    ((size as u16 * 115 + 50) / 100) as u8
}
fn is_credential_label(value: &str) -> bool {
    CREDENTIAL_LABELS.iter().any(|label| value.eq_ignore_ascii_case(label))
}

#[derive(Clone, Copy)]
#[repr(C)]
struct Row {
    start: u32,
    len: u16,
    page: u16,
    x: i16,
    y: i16,
    width: i16,
    size: u8,
    height: u8,
    kind: Kind,
    icon: u8,
    progress: u8,
    /// Low remaining quota, drawn inverted.
    alert: bool,
    /// Rows of one quota window share a non-zero group and stay on one page.
    group: u16,
    /// Horizontal offset of an inline bar from the card's text column.
    dx: i16,
}

#[repr(C)]
pub struct Ui {
    rows: [Row; ROWS],
    text: [u8; BYTES],
    count: usize,
    bytes: usize,
    pages: u32,
    splits: u32,
    size: u8,
    lang: Lang,
    source_count: usize,
    /// Visible cards as (rank, index), sorted once per build.
    order: [(usize, usize); MAX_CARDS],
    order_count: usize,
    /// Earliest instant at which time-dependent wording in this layout changes.
    next_change: i64,
    /// Latest update time among visible cards, shown in the header; 0 when unknown.
    updated: i64,
}

/// Where the next card starts.
struct Cursor {
    page: u16,
    y: i16,
    column: i16,
}
impl Cursor {
    fn next_page(&mut self, top: i16) -> Result<(), ()> {
        self.page = self.page.checked_add(1).ok_or(())?;
        self.y = top;
        self.column = 0;
        Ok(())
    }
    /// Starts a fresh page unless already at the top of the first column.
    fn fresh_page(&mut self, top: i16) -> Result<(), ()> {
        if self.y != top || self.column != 0 {
            self.next_page(top)?;
        }
        Ok(())
    }
}

impl Ui {
    fn reset(&mut self, size: u8, status: &Status) {
        self.count = 0;
        self.bytes = 0;
        self.pages = 0;
        self.splits = 0;
        self.size = size;
        self.lang = status.lang();
        self.next_change = time::next_boundary(status.now, status.zone());
    }
    fn row_text(&self, row: &Row) -> &str {
        let start = row.start as usize;
        core::str::from_utf8(&self.text[start..start + row.len as usize]).unwrap()
    }
    fn add(&mut self, text: &str, width: i16, size: u8, kind: Kind, icon: u8, progress: u8) -> Result<(), ()> {
        if self.count == ROWS || self.bytes + text.len() > BYTES || text.len() > u16::MAX as usize {
            return Err(());
        }
        self.text[self.bytes..self.bytes + text.len()].copy_from_slice(text.as_bytes());
        self.rows[self.count] = Row {
            start: self.bytes as u32,
            len: text.len() as u16,
            page: 0,
            x: MARGIN,
            y: 0,
            width,
            size,
            height: kind.height(size),
            kind,
            icon,
            progress,
            alert: false,
            group: 0,
            dx: 0,
        };
        self.bytes += text.len();
        self.count += 1;
        Ok(())
    }
    fn line(&mut self, value: &str, width: i16, size: u8, header: bool, icon: u8) -> Result<(), ()> {
        self.line_weight(value, width, size, header, icon, header)
    }
    /// Wraps `value` into rows; glyphs missing from the font appear as `[U+XXXX]`.
    fn line_weight(&mut self, value: &str, width: i16, size: u8, header: bool, icon: u8, bold: bool) -> Result<(), ()> {
        let kind = if header { Kind::Provider } else { Kind::Body };
        let limit = width - if header { ICON_SPACE + CHIP_PAD } else { 1 };
        let mut line = Text::new();
        let mut used = 0;
        for cp in value.chars() {
            let mut fallback = Text::new();
            let mut scalar = [0; 4];
            let printable = if cp == '\n' {
                "\n"
            } else if font::covered(cp, size) {
                cp.encode_utf8(&mut scalar)
            } else {
                let _ = write!(fallback, "[U+{:04X}]", cp as u32);
                fallback.as_str()
            };
            for ch in printable.chars() {
                let advance = font::advance_weight(ch, size, bold);
                if ch == '\n' || (used + advance > limit && line.len > 0) || line.len + 4 > TEXT {
                    self.add(line.as_str(), width, size, kind, icon, 0)?;
                    line = Text::new();
                    used = 0;
                    if ch == '\n' {
                        continue;
                    }
                }
                line.write_char(ch).map_err(|_| ())?;
                used += advance;
            }
        }
        if line.len > 0 {
            self.add(line.as_str(), width, size, kind, icon, 0)?;
        }
        Ok(())
    }
    fn styled(&mut self, value: &str, width: i16, size: u8, kind: Kind) -> Result<(), ()> {
        let start = self.count;
        self.line_weight(value, width, size, kind == Kind::Provider, 0, kind.bold())?;
        for row in &mut self.rows[start..self.count] {
            row.kind = kind;
            row.height = kind.height(size);
        }
        Ok(())
    }
    fn metadata(&mut self, label: &str, value: &str, width: i16) -> Result<(), ()> {
        if value.is_empty() {
            return Ok(());
        }
        let mut text = Text::new();
        let _ = write!(text, "{}{}{}", label, if label.is_empty() { "" } else { " " }, value);
        if text.overflow {
            self.styled(label, width, META_SIZE, Kind::Meta)?;
            self.styled(value, width, META_SIZE, Kind::Meta)
        } else {
            self.styled(text.as_str(), width, META_SIZE, Kind::Meta)
        }
    }
    fn meta(&mut self, value: &Text, width: i16, kind: Kind) -> Result<(), ()> {
        if value.overflow {
            return Err(());
        }
        self.styled(value.as_str(), width, META_SIZE, kind)
    }
    /// Account name in bold with the plan beside it when both fit on one row.
    fn identity(&mut self, user: &str, plan: &str, width: i16) -> Result<(), ()> {
        let start = self.count;
        self.styled(user, width, self.size, Kind::Account)?;
        if plan.is_empty() {
            return Ok(());
        }
        let fits = self.count == start + 1
            && user.chars().all(|cp| font::covered(cp, self.size))
            && plan.chars().all(|cp| font::covered(cp, META_SIZE))
            && measure_weight(user, self.size, true) + measure(plan, META_SIZE) + 5 <= width;
        if fits {
            self.add(plan, width, META_SIZE, Kind::Plan, 0, 0)
        } else {
            self.styled(plan, width, META_SIZE, Kind::Meta)
        }
    }
    /// Adds `stamp` right-aligned on row `first` when that is the only row since
    /// `first` and both fit side by side.
    fn stamp(&mut self, first: usize, stamp: &str, width: i16) -> Result<bool, ()> {
        if first >= self.count || self.height(first + 1, self.count) != 0 {
            return Ok(false);
        }
        let used: i16 = self.rows[first..self.count]
            .iter()
            .map(|row| {
                let text = self.row_text(row);
                match row.kind {
                    Kind::Plan => PADDING + measure(text, row.size),
                    Kind::Provider => ICON_SPACE + measure_weight(text, row.size, true) + CHIP_PAD,
                    kind => measure_weight(text, row.size, kind.bold()),
                }
            })
            .sum();
        if used + GAP + measure(stamp, META_SIZE) > width {
            return Ok(false);
        }
        self.add(stamp, width, META_SIZE, Kind::Stamp, 0, 0)?;
        Ok(true)
    }
    fn formatted(&mut self, value: &Text, width: i16) -> Result<(), ()> {
        if value.overflow {
            return Err(());
        }
        self.line(value.as_str(), width, self.size, false, 0)
    }
    fn labeled(&mut self, lang: Lang, label: Msg, value: &str, width: i16) -> Result<(), ()> {
        if value.is_empty() {
            return Ok(());
        }
        let label = label.text(lang);
        let text = i18n::message(lang, Msg::LabelValue, &[&label, &value]);
        if !text.overflow {
            return self.formatted(&text, width);
        }
        // Long server fields are streamed without truncation.
        self.line(label, width, self.size, false, 0)?;
        self.line(value, width, self.size, false, 0)
    }
    fn height(&self, start: usize, end: usize) -> i16 {
        let total: i32 = self.rows[start..end]
            .iter()
            .filter(|row| row.kind.takes_space())
            .map(|row| row.height as i32)
            .sum();
        total.min(i16::MAX as i32) as i16
    }
    fn alert(&mut self, start: usize) {
        for row in &mut self.rows[start..self.count] {
            row.alert = true;
        }
    }

    fn card(&mut self, source: &Source, index: usize, card: &Card<'_>, width: i16, heading: bool, status: &Status) -> Result<(), ()> {
        let lang = status.lang();
        let provider = card.provider;
        // The update time sits beside the heading or account when there is room.
        let cached = !card.as_of.is_empty();
        let value = if cached { card.as_of } else { card.read_at };
        // A plain update time in the same minute as the header's adds nothing.
        let repeated = !status.banner()
            && !cached
            && !card.read_deferred
            && self.updated != 0
            && time::instant(value).is_some_and(|at| at.div_euclid(60) == self.updated.div_euclid(60));
        let updated = if !value.is_empty() && !repeated {
            Some(time::updated(lang, value, cached, card.read_deferred, status.now, status.zone()))
        } else {
            None
        };
        if updated.as_ref().is_some_and(|text| text.overflow) {
            return Err(());
        }
        let updated = updated.as_ref().map(Text::as_str);
        let mut stamped = updated.is_none();
        if heading {
            let title = if card.name.eq_ignore_ascii_case(provider) { card.name } else { provider };
            let first = self.count;
            self.line(title, width, META_SIZE, true, font::icon(provider))?;
            if let Some(updated) = updated {
                stamped = self.stamp(first, updated, width)?;
            }
        }
        let user = card.user.trim();
        let plan = card.plan.trim();
        let show_user = !user.is_empty() && !user.eq_ignore_ascii_case(provider) && !is_credential_label(user);
        let show_plan = !plan.eq_ignore_ascii_case(provider) && !plan.eq_ignore_ascii_case(user);
        let first = self.count;
        self.identity(if show_user { user } else { "" }, if show_plan { plan } else { "" }, width)?;
        if let (false, Some(updated)) = (stamped, updated) {
            stamped = self.stamp(first, updated, width)?;
        }

        // Secondary names, skipping any value already shown.
        let mut seen = [provider, user, plan, "", "", "", "", ""];
        let mut seen_count = 3;
        let extras = [
            (None, card.name),
            (Some(Msg::Alias), card.alias),
            (Some(Msg::Seat), card.seat),
            (Some(Msg::From), card.from),
            (Some(Msg::Kind), card.kind),
        ];
        for (label, value) in extras {
            let value = value.trim();
            if value.is_empty() || seen[..seen_count].iter().any(|s| s.eq_ignore_ascii_case(value)) {
                continue;
            }
            seen[seen_count] = value;
            seen_count += 1;
            match label {
                None => self.line(value, width, self.size, false, 0)?,
                Some(label) => self.labeled(lang, label, value, width)?,
            }
        }

        self.metadata(Msg::Balance.text(lang), card.balance, width)?;
        if !card.until.is_empty() {
            let until = time::absolute(lang, card.until, status.now, status.zone());
            self.meta(&i18n::message(lang, Msg::ExpiresAt, &[&until.as_str()]), width, Kind::Meta)?;
        }
        self.metadata(Msg::Renews.text(lang), card.renew, width)?;
        for part in 0..card.balances {
            let balance = source.balance(index, part);
            self.line(balance.label, width, self.size, false, 0)?;
            self.line(balance.text, width, self.size, false, 0)?;
            if balance.failed {
                self.line(Msg::BalanceUnavailable.text(lang), width, self.size, false, 0)?;
            }
        }
        if card.failed {
            self.styled(Msg::ReadFailed.text(lang), width, META_SIZE, Kind::Meta)?;
        }
        let stale = !card.as_of.is_empty() || card.failed || card.read_deferred;
        for child in 0..card.windows {
            let window = source.window(index, child);
            self.window(&window, width, stale, status)?;
        }
        if card.windows == 0 && card.balance.is_empty() && !card.failed {
            self.styled(Msg::NoWindows.text(lang), width, META_SIZE, Kind::Meta)?;
        }
        if let (false, Some(updated)) = (stamped, updated) {
            self.styled(updated, width, META_SIZE, Kind::Meta)?;
        }
        Ok(())
    }

    /// One quota window: rule, name with remaining quota and (when it fits) the
    /// bar on one row, then details and reset.
    fn window(&mut self, window: &Window<'_>, width: i16, stale: bool, status: &Status) -> Result<(), ()> {
        let lang = status.lang();
        let window_start = self.count;
        self.add("", width, META_SIZE, Kind::Rule, 0, 0)?;
        let resets_at = time::instant(window.resets_at);
        if let Some(at) = resets_at.filter(|at| *at > status.now) {
            self.next_change = self.next_change.min(at);
        }
        let expired = stale && resets_at.is_some_and(|at| at <= status.now);
        let used = window.used.parse::<f32>().ok().filter(|used| used.is_finite());
        let mut quota = Text::new();
        let mut left = None;
        let mut quota_size = self.size;
        if expired {
            quota.push(Msg::QuotaUnknown.text(lang));
        } else if window.unlimited {
            quota.push(Msg::Unlimited.text(lang));
        } else if !window.remaining.is_empty() {
            // The bar shows the remaining share, so the number needs no label.
            let _ = write!(quota, "{}%", window.remaining);
            left = window.remaining.parse::<f32>().ok();
            quota_size = QUOTA_SIZE;
        } else if let Some(used) = used {
            let _ = write!(quota, "{:.0}%", (100.0 - used).max(0.0));
            left = Some(100.0 - used);
            quota_size = QUOTA_SIZE;
        } else {
            quota.push(Msg::Unknown.text(lang));
        }
        if quota.overflow {
            return Err(());
        }
        let alert = left.is_some_and(|left| left <= LOW_REMAINING);

        let name = if window.name.is_empty() { Msg::QuotaWindow.text(lang) } else { window.name };
        let name_width = if name.chars().all(|cp| font::covered(cp, self.size)) { measure(name, self.size) } else { width };
        let quota_width = measure_weight(quota.as_str(), quota_size, true) + if alert { 6 } else { 0 };
        let quota_start;
        let one_row = name_width + quota_width + 8 <= width;
        if one_row {
            // The larger quota shares the name's baseline and uses the line's leading.
            self.add(name, width, self.size, Kind::Window, 0, 0)?;
            quota_start = self.count;
            self.add(quota.as_str(), width, quota_size, Kind::Right, 0, 0)?;
        } else {
            self.line(name, width, self.size, false, 0)?;
            quota_start = self.count;
            self.styled(quota.as_str(), width, quota_size, Kind::Bold)?;
        }
        if alert {
            self.alert(quota_start);
        }

        if !expired && !window.unlimited {
            let progress = used.map_or(0, |used| (100.0 - used).clamp(0.0, 100.0) as u8);
            let inline_width = width - name_width - quota_width - 2 * GAP;
            if one_row && inline_width >= INLINE_BAR_MIN {
                let kind = if used.is_some() { Kind::InlineBar } else { Kind::InlineUnknownBar };
                self.add("", inline_width, self.size, kind, 0, progress)?;
                self.rows[self.count - 1].dx = name_width + GAP;
            } else {
                let kind = if used.is_some() { Kind::Bar } else { Kind::UnknownBar };
                self.add("", width, self.size, kind, 0, progress)?;
            }
            self.rows[self.count - 1].alert = alert;
            if used.is_some_and(|used| used > 100.0) {
                self.formatted(&i18n::message(lang, Msg::Used, &[&window.used]), width)?;
            }
        }
        self.line(window.display, width, self.size, false, 0)?;
        let details = [
            (Msg::Amount, window.amount),
            (Msg::Limit, window.limit),
            (Msg::Unit, window.unit),
            (Msg::Pool, window.pool),
            (Msg::Hold, window.holds_why),
        ];
        for (label, value) in details {
            self.labeled(lang, label, value, width)?;
        }
        if !window.resets_at.is_empty() {
            let at = time::absolute(lang, window.resets_at, status.now, status.zone());
            self.meta(&i18n::message(lang, Msg::ResetsAt, &[&at.as_str()]), width, Kind::Reset)?;
        } else if !window.reset_secs.is_empty() {
            self.meta(&i18n::message(lang, Msg::ResetsIn, &[&window.reset_secs]), width, Kind::Meta)?;
        }
        for row in &mut self.rows[window_start..self.count] {
            row.group = (window_start + 1) as u16;
        }
        Ok(())
    }

    /// Orders the visible cards once per build.
    fn collect(&mut self, source: &Source) -> Result<(), ()> {
        self.source_count = source.count;
        self.order_count = 0;
        self.updated = 0;
        for index in 0..source.count {
            if (source.visible)(source.context, index) == 0 {
                continue;
            }
            let card = source.card(index);
            let value = if card.as_of.is_empty() { card.read_at } else { card.as_of };
            if let Some(at) = time::instant(value) {
                self.updated = self.updated.max(at);
            }
            if self.order_count == MAX_CARDS {
                return Err(());
            }
            self.order[self.order_count] = ((source.rank)(source.context, index), index);
            self.order_count += 1;
        }
        self.order[..self.order_count].sort_unstable();
        Ok(())
    }

    fn layout(&mut self, source: &Source, status: &Status, size: u8) -> Result<(), ()> {
        self.reset(size, status);
        let top = CARD_TOP;
        let usable = CARD_BOTTOM - top;
        let two_columns = status.layout == layout::TWO_COLUMNS;
        let mut cursor = Cursor { page: 0, y: top, column: 0 };
        let mut previous = "";
        for slot in 0..self.order_count {
            let index = self.order[slot].1;
            let card = source.card(index);
            let provider = card.provider;
            let start = self.count;
            let text_start = self.bytes;
            let mut outer = if two_columns { HALF_WIDTH } else { FULL_WIDTH };
            let inner = |outer: i16| outer - 2 * PADDING;
            let heading = previous != provider || cursor.y == top;
            self.card(source, index, &card, inner(outer), heading, status)?;
            // A card too tall for a column is laid out at full width on its own page.
            if self.height(start, self.count) + 2 * PADDING > usable && outer == HALF_WIDTH {
                self.count = start;
                self.bytes = text_start;
                outer = FULL_WIDTH;
                cursor.fresh_page(top)?;
                self.card(source, index, &card, inner(outer), true, status)?;
            }
            let mut end = self.count;
            let card_height = self.height(start, end) + 2 * PADDING;
            // Move a card that would fit on its own to the next column or page.
            if card_height <= usable && cursor.y + card_height > CARD_BOTTOM {
                if outer == HALF_WIDTH && cursor.column == 0 {
                    cursor.column = 1;
                    cursor.y = top;
                } else {
                    cursor.next_page(top)?;
                }
                self.count = start;
                self.bytes = text_start;
                self.card(source, index, &card, inner(outer), true, status)?;
                end = self.count;
            }
            if self.height(start, end) + 2 * PADDING > usable {
                cursor.fresh_page(top)?;
                self.splits += 1;
            }
            self.place(source, index, provider, start, end, outer, top, &mut cursor)?;
            cursor.y += CARD_SPACING;
            previous = provider;
            if two_columns && outer == FULL_WIDTH {
                cursor.next_page(top)?;
                previous = "";
            }
        }
        self.pages = self.rows[..self.count].iter().map(|row| row.page as u32 + 1).max().unwrap_or(0);
        Ok(())
    }

    /// Positions rows `start..end` of one card, splitting it across pages when
    /// needed, and adds its frame(s). Leaves `cursor.y` at the frame bottom.
    #[allow(clippy::too_many_arguments)]
    fn place(&mut self, source: &Source, index: usize, provider: &str, start: usize, end: usize, outer: i16, top: i16, cursor: &mut Cursor) -> Result<(), ()> {
        let usable = CARD_BOTTOM - top;
        let mut box_y = cursor.y;
        let mut x = MARGIN + cursor.column * COLUMN_STEP;
        let mut y = cursor.y + PADDING;
        let mut baseline = y;
        let mut baseline_size = self.size;
        let mut plan_x = x + PADDING;
        for at in start..end {
            let row = self.rows[at];
            let h = row.height as i16;
            let first_in_group = row.group != 0 && (at == start || self.rows[at - 1].group != row.group);
            let mut group_end = at + 1;
            if first_in_group {
                while group_end < end && self.rows[group_end].group == row.group {
                    group_end += 1;
                }
            }
            let block_height = if first_in_group { self.height(at, group_end) } else { h };
            // Reserve the card's bottom padding and keep complete windows together.
            let overflow = y + h + PADDING > CARD_BOTTOM
                || (first_in_group && block_height <= usable - CONTINUATION_RESERVE && y + block_height + PADDING > CARD_BOTTOM);
            if !row.kind.overlay() && overflow {
                self.outline(cursor.page, x, box_y, outer, y + PADDING - box_y)?;
                cursor.next_page(top)?;
                x = MARGIN;
                box_y = top;
                y = top + PADDING;
                let added = self.count;
                self.continuation(source, index, provider, outer - 2 * PADDING)?;
                for continued in &mut self.rows[added..self.count] {
                    continued.page = cursor.page;
                    continued.x = x + PADDING;
                    continued.y = y;
                    y += continued.height as i16;
                }
            }
            let placed = &mut self.rows[at];
            placed.page = cursor.page;
            placed.x = if row.kind == Kind::Plan { plan_x } else { x + PADDING + row.dx };
            placed.y = match row.kind {
                // Overlay text shares the baseline of the row it sits on.
                Kind::Plan | Kind::Stamp | Kind::Right => baseline - text_top(row.size),
                // Inline bars are centred on the row's capital height.
                Kind::InlineBar | Kind::InlineUnknownBar => baseline - baseline_size as i16 * 36 / 100 - BAR_HEIGHT / 2,
                _ => y,
            };
            if !row.kind.overlay() {
                baseline = y + text_top(row.size);
                baseline_size = row.size;
                if row.kind == Kind::Account {
                    plan_x = x + PADDING + measure_weight(self.row_text(&row), row.size, true) + PADDING;
                }
            }
            y += h;
        }
        self.outline(cursor.page, x, box_y, outer, y + PADDING - box_y)?;
        cursor.y = y + PADDING;
        Ok(())
    }
    fn outline(&mut self, page: u16, x: i16, y: i16, width: i16, height: i16) -> Result<(), ()> {
        self.add("", width, META_SIZE, Kind::Frame, 0, 0)?;
        let row = &mut self.rows[self.count - 1];
        row.page = page;
        row.x = x;
        row.y = y;
        row.height = u8::try_from(height).map_err(|_| ())?;
        Ok(())
    }
    /// Provider and account repeated at the top of a card continued from the previous page.
    fn continuation(&mut self, source: &Source, index: usize, provider: &str, width: i16) -> Result<(), ()> {
        self.compact(provider, META_SIZE, Kind::Provider, font::icon(provider), width)?;
        let card = source.card(index);
        let (user, plan) = (card.user, card.plan);
        let mut context = Text::new();
        let _ = write!(context, "{}{}{}", user, if user.is_empty() || plan.is_empty() { "" } else { " " }, plan);
        let label = i18n::message(self.lang, Msg::Continued, &[&context.as_str()]);
        self.compact(label.as_str(), self.size, Kind::Account, 0, width)
    }
    /// Single row, shortened with "..." when it does not fit.
    fn compact(&mut self, value: &str, size: u8, kind: Kind, icon: u8, width: i16) -> Result<(), ()> {
        let limit = width - if kind == Kind::Provider { ICON_SPACE + CHIP_PAD } else { 1 };
        let mut out = Text::new();
        for cp in value.chars() {
            let mut scalar = [0; 4];
            let mut fallback = Text::new();
            let text = if font::covered(cp, size) {
                cp.encode_utf8(&mut scalar)
            } else {
                let _ = write!(fallback, "[U+{:04X}]", cp as u32);
                fallback.as_str()
            };
            if measure(out.as_str(), size) + measure(text, size) + measure("...", size) > limit {
                out.push("...");
                break;
            }
            out.push(text);
        }
        self.add(out.as_str(), width, size, kind, icon, 0)
    }

    fn draw(&self, status: &Status, page: u32, available: bool, canvas: &mut Canvas<'_>) {
        canvas.clear();
        if !available || !status.quotas() {
            status::draw(status, canvas);
            return;
        }
        let lang = status.lang();
        self.header(status, page, canvas);
        if self.pages == 0 {
            let empty = if self.source_count == 0 { Msg::NoQuotas } else { Msg::NoMatches };
            canvas.text(empty.text(lang), MARGIN, EMPTY_MESSAGE_Y, 16, false);
        }
        for row in self.rows[..self.count].iter().filter(|row| row.page as u32 == page) {
            self.draw_row(row, canvas);
        }
    }
    /// Header drawn in black, then inverted into a white-on-black band.
    fn header(&self, status: &Status, page: u32, canvas: &mut Canvas<'_>) {
        let lang = status.lang();
        let battery_left = status::battery(status, canvas, 10);
        let mut right = battery_left - 12;
        if self.pages > MAX_PAGE_DOTS {
            let mut number = Text::new();
            let _ = write!(number, "{} / {}", page + 1, self.pages);
            right -= measure(number.as_str(), PAGE_SIZE);
            canvas.text(number.as_str(), right, HEADER_BASELINE - PAGE_SIZE as i16, PAGE_SIZE, false);
        } else if self.pages > 1 {
            // One dot per page; the current page is filled.
            right -= self.pages as i16 * PAGE_DOT_STEP - (PAGE_DOT_STEP - PAGE_DOT);
            for index in 0..self.pages {
                let x = right + index as i16 * PAGE_DOT_STEP;
                canvas.round_frame(x, 14, PAGE_DOT, PAGE_DOT);
                if index == page {
                    canvas.rect(x + 1, 15, PAGE_DOT - 2, PAGE_DOT - 2);
                }
            }
        }
        let limit = right - 10 - MARGIN;
        if status.banner() {
            // The state replaces the title while cached quotas are shown.
            let banner = if status.state == state::FAILURE && status.wifi_retry_seconds > 0 {
                i18n::message(lang, Msg::OfflineRetryBanner, &[&status.wifi_retry_seconds.div_ceil(60)])
            } else {
                let msg = if status.state != state::CONNECTED { Msg::OfflineBanner } else { Msg::RetryBanner };
                i18n::message(lang, msg, &[])
            };
            let size = if measure_weight(banner.as_str(), PAGE_SIZE, true) <= limit { PAGE_SIZE } else { META_SIZE };
            let text = fit(banner.as_str(), size, limit);
            canvas.text(text.as_str(), MARGIN, HEADER_BASELINE - size as i16, size, true);
        } else {
            let mut title = Text::new();
            title.push(Msg::QuotaTitle.text(lang));
            let time = (self.updated != 0).then(|| time::hhmm(self.updated, status.zone())).flatten();
            if time.is_some() {
                title.push(" - ");
            }
            let y = HEADER_BASELINE - TITLE_SIZE as i16;
            canvas.text(title.as_str(), MARGIN, y, TITLE_SIZE, true);
            if let Some(time) = time {
                let x = MARGIN + measure_weight(title.as_str(), TITLE_SIZE, true);
                canvas.clock(x, HEADER_BASELINE - 13);
                canvas.text(time.as_str(), x + 16, y, TITLE_SIZE, true);
            }
        }
        canvas.invert(0, 0, font::WIDTH, HEADER_HEIGHT);
    }
    fn draw_row(&self, row: &Row, canvas: &mut Canvas<'_>) {
        match row.kind {
            Kind::Frame => canvas.round_frame(row.x, row.y, row.width, row.height as i16),
            // Dotted, so window separators stay lighter than card borders.
            Kind::Rule => {
                for dx in (0..row.width).step_by(3) {
                    canvas.pixel(row.x + dx, row.y + 1);
                }
            }
            Kind::Bar | Kind::UnknownBar | Kind::InlineBar | Kind::InlineUnknownBar => {
                let (x, y, w) = (row.x, bar_y(row), row.width);
                if matches!(row.kind, Kind::UnknownBar | Kind::InlineUnknownBar) {
                    // Dashed outline: quota not reported.
                    for dx in (0..w).step_by(4) {
                        canvas.rect(x + dx, y, 2.min(w - dx), 1);
                        canvas.rect(x + dx, y + BAR_HEIGHT - 1, 2.min(w - dx), 1);
                    }
                    canvas.rect(x, y, 1, BAR_HEIGHT);
                    canvas.rect(x + w - 1, y, 1, BAR_HEIGHT);
                } else if row.alert {
                    // Low quota: doubled outline, solid remainder and a hatched used part.
                    canvas.round_frame(x, y, w, BAR_HEIGHT);
                    canvas.frame(x + 1, y + 1, w - 2, BAR_HEIGHT - 2);
                    let fill = (w - 4) * row.progress as i16 / 100;
                    canvas.rect(x + 2, y + 2, fill, BAR_HEIGHT - 4);
                    for py in y + 2..y + BAR_HEIGHT - 2 {
                        for px in x + 3 + fill..x + w - 2 {
                            if (px + py) % 3 == 0 {
                                canvas.pixel(px, py);
                            }
                        }
                    }
                } else {
                    // Rounded outline, a 1px white gap, then the remaining-quota fill.
                    canvas.round_frame(x, y, w, BAR_HEIGHT);
                    canvas.rect(x + 2, y + 2, (w - 4) * row.progress as i16 / 100, BAR_HEIGHT - 4);
                }
            }
            _ => {
                let value = self.row_text(row);
                let bold = row.kind.bold();
                let text_width = measure_weight(value, row.size, bold);
                let x = match row.kind {
                    Kind::Provider => {
                        canvas.icon(row.icon, row.x + 2, row.y - 1);
                        row.x + ICON_SPACE
                    }
                    Kind::Right if row.alert => row.x + row.width - text_width - 4,
                    Kind::Right | Kind::Stamp => row.x + row.width - text_width - 1,
                    _ if row.alert => row.x + 3,
                    _ => row.x,
                };
                if row.alert {
                    canvas.rect(x - 3, row.y + 1, text_width + 6, line_height(row.size) as i16 - 2);
                }
                canvas.text_ink(value, x, row.y - 1, row.size, bold, !row.alert);
                if row.kind == Kind::Provider {
                    // Provider label: icon and name in white on a rounded black tag.
                    let (w, h) = (ICON_SPACE + text_width + CHIP_PAD, 17);
                    canvas.invert(row.x, row.y - 1, w, h);
                    for (cx, cy) in [(row.x, row.y - 1), (row.x + w - 1, row.y - 1), (row.x, row.y + h - 2), (row.x + w - 1, row.y + h - 2)] {
                        canvas.erase(cx, cy, 1, 1);
                    }
                }
            }
        }
    }
}

#[no_mangle]
pub extern "C" fn note4_ui_size() -> usize {
    core::mem::size_of::<Ui>()
}
#[no_mangle]
pub extern "C" fn note4_ui_align() -> usize {
    core::mem::align_of::<Ui>()
}
#[no_mangle]
pub unsafe extern "C" fn note4_ui_init(ui: *mut Ui) {
    core::ptr::write_bytes(ui, 0, 1);
}
#[no_mangle]
pub unsafe extern "C" fn note4_ui_build(ui: *mut Ui, source: *const Source, status: *const Status) -> i32 {
    let (Some(ui), Some(source), Some(status)) = (ui.as_mut(), source.as_ref(), status.as_ref()) else {
        return -1;
    };
    if ui.collect(source).is_err() {
        return -1;
    }
    let sizes = if status.adaptive != 0 { &BODY_SIZES[..] } else { &BODY_SIZES[..1] };
    // Smallest (pages, splits); ties keep the larger text.
    let mut best: Option<(u32, u32, u8)> = None;
    let mut current = None;
    for &size in sizes {
        current = ui.layout(source, status, size).is_ok().then_some(size);
        if current.is_some() && best.map_or(true, |(pages, splits, _)| (ui.pages, ui.splits) < (pages, splits)) {
            best = Some((ui.pages, ui.splits, size));
        }
        // A single page without split cards cannot improve with smaller text.
        if best.is_some_and(|(pages, splits, _)| pages <= 1 && splits == 0) {
            break;
        }
    }
    let Some((_, _, size)) = best else {
        return -1;
    };
    if current != Some(size) && ui.layout(source, status, size).is_err() {
        return -1;
    }
    0
}
#[no_mangle]
pub unsafe extern "C" fn note4_ui_pages(ui: *const Ui) -> u32 {
    (*ui).pages
}
#[no_mangle]
pub unsafe extern "C" fn note4_ui_used(ui: *const Ui) -> u32 {
    ((*ui).count * core::mem::size_of::<Row>() + (*ui).bytes) as u32
}
#[no_mangle]
pub unsafe extern "C" fn note4_ui_next_change(ui: *const Ui) -> i64 {
    (*ui).next_change
}
#[no_mangle]
pub unsafe extern "C" fn note4_ui_draw(ui: *const Ui, status: *const Status, page: u32, available: u32, frame: *mut u8, length: usize) -> i32 {
    if ui.is_null() || status.is_null() || frame.is_null() || length != FRAME_BYTES {
        return -1;
    }
    let canvas = &mut Canvas(core::slice::from_raw_parts_mut(frame, length));
    (*ui).draw(&*status, page, available != 0, canvas);
    0
}
#[cfg(feature = "firmware")]
#[panic_handler]
fn panic(_: &core::panic::PanicInfo<'_>) -> ! {
    extern "C" {
        fn abort() -> !;
    }
    unsafe { abort() }
}

#[cfg(test)]
mod tests;
