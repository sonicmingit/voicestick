//! Stable per-tool defaults shared by selection and startup restoration.
use sayall_windows::{
    send_input::{KeyChord, KeyCode},
    volcengine::VoiceMode,
};
pub fn resolve_chord(
    mode: VoiceMode,
    saved: Option<KeyChord>,
    other: Option<Vec<KeyCode>>,
) -> Option<KeyChord> {
    match mode {
        VoiceMode::Volcengine => None,
        VoiceMode::Wechat => Some(KeyChord {
            keys: vec![KeyCode::LeftControl, KeyCode::LeftWindows],
        }),
        VoiceMode::Other => match other {
            Some(keys) if keys.is_empty() => None,
            Some(keys) => Some(KeyChord { keys }),
            None => Some(KeyChord {
                keys: vec![KeyCode::RightAlt],
            }),
        },
        VoiceMode::Doubao => saved
            .filter(|chord| {
                matches!(
                    chord.keys.as_slice(),
                    [KeyCode::RightAlt] | [KeyCode::LeftControl, KeyCode::LeftWindows]
                )
            })
            .or_else(|| {
                Some(KeyChord {
                    keys: vec![KeyCode::RightAlt],
                })
            }),
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn selection_and_restart_use_the_same_tool_defaults() {
        let old = Some(KeyChord {
            keys: vec![KeyCode::LeftAlt],
        });
        assert_eq!(
            resolve_chord(VoiceMode::Volcengine, old.clone(), None),
            None
        );
        assert_eq!(
            resolve_chord(VoiceMode::Wechat, old.clone(), Some(vec![]))
                .unwrap()
                .keys,
            vec![KeyCode::LeftControl, KeyCode::LeftWindows]
        );
        assert_eq!(
            resolve_chord(VoiceMode::Doubao, old, None).unwrap().keys,
            vec![KeyCode::RightAlt]
        );
    }
    #[test]
    fn other_keeps_the_explicit_no_key_choice() {
        assert_eq!(resolve_chord(VoiceMode::Other, None, Some(vec![])), None);
        assert_eq!(
            resolve_chord(VoiceMode::Other, None, Some(vec![KeyCode::LeftAlt]))
                .unwrap()
                .keys,
            vec![KeyCode::LeftAlt]
        );
    }
}
