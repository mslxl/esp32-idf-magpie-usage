use crate::{resources::ZONES, Text};
use core::fmt::Write;

fn days(year: i64, month: i64, day: i64) -> i64 {
    let y = year - i64::from(month <= 2);
    let era = y.div_euclid(400);
    let yo = y - era * 400;
    let m = month + if month > 2 { -3 } else { 9 };
    era * 146097 + yo * 365 + yo / 4 - yo / 100 + (153 * m + 2) / 5 + day - 1 - 719468
}
pub fn instant(value: &str) -> Option<i64> {
    let b = value.as_bytes();
    if b.len() < 20 || b[4] != b'-' || b[7] != b'-' || b[10] != b'T' || b[13] != b':' || b[16] != b':' { return None; }
    let number = |a, z| core::str::from_utf8(&b[a..z]).ok()?.parse::<i64>().ok();
    let (year, month, day, hour, minute, second) = (number(0,4)?, number(5,7)?, number(8,10)?, number(11,13)?, number(14,16)?, number(17,19)?);
    let leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    let max = match month { 2 => if leap {29} else {28}, 4|6|9|11 => 30, 1|3|5|7|8|10|12 => 31, _ => return None };
    if year < 100 || day < 1 || day > max || hour > 23 || minute > 59 || second > 59 { return None; }
    let mut at = 19;
    if b[at] == b'.' {
        at += 1;
        let start = at;
        while at < b.len() && b[at].is_ascii_digit() { at += 1; }
        if at == start { return None; }
    }
    let offset = match b.get(at)? {
        b'Z' if at + 1 == b.len() => 0,
        sign @ (b'+' | b'-') if at + 6 == b.len() && b[at + 3] == b':' => {
            let h = number(at+1,at+3)?; let m = number(at+4,at+6)?;
            if h > 23 || m > 59 { return None; }
            (h * 3600 + m * 60) * if *sign == b'+' {1} else {-1}
        }
        _ => return None,
    };
    Some(days(year,month,day)*86400 + hour*3600 + minute*60 + second - offset)
}
fn local(epoch: i64, zone: &str) -> Option<(i64,i64,i64,i64,i64)> {
    if !(946684800..4102444800).contains(&epoch) { return None; }
    let (_, rules) = ZONES.iter().find(|(name,_)| *name == zone)?;
    let index = rules.partition_point(|(at,_)| *at <= epoch).saturating_sub(1);
    let stamp = epoch + rules[index].1 as i64;
    let day = stamp.div_euclid(86400);
    let z = day + 719468;
    let era = z.div_euclid(146097);
    let doe = z - era*146097;
    let yoe = (doe - doe/1460 + doe/36524 - doe/146096)/365;
    let year = yoe + era*400;
    let doy = doe - (365*yoe + yoe/4 - yoe/100);
    let mp = (5*doy+2)/153;
    let d = doy - (153*mp+2)/5+1;
    let m = mp + if mp < 10 {3} else {-9};
    Some((year+i64::from(m<=2),m,d,stamp.rem_euclid(86400)/60,day))
}
pub fn absolute(value: &str, now: i64, zone: &str) -> Text {
    let mut out = Text::new();
    let Some(epoch) = instant(value) else { out.push("时间无效"); return out; };
    let (Some((y,m,d,clock,day)),Some((cy,_,_,_,today))) = (local(epoch,zone),local(now,zone)) else { out.push("时间超出支持范围"); return out; };
    if day == today { out.push("今天 "); }
    else if day == today+1 { out.push("明天 "); }
    else if day > today+1 && day < today - (today+3).rem_euclid(7)+7 {
        out.push("星期"); out.push(["日","一","二","三","四","五","六"][(day+4).rem_euclid(7) as usize]); out.push(" ");
    } else {
        if y != cy { let _ = write!(out,"{}年",y); }
        let _ = write!(out,"{}月{}日 ",m,d);
    }
    let _ = write!(out,"{:02}:{:02}",clock/60,clock%60);
    out
}
pub fn reading(value: &str, cached: bool, deferred: bool, now: i64) -> Text {
    let mut out = Text::new();
    let Some(epoch) = instant(value) else { out.push("读取时间无效"); return out; };
    if now < 946684800 { out.push("设备时间未知"); return out; }
    let age = now-epoch;
    if age < 0 { out.push("读取时间在未来"); return out; }
    if cached { out.push("缓存 · "); } else if deferred { out.push("未刷新 · "); }
    for (duration,unit) in [(31536000,"年"),(2592000,"个月"),(604800,"周"),(86400,"天"),(3600,"小时"),(60,"分钟")] {
        if age >= duration { let _ = write!(out,"{}{}前",(age+duration/2)/duration,unit); return out; }
    }
    out.push("刚刚"); out
}
