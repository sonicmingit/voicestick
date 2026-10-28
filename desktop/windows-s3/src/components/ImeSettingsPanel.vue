<script setup lang="ts">
import { computed, onMounted, onUnmounted, ref } from "vue";
import { t } from "../lib/i18n";
import { getAudioSnapshot, listAudioEndpoints, selectAudioEndpoint, getGainDb, setGainDb, getVoiceHoldHotkey, setVoiceHoldHotkey, setOtherVoiceHotkey, openVbCableDownloadPage,
  type AudioEndpoint, type AudioSnapshot, type VoiceInputTool, type KeyCode } from "../lib/bridge";
const props = defineProps<{ tool: VoiceInputTool }>();
const endpoints = ref<AudioEndpoint[]>([]);
const audio = ref<AudioSnapshot | null>(null);
const chordId = ref("left_ctrl_win");
const busy = ref(false);
const message = ref("");
const gain = ref(0);
let disposed = false;
onUnmounted(() => { disposed = true; });
const name = computed(() => props.tool === "doubao" ? t("豆包输入法") : props.tool === "wechat" ? t("微信输入法") : t("其他工具"));
const allChoices: Array<{ id: string; label: string; keys: KeyCode[] }> = [
  { id: "left_ctrl_win", label: "左 Ctrl + 左 Win", keys: ["left_control", "left_windows"] },
  { id: "right_alt", label: "右 Alt", keys: ["right_alt"] },
  { id: "left_alt", label: "左 Alt", keys: ["left_alt"] },
  { id: "none", label: "不按键", keys: [] },
];
const choices = computed(() => allChoices.filter(choice => props.tool === "wechat" ? choice.id === "left_ctrl_win" : props.tool === "doubao" ? ["left_ctrl_win", "right_alt"].includes(choice.id) : true));
async function refresh() {
  busy.value = true; message.value = "";
  try {
    const [list, snapshot, db, chord] = await Promise.all([listAudioEndpoints(), getAudioSnapshot(), getGainDb(), getVoiceHoldHotkey()]);
    if (disposed) return; endpoints.value = list; audio.value = snapshot; gain.value = db;
    chordId.value = choices.value.find(choice => JSON.stringify(choice.keys) === JSON.stringify(chord?.keys ?? []))?.id ?? "left_ctrl_win";
  } catch (error) { message.value = String(error); }
  finally { busy.value = false; }
}
async function chooseEndpoint(event: Event) {
  const id = (event.target as HTMLSelectElement).value; if (!id) return;
  busy.value = true;
  try { audio.value = await selectAudioEndpoint(id); }
  catch (error) { message.value = String(error); }
  finally { busy.value = false; }
}
async function chooseChord() {
  busy.value = true;
  try { const keys = choices.value.find(choice => choice.id === chordId.value)!.keys; await setVoiceHoldHotkey(keys.length ? { keys } : null); if (props.tool === "other") await setOtherVoiceHotkey(keys); }
  catch (error) { message.value = String(error); }
  finally { busy.value = false; }
}
async function saveGain() { try { gain.value = await setGainDb(gain.value); } catch (error) { message.value = String(error); } }
onMounted(refresh);
</script>
<template>
  <article class="card setup-card">
    <div class="card-title-row"><h2>{{ name }}</h2><button class="secondary-button" :disabled="busy" @click="refresh">{{ t("刷新设备列表") }}</button></div>
    <p class="muted">{{ t("输入法模式通过虚拟声卡传送音频，请在对应工具中选择 CABLE Output 麦克风。") }}</p>
    <div class="ime-fields">
      <label for="ime-endpoint">{{ t("语音设备") }}</label>
      <select id="ime-endpoint" :value="audio?.selectedEndpointId ?? ''" :disabled="busy" @change="chooseEndpoint"><option value="">{{ t("选择设备") }}</option><option v-for="endpoint in endpoints" :key="endpoint.id" :value="endpoint.id">{{ endpoint.name }}{{ endpoint.isVirtualCableCandidate ? ' · ' + t('推荐') : '' }}</option></select>
      <label for="ime-hotkey">{{ t("按住说话快捷键") }}</label>
      <select id="ime-hotkey" v-model="chordId" :disabled="busy" @change="chooseChord"><option v-for="choice in choices" :key="choice.id" :value="choice.id">{{ t(choice.label) }}</option></select>
      <label for="ime-gain">{{ t("增益") }} · {{ gain }} dB</label><input id="ime-gain" v-model.number="gain" type="range" min="0" max="24" @change="saveGain" />
    </div>
    <p class="tiny muted">{{ t("请把输入法或工具的语音快捷键设置为与这里一致。") }}</p>
    <p v-if="!endpoints.some(endpoint => endpoint.isVirtualCableCandidate)"><button class="link-button" @click="openVbCableDownloadPage">{{ t("打开 VB-CABLE 下载页") }}</button></p>
    <p v-if="message" class="error-text" role="alert">{{ message }}</p>
  </article>
</template>
<style scoped>.ime-fields{display:grid;grid-template-columns:160px 1fr;gap:18px;align-items:center;margin:24px 0}.ime-fields select{padding:12px;border-radius:12px;background:var(--surface-control);color:var(--text-primary);border:1px solid var(--border);font:inherit}</style>
