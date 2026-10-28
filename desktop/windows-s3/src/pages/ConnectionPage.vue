<script setup lang="ts">
import { computed, onMounted, onUnmounted, ref, watch } from "vue";
import ImeSettingsPanel from "../components/ImeSettingsPanel.vue";
import BatteryIndicator from "../components/BatteryIndicator.vue";
import { t, tPage } from "../lib/i18n";
import { connectRemote, disconnectRemote, getConnectionSnapshot, scanPairedRemotes, remoteModelLabel,
  connectionPhaseLabel, getVolcengineConfig, saveVolcengineConfig, testVolcengineConnection, getVolcengineStatus,
  saveSpeechPreferences, type VoiceMode, type SpeechOutputMode, type OverlayPosition, type ConnectionSnapshot, type RuntimeSnapshot, type PairedRemote, type VolcengineConfig, type VolcengineStatus } from "../lib/bridge";
const props = defineProps<{ runtime: RuntimeSnapshot | null }>();
const connection = ref<ConnectionSnapshot | null>(null);
const devices = ref<PairedRemote[]>([]);
const scanning = ref(false);
const connecting = ref("");
const disconnecting = ref(false);
const message = ref("");
const config = ref<VolcengineConfig | null>(null);
const mode = ref<VoiceMode>("volcengine");
const outputMode = ref<SpeechOutputMode>("focused_app");
const floatingWindow = ref(true);
const overlayPosition = ref<OverlayPosition>("center");
const savingPreferences = ref(false);
const tools: Array<{ id: VoiceMode; name: string; note: string }> = [
  { id: "volcengine", name: "火山豆包 API", note: "直接识别，实时浮窗" },
  { id: "doubao", name: "豆包输入法", note: "使用输入法语音输入" },
  { id: "wechat", name: "微信输入法", note: "使用输入法语音输入" },
  { id: "other", name: "其他工具", note: "自己指定按键" },
];
const modeName = computed(() => tools.find(tool => tool.id === mode.value)?.name ?? "火山豆包 API");
const imeTool = computed(() => mode.value === "volcengine" ? "other" : mode.value);
async function preferences(nextMode = mode.value) {
  const previousMode = mode.value;
  savingPreferences.value = true; configError.value = false;
  try {
    const value = await saveSpeechPreferences(nextMode, outputMode.value, floatingWindow.value, overlayPosition.value);
    config.value = value; mode.value = value.voiceMode; outputMode.value = value.outputMode; floatingWindow.value = value.floatingWindow; overlayPosition.value = value.overlayPosition; if (value.voiceMode !== previousMode) apiKey.value = "";
  } catch (error) { configMessage.value = errorText(error); configError.value = true;
    if (config.value) { outputMode.value = config.value.outputMode; floatingWindow.value = config.value.floatingWindow; overlayPosition.value = config.value.overlayPosition; }
  } finally { savingPreferences.value = false; }
}
const apiKey = ref("");
const resourceId = ref("volc.seedasr.sauc.duration");
const hotwords = ref("");
const saving = ref(false);
const testing = ref(false);
const configMessage = ref("");
const configError = ref(false);
const copied = ref(false);
const asr = ref<VolcengineStatus>({ generation: 0, phase: "ready", text: "", lastError: null });
let timer: ReturnType<typeof setInterval> | undefined;
let disposed = false;
let polling = false;
watch(() => props.runtime?.platform.connection, snapshot => { if (snapshot) connection.value = snapshot; }, { immediate: true });
const connectionActive = computed(() => connection.value && ["connecting", "discovering", "awaiting_capabilities", "ready", "streaming", "draining", "reconnecting", "suspended"].includes(connection.value.phase));
const ready = computed(() => connection.value && ["ready", "streaming", "draining"].includes(connection.value.phase));
const phaseTone = computed(() => ready.value ? "success" : connection.value?.phase === "failed" ? "error" : "pending");
const asrLabel = computed(() => {
  switch (asr.value.phase) {
    case "connecting": return t("正在连接语音服务…");
    case "listening": return t("正在听写…");
    case "recognizing": return t("正在识别…");
    case "completed": return t("文字已发送到输入框");
    case "copied": return t("已复制到剪贴板");
    case "copied_and_inserted": return t("已复制并输入到当前窗口");
    case "copy_required": return t("识别完成，请复制文字");
    case "empty": return t("没有识别到语音");
    case "failed": return t("语音识别失败");
    case "cancelled": return t("已取消");
    default: return t("等待按住说话");
  }
});
function errorText(error: unknown): string { return typeof error === "string" ? error : t("操作没有完成，请稍后重试。"); }
async function scan() {
  scanning.value = true; message.value = "";
  try { devices.value = await scanPairedRemotes(); if (!devices.value.length) message.value = t("没有找到设备，请确认 StickS3 已开机并在 Windows 中配对。"); }
  catch (error) { message.value = errorText(error); }
  finally { scanning.value = false; }
}
async function connect(device: PairedRemote) {
  connecting.value = device.id; message.value = "";
  try { connection.value = await connectRemote(device.id); }
  catch (error) { message.value = errorText(error); }
  finally { connecting.value = ""; }
}
async function disconnect() {
  disconnecting.value = true;
  try { connection.value = await disconnectRemote(); }
  catch (error) { message.value = errorText(error); }
  finally { disconnecting.value = false; }
}
async function loadConfig() {
  try { const value = await getVolcengineConfig(); if (disposed) return;
    config.value = value; resourceId.value = value.resourceId; hotwords.value = value.hotwords.join("\n"); mode.value = value.voiceMode ?? "volcengine"; outputMode.value = value.outputMode ?? "focused_app"; floatingWindow.value = value.floatingWindow ?? true; overlayPosition.value = value.overlayPosition ?? "center"; }
  catch (error) { configMessage.value = errorText(error); configError.value = true; }
}
async function save() {
  saving.value = true; configMessage.value = ""; configError.value = false;
  try {
    config.value = await saveVolcengineConfig(apiKey.value.trim() || null, resourceId.value,
      hotwords.value.split(/[,，\n]/).map(word => word.trim()).filter(Boolean));
    apiKey.value = ""; configMessage.value = t("API 配置已保存");
  } catch (error) { configMessage.value = errorText(error); configError.value = true; }
  finally { saving.value = false; }
}
async function testConnection() {
  testing.value = true; configMessage.value = ""; configError.value = false;
  try { await testVolcengineConnection(); configMessage.value = t("API 连接正常，已收到识别服务响应。"); }
  catch (error) { configMessage.value = errorText(error); configError.value = true; }
  finally { testing.value = false; }
}
async function poll() {
  if (polling || disposed) return; polling = true;
  try { const value = await getVolcengineStatus(); if (!disposed) asr.value = value; }
  catch { /* Configuration commands surface their own errors; avoid repeating poll failures. */ }
  finally { polling = false; }
}
async function copyResult() {
  try { await navigator.clipboard.writeText(asr.value.text); copied.value = true; }
  catch { message.value = t("复制失败，请手动选择文字复制。"); }
}
watch(() => asr.value.text, () => { copied.value = false; });
onMounted(async () => {
  await Promise.all([loadConfig(), poll(), getConnectionSnapshot().then(value => { if (!disposed) connection.value = value; }).catch(() => undefined)]);
  if (disposed) return; timer = setInterval(poll, 350);
  if (!connectionActive.value) void scan();
});
onUnmounted(() => { disposed = true; if (timer) clearInterval(timer); apiKey.value = ""; });
</script>

