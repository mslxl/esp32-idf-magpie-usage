//! Every fixed on-screen string, in English and Chinese.
//!
//! Templates use `{}` placeholders that `format` fills in order, so each
//! language keeps its own word order. Tests check that every glyph used here
//! is present in the baked fonts.

use crate::Text;
use core::fmt::{self, Write};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u8)]
pub enum Lang {
    En,
    Zh,
}

impl Lang {
    /// Display language code from the device settings; unknown codes fall back to English.
    pub fn from_code(code: u32) -> Self {
        if code == 1 {
            Lang::Zh
        } else {
            Lang::En
        }
    }
}

macro_rules! messages {
    ($($name:ident => $en:expr, $zh:expr;)*) => {
        #[derive(Clone, Copy, Debug, PartialEq, Eq)]
        pub enum Msg { $($name),* }

        impl Msg {
            #[cfg(test)]
            pub const ALL: &'static [Msg] = &[$(Msg::$name),*];

            pub fn text(self, lang: Lang) -> &'static str {
                match (self, lang) {
                    $((Msg::$name, Lang::En) => $en, (Msg::$name, Lang::Zh) => $zh,)*
                }
            }
        }
    };
}

messages! {
    // Quota page header and page-level states.
    Title => "Magpie Usage", "Magpie 用量";
    OfflineBanner => "Last data - offline; reconnecting", "上次数据 · 离线，正在重连";
    OfflineRetryBanner => "Last data - offline; retrying in {} min", "上次数据 · 离线，{} 分钟后重试";
    RetryBanner => "Last data - request failed; retrying", "上次数据 · 请求失败，正在重试";
    NoQuotas => "Magpie has no account quotas", "Magpie 没有账号额度";
    NoMatches => "No accounts match display settings", "没有符合显示设置的账号";

    // Card fields.
    LabelValue => "{}: {}", "{}：{}";
    Alias => "Alias", "别名";
    Seat => "Seat", "席位";
    From => "From", "来源";
    Kind => "Kind", "类型";
    Amount => "Amount", "数量";
    Limit => "Limit", "上限";
    Unit => "Unit", "单位";
    Pool => "Pool", "额度池";
    Hold => "Hold", "暂停";
    Balance => "Balance", "余额";
    BalanceUnavailable => "Balance unavailable", "余额不可用";
    ExpiresAt => "Expires {}", "到期 {}";
    Renews => "Renews", "续费";
    ReadFailed => "Read failed; showing last data", "读取失败，显示上次数据";
    NoWindows => "No quota windows", "暂无额度窗口";
    QuotaWindow => "Quota window", "额度窗口";
    QuotaUnknown => "Current quota unknown", "当前额度未知";
    Unlimited => "Unlimited", "无限";
    Unknown => "Unknown", "未知";
    Remaining => "{}% left", "剩余 {}%";
    Used => "{}% used", "已用 {}%";
    ResetsAt => "Resets {}", "重置 {}";
    ResetsIn => "Resets in {} s", "{} 秒后重置";
    Updated => "Updated {}", "更新 {}";
    Cached => "{} · cached", "{} · 缓存";
    NotRefreshed => "{} · not refreshed", "{} · 未刷新";
    Continued => "{} · cont.", "{} · 续";

    // Times.
    InvalidTime => "Invalid time", "时间无效";
    TimeOutOfRange => "Time out of range", "时间超出支持范围";
    InvalidReadTime => "Invalid read time", "读取时间无效";
    DeviceTimeUnknown => "Device time unknown", "设备时间未知";
    Today => "Today", "今天";
    Tomorrow => "Tomorrow", "明天";
    Date => "{} {}", "{}月{}日";
    DateWithYear => "{} {}, {}", "{}年{}月{}日";

    // Status pages: titles.
    StorageUnavailable => "STORAGE UNAVAILABLE", "设置存储不可用";
    StorageDamaged => "SETTINGS STORAGE ERROR", "设置存储损坏";
    BluetoothPairing => "BLUETOOTH PAIRING", "蓝牙配对";
    ExitSetup => "Exit setup?", "退出设置？";
    DeviceSettings => "DEVICE SETTINGS", "设备设置";
    SavingConfiguration => "SAVING CONFIGURATION", "正在保存配置";
    MagpieSetupRequired => "MAGPIE SETUP REQUIRED", "需要设置 Magpie";
    MagpieLoginExpired => "MAGPIE LOGIN EXPIRED", "Magpie 登录失效";
    AccessDenied => "ACCESS DENIED", "访问被拒绝";
    CertificateError => "CERTIFICATE ERROR", "证书错误";
    QuotaReadingFailed => "QUOTA READING FAILED", "额度读取失败";
    ConnectingWifi => "CONNECTING WI-FI", "正在连接 Wi-Fi";
    ConnectionFailed => "CONNECTION FAILED", "连接失败";
    SyncingClock => "SYNCING CLOCK", "正在同步时钟";
    MagpieSignIn => "MAGPIE SIGN IN", "正在登录 Magpie";
    ReadingQuotas => "READING QUOTAS", "正在读取额度";
    Starting => "STARTING", "正在启动";

    // Status pages: details.
    StorageRetry => "Settings storage could not start. Press OK to retry. Existing settings are preserved.",
        "设置存储无法启动。按 OK 重试，现有设置保持不变。";
    StorageErase => "Stored settings cannot be read. Hold DOWN to erase settings and start setup. OK: retry",
        "无法读取已保存的设置。长按 DOWN 清除设置并开始设置。OK：重试";
    PairingCode => "PAIRING CODE: {}", "配对码：{}";
    DiscardChanges => "Unsaved changes are lost. Saved settings remain. OK: discard / UP or DOWN: keep editing",
        "未保存的修改将丢失，已保存的设置保留。OK：放弃 / UP 或 DOWN：继续编辑";
    OpenSetup => "Open the setup page and connect {}. Wi-Fi: 2.4 GHz. OK: exit setup",
        "打开设置网页并连接 {}。Wi-Fi：2.4 GHz。OK：退出设置";
    PleaseWait => "Please wait", "请稍候";
    ConfigureMagpie => "Hold DOWN to configure Magpie Web", "长按 DOWN 设置 Magpie Web";
    SaveWebKey => "Hold DOWN and save a valid Web key", "长按 DOWN 并保存有效的 Web key";
    CheckPermissions => "Magpie refused access. Check server permissions.", "Magpie 拒绝访问，请检查服务器权限。";
    CheckCertificate => "Check certificate, hostname and device time", "请检查证书、主机名和设备时间";
    RequestTimedOut => "Magpie request timed out", "Magpie 请求超时";
    InvalidResponse => "Invalid Magpie response", "Magpie 响应无效";
    LowMemory => "Quota update: low memory", "额度更新：内存不足";
    RateLimited => "Magpie rate limit reached", "Magpie 请求过于频繁";
    ConnectionError => "Magpie connection failed", "Magpie 连接失败";
    ReconnectingWifi => ". Offline; reconnecting to Wi-Fi", "。离线，正在重连 Wi-Fi";
    RetryIn => ". Retry in {}s", "。{} 秒后重试";
    NetworkRound => "Network {} / 5    Round {} / 3", "网络 {} / 5    第 {} / 3 轮";
    WifiFailed => "All configured networks failed in 3 rounds. OK: retry", "所有已配置网络 3 轮均连接失败。OK：重试";
    WifiFailedRetry => "All configured networks failed in 3 rounds. Retrying automatically in {} min. OK: retry now",
        "所有已配置网络 3 轮均连接失败。{} 分钟后自动重试。OK：立即重试";
    SettingTime => "Setting time before HTTPS verification", "HTTPS 验证前正在校准时间";
    SigningIn => "Signing in to Magpie Web", "正在登录 Magpie Web";
    WaitingReadings => "Waiting for provider readings", "正在等待厂商读数";
    StartingDetail => "Starting", "正在启动";
}

