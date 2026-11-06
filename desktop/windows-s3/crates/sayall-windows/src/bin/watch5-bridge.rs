#[cfg(windows)]
fn main() {
    if let Err(error) = sayall_windows::watch5::run() {
        eprintln!("{error}");
        std::process::exit(1);
    }
}
#[cfg(not(windows))]
fn main() {}
