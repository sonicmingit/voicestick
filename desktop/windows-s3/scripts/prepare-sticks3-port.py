"""One-time, reviewable customization of the public SayAll checkout for StickS3."""
from pathlib import Path
import json
import re
import shutil

ROOT = Path(__file__).resolve().parents[1]

def edit(name, old, new, count=-1):
    path = ROOT / name
    value = path.read_text(encoding="utf-8")
    if old not in value:
        raise RuntimeError(f"Missing edit anchor in {name}: {old[:70]}")
    path.write_text(value.replace(old, new, count), encoding="utf-8")

# The modified app has an independent identity, settings, log, history and IPC event.
path = ROOT / "src-tauri/tauri.conf.json"
config = json.loads(path.read_text(encoding="utf-8"))
config.update(productName="VoiceStick S3", version="0.1.0", identifier="me.voicestick.sayall.sticks3")
config["app"]["windows"][0]["title"] = "VoiceStick S3 · 实验版"
config["bundle"].update(publisher="VoiceStick", createUpdaterArtifacts=False, resources=[],
    shortDescription="StickS3 双按键语音输入实验版", longDescription="基于 SayAll 公开源码适配 M5Stack StickS3。")
config["bundle"]["windows"]["nsis"].update(startMenuFolder="VoiceStick S3", installerHooks=None)
config["build"]["beforeBundleCommand"] = ""
config["plugins"].pop("updater", None)
path.write_text(json.dumps(config, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
for folder in ["src-tauri/src", "crates/sayall-windows/src"]:
    for path in (ROOT / folder).rglob("*.rs"):
        text = path.read_text(encoding="utf-8")
        changed = text.replace('.join("SayAll")', '.join("VoiceStickS3")')
        changed = changed.replace(r'Local\SayAll-', r'Local\VoiceStickS3-')
        if changed != text: path.write_text(changed, encoding="utf-8")

edit("src-tauri/src/updater.rs", "    let started = Instant::now();\n    let include_prereleases = state",
     '    return Err("StickS3 实验版请使用本地构建更新".to_owned());\n    #[allow(unreachable_code)]\n    let started = Instant::now();\n    let include_prereleases = state', 1)
edit("src-tauri/src/updater.rs", "pub async fn install_app_update(app: AppHandle, state: State<'_, AppState>) -> Result<(), String> {",
     'pub async fn install_app_update(app: AppHandle, state: State<\'_, AppState>) -> Result<(), String> {\n    return Err("StickS3 实验版请使用本地构建更新".to_owned());\n    #[allow(unreachable_code)]', 1)

# Preserve upstream mapping IDs for portable configuration compatibility.
edit("src/lib/bridge.ts", '"rc001" | "rc003" | "unknown"', '"rc001" | "rc003" | "stick_s3" | "unknown"')
edit("src/lib/bridge.ts", '| { type: "disabled" }', '| { type: "disabled" }\n  | { type: "voice_hold" }', 1)
edit("src/lib/bridge.ts", '      rc001: "小米蓝牙语音遥控器 2",', '      stick_s3: "M5Stack StickS3",\n      rc001: "小米蓝牙语音遥控器 2",')
edit("src/lib/bridge.ts", '  if (!action || action.type === "disabled") return t("未设置");',
     '  if (!action || action.type === "disabled") return t("未设置");\n  if (action.type === "voice_hold") return t("按住说话");')
edit("src/lib/bridge.ts", 'return t("确定");', 'return t("正面按键");')
edit("src/lib/bridge.ts", 'return t("菜单");', 'return t("侧面按键");')

path = ROOT / "src/pages/ButtonsPage.vue"
text = path.read_text(encoding="utf-8")
text = re.sub(r'const PLACEMENTS: Placement\[\] = \[.*?\n\];', '''const PLACEMENTS: Placement[] = [
  { button: "ok", side: "left", anchor: [0.49, 0.69], targetY: 0.43 },
  { button: "menu", side: "right", anchor: [0.94, 0.28], targetY: 0.43 },
];''', text, count=1, flags=re.S)
text = text.replace('src="/RC003-remote-photo@2x.png"', 'src="/sticks3.png"')
text = text.replace(":alt=\"t('小米蓝牙语音遥控器 2 Pro 示意图')\"", ':alt="t(\'M5Stack StickS3\')"')
# There are precisely two physical buttons, so remove the separate Xiaomi voice-key card and its wires.
text = re.sub(r'      <article\s+class="mapping-card voice-card right".*?</article>', '', text, count=1, flags=re.S)
text = re.sub(r'        <path\s+:d="connectionPath\(VOICE_PLACEMENT\)".*?/>', '', text, count=1, flags=re.S)
text = re.sub(r'        <polygon :points="arrowPolygon\(VOICE_PLACEMENT\)".*?/>', '', text, count=1)
text = re.sub(r'        <span\s+class="[^"\n]*voice[^"\n]*".*?</span>', '', text, count=1, flags=re.S) if False else text
text = text.replace('<label\n            class="toggle-row"\n            :title="captureSwitchTitle"', '<label\n            v-if="false"\n            class="toggle-row"\n            :title="captureSwitchTitle"')
text = text.replace('<div class="action-sections">', '''<div class="action-sections">
        <section v-if="editingTarget.trigger === 'long'" class="action-section">
          <h4 class="action-section-title">{{ t("语音输入") }}</h4>
          <button class="chip" type="button" @click="applyAction({ type: 'voice_hold' })">{{ t("按住说话") }}</button>
        </section>''', 1)
path.write_text(text, encoding="utf-8")

path = ROOT / "src/pages/ConnectionPage.vue"
text = path.read_text(encoding="utf-8")
text = text.replace('note: "要开启“支持更多输入工具”"', 'note: "通过 StickS3 键盘通道启动"')
text = text.replace('for="capture-switch-doubao"', 'v-if="false" for="capture-switch-doubao"')
text = text.replace('for="capture-switch-other"', 'v-if="false" for="capture-switch-other"')
text = text.replace('“支持更多输入工具”和“按键”页的“全按键支持”是同一个开关，两处随时同步；每次开启都会弹出一次系统授权。',
    'StickS3 使用设备自身的语音快捷键通道，无需开启小米遥控器的全按键支持。')
text = text.replace('已配对的遥控器', '已配对的 StickS3')
path.write_text(text, encoding="utf-8")

# Required fork attribution is visible; private membership modules remain upstream stubs.
for name in ["src/components/Sidebar.vue", "src/pages/SettingsPage.vue", "index.html"]:
    path = ROOT / name
    text = path.read_text(encoding="utf-8").replace("无线麦 SayAll", "VoiceStick S3").replace('<title>SayAll</title>', '<title>VoiceStick S3</title>')
    path.write_text(text, encoding="utf-8")
for name in ["src-tauri/src/lib.rs", "src-tauri/src/diagnostics.rs"]:
    path = ROOT / name
    text = path.read_text(encoding="utf-8").replace('"无线麦 SayAll"', '"VoiceStick S3"').replace('"SayAll"', '"VoiceStick S3"')
    path.write_text(text, encoding="utf-8")

shutil.copyfile(ROOT.parent / "docs/sticks3.png", ROOT / "public/sticks3.png")
print("StickS3 UI, isolated identity, local-only updater and device photo prepared.")
