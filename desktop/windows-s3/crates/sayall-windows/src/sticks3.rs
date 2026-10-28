//! VoiceStick firmware v1 wire protocol. Independent of WinRT and the Xiaomi ATVV path.
use crate::raw_input::RemoteButton;
use crate::send_input::{ButtonAction, ButtonMappings, ButtonTrigger};
use serde::Deserialize;

pub const SERVICE: u128 = 0x8f2f0b846e6f4b2388f73a3ceafc5100;
pub const AUDIO: u128 = SERVICE + 1;
pub const STATE: u128 = SERVICE + 2;
pub const CONTROL: u128 = SERVICE + 3;

pub fn is_device_name(name: &str) -> bool {
    let name = name.trim();
    name.len() == 7
        && name.as_bytes()[..3].eq_ignore_ascii_case(b"VS-")
        && name.as_bytes()[3..].iter().all(u8::is_ascii_hexdigit)
}

#[derive(Debug, Deserialize)]
pub struct StateEvent {
    pub event: String,
    pub level: Option<serde_json::Value>,
    pub hardware: Option<String>,
    pub button: Option<String>,
    pub session_id: Option<u32>,
    pub slot: Option<usize>,
    pub rev: Option<u32>,
    pub button_gestures: Option<u32>,
    pub hid_voice: Option<u32>,
    pub down: Option<u32>,
    pub ok: Option<u32>,
}

pub fn parse_state(bytes: &[u8]) -> Result<StateEvent, String> {
    if bytes.len() < 4 || bytes[..2] != [1, 0x10] {
        return Err("invalid_state_header".into());
    }
    let len = u16::from_le_bytes([bytes[2], bytes[3]]) as usize;
    if bytes.len() != len + 4 {
        return Err("invalid_state_length".into());
    }
    serde_json::from_slice(&bytes[4..]).map_err(|_| "invalid_state_json".into())
}

pub fn battery_level(event: &StateEvent) -> Option<u8> {
    let level = event.level.as_ref()?.as_u64()?;
    (level <= 100).then_some(level as u8)
}

#[derive(Debug, PartialEq, Eq)]
pub struct AudioFrame<'a> {
    pub session: u32,
    pub seq: u32,
    pub end: bool,
    pub payload: &'a [u8],
}

pub fn parse_audio(bytes: &[u8]) -> Result<AudioFrame<'_>, String> {
    if bytes.len() < 16 || bytes[..4] != [1, 1, 16, 0] {
        return Err("invalid_audio_header".into());
    }
    let len = u16::from_le_bytes([bytes[14], bytes[15]]) as usize;
    if bytes.len() != len + 16 || bytes[12] & !3 != 0 {
        return Err("invalid_audio_length_or_flags".into());
    }
    Ok(AudioFrame {
        session: u32::from_le_bytes(bytes[4..8].try_into().unwrap()),
        seq: u32::from_le_bytes(bytes[8..12].try_into().unwrap()),
        end: bytes[12] & 2 != 0,
        payload: &bytes[16..],
    })
}

