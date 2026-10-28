fn main() {
    #[cfg(windows)]
    unsafe {
        use windows::Win32::{
            Foundation::HGLOBAL,
            System::{
                DataExchange::{CloseClipboard, GetClipboardData, OpenClipboard},
                Memory::{GlobalLock, GlobalUnlock},
                Ole::CF_UNICODETEXT,
            },
        };
        let expected = "VoiceStick 剪贴板验证 ✓";
        sayall_windows::text_output_windows::copy_text(expected).unwrap();
        let mut opened = false;
        for _ in 0..20 {
            if OpenClipboard(None).is_ok() {
                opened = true;
                break;
            }
            std::thread::sleep(std::time::Duration::from_millis(20));
        }
        assert!(opened, "clipboard remained busy during readback");
        let handle = GetClipboardData(CF_UNICODETEXT.0 as u32).unwrap();
        let memory = HGLOBAL(handle.0);
        let pointer = GlobalLock(memory).cast::<u16>();
        assert!(!pointer.is_null());
        let expected: Vec<u16> = expected.encode_utf16().chain(Some(0)).collect();
        let actual = std::slice::from_raw_parts(pointer, expected.len());
        let matches = actual == expected.as_slice();
        let _ = GlobalUnlock(memory);
        let _ = CloseClipboard();
        assert!(matches);
        println!("clipboard_unicode_roundtrip=passed");
    }
}
