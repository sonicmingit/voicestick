//! Final transcript clipboard output. Owns an invisible clipboard owner window;
//! never reads or logs the user's previous clipboard contents.
use std::{mem, thread, time::Duration};
use windows::{
    core::w,
    Win32::{
        Foundation::{GlobalFree, HANDLE, HGLOBAL},
        System::{
            DataExchange::{CloseClipboard, EmptyClipboard, OpenClipboard, SetClipboardData},
            Memory::{GlobalAlloc, GlobalLock, GlobalUnlock, GMEM_MOVEABLE},
            Ole::CF_UNICODETEXT,
        },
        UI::WindowsAndMessaging::{
            CreateWindowExW, DestroyWindow, HWND_MESSAGE, WINDOW_EX_STYLE, WINDOW_STYLE,
        },
    },
};

pub fn copy_text(text: &str) -> Result<(), String> {
    unsafe {
        let owner = CreateWindowExW(
            WINDOW_EX_STYLE(0),
            w!("STATIC"),
            None,
            WINDOW_STYLE(0),
            0,
            0,
            0,
            0,
            Some(HWND_MESSAGE),
            None,
            None,
            None,
        )
        .map_err(|_| "无法创建剪贴板窗口".to_owned())?;
        let result = (|| {
            let mut opened = false;
            for _ in 0..10 {
                if OpenClipboard(Some(owner)).is_ok() {
                    opened = true;
                    break;
                }
                thread::sleep(Duration::from_millis(20));
            }
            if !opened {
                return Err("剪贴板正被其他程序占用，请稍后复制".into());
            }
            let result = (|| {
                let utf16: Vec<u16> = text.encode_utf16().chain(Some(0)).collect();
                let memory = GlobalAlloc(GMEM_MOVEABLE, utf16.len() * mem::size_of::<u16>())
                    .map_err(|_| "无法分配剪贴板内存")?;
                let pointer = GlobalLock(memory);
                if pointer.is_null() {
                    let _ = GlobalFree(Some(memory));
                    return Err("无法写入剪贴板内存".into());
                }
                std::ptr::copy_nonoverlapping(utf16.as_ptr(), pointer.cast::<u16>(), utf16.len());
                let _ = GlobalUnlock(memory);
                if EmptyClipboard().is_err() {
                    let _ = GlobalFree(Some(memory));
                    return Err("无法清空剪贴板".into());
                }
                if SetClipboardData(CF_UNICODETEXT.0 as u32, Some(HANDLE(memory.0))).is_err() {
                    let _ = GlobalFree(Some(HGLOBAL(memory.0)));
                    return Err("复制到剪贴板失败".into());
                }
                Ok(())
            })();
            let _ = CloseClipboard();
            result
        })();
        let _ = DestroyWindow(owner);
        crate::gatt_note(format!(
            "asr_output target=clipboard result={}",
            if result.is_ok() { "copied" } else { "failed" }
        ));
        result
    }
}
