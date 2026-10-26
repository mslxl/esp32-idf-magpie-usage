use super::*;
use std::{collections::BTreeMap, ffi::CString};

type Fields = BTreeMap<&'static str, CString>;
struct Card { fields: Fields, windows: Vec<Fields> }
struct Fixture { cards: Vec<Card>, visible: bool }
fn fields(items: &[(&'static str, &str)]) -> Fields {
    items.iter().map(|(key, value)| (*key, CString::new(*value).unwrap())).collect()
}
extern "C" fn text(context: *const c_void, card: usize, section: u32, child: usize, field: *const c_char) -> *const c_char {
    let fixture = unsafe { &*context.cast::<Fixture>() };
    let key = unsafe { CStr::from_ptr(field) }.to_str().unwrap();
    let card = &fixture.cards[card];
    let values = if section == 1 { &card.windows[child] } else { &card.fields };
    values.get(key).map_or(c"".as_ptr(), |value| value.as_ptr())
}
extern "C" fn children(context: *const c_void, card: usize, section: u32) -> usize {
    if section == 1 { unsafe { &*context.cast::<Fixture>() }.cards[card].windows.len() } else { 0 }
}
extern "C" fn visible(context: *const c_void, _: usize) -> u32 { unsafe { &*context.cast::<Fixture>() }.visible as u32 }
extern "C" fn rank(_: *const c_void, card: usize) -> usize { card }
fn source(fixture: &Fixture) -> Source {
    Source { context: (fixture as *const Fixture).cast(), count: fixture.cards.len(), text, children, visible, rank }
}
fn status(layout: u32) -> Status {
    Status { state: 5, slot: 0, round: 0, storage_error: 0, pairing: 0, passkey: 0,
        phase: 4, result: 0, error_page: 0, reading: 0, retry_seconds: 0, layout, adaptive: 1,
        now: 1793016000, timezone: c"Asia/Shanghai".as_ptr(), ble_name: c"magpie-usage-test".as_ptr(),
        battery_percent: 75, battery_flags: 1 }
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
fn draw(ui: &Ui, state: &Status, page: u32) -> Vec<u16> {
    let mut pixels = vec![0; 120000];
    assert_eq!(unsafe { note4_ui_draw(ui, state, page, 1, pixels.as_mut_ptr(), pixels.len()) }, 0);
    assert!(pixels.iter().all(|pixel| matches!(*pixel, 0 | 65535)));
    pixels
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
        assert_eq!(ui.pages,if layout==0 {3}else{2});
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
fn all_fixed_card_labels_are_in_the_baked_font() {
    for cp in "翻页长按更新续费读取失败，显示上次数据暂无额度窗口续".chars() {
        assert!(font::covered(cp,14),"missing fixed UI character {cp}");
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