const WEEKDAYS: [[&str; 7]; 2] = [
    ["Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"],
    ["星期日", "星期一", "星期二", "星期三", "星期四", "星期五", "星期六"],
];
const MONTHS: [[&str; 12]; 2] = [
    ["Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"],
    ["1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12"],
];

/// Weekday name; 0 is Sunday.
pub fn weekday(lang: Lang, day: usize) -> &'static str {
    WEEKDAYS[lang as usize][day % 7]
}

/// Month as used by the `Date` templates; 1 is January.
pub fn month(lang: Lang, month: usize) -> &'static str {
    MONTHS[lang as usize][(month + 11) % 12]
}

/// Appends `template` with each `{}` replaced by the next argument.
pub fn format(out: &mut Text, template: &str, args: &[&dyn fmt::Display]) {
    let mut rest = template;
    let mut args = args.iter();
    while let Some(at) = rest.find("{}") {
        out.push(&rest[..at]);
        if let Some(arg) = args.next() {
            let _ = write!(out, "{}", arg);
        }
        rest = &rest[at + 2..];
    }
    out.push(rest);
}

/// Shorthand for formatting one message into a fresh buffer.
pub fn message(lang: Lang, msg: Msg, args: &[&dyn fmt::Display]) -> Text {
    let mut out = Text::new();
    format(&mut out, msg.text(lang), args);
    out
}

#[cfg(test)]
pub fn every_string() -> impl Iterator<Item = &'static str> {
    let langs = [Lang::En, Lang::Zh];
    let messages = Msg::ALL.iter().flat_map(move |msg| langs.into_iter().map(move |lang| msg.text(lang)));
    messages
        .chain(WEEKDAYS.iter().flatten().copied())
        .chain(MONTHS.iter().flatten().copied())
}
