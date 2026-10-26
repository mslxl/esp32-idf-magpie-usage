use crate::{cstr, font::{self, Canvas}, Status, Text};
use core::fmt::Write;

pub fn draw(status: &Status, canvas: &mut Canvas<'_>) {
    canvas.text("Magpie Usage",24,20,16,true);
    battery(status,canvas,24);
    let mut detail=Text::new();
    let title=if status.storage_error!=0 {
        detail.push("Settings storage could not start. Press OK to retry. Existing settings are preserved.");
        "STORAGE UNAVAILABLE"
    } else if status.pairing!=0 {
        let _=write!(detail,"PAIRING CODE: {:06}",status.passkey);
        "BLUETOOTH PAIRING"
    } else if status.state==2 {
        detail.push("Unsaved changes are lost. Saved settings remain. OK: discard / UP or DOWN: keep editing");
        "Exit setup?"
    } else if status.state==1 {
        let _=write!(detail,"Open the setup page and connect {}. Wi-Fi: 2.4 GHz. OK: exit setup",unsafe {cstr(status.ble_name)});
        "DEVICE SETTINGS"
    } else if status.state==3 {detail.push("Please wait");"SAVING CONFIGURATION"}
    else if status.phase==6 {
        match status.result {
            1=>{detail.push("Hold DOWN to configure Magpie Web");"MAGPIE SETUP REQUIRED"}
            2=>{detail.push("Hold DOWN and save a valid Web key");"MAGPIE LOGIN EXPIRED"}
            3=>{detail.push("Magpie refused access. Check server permissions.");"ACCESS DENIED"}
            4=>{detail.push("Check certificate, hostname and device time");"CERTIFICATE ERROR"}
            _=>{
                detail.push(match status.result {6=>"Magpie request timed out",8=>"Invalid Magpie response",9=>"Quota update: low memory",10=>"Magpie rate limit reached",_=>"Magpie connection failed"});
                if status.state!=5 {detail.push(". Offline; reconnecting to Wi-Fi");}
                if status.retry_seconds>0 {let _=write!(detail,". Retry in {}s",status.retry_seconds);}
                "QUOTA READING FAILED"
            }
        }
    } else if status.state==4 {
        let _=write!(detail,"Network {} / 5    Round {} / 3",status.slot+1,status.round);
        "CONNECTING WI-FI"
    } else if status.state==6 {detail.push("All configured networks failed in 3 rounds. OK: retry");"CONNECTION FAILED"}
    else if status.state==5 {
        match status.phase {
            1=>{detail.push("Setting time before HTTPS verification");"SYNCING CLOCK"}
            2=>{detail.push("Signing in to Magpie Web");"MAGPIE SIGN IN"}
            _=>{detail.push("Waiting for provider readings");"READING QUOTAS"}
        }
    } else {detail.push("Starting");"STARTING"};
    paragraph(canvas,title,24,78,352,16,true);
    paragraph(canvas,detail.as_str(),24,126,352,16,false);
}
pub fn battery(status:&Status,canvas:&mut Canvas<'_>,y:i16) -> i16 {
    let valid=status.battery_flags&1!=0;
    let charging=status.battery_flags&2!=0;
    let percent=status.battery_percent.min(100);
    let mut label=Text::new();
    if valid {let _=write!(label,"{}%",percent);} else {label.push("--");}
    let left=354-font::measure(label.as_str(),14);
    canvas.text(label.as_str(),left,y-2,14,false);
    // Clipped corners and a small terminal keep the 30x14 silhouette light.
    let x=362;
    canvas.rect(x+1,y,25,1);canvas.rect(x+1,y+13,25,1);
    canvas.rect(x,y+1,1,12);canvas.rect(x+26,y+1,1,12);
    canvas.rect(x+28,y+4,2,6);
    if valid {
        let width=if percent==0 {0}else{((percent*21+99)/100) as i16};
        canvas.rect(x+3,y+3,width,8);
        if percent<=10 && !charging {
            canvas.rect(x+12,y+3,2,5);canvas.rect(x+12,y+9,2,2);
        }
    } else if !charging {
        canvas.text("?",x+10,y-1,14,false);
    }
    if charging {
        // White inset separates the bolt from a dark battery fill.
        for dy in 1..12 {for dx in 8..19 {canvas.0[(y+dy) as usize*400+(x+dx) as usize]=0xffff;}}
        for (dy,(offset,width)) in [(14,3),(13,3),(12,3),(11,6),(10,6),(13,3),(12,3),(11,3),(10,3)].into_iter().enumerate() {
            canvas.rect(x+offset,y+2+dy as i16,width,1);
        }
    }
    left
}

fn paragraph(canvas:&mut Canvas<'_>,value:&str,x:i16,mut y:i16,width:i16,size:u8,bold:bool) {
    let mut line=Text::new(); let mut used=0;
    for cp in value.chars() {
        let advance=font::advance(cp,size);
        if used+advance>width && line.len>0 {
            canvas.text(line.as_str(),x,y,size,bold);y+=size as i16+5;line=Text::new();used=0;
        }
        if y+size as i16>262 {break;}
        let _=line.write_char(cp);used+=advance;
    }
    canvas.text(line.as_str(),x,y,size,bold);
}
