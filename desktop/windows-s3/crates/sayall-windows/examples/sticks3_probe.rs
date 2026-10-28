//! Local verification: enumerate paired StickS3/CABLE devices and optionally
//! connect, confirm firmware acknowledgement, then release the connection.
#[cfg(windows)]
fn main() {
    if std::env::args().any(|arg| arg == "--exit-app") {
        sayall_windows::graceful_exit::signal_named(
            sayall_windows::graceful_exit::GRACEFUL_EXIT_EVENT_NAME,
        )
        .expect("request graceful exit of the StickS3 experiment");
        println!("graceful_exit=requested");
        return;
    }
    use sayall_windows::{ConnectionPhase, WindowsPlatform};
    use std::{
        thread,
        time::{Duration, Instant},
    };
    let _ = sayall_windows::initialize_diagnostic_log(
        std::env::current_dir().unwrap().join("sticks3-device.log"),
        sayall_windows::DiagnosticLogMetadata {
            app_version: "0.1.0".into(),
            app_build: "local-probe".into(),
            source_revision: "working-tree".into(),
            build_channel: "local".into(),
            release_tag: "none".into(),
        },
    );
    let platform = WindowsPlatform::default();
    platform.set_button_mappings(sayall_windows::sticks3::default_mappings());
    let remotes = platform
        .scan_paired_remotes()
        .expect("paired device enumeration");
    let endpoints = platform.list_audio_endpoints().expect("audio enumeration");
    println!(
        "paired_sticks3={} cable_endpoints={}",
        remotes.len(),
        endpoints
            .iter()
            .filter(|e| e.is_virtual_cable_candidate)
            .count()
    );
    if std::env::args().any(|arg| arg == "--connect") {
        if let Some(remote) = remotes.first() {
            match platform.connect_remote(remote.id.clone()) {
                Ok(_) => {
                    let started = Instant::now();
                    loop {
                        let state = platform.connection_snapshot();
                        if state.phase == ConnectionPhase::Ready {
                            println!("firmware_ack=passed ready=true");
                            break;
                        }
                        if started.elapsed() > Duration::from_secs(20) {
                            println!("firmware_ack=deferred phase={:?}", state.phase);
                            break;
                        }
                        thread::sleep(Duration::from_millis(100));
                    }
                }
                Err(error) => println!("firmware_ack=deferred connection_error={error}"),
            }
        } else {
            println!("firmware_ack=deferred no_paired_sticks3");
        }
    }
    let _ = platform.shutdown_ble_for_exit(Duration::from_secs(8));
}
#[cfg(not(windows))]
fn main() {
    println!("Windows host required");
}