<template>
  <section>
    <header class="page-header">
      <h1>{{ tPage("连接") }}</h1>
      <span class="badge" :class="phaseTone">{{ connectionPhaseLabel(connection?.phase ?? "idle") }}</span>
    </header>
    <div class="device-row api-device-row">
      <article class="card">
        <div class="card-title-row">
          <h2>{{ t("遥控器连接") }}</h2>
          <button class="primary-button" :disabled="scanning || Boolean(connectionActive)" @click="scan">{{ scanning ? t("扫描中…") : t("扫描已配对设备") }}</button>
        </div>
        <div class="status-panel" aria-live="polite">
          <div class="status-copy">
            <div class="status-heading"><span class="status-dot" :class="phaseTone"></span><strong>{{ connection?.remoteName ? remoteModelLabel(connection.remoteModel) : "M5Stack StickS3" }}</strong></div>
            <small>{{ connectionPhaseLabel(connection?.phase ?? "idle") }}</small>
          </div>
          <button v-if="connectionActive" class="secondary-button status-action" :disabled="disconnecting" @click="disconnect">{{ disconnecting ? t("断开中…") : t("断开") }}</button>
        </div>
        <div v-if="ready && connection" class="chip-row">
          <span class="device-chip"><BatteryIndicator :connection="connection" /></span>
          <span class="device-chip"><span class="status-dot success"></span>{{ t("语音按键已就绪") }}</span>
        </div>
        <p v-if="connection?.lastError" class="error-text" role="alert">{{ connection.lastError }}</p>
        <p v-if="message" class="operation-message" role="status">{{ message }}</p>
        <ul v-if="!connectionActive && devices.length" class="device-list">
          <li v-for="device in devices" :key="device.id"><div><strong>{{ device.name }}</strong><small>{{ remoteModelLabel(device.model) }}</small></div>
            <button :disabled="Boolean(connecting)" @click="connect(device)">{{ connecting === device.id ? t("连接中…") : t("连接") }}</button></li>
        </ul>
      </article>
      <article class="card">
        <div class="card-title-row"><h2>{{ t("语音识别") }}</h2><span class="pill" :class="config?.keyConfigured ? 'ok' : 'warn'">{{ config?.keyConfigured ? t("已配置") : t("待配置") }}</span></div>
        <div class="status-panel"><div class="status-copy"><strong>{{ t(modeName) }}</strong><small>{{ mode === "volcengine" ? asrLabel : t("等待按住说话") }}</small></div></div>
        <p class="muted api-help">{{ t("按住设备的语音按键说话，松开后自动识别并写入当前输入框。") }}</p>
        <p class="tiny muted">{{ t("首次使用会自动导入原 VoiceStick 的火山 API 配置。") }}</p>
      </article>
    </div>
    <article class="card setup-card tool-selector"><div class="card-title-row"><h2>{{ t("语音输入工具") }}</h2></div>
      <div class="tool-cards"><button v-for="tool in tools" :key="tool.id" class="tool-card" :class="{ selected: mode === tool.id }" :aria-pressed="mode === tool.id" :disabled="savingPreferences" @click="preferences(tool.id)"><span class="tool-radio"></span><div><strong>{{ t(tool.name) }}</strong><small>{{ t(tool.note) }}</small></div></button></div>
    </article>
    <ImeSettingsPanel v-if="mode !== 'volcengine'" :key="mode" :tool="imeTool" />
    <article v-if="mode === 'volcengine'" class="card setup-card api-settings">
      <div class="card-title-row"><div><h2>{{ t("火山豆包 API") }}</h2><p class="muted">{{ t("使用火山引擎流式语音识别，音频会发送到你配置的火山账户。") }}</p></div></div>
      <div class="speech-options">
        <label for="speech-output">{{ t("文字输出方式") }}</label>
        <select id="speech-output" v-model="outputMode" :disabled="savingPreferences" @change="preferences()"><option value="focused_app">{{ t("自动输入到当前输入框") }}</option><option value="clipboard_and_input">{{ t("复制到剪贴板并输入到当前窗口") }}</option></select>
        <label for="overlay-position">{{ t("浮窗位置") }}</label><select id="overlay-position" v-model="overlayPosition" :disabled="savingPreferences || !floatingWindow" @change="preferences()"><option value="center">{{ t("居中") }}</option><option value="top_left">{{ t("左上") }}</option><option value="top_right">{{ t("右上") }}</option><option value="bottom_left">{{ t("左下") }}</option><option value="bottom_right">{{ t("右下") }}</option></select>
        <label class="overlay-toggle"><input v-model="floatingWindow" type="checkbox" :disabled="savingPreferences" @change="preferences()" />{{ t("显示实时语音浮窗") }}</label>
      </div>
      <form class="api-form" @submit.prevent="save">
        <label for="volcengine-key">API Key</label>
        <input id="volcengine-key" v-model="apiKey" type="password" autocomplete="off" spellcheck="false" :placeholder="config?.keyConfigured ? t('已保存，留空保留原 Key') : t('填写火山引擎 API Key')" />
        <label for="volcengine-resource">{{ t("识别模型") }}</label>
        <select id="volcengine-resource" v-model="resourceId">
          <option value="volc.seedasr.sauc.duration">{{ t("豆包 2.0 · 小时版") }}</option>
          <option value="volc.seedasr.sauc.concurrent">{{ t("豆包 2.0 · 并发版") }}</option>
          <option value="volc.bigasr.sauc.duration">{{ t("豆包 1.0 · 小时版") }}</option>
          <option value="volc.bigasr.sauc.concurrent">{{ t("豆包 1.0 · 并发版") }}</option>
        </select>
        <label for="volcengine-hotwords">{{ t("热词（可选）") }}</label>
        <textarea id="volcengine-hotwords" v-model="hotwords" rows="3" :placeholder="t('每行一个词，或用逗号分隔')"></textarea>
        <p class="tiny muted api-field-note">{{ t("API Key 仅保存在本机，使用 Windows 账户加密，不会显示在日志中。") }}</p>
        <div class="button-row api-field-note">
          <button class="primary-button" type="submit" :disabled="saving || testing || !config">{{ saving ? t("保存中…") : t("保存配置") }}</button>
          <button class="secondary-button" type="button" :disabled="testing || saving || !config?.keyConfigured || Boolean(apiKey)" @click="testConnection">{{ testing ? t("测试中…") : t("测试 API 连接") }}</button>
        </div>
      </form>
      <p v-if="configMessage" :class="configError ? 'error-text' : 'operation-message'" role="status">{{ configMessage }}</p>
    </article>
    <article v-if="mode === 'volcengine'" class="card api-result" aria-live="polite">
      <div class="card-title-row"><h2>{{ t("识别结果") }}</h2><button v-if="asr.text" class="secondary-button" @click="copyResult">{{ copied ? t("已复制") : t("复制文字") }}</button></div>
      <p class="muted">{{ asrLabel }}</p>
      <p v-if="asr.lastError" class="error-text">{{ asr.lastError }}</p>
      <p v-if="asr.text" class="api-transcript">{{ asr.text }}</p>
    </article>
  </section>