#[derive(Debug)]
pub struct StreamSequence {
    session: u32,
    next: u32,
    ended: bool,
}
impl StreamSequence {
    pub fn new(session: u32) -> Self {
        Self {
            session,
            next: 0,
            ended: false,
        }
    }
    pub fn accept(&mut self, frame: &AudioFrame<'_>) -> Result<bool, &'static str> {
        if self.ended || frame.session != self.session || frame.seq < self.next {
            return Ok(false);
        }
        if frame.seq != self.next {
            return Err("audio_sequence_gap");
        }
        self.next = self.next.wrapping_add(1);
        self.ended = frame.end;
        Ok(true)
    }
}

// Keep the upstream portable mapping format: OK = physical front key, Menu = side key.
pub fn slot_target(slot: usize) -> Option<(RemoteButton, ButtonTrigger)> {
    let button = match slot / 3 {
        0 => RemoteButton::Ok,
        1 => RemoteButton::Menu,
        _ => return None,
    };
    Some((
        button,
        [
            ButtonTrigger::Single,
            ButtonTrigger::Double,
            ButtonTrigger::Long,
        ][slot % 3],
    ))
}

pub fn button_map(mappings: &ButtonMappings) -> String {
    (0..6)
        .map(|slot| {
            let (button, trigger) = slot_target(slot).unwrap();
            match mappings.action_for(button, trigger) {
                // Preserve a usable microphone when custom shortcut mappings are switched off.
                _ if !mappings.enabled => {
                    if slot == 2 {
                        '1'
                    } else {
                        '0'
                    }
                }
                ButtonAction::VoiceHold if trigger == ButtonTrigger::Long => '1',
                ButtonAction::VoiceHold | ButtonAction::Disabled => '0',
                _ => '3',
            }
        })
        .collect()
}

pub fn default_mappings() -> ButtonMappings {
    use crate::send_input::{ButtonActions, KeyChord, KeyCode};
    let shortcut = |keys| ButtonAction::Shortcut {
        chord: KeyChord { keys },
    };
    let mut mappings = ButtonMappings::default();
    mappings.actions.insert(
        RemoteButton::Ok,
        ButtonActions {
            single: shortcut(vec![KeyCode::Enter]),
            long: ButtonAction::VoiceHold,
            ..Default::default()
        },
    );
    mappings.actions.insert(
        RemoteButton::Menu,
        ButtonActions {
            single: shortcut(vec![KeyCode::Control, KeyCode::Z]),
            double: shortcut(vec![KeyCode::Control, KeyCode::Y]),
            long: shortcut(vec![KeyCode::Control, KeyCode::V]),
            ..Default::default()
        },
    );
    mappings
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn battery_messages_accept_only_integer_percentages() {
        for (value, expected) in [
            (serde_json::json!(0), Some(0)),
            (serde_json::json!(100), Some(100)),
            (serde_json::json!(101), None),
            (serde_json::json!(-1), None),
            (serde_json::json!("42"), None),
            (serde_json::json!(42.5), None),
        ] {
            let event: StateEvent =
                serde_json::from_value(serde_json::json!({"event":"battery", "level":value}))
                    .unwrap();
            assert_eq!(battery_level(&event), expected);
        }
    }
    #[test]
    fn names_do_not_accept_unrelated_devices() {
        for name in ["VS-1234", "vs-abCD"] {
            assert!(is_device_name(name));
        }
        for name in ["VS-", "VS-12345", "VS-G123", "小米蓝牙", "你好abc"] {
            assert!(!is_device_name(name));
        }
    }
    #[test]
    fn frames_reject_truncation_and_preserve_full_session_id() {
        let mut bytes = vec![1, 1, 16, 0, 0x78, 0x56, 0x34, 0x12, 9, 0, 0, 0, 2, 0, 0, 0];
        let frame = parse_audio(&bytes).unwrap();
        assert_eq!(frame.session, 0x12345678);
        assert_eq!(frame.seq, 9);
        assert!(frame.end);
        bytes[14] = 1;
        assert!(parse_audio(&bytes).is_err());
        assert!(parse_state(&[1, 16, 8, 0, b'{']).is_err());
    }
    #[test]
    fn two_buttons_have_exactly_six_slots_and_recording_is_hold_only() {
        assert!(slot_target(5).is_some());
        assert!(slot_target(6).is_none());
        let mut mappings = ButtonMappings::default();
        mappings.actions.clear();
        mappings.actions.entry(RemoteButton::Ok).or_default().long = ButtonAction::VoiceHold;
        mappings
            .actions
            .entry(RemoteButton::Menu)
            .or_default()
            .single = ButtonAction::VoiceHold;
        assert_eq!(button_map(&mappings), "001000");
        mappings.enabled = false;
        assert_eq!(button_map(&mappings), "001000");
    }
    #[test]
    fn old_sessions_duplicates_and_late_audio_cannot_reopen_an_ended_stream() {
        let mut guard = StreamSequence::new(0x12345678);
        let mut frame = AudioFrame {
            session: 1,
            seq: 0,
            end: false,
            payload: &[1],
        };
        assert_eq!(guard.accept(&frame), Ok(false));
        frame.session = 0x12345678;
        assert_eq!(guard.accept(&frame), Ok(true));
        assert_eq!(guard.accept(&frame), Ok(false));
        frame.seq = 1;
        frame.end = true;
        assert_eq!(guard.accept(&frame), Ok(true));
        frame.seq = 2;
        frame.end = false;
        assert_eq!(guard.accept(&frame), Ok(false));
    }
    #[test]
    fn a_missing_first_or_middle_frame_is_an_explicit_error() {
        let mut guard = StreamSequence::new(42);
        let mut frame = AudioFrame {
            session: 42,
            seq: 1,
            end: false,
            payload: &[],
        };
        assert_eq!(guard.accept(&frame), Err("audio_sequence_gap"));
        frame.seq = 0;
        assert_eq!(guard.accept(&frame), Ok(true));
        frame.seq = 2;
        assert_eq!(guard.accept(&frame), Err("audio_sequence_gap"));
    }
}
