//! Nonactivating local streaming transcript window. Polling reads only in-memory
//! speech status; transcripts never enter diagnostics or a remote webview.
use std::time::{Duration, Instant};
use tauri::Manager;
const LABEL: &str = "speech-overlay";
pub fn initialize(app: &tauri::AppHandle) {
    let window = match tauri::WebviewWindowBuilder::new(
        app,
        LABEL,
        tauri::WebviewUrl::App("speech-overlay.html".into()),
    )
    .title("VoiceStick · 实时语音")
    .inner_size(500.0, 140.0)
    .resizable(false)
    .decorations(false)
    .transparent(true)
    .shadow(false)
    .always_on_top(true)
    .skip_taskbar(true)
    .focused(false)
    .focusable(false)
    .visible(false)
    .build()
    {
        Ok(window) => window,
        Err(_) => {
            sayall_windows::gatt_note("speech_overlay phase=create result=failed".into());
            return;
        }
    };
    let _ = window.set_ignore_cursor_events(true);
    let app = app.clone();
    std::thread::Builder::new()
        .name("speech-overlay".into())
        .spawn(move || {
            let mut shown = false;
            let mut generation = 0;
            let mut terminal: Option<Instant> = None;
            let mut last_phase = String::new();
            loop {
                std::thread::sleep(Duration::from_millis(100));
                let Some(window) = app.get_webview_window(LABEL) else {
                    break;
                };
                let status = sayall_windows::volcengine::status();
                let active = matches!(
                    status.phase.as_str(),
                    "connecting" | "listening" | "recognizing" | "recognized"
                );
                let finished = matches!(
                    status.phase.as_str(),
                    "completed"
                        | "copied"
                        | "copied_and_inserted"
                        | "copy_required"
                        | "failed"
                        | "empty"
                );
                let new_generation = status.generation != generation;
                if new_generation {
                    generation = status.generation;
                    terminal = None;
                    last_phase.clear();
                }
                if finished && last_phase != status.phase {
                    terminal = Some(Instant::now() + Duration::from_millis(1000));
                }
                last_phase = status.phase.clone();
                let visible = status.generation > 0
                    && sayall_windows::volcengine::overlay_enabled()
                    && (active
                        || (finished
                            && terminal.is_some_and(|deadline| Instant::now() < deadline)));
                if visible && (!shown || new_generation) {
                    let app_for_show = app.clone();
                    let window_for_show = window.clone();
                    let _ = app.run_on_main_thread(move || {
                        let before = sayall_windows::focus_windows::foreground_process_id();
                        let monitor = app_for_show
                            .cursor_position()
                            .ok()
                            .and_then(|point| {
                                app_for_show
                                    .monitor_from_point(point.x, point.y)
                                    .ok()
                                    .flatten()
                            })
                            .or_else(|| app_for_show.primary_monitor().ok().flatten());
                        if let Some(monitor) = monitor {
                            if let Ok(size) = window_for_show.outer_size() {
                                let area = monitor.work_area().size;
                                let origin = monitor.work_area().position;
                                use sayall_windows::volcengine::OverlayPosition;
                                let margin = (24.0 * monitor.scale_factor()) as i32;
                                let dx = (area.width as i32 - size.width as i32).max(0);
                                let dy = (area.height as i32 - size.height as i32).max(0);
                                let edge_x = margin.min(dx);
                                let edge_y = margin.min(dy);
                                let (x, y) = match sayall_windows::volcengine::overlay_position() {
                                    OverlayPosition::Center => (dx / 2, dy / 2),
                                    OverlayPosition::TopLeft => (edge_x, edge_y),
                                    OverlayPosition::TopRight => (dx - edge_x, edge_y),
                                    OverlayPosition::BottomLeft => (edge_x, dy - edge_y),
                                    OverlayPosition::BottomRight => (dx - edge_x, dy - edge_y),
                                };
                                let (x, y) = (origin.x + x, origin.y + y);
                                let _ = window_for_show
                                    .set_position(tauri::PhysicalPosition::new(x, y));
                            }
                        }
                        let result = window_for_show.show();
                        let after = sayall_windows::focus_windows::foreground_process_id();
                        sayall_windows::gatt_note(format!(
                            "speech_overlay phase=show result={} focus_preserved={}",
                            if result.is_ok() { "shown" } else { "failed" },
                            before == after
                        ));
                    });
                    shown = true;
                } else if !visible && shown {
                    let _ = window.hide();
                    shown = false;
                    sayall_windows::gatt_note("speech_overlay phase=hide result=hidden".into());
                }
            }
        })
        .ok();
}