</template>

<style scoped>
.api-device-row { grid-template-columns: minmax(0, 1fr) minmax(0, 1fr); }
.speech-options{display:flex;gap:16px;align-items:center;flex-wrap:wrap;margin:22px 0}.speech-options select{padding:10px;border-radius:10px;border:1px solid var(--border);background:var(--surface-control);color:var(--text-primary);font:inherit}.overlay-toggle{display:flex;gap:8px;align-items:center}.tool-cards{display:grid;grid-template-columns:repeat(4,1fr);gap:12px}.tool-card{display:flex;text-align:left;gap:10px;padding:16px}.tool-card small{display:block;margin-top:5px}.tool-selector{margin-bottom:20px}
.api-help { line-height: 1.7; }
.api-form { display: grid; grid-template-columns: 140px minmax(0, 1fr); gap: 16px 24px; align-items: center; margin-top: 24px; max-width: 760px; }
.api-form input, .api-form select, .api-form textarea { width: 100%; box-sizing: border-box; border: 1px solid var(--border); border-radius: 12px; padding: 11px 14px; background: var(--surface-control); color: var(--text-primary); font: inherit; }
.api-form textarea { resize: vertical; }
.api-form label { font-weight: 600; }
.api-field-note { grid-column: 2; margin: 0; }
.api-result { margin-top: 20px; }
.api-transcript { white-space: pre-wrap; user-select: text; line-height: 1.8; }
@media (max-width: 850px) { .api-device-row { grid-template-columns: 1fr; } }
@media (max-width: 600px) { .api-form { grid-template-columns: 1fr; gap: 10px; } .api-field-note { grid-column: 1; } }
</style>
