#![cfg_attr(feature = "firmware", no_std)]

mod font;
mod time;
mod status;
mod resources { include!("../../../.note4-build/native-assets/resources.rs"); }
use core::{ffi::{c_char, c_void, CStr}, fmt::{self, Write}};
use font::{Canvas, measure};

const ROWS: usize = 4096;
const BYTES: usize = 262144;
const TEXT: usize = 512;
const PROVIDER: u8 = 1;
const BAR: u8 = 2;
const WINDOW: u8 = 3;
const RIGHT: u8 = 4;
const ACCOUNT: u8 = 5;
const PLAN: u8 = 6;
const META: u8 = 7;
const RULE: u8 = 8;
const BOX: u8 = 9;
const UNKNOWN: u8 = 10;
const BOLD: u8 = 11;
const RESET: u8 = 12;
const CARD_TOP: i16 = 40;
const CARD_BOTTOM: i16 = 265;

pub struct Text { bytes: [u8; TEXT], len: usize, overflow: bool }
impl Text {
    fn new() -> Self { Self { bytes: [0; TEXT], len: 0, overflow: false } }
    fn push(&mut self, value: &str) { let _ = self.write_str(value); }
    fn as_str(&self) -> &str { core::str::from_utf8(&self.bytes[..self.len]).unwrap() }
}
impl Write for Text {
    fn write_str(&mut self, value: &str) -> fmt::Result {
        if self.len + value.len() > TEXT { self.overflow = true; return Err(fmt::Error); }
        self.bytes[self.len..self.len+value.len()].copy_from_slice(value.as_bytes());
        self.len += value.len(); Ok(())
    }
}
#[repr(C)]
pub struct Source {
    context: *const c_void,
    count: usize,
    text: extern "C" fn(*const c_void, usize, u32, usize, *const c_char) -> *const c_char,
    children: extern "C" fn(*const c_void, usize, u32) -> usize,
    visible: extern "C" fn(*const c_void, usize) -> u32,
    rank: extern "C" fn(*const c_void, usize) -> usize,
}
impl Source {
    fn get(&self, card: usize, section: u32, child: usize, field: &'static [u8]) -> &str {
        let p = (self.text)(self.context,card,section,child,field.as_ptr().cast());
        if p.is_null() { "" } else { unsafe { CStr::from_ptr(p) }.to_str().unwrap_or("[invalid UTF-8]") }
    }
    fn card(&self, card: usize, field: &'static [u8]) -> &str { self.get(card,0,0,field) }
}
#[repr(C)]
pub struct Status {
    state: u32, slot: u32, round: u32, storage_error: u32, pairing: u32, passkey: u32,
    phase: u32, result: u32, error_page: u32, reading: u32, retry_seconds: u32,
    layout: u32, adaptive: u32, now: i64, timezone: *const c_char, ble_name: *const c_char,
    battery_percent: u32, battery_flags: u32,
}
impl Status {
    fn zone(&self) -> &str { unsafe { cstr(self.timezone) } }
    fn top(&self) -> i16 { if self.state != 5 || self.phase == 6 { 64 } else { CARD_TOP } }
    fn quotas(&self) -> bool {
        self.storage_error == 0 && self.pairing == 0 && !(1..=3).contains(&self.state)
            && !(self.phase == 6 && ((1..=4).contains(&self.result) || self.error_page != 0))
    }
}
unsafe fn cstr<'a>(p: *const c_char) -> &'a str {
    if p.is_null() { "" } else { CStr::from_ptr(p).to_str().unwrap_or("") }
}
#[derive(Clone,Copy)]
#[repr(C)]
struct Row {
    start: u32, len: u16, page: u16, x: i16, y: i16, width: i16,
    size: u8, height: u8, kind: u8, icon: u8, progress: u8, group: u16,
}
#[repr(C)]
pub struct Ui {
    rows: [Row; ROWS], text: [u8; BYTES], count: usize, bytes: usize,
    pages: u32, splits: u32, size: u8, source_count: usize,
}
impl Ui {
    fn reset(&mut self, size: u8) { self.count=0; self.bytes=0; self.pages=0; self.splits=0; self.size=size; }
    fn add(&mut self, text: &str, width: i16, size: u8, kind: u8, icon: u8, progress: u8) -> Result<(),()> {
        if self.count == ROWS || self.bytes + text.len() > BYTES || text.len() > u16::MAX as usize { return Err(()); }
        self.text[self.bytes..self.bytes+text.len()].copy_from_slice(text.as_bytes());
        self.rows[self.count] = Row { start:self.bytes as u32,len:text.len() as u16,page:0,x:8,y:0,width,size,
            height: match kind { RIGHT|PLAN|BOX => 0, BAR|UNKNOWN => 6, RULE => 5,
                PROVIDER => 19, ACCOUNT => 18, META => 18, RESET => 17, _ => ((size as u16*115+50)/100) as u8 },
            kind,icon,progress,group:0 };
        self.bytes += text.len(); self.count += 1; Ok(())
    }
    fn line(&mut self, value: &str, width: i16, size: u8, header: bool, icon: u8) -> Result<(),()> {
        self.line_weight(value,width,size,header,icon,header)
    }
    fn line_weight(&mut self,value:&str,width:i16,size:u8,header:bool,icon:u8,bold:bool)->Result<(),()> {
        let mut line = Text::new();
        let limit = width - if header {20} else {1};
        let mut used = 0;
        for cp in value.chars() {
            let mut fallback = Text::new();
            let mut scalar = [0;4];
            let printable = if cp=='\n' { "\n" } else if font::covered(cp,size) { cp.encode_utf8(&mut scalar) }
                else { let _ = write!(fallback,"[U+{:04X}]",cp as u32); fallback.as_str() };
            for ch in printable.chars() {
                let advance = font::advance_weight(ch,size,bold);
                if ch=='\n' || (used+advance > limit && line.len>0) || line.len+4>TEXT {
                    self.add(line.as_str(),width,size,header as u8,icon,0)?;
                    line = Text::new(); used=0;
                    if ch=='\n' { continue; }
                }
                line.write_char(ch).map_err(|_| ())?; used += advance;
            }
        }
        if line.len>0 { self.add(line.as_str(),width,size,header as u8,icon,0)?; }
        Ok(())
    }
    fn styled(&mut self, value: &str, width: i16, size: u8, kind: u8) -> Result<(),()> {
        let start=self.count;
        self.line_weight(value,width,size,kind==PROVIDER,0,matches!(kind,PROVIDER|ACCOUNT|BOLD))?;
        for row in &mut self.rows[start..self.count] {
            row.kind=kind;
            row.height=match kind { PROVIDER => 19, ACCOUNT => 18, META => 18, RESET => 17, _ => ((size as u16*115+50)/100) as u8 };
        }
        Ok(())
    }
    fn metadata(&mut self, label: &str, value: &str, width: i16) -> Result<(),()> {
        if value.is_empty() {return Ok(());}
        let mut text=Text::new();
        let _=write!(text,"{}{}{}",label,if label.is_empty(){""}else{" "},value);
        if text.overflow { self.styled(label,width,14,META)?; self.styled(value,width,14,META) }
        else {self.styled(text.as_str(),width,14,META)}
    }
    fn identity(&mut self, user:&str, plan:&str, width:i16) -> Result<(),()> {
        let start=self.count;
        self.styled(user,width,self.size,ACCOUNT)?;
        if !plan.is_empty() {
            if self.count==start+1 && user.chars().all(|cp|font::covered(cp,self.size))
                && plan.chars().all(|cp|font::covered(cp,14))
                && font::measure_weight(user,self.size,true)+measure(plan,14)+5<=width {
                self.add(plan,width,14,PLAN,0,0)?;
            } else {
                let start=self.count;
                self.styled(plan,width,14,META)?;
                for row in &mut self.rows[start..self.count] {row.height=20;}
            }
        }
        Ok(())
    }
    fn formatted(&mut self, value: &Text, width: i16) -> Result<(),()> {
        if value.overflow { return Err(()); } self.line(value.as_str(),width,self.size,false,0)
    }
    fn labeled(&mut self, label: &str, value: &str, width: i16) -> Result<(),()> {
        if value.is_empty() { return Ok(()); }
        let mut text=Text::new(); let _=write!(text,"{}: {}",label.trim_end_matches([':', ' ']),value);
        if !text.overflow { return self.formatted(&text,width); }
        // Long server fields are streamed without truncation.
        self.line(label,width,self.size,false,0)?;
        self.line(value,width,self.size,false,0)
    }
    fn height(&self, start: usize, end: usize) -> i16 {
        self.rows[start..end].iter().filter(|r|!matches!(r.kind,RIGHT|PLAN|BOX)).map(|r|r.height as i32).sum::<i32>().min(32767) as i16
    }
    fn card(&mut self, source: &Source, index: usize, width: i16, heading: bool, status: &Status) -> Result<(),()> {
        let get = |field| source.card(index,field);
        let provider = get(b"provider\0");
        let name = get(b"name\0");
        if heading { self.line(if name.eq_ignore_ascii_case(provider) {name} else {provider},width,14,true,font::icon(provider))?; }
        let user=get(b"user\0").trim(); let plan=get(b"plan\0").trim();
        let show_user=!user.is_empty() && !user.eq_ignore_ascii_case(provider)
            && !["api key","api_key","api-key","token"].iter().any(|value|user.eq_ignore_ascii_case(value));
        self.identity(if show_user {user}else{""},
            if !plan.eq_ignore_ascii_case(provider) && !plan.eq_ignore_ascii_case(user) {plan}else{""},width)?;
        let mut seen = ["";10]; seen[0]=provider; seen[1]=user; seen[2]=plan; let mut seen_count=3;
        for (label,field) in [("",b"name\0" as &[u8]),("Alias: ",b"alias\0"),("Seat: ",b"seat\0"),("From: ",b"from\0"),("Kind: ",b"kind\0")] {
            let value=get(field).trim();
            if value.is_empty() || seen[..seen_count].iter().any(|s|s.eq_ignore_ascii_case(value))
                || (field==b"user\0" && ["api key","api_key","api-key","token"].iter().any(|s|value.eq_ignore_ascii_case(s))) { continue; }
            seen[seen_count]=value; seen_count+=1;
            if label.is_empty() { self.line(value,width,self.size,false,0)?; }
            else { self.labeled(label,value,width)?; }
        }
        if !get(b"balance\0").is_empty() { self.metadata("余额",get(b"balance\0"),width)?; }
        if !get(b"until\0").is_empty() {
            let mut text=Text::new(); let _=write!(text,"到期 {}",time::absolute(get(b"until\0"),status.now,status.zone()).as_str()); self.styled(text.as_str(),width,14,META)?;
        }
        self.metadata("续费",get(b"renew\0"),width)?;
        let cached=get(b"asOf\0"); let read=get(b"readAt\0");
        for part in 0..(source.children)(source.context,index,2) {
            self.line(source.get(index,2,part,b"label\0"),width,self.size,false,0)?;
            self.line(source.get(index,2,part,b"text\0"),width,self.size,false,0)?;
            if !source.get(index,2,part,b"error\0").is_empty() { self.line("Balance unavailable",width,self.size,false,0)?; }
        }
        let failed=!get(b"error\0").is_empty();
        if failed { self.styled("读取失败，显示上次数据",width,14,META)?; }
        let windows=(source.children)(source.context,index,1);
        for child in 0..windows {
            let window_start=self.count;
            self.add("",width,14,RULE,0,0)?;
            let w=|field|source.get(index,1,child,field);
            let name=w(b"name\0");
            let mut quota=Text::new();
            let expired=time::instant(w(b"resetsAt\0")).is_some_and(|at|at<=status.now)
                && (!cached.is_empty() || failed || get(b"readDeferred\0")=="true");
            if expired { quota.push("当前额度未知"); }
            else if w(b"unlimited\0")=="true" { quota.push("无限"); }
            else if !w(b"remaining\0").is_empty() { let _=write!(quota,"剩余 {}%",w(b"remaining\0")); }
            else if let Ok(used)=w(b"used\0").parse::<f32>() {
                if used.is_finite() { let _=write!(quota,"剩余 {:.0}%",(100.0-used).max(0.0)); }
                else { quota.push("未知"); }
            } else { quota.push("未知"); }
            if quota.overflow { return Err(()); }
            let name=if name.is_empty(){"Quota window"}else{name};
            if measure(name,self.size)+font::measure_weight(quota.as_str(),self.size,true)+8<=width {
                self.add(name,width,self.size,WINDOW,0,0)?;
                self.add(quota.as_str(),width,self.size,RIGHT,0,0)?;
            } else { self.line(name,width,self.size,false,0)?; self.styled(quota.as_str(),width,self.size,BOLD)?; }
            if !expired && w(b"unlimited\0")!="true" {
                match w(b"used\0").parse::<f32>().ok().filter(|used|used.is_finite()) {
                    Some(used) => {
                        self.add("",width,self.size,BAR,0,(100.0-used).clamp(0.0,100.0) as u8)?;
                        if used>100.0 { let mut t=Text::new(); let _=write!(t,"已用 {}%",w(b"used\0")); self.formatted(&t,width)?; }
                    }
                    None => self.add("",width,self.size,UNKNOWN,0,0)?,
                }
            }
            self.line(w(b"display\0"),width,self.size,false,0)?;
            for (label,field) in [("Amount",b"amount\0" as &[u8]),("Limit",b"limit\0"),("Unit",b"unit\0"),("Pool",b"pool\0"),("Hold",b"holdsWhy\0")] { self.labeled(label,w(field),width)?; }
            if !w(b"resetsAt\0").is_empty() {
                let mut t=Text::new(); let _=write!(t,"重置 {}",time::absolute(w(b"resetsAt\0"),status.now,status.zone()).as_str()); self.styled(t.as_str(),width,14,RESET)?;
            } else if !w(b"resetSecs\0").is_empty() { self.metadata("重置秒数",w(b"resetSecs\0"),width)?; }
            for row in &mut self.rows[window_start..self.count] {row.group=(window_start+1) as u16;}
        }
        if windows==0 && get(b"balance\0").is_empty() && !failed { self.styled("暂无额度窗口",width,14,META)?; }
        if !cached.is_empty() || !read.is_empty() {
            let t=time::reading(if cached.is_empty(){read}else{cached},!cached.is_empty(),get(b"readDeferred\0")=="true",status.now);
            self.metadata("更新",t.as_str(),width)?;
        }
        Ok(())
    }
    fn layout(&mut self, source: &Source, status: &Status, size: u8) -> Result<(),()> {
        self.reset(size); self.source_count=source.count;
        let mut indices=[0usize;1024]; let mut n=0;
        for index in 0..source.count {
            if (source.visible)(source.context,index)!=0 { if n==indices.len(){return Err(());} indices[n]=index; n+=1; }
        }
        indices[..n].sort_unstable_by_key(|i|((source.rank)(source.context,*i),*i));
        let top=status.top(); let usable=CARD_BOTTOM-top;
        let mut page=0u16; let mut y=top; let mut col=0; let mut previous="";
        for &index in &indices[..n] {
            let provider=source.card(index,b"provider\0");
            let start=self.count; let text_start=self.bytes;
            let mut outer=if status.layout==1 {188}else{384};
            self.card(source,index,outer-8,previous!=provider || y==top,status)?;
            if self.height(start,self.count)+8>usable && outer==188 {
                self.count=start; self.bytes=text_start; outer=384;
                if y!=top || col!=0 {page=page.checked_add(1).ok_or(())?;} y=top; col=0;
                self.card(source,index,outer-8,true,status)?;
            }
            let mut end=self.count;
            if self.height(start,end)+8<=usable && y+self.height(start,end)+8>CARD_BOTTOM {
                if outer==188 && col==0 {col=1;} else {page=page.checked_add(1).ok_or(())?;col=0;} y=top;
                self.count=start; self.bytes=text_start;
                self.card(source,index,outer-8,true,status)?; end=self.count;
            }
            if self.height(start,end)+8>usable {
                if y!=top || col!=0 {page=page.checked_add(1).ok_or(())?;} y=top; col=0; self.splits+=1;
            }
            let mut box_y=y; let mut x=8+col*196; y+=4;
            let mut overlay_y=y; let mut plan_x=x+4;
            for at in start..end {
                let row=self.rows[at];
                let h=row.height as i16;
                let first_in_group=row.group!=0 && (at==start || self.rows[at-1].group!=row.group);
                let mut group_end=at+1;
                if first_in_group {while group_end<end && self.rows[group_end].group==row.group {group_end+=1;}}
                let block_height=if first_in_group {self.height(at,group_end)} else {h};
                // Reserve the card's bottom padding and keep complete windows together.
                if !matches!(row.kind,RIGHT|PLAN) && (y+h+4>CARD_BOTTOM ||
                    (first_in_group && block_height<=usable-48 && y+block_height+4>CARD_BOTTOM)) {
                    self.outline(page,x,box_y,outer,y+4-box_y)?;
                    page=page.checked_add(1).ok_or(())?; x=8; col=0; box_y=top; y=top+4;
                    let added=self.count;
                    self.continuation(source,index,provider,outer-8)?;
                    for c in added..self.count {
                        self.rows[c].page=page; self.rows[c].x=x+4; self.rows[c].y=y;
                        y+=self.rows[c].height as i16;
                    }
                }
                self.rows[at].x=if row.kind==PLAN {plan_x}else{x+4};
                self.rows[at].page=page;
                self.rows[at].y=if matches!(row.kind,RIGHT|PLAN) {overlay_y}else{y};
                if !matches!(row.kind,RIGHT|PLAN) {
                    overlay_y=y;
                    if row.kind==ACCOUNT {
                        let value=core::str::from_utf8(&self.text[row.start as usize..row.start as usize+row.len as usize]).unwrap();
                        plan_x=x+4+font::measure_weight(value,row.size,true)+4;
                    }
                }
                y+=h;
            }
            self.outline(page,x,box_y,outer,y+4-box_y)?;
            y+=9; previous=provider;
            if status.layout==1 && outer==384 {page=page.checked_add(1).ok_or(())?;y=top;col=0;previous="";}
        }
        self.pages=if self.count==0 {0} else {self.rows[..self.count].iter().map(|r|r.page as u32+1).max().unwrap_or(0)};
        Ok(())
    }
    fn outline(&mut self,page:u16,x:i16,y:i16,width:i16,height:i16)->Result<(),()> {
        self.add("",width,14,BOX,0,0)?;
        let row=&mut self.rows[self.count-1];
        row.page=page;row.x=x;row.y=y;row.height=u8::try_from(height).map_err(|_|())?;
        Ok(())
    }
    fn continuation(&mut self, source:&Source, index:usize, provider:&str,width:i16)->Result<(),()> {
        self.compact(provider,14,PROVIDER,font::icon(provider),width)?;
        let user=source.card(index,b"user\0"); let plan=source.card(index,b"plan\0");
        let mut context=Text::new();
        let _=write!(context,"{}{}{} · 续",user,if user.is_empty()||plan.is_empty(){""}else{" "},plan);
        self.compact(context.as_str(),self.size,ACCOUNT,0,width)
    }
    fn compact(&mut self, value:&str, size:u8,kind:u8,icon:u8,width:i16)->Result<(),()> {
        let limit=width-if kind==PROVIDER {20}else{1}; let mut out=Text::new();
        for cp in value.chars() {
            let mut scalar=[0;4]; let mut fallback=Text::new();
            let text=if font::covered(cp,size){cp.encode_utf8(&mut scalar)}else{let _=write!(fallback,"[U+{:04X}]",cp as u32);fallback.as_str()};
            if measure(out.as_str(),size)+measure(text,size)+measure("...",size)>limit {out.push("...");break;}
            out.push(text);
        }
        self.add(out.as_str(),width,size,kind,icon,0)
    }
    fn draw(&self, status: &Status, page: u32, available: bool, canvas: &mut Canvas<'_>) {
        canvas.0.fill(0xffff);
        if !available || !status.quotas() { status::draw(status,canvas); return; }
        canvas.text("Magpie Usage",8,5,18,true);
        let mut number=Text::new(); let _=write!(number,"{} / {}",if self.pages==0 {0}else{page+1},self.pages);
        let battery_left=status::battery(status,canvas,10);
        canvas.text(number.as_str(),battery_left-12-measure(number.as_str(),16),7,16,false);
        canvas.rect(8,33,384,2);
        if status.top()>CARD_TOP {
            canvas.text(if status.state!=5 {"Last data - offline; reconnecting"}else{"Last data - request failed; retrying"},8,36,14,false);
        }
        if self.pages==0 {canvas.text(if self.source_count==0 {"Magpie has no account quotas"}else{"No accounts match display settings"},8,100,16,false);}
        for row in &self.rows[..self.count] {
            if row.page as u32!=page {continue;}
            let value=core::str::from_utf8(&self.text[row.start as usize..row.start as usize+row.len as usize]).unwrap();
            match row.kind {
                BOX => {
                    canvas.rect(row.x,row.y,row.width,1);
                    canvas.rect(row.x,row.y+row.height as i16-1,row.width,1);
                    canvas.rect(row.x,row.y,1,row.height as i16);
                    canvas.rect(row.x+row.width-1,row.y,1,row.height as i16);
                }
                RULE => {canvas.rect(row.x,row.y+2,row.width,1);}
                BAR|UNKNOWN => {
                    let y=row.y+2;
                    if row.kind==UNKNOWN {
                        for dx in (0..row.width).step_by(4) {
                            canvas.rect(row.x+dx,y,2.min(row.width-dx),1);
                            canvas.rect(row.x+dx,y+3,2.min(row.width-dx),1);
                        }
                        canvas.rect(row.x,y,1,4);canvas.rect(row.x+row.width-1,y,1,4);
                    } else {
                        canvas.rect(row.x,y,row.width,1);canvas.rect(row.x,y+3,row.width,1);
                        canvas.rect(row.x,y,1,4);canvas.rect(row.x+row.width-1,y,1,4);
                        canvas.rect(row.x+1,y+1,(row.width-2)*row.progress as i16/100,2);
                    }
                }
                _ => {
                    let x=if row.kind==PROVIDER {canvas.icon(row.icon,row.x,row.y-1);row.x+20}
                        else if row.kind==RIGHT {row.x+row.width-font::measure_weight(value,row.size,true)-1}else{row.x};
                    canvas.text(value,x,row.y-1,row.size,matches!(row.kind,PROVIDER|ACCOUNT|RIGHT|BOLD));
                }
            }
        }
    }
}
#[no_mangle]
pub extern "C" fn note4_ui_size() -> usize {core::mem::size_of::<Ui>()}
#[no_mangle]
pub extern "C" fn note4_ui_align() -> usize {core::mem::align_of::<Ui>()}
#[no_mangle]
pub unsafe extern "C" fn note4_ui_init(ui:*mut Ui) {core::ptr::write_bytes(ui,0,1);}
#[no_mangle]
pub unsafe extern "C" fn note4_ui_build(ui:*mut Ui,source:*const Source,status:*const Status)->i32 {
    let (Some(ui),Some(source),Some(status))=(ui.as_mut(),source.as_ref(),status.as_ref()) else{return -1;};
    let mut best=(u32::MAX,u32::MAX,16);
    for size in [16,15,14] {
        if size!=16 && status.adaptive==0 {break;}
        if ui.layout(source,status,size).is_ok() && (ui.pages,ui.splits)<(best.0,best.1) {best=(ui.pages,ui.splits,size);}
    }
    if best.0==u32::MAX {return -1;}
    if ui.layout(source,status,best.2).is_err() {return -1;}
    0
}
#[no_mangle]
pub unsafe extern "C" fn note4_ui_pages(ui:*const Ui)->u32 {(*ui).pages}
#[no_mangle]
pub unsafe extern "C" fn note4_ui_used(ui:*const Ui)->u32 {((*ui).count*core::mem::size_of::<Row>()+(*ui).bytes) as u32}
#[no_mangle]
pub unsafe extern "C" fn note4_ui_draw(ui:*const Ui,status:*const Status,page:u32,available:u32,pixels:*mut u16,length:usize)->i32 {
    if ui.is_null()||status.is_null()||pixels.is_null()||length!=120000 {return -1;}
    (*ui).draw(&*status,page,available!=0,&mut Canvas(core::slice::from_raw_parts_mut(pixels,length)));0
}
#[cfg(feature="firmware")]
#[panic_handler]
fn panic(_: &core::panic::PanicInfo<'_>) -> ! {
    extern "C" {fn abort()->!;}
    unsafe {abort()}
}

#[cfg(test)]
mod tests;
