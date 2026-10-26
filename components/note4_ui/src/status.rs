use crate::{cstr, font::{self, Canvas}, Status, Text};
use core::fmt::Write;

pub fn draw(status: &Status, canvas: &mut Canvas<'_>) {
    canvas.text("Magpie Usage",24,20,16,true);
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
    canvas.rect(24,267,352,1);
    canvas.text("Hold DOWN: setup",24,276,14,false);
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
