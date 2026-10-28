//! Public Volcengine V3 ASR framing. No credentials, audio or text are logged.
use flate2::{read::GzDecoder, write::GzEncoder, Compression};
use serde_json::{json, Value};
use std::io::{Read, Write};

pub const ENDPOINT: &str = "wss://openspeech.bytedance.com/api/v3/sauc/bigmodel_async";
pub const RESOURCES: [&str; 4] = [
    "volc.seedasr.sauc.duration",
    "volc.seedasr.sauc.concurrent",
    "volc.bigasr.sauc.duration",
    "volc.bigasr.sauc.concurrent",
];
pub fn request(hotwords: &[String]) -> Vec<u8> {
    let mut value = json!({"user":{"uid":"voicestick-s3"},
        "audio":{"format":"pcm","codec":"raw","rate":16000,"bits":16,"channel":1},
        "request":{"model_name":"bigmodel","enable_nonstream":true,
            "enable_itn":true,"enable_punc":true,"enable_ddc":true,"result_type":"full"}});
    if !hotwords.is_empty() {
        value["request"]["corpus"] = json!({"context": json!({"hotwords": hotwords.iter()
            .map(|word| json!({"word":word})).collect::<Vec<_>>()}).to_string()});
    }
    frame(0x10, 0x11, &serde_json::to_vec(&value).unwrap())
}
pub fn audio(pcm: &[i16], last: bool) -> Vec<u8> {
    let bytes: Vec<u8> = pcm.iter().flat_map(|sample| sample.to_le_bytes()).collect();
    frame(if last { 0x22 } else { 0x20 }, 0x01, &bytes)
}
fn frame(kind: u8, encoding: u8, data: &[u8]) -> Vec<u8> {
    let mut gzip = GzEncoder::new(Vec::new(), Compression::fast());
    gzip.write_all(data).unwrap();
    let compressed = gzip.finish().unwrap();
    let mut result = vec![0x11, kind, encoding, 0];
    result.extend_from_slice(&(compressed.len() as u32).to_be_bytes());
    result.extend_from_slice(&compressed);
    result
}
#[derive(Debug, PartialEq)]
pub struct Response {
    pub text: String,
    pub last: bool,
}
pub fn parse(data: &[u8]) -> Result<Response, String> {
    let invalid = || "语音服务返回了无效数据".to_owned();
    if data.len() < 8 || data[0] >> 4 != 1 {
        return Err(invalid());
    }
    let kind = data[1] >> 4;
    let flags = data[1] & 15;
    let mut offset = (data[0] & 15) as usize * 4;
    if offset < 4 {
        return Err(invalid());
    }
    let mut read_u32 = || {
        let value = data.get(offset..offset + 4).ok_or_else(invalid)?;
        offset += 4;
        Ok::<_, String>(u32::from_be_bytes(value.try_into().unwrap()))
    };
    let code = if kind == 15 {
        Some(read_u32()?)
    } else if flags & 1 != 0 {
        let _ = read_u32()?;
        None
    } else {
        None
    };
    let length = read_u32()? as usize;
    let body = data
        .get(offset..offset.checked_add(length).ok_or_else(invalid)?)
        .ok_or_else(invalid)?;
    if let Some(code) = code {
        return Err(format!(
            "语音服务错误（{code}），请检查 API Key、资源和额度"
        ));
    }
    if kind != 9 {
        return Err(invalid());
    }
    let body = match data[2] & 15 {
        0 => body.to_vec(),
        1 => {
            let mut output = Vec::new();
            GzDecoder::new(body)
                .take(2_000_001)
                .read_to_end(&mut output)
                .map_err(|_| invalid())?;
            if output.len() > 2_000_000 {
                return Err(invalid());
            }
            output
        }
        _ => return Err(invalid()),
    };
    let value: Value = serde_json::from_slice(&body).map_err(|_| invalid())?;
    if let Some(code) = value["code"].as_i64() {
        if code != 0 && code != 1000 && code != 20000000 {
            return Err(format!("语音服务错误（{code}），请检查资源和额度"));
        }
    }
    let text = value
        .pointer("/result/text")
        .and_then(Value::as_str)
        .or_else(|| value.pointer("/result/0/text").and_then(Value::as_str))
        .unwrap_or("")
        .to_owned();
    Ok(Response {
        text,
        last: flags & 2 != 0,
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn request_is_pcm_and_gzip() {
        let bytes = request(&["测试词".into()]);
        assert_eq!(&bytes[..4], &[0x11, 0x10, 0x11, 0]);
        let mut body = String::new();
        GzDecoder::new(&bytes[8..])
            .read_to_string(&mut body)
            .unwrap();
        let value: Value = serde_json::from_str(&body).unwrap();
        assert_eq!(value["audio"]["format"], "pcm");
        assert!(value["request"]["corpus"]["context"]
            .as_str()
            .unwrap()
            .contains("测试词"));
    }
    #[test]
    fn audio_end_and_endianness() {
        let bytes = audio(&[0x1234, -1], true);
        assert_eq!(bytes[1], 0x22);
        let mut pcm = vec![];
        GzDecoder::new(&bytes[8..]).read_to_end(&mut pcm).unwrap();
        assert_eq!(pcm, vec![0x34, 0x12, 255, 255]);
    }
    #[test]
    fn parses_final_and_rejects_truncated_or_server_errors() {
        let mut bytes = frame(0x93, 0x11, br#"{"result":{"text":"hello"}}"#);
        bytes.splice(4..4, (-1i32).to_be_bytes());
        assert_eq!(
            parse(&bytes).unwrap(),
            Response {
                text: "hello".into(),
                last: true
            }
        );
        for len in 0..bytes.len() {
            assert!(parse(&bytes[..len]).is_err());
        }
        assert!(parse(&[0x11, 0xf0, 0, 0, 0, 0, 1, 147, 0, 0, 0, 0])
            .unwrap_err()
            .contains("403"));
    }
}
