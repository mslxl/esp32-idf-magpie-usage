use super::*;
use std::{collections::BTreeMap, ffi::CString};

type Fields = BTreeMap<&'static str, CString>;
struct Card { fields: Fields, windows: Vec<Fields> }
struct Fixture { cards: Vec<Card>, visible: bool }
fn fields(items: &[(&'static str, &str)]) -> Fields {
    items.iter().map(|(key, value)| (*key, CString::new(*value).unwrap())).collect()
}
fn value(values: &Fields, key: &str) -> *const c_char {
    values.get(key).map_or(core::ptr::null(), |value| value.as_ptr())
}
fn flag(values: &Fields, key: &str) -> u32 {
    values.get(key).is_some_and(|value| value.as_bytes() == b"true") as u32
}
fn fixture_of(context: *const c_void) -> &'static Fixture {
    unsafe { &*context.cast::<Fixture>() }
}
extern "C" fn card(context: *const c_void, index: usize, out: *mut CardFields) {
    let card = &fixture_of(context).cards[index];
    let f = &card.fields;
    unsafe {
        *out = CardFields {
            provider: value(f, "provider"), name: value(f, "name"), user: value(f, "user"),
            plan: value(f, "plan"), alias: value(f, "alias"), seat: value(f, "seat"),
            from: value(f, "from"), kind: value(f, "kind"), balance: value(f, "balance"),
            until: value(f, "until"), renew: value(f, "renew"), as_of: value(f, "asOf"),
            read_at: value(f, "readAt"), read_deferred: flag(f, "readDeferred"),
            failed: f.contains_key("error") as u32, windows: card.windows.len(), balances: 0,
        };
    }
}
extern "C" fn window(context: *const c_void, index: usize, child: usize, out: *mut WindowFields) {
    let w = &fixture_of(context).cards[index].windows[child];
    unsafe {
        *out = WindowFields {
            name: value(w, "name"), display: value(w, "display"), used: value(w, "used"),
            remaining: value(w, "remaining"), resets_at: value(w, "resetsAt"),
            reset_secs: value(w, "resetSecs"), amount: value(w, "amount"), limit: value(w, "limit"),
            unit: value(w, "unit"), pool: value(w, "pool"), holds_why: value(w, "holdsWhy"),
            unlimited: flag(w, "unlimited"),
        };
    }
}
extern "C" fn balance(_: *const c_void, _: usize, _: usize, _: *mut BalanceFields) {
    unreachable!("fixtures have no balance parts");
}
extern "C" fn visible(context: *const c_void, _: usize) -> u32 { fixture_of(context).visible as u32 }
extern "C" fn rank(_: *const c_void, card: usize) -> usize { card }
fn source(fixture: &Fixture) -> Source {
    Source { context: (fixture as *const Fixture).cast(), count: fixture.cards.len(), card, window, balance, visible, rank }
}
fn status(layout: u32) -> Status {
    Status { state: 5, slot: 0, round: 0, storage_error: 0, pairing: 0, passkey: 0,
        phase: 4, result: 0, error_page: 0, reading: 0, retry_seconds: 0, layout, adaptive: 1,
        now: 1793016000, timezone: c"Asia/Shanghai".as_ptr(), ble_name: c"magpie-usage-test".as_ptr(),
        battery_percent: 75, battery_flags: 1, language: 0, wifi_retry_seconds: 0 }
}
fn arena() -> Box<Ui> {
    unsafe {
        let layout = std::alloc::Layout::from_size_align(note4_ui_size(), note4_ui_align()).unwrap();
        let pointer = std::alloc::alloc_zeroed(layout).cast::<Ui>();
        assert!(!pointer.is_null()); note4_ui_init(pointer); Box::from_raw(pointer)
    }
}
fn fixture(count: usize, windows: usize) -> Fixture {
    Fixture { visible: true, cards: (0..count).map(|index| Card {
        fields: fields(&[("provider", ["Codex", "Claude", "Copilot", "DeepSeek", "Other"][index % 5]),
            ("user", &format!("account-{}", index + 1)), ("plan", "Pro")]),
        windows: (0..windows).map(|window| fields(&[("name", &format!("Window {}", window + 1)),
            ("used", "25"), ("resetsAt", "2026-10-26T17:30:00Z")])).collect(),
    }).collect() }
}
/// Draws one page and expands the 1bpp frame to one value per pixel (0 = black).
fn draw(ui: &Ui, state: &Status, page: u32) -> Vec<u16> {
    let mut frame = vec![0xA5; FRAME_BYTES];
    assert_eq!(unsafe { note4_ui_draw(ui, state, page, 1, frame.as_mut_ptr(), frame.len()) }, 0);
    (0..400 * 300).map(|i| if frame[i / 8] & (0x80 >> (i % 8)) != 0 { 0 } else { 65535 }).collect()
}
fn texts(ui: &Ui) -> Vec<String> {
    ui.rows[..ui.count].iter().map(|row| ui.row_text(row).to_string()).collect()
}
fn build(model: &Fixture, state: &Status) -> Box<Ui> {
    let mut ui = arena();
    assert_eq!(unsafe { note4_ui_build(&mut *ui, &source(model), state) }, 0);
    ui
}
fn artifact(name: &str, pixels: &[u16]) {
    if let Ok(directory) = std::env::var("NOTE4_UI_ARTIFACT_DIR") {
        let path = std::path::Path::new(&directory); std::fs::create_dir_all(path).unwrap();
        let mut data = b"P6\n400 300\n255\n".to_vec();
        for &pixel in pixels { data.extend_from_slice(if pixel == 0 { &[0, 0, 0] } else { &[255, 255, 255] }); }
        std::fs::write(path.join(format!("{name}.ppm")), data).unwrap();
    }
}

fn prototype_fixture() -> Fixture {
    Fixture { visible:true, cards: [
        ("CODEX","work@example.test","Plus",vec![("5 小时","42","2026-10-25T08:00:00Z")]),
        ("CODEX","personal@example.test","Pro",vec![("每周","18","2026-10-28T00:00:00Z")]),
        ("CLAUDE","studio@example.test","Max",vec![("会话","61","2026-10-25T09:00:00Z"),("每周","36","2026-10-29T00:00:00Z")]),
        ("DEEPSEEK","API 项目","余额",vec![]),
    ].into_iter().map(|(provider,user,plan,windows)| {
        let mut data=fields(&[("provider",provider),("user",user),("plan",plan),("readAt","2026-10-25T06:30:00Z")]);
        if provider=="DEEPSEEK" {data.insert("balance",CString::new("¥ 128.60").unwrap());}
        Card { fields:data, windows:windows.into_iter().map(|(name,used,resets)|fields(&[("name",name),("used",used),("resetsAt",resets)])).collect() }
    }).collect() }
}

#[test]
fn compact_cards_have_complete_borders_and_preserve_every_page() {
    let model=prototype_fixture();
    for layout in [0,1] {
        let mut state=status(layout);state.now=time::instant("2026-10-25T06:30:00Z").unwrap();
        let mut ui=arena();
        assert_eq!(unsafe {note4_ui_build(&mut *ui,&source(&model),&state)},0);
        // The card area extends to the bottom margin since the footer hints were removed.
        assert_eq!(ui.pages,if layout==0 {2}else{1});
        for page in 0..ui.pages {
            let pixels=draw(&ui,&state,page);
            assert!((8..if layout==0 {392}else{196}).all(|x|pixels[40*400+x]==0),"first card top border");
            let border_x=if layout==0 {391}else{195};
            assert!((40..60).all(|y|pixels[y*400+8]==0 && pixels[y*400+border_x]==0));
            if layout==1 {assert!((204..392).all(|x|pixels[40*400+x]==0));}
            artifact(&format!("prototype-{}-{page}",if layout==0 {"A"}else{"B"}),&pixels);
        }
    }
}

#[test]
fn battery_states_update_only_the_header_and_preserve_card_layout() {
    let model=prototype_fixture();
    for layout in [0,1] {
        let mut state=status(layout);let mut ui=arena();
        assert_eq!(unsafe {note4_ui_build(&mut *ui,&source(&model),&state)},0);
        let baseline=draw(&ui,&state,0);
        let mut headers=Vec::new();
        for (name,percent,flags) in [("empty",0,1),("low",8,1),("half",50,1),("full",100,5),("charging",50,3),("unknown",0,0)] {
            state.battery_percent=percent;state.battery_flags=flags;
            let pixels=draw(&ui,&state,0);
            assert_eq!(&pixels[33*400..],&baseline[33*400..],"battery must not change cards or footer");
            assert!((363..388).all(|x|pixels[10*400+x]==0),"battery outline");
            assert!((14..20).all(|y|pixels[y*400+390]==0),"battery terminal");
            let header=pixels[..33*400].to_vec();
            assert!(!headers.contains(&header),"each power state must be visually distinct");
            headers.push(header);
            artifact(&format!("battery-{}-{name}",if layout==0 {"A"}else{"B"}),&pixels);
            state.pairing=1;
            let setup=draw(&ui,&state,0);
            assert!((363..388).all(|x|setup[24*400+x]==0),"status screens retain battery");
            state.pairing=0;
        }
    }
}

#[test]
fn every_fixed_string_is_in_every_baked_font_size() {
    for text in i18n::every_string() {
        for cp in text.chars() {
            for size in [14, 15, 16, 18] {
                assert!(font::covered(cp, size), "missing UI character {cp:?} at {size}px in {text:?}");
            }
        }
    }
}

#[test]
fn cached_pages_render_deterministically_for_both_layouts_and_long_cards() {
    for (name, count, windows) in [("ordinary", 8, 1), ("tall", 1, 24), ("many", 120, 2)] {
        for layout in [0, 1] {
            let model = fixture(count, windows); let state = status(layout); let mut ui = arena();
            assert_eq!(unsafe { note4_ui_build(&mut *ui, &source(&model), &state) }, 0);
            let pages = unsafe { note4_ui_pages(&*ui) };
            assert!(pages > 1, "{name} layout {layout}");
            let first = draw(&ui, &state, 0);
            for page in 0..pages { let frame = draw(&ui, &state, page); if name != "many" {artifact(&format!("{name}-{layout}-{page}"), &frame);} }
            assert_eq!(draw(&ui, &state, 0), first);
        }
    }
}

#[test]
fn rejected_candidate_preserves_committed_render_and_subsequent_build_recovers() {
    let state = status(0); let good = fixture(2, 1); let large = fixture(1025, 1);
    let mut committed = arena(); let mut candidate = arena();
    assert_eq!(unsafe { note4_ui_build(&mut *committed, &source(&good), &state) }, 0);
    let before = draw(&committed, &state, 0);
    assert_eq!(unsafe { note4_ui_build(&mut *candidate, &source(&large), &state) }, -1);
    assert_eq!(draw(&committed, &state, 0), before);
    assert_eq!(unsafe { note4_ui_build(&mut *candidate, &source(&good), &state) }, 0);
    assert_eq!(draw(&candidate, &state, 0), before);
}

#[test]
fn empty_snapshot_filter_and_authentication_are_distinct_display_states() {
    let mut ui = arena(); let mut state = status(0); let empty = fixture(0, 0);
    assert_eq!(unsafe { note4_ui_build(&mut *ui, &source(&empty), &state) }, 0);
    let empty_frame = draw(&ui, &state, 0); artifact("empty", &empty_frame);
    let mut hidden = fixture(1, 1); hidden.visible = false;
    assert_eq!(unsafe { note4_ui_build(&mut *ui, &source(&hidden), &state) }, 0);
    let filtered = draw(&ui, &state, 0); assert_ne!(filtered, empty_frame); artifact("filtered", &filtered);
    state.phase = 6; state.result = 2;
    let auth = draw(&ui, &state, 0); assert_ne!(auth, filtered); artifact("auth", &auth);
}

#[test]
fn unsupported_unicode_and_long_fields_remain_renderable() {
    let mut model = fixture(1, 1);
    model.cards[0].fields.insert("user", CString::new("长账号🙂".repeat(100)).unwrap());
    let mut ui = arena(); let state = status(1);
    assert_eq!(unsafe { note4_ui_build(&mut *ui, &source(&model), &state) }, 0);
    assert!(unsafe { note4_ui_pages(&*ui) } > 1);
    artifact("unicode", &draw(&ui, &state, 0));
}

fn at(value: &str) -> i64 { time::instant(value).unwrap() }
fn one_window(used: &str, read_key: &'static str, read: &str) -> Fixture {
    Fixture { visible: true, cards: vec![Card {
        fields: fields(&[("provider", "Codex"), ("user", "work@example.test"), ("plan", "Plus"), (read_key, read)]),
        windows: vec![fields(&[("name", "Weekly"), ("used", used), ("resetsAt", "2026-10-25T08:00:00Z")])],
    }] }
}
fn zh(mut state: Status) -> Status { state.language = 1; state }

#[test]
fn card_text_follows_the_display_language() {
    let model = one_window("42", "readAt", "2026-10-25T06:30:00Z");
    let mut state = status(0);
    state.now = at("2026-10-25T06:30:00Z");
    let en = texts(&build(&model, &state));
    for expected in ["58% left", "Resets Today 16:00", "Updated 14:30"] {
        assert!(en.iter().any(|t| t == expected), "{expected} in {en:?}");
    }
    let cn = texts(&build(&model, &zh(state)));
    for expected in ["剩余 58%", "重置 今天 16:00", "更新 14:30"] {
        assert!(cn.iter().any(|t| t == expected), "{expected} in {cn:?}");
    }
    let mut state = status(0);
    state.now = at("2026-10-25T06:30:00Z");
    let en_frame = draw(&build(&model, &state), &state, 0);
    let state = zh(state);
    let zh_frame = draw(&build(&model, &state), &state, 0);
    assert_ne!(en_frame, zh_frame);
    artifact("language-zh", &zh_frame);
}

#[test]
fn updated_time_is_absolute_and_stable_within_the_day() {
    let model = one_window("42", "readAt", "2026-10-24T06:30:00Z");
    let mut state = status(0);
    state.now = at("2026-10-25T06:30:00Z");
    let before = draw(&build(&model, &state), &state, 0);
    assert!(texts(&build(&model, &state)).iter().any(|t| t == "Updated Oct 24 14:30"));
    assert!(texts(&build(&model, &zh(status_at(&state)))).iter().any(|t| t == "更新 10月24日 14:30"));
    state.now += 50 * 60;
    assert_eq!(draw(&build(&model, &state), &state, 0), before, "no per-minute changes");

    let cached = one_window("42", "asOf", "2026-10-25T06:30:00Z");
    assert!(texts(&build(&cached, &state)).iter().any(|t| t == "Updated 14:30 · cached"));
    assert!(texts(&build(&cached, &zh(status_at(&state)))).iter().any(|t| t == "更新 14:30 · 缓存"));
    let mut deferred = one_window("42", "readAt", "2026-10-25T06:30:00Z");
    deferred.cards[0].fields.insert("readDeferred", CString::new("true").unwrap());
    assert!(texts(&build(&deferred, &state)).iter().any(|t| t == "Updated 14:30 · not refreshed"));
}
fn status_at(state: &Status) -> Status { let mut copy = status(state.layout); copy.now = state.now; copy }

#[test]
fn next_change_is_the_earliest_reset_or_local_midnight() {
    let mut state = status(0);
    state.now = at("2026-10-25T06:30:00Z");
    let ui = build(&one_window("42", "readAt", "2026-10-25T06:30:00Z"), &state);
    assert_eq!(unsafe { note4_ui_next_change(&*ui) }, at("2026-10-25T08:00:00Z"));
    let ui = build(&fixture(0, 0), &state);
    assert_eq!(unsafe { note4_ui_next_change(&*ui) }, at("2026-10-25T16:00:00Z"), "Shanghai midnight");
    state.now = 0;
    let ui = build(&fixture(0, 0), &state);
    assert_eq!(unsafe { note4_ui_next_change(&*ui) }, time::MIN_EPOCH, "unsynced clock waits for sync");
}

#[test]
fn low_remaining_quota_is_inverted_and_bars_are_outlined() {
    let mut state = status(0);
    state.now = at("2026-10-25T06:30:00Z");
    for (used, alert) in [("90", true), ("85", true), ("80", false)] {
        let ui = build(&one_window(used, "readAt", "2026-10-25T06:30:00Z"), &state);
        let pixels = draw(&ui, &state, 0);
        let rows = &ui.rows[..ui.count];
        let right = rows.iter().find(|row| row.kind == Kind::Right).unwrap();
        assert_eq!(right.alert, alert, "used {used}");
        let corner = pixels[(right.y + 1) as usize * 400 + (right.x + right.width - 2) as usize];
        assert_eq!(corner == 0, alert, "inverted label background for used {used}");
        let bar = rows.iter().find(|row| row.kind == Kind::Bar).unwrap();
        let (x, y) = (bar.x as usize, (bar.y + BAR_TOP) as usize);
        assert!((y..y + BAR_HEIGHT as usize).all(|py| pixels[py * 400 + x] == 0), "left outline");
        assert!(pixels[(y + 4) * 400 + x + 1] != 0, "white gap inside the outline");
        assert!(pixels[(y + 4) * 400 + x + 2] == 0, "remaining fill");
        if alert { artifact(&format!("low-{used}"), &pixels); }
    }
}

#[test]
fn storage_and_wifi_retry_status_pages_in_both_languages() {
    let mut state = status(0);
    state.storage_error = 2;
    assert!(status::content(&state).1.as_str().contains("Hold DOWN to erase settings and start setup. OK: retry"));
    assert!(status::content(&zh(status_storage(2))).1.as_str().contains("长按 DOWN 清除设置"));
    state.storage_error = 1;
    assert!(status::content(&state).1.as_str().contains("Press OK to retry"));
    let mut wifi = status(0);
    wifi.state = 6;
    assert!(status::content(&wifi).1.as_str().ends_with("OK: retry"));
    wifi.wifi_retry_seconds = 299;
    assert!(status::content(&wifi).1.as_str().contains("Retrying automatically in 5 min. OK: retry now"));
    assert!(status::content(&zh(status_wifi(299))).1.as_str().contains("5 分钟后自动重试"));
    let ui = build(&fixture(0, 0), &wifi);
    let frame = draw_status(&ui, &wifi);
    artifact("wifi-retry-en", &frame);
    let cn = zh(status_wifi(299));
    assert_ne!(draw_status(&ui, &cn), frame);
    artifact("wifi-retry-zh", &draw_status(&ui, &cn));
    artifact("storage-erase-zh", &draw_status(&ui, &zh(status_storage(2))));
    artifact("storage-erase-en", &draw_status(&ui, &status_storage(2)));
    // With cached quotas the countdown appears in the offline banner instead.
    let mut model = fixture(1, 1);
    model.cards[0].fields.insert("asOf", CString::new("2026-10-25T06:30:00Z").unwrap());
    let ui = build(&model, &wifi);
    let banner = draw(&ui, &wifi, 0);
    wifi.wifi_retry_seconds = 61;
    assert_ne!(draw(&ui, &wifi, 0)[..64 * 400], banner[..64 * 400], "banner shows the minutes left");
    artifact("wifi-retry-banner", &banner);
}
fn draw_status(ui: &Ui, state: &Status) -> Vec<u16> {
    let mut frame = vec![0; FRAME_BYTES];
    assert_eq!(unsafe { note4_ui_draw(ui, state, 0, 0, frame.as_mut_ptr(), frame.len()) }, 0);
    (0..400 * 300).map(|i| if frame[i / 8] & (0x80 >> (i % 8)) != 0 { 0 } else { 65535 }).collect()
}
fn status_storage(code: u32) -> Status { let mut s = status(0); s.storage_error = code; s }
fn status_wifi(seconds: u32) -> Status { let mut s = status(0); s.state = 6; s.wifi_retry_seconds = seconds; s }
