<script setup lang="ts">
import { invoke } from "@tauri-apps/api/core";
import { onMounted, onUnmounted, ref } from "vue";

type Status = { enabled: boolean; online: boolean; serial: string; keys: [number, number] | null; pending: boolean; ble_enabled: boolean; ble_online: boolean };
const state = ref<Status>({ enabled: false, online: false, serial: "", keys: null, pending: false, ble_enabled: false, ble_online: false });
const actions = ["回车", "撤销", "粘贴"];
const address = ref("");
const error = ref("");
const busy = ref(false);
let timer: ReturnType<typeof setInterval>;
let generation = 0;
async function refresh() {
  const request = generation;
  if (busy.value) return;
  try {
    const snapshot = await invoke<Status>("watch_status");
    if (request === generation) state.value = snapshot;
  } catch { /* The plain browser preview has no native receiver. */ }
}
async function change(disconnect: boolean) {
  generation++;
  busy.value = true;
  error.value = "";
  try {
    state.value = disconnect ? await invoke<Status>("disconnect_watch")
      : await invoke<Status>("connect_watch", { address: address.value.trim() });
  } catch (e) { error.value = String(e); }
  finally { busy.value = false; }
}
async function toggleBle() {
  generation++; busy.value=true; error.value="";
  try { state.value=await invoke<Status>("watch_ble_mode",{enable:!state.value.ble_enabled}); }
  catch(e){ error.value=String(e); }
  finally{ busy.value=false; }
}
async function setKey(slot: number, event: Event) {
  if (!state.value.keys) return;
  const keys = [...state.value.keys];
  keys[slot] = Number((event.target as HTMLSelectElement).value);
  generation++;
  busy.value = true;
  error.value = "";
  try {
    await invoke("set_watch_keys", { keys });
    state.value = { ...state.value, pending: true };
  } catch (e) { error.value = String(e); }
  finally { busy.value = false; }
}
onMounted(() => { void refresh(); timer = setInterval(refresh, 2000); });
onUnmounted(() => { generation++; clearInterval(timer); });
</script>

<template>
  <article class="card setup-card">
    <div class="card-title-row"><h2>小米 Watch 5</h2><span class="pill" :class="state.online ? 'ok' : 'warn'">{{ state.online ? "已连接" : state.enabled ? "等待手表" : "未启用" }}</span></div>
    <p class="muted">手表通过无线调试连接，使用本页的火山豆包 API 和文字输出设置。首次使用请先在电脑上授权 ADB。</p>
    <form class="chip-row" @submit.prevent="change(false)">
      <label for="watch-address">手表地址</label>
      <input id="watch-address" v-model="address" :placeholder="state.serial || '留空自动查找已连接手表'" :disabled="busy" />
      <button class="primary-button" :disabled="busy">{{ busy ? "处理中…" : state.enabled ? "重新连接" : "连接手表" }}</button>
      <button v-if="state.enabled" class="secondary-button" type="button" :disabled="busy" @click="change(true)">断开手表</button>
    </form>
    <div v-if="state.enabled" class="chip-row">
      <button type="button" class="secondary-button" :disabled="busy" @click="toggleBle">{{ state.ble_enabled ? "关闭蓝牙接收" : "启用蓝牙 BLE（实验）" }}</button>
      <small v-if="state.ble_enabled">{{ state.ble_online ? "BLE 已连接" : "请在手表顶部设置选择「使用蓝牙 BLE」" }}</small>
    </div>
    <div v-if="state.keys" class="chip-row">
      <label v-for="(key, i) in state.keys" :key="i">{{ i === 0 ? "左键" : "右键" }}
        <select :value="key" :disabled="busy || state.pending || !state.online" @change="setKey(i, $event)">
          <option v-for="(name, value) in actions" :key="value" :value="value">{{ name }}</option>
        </select>
      </label>
      <small v-if="state.pending">等待手表确认…</small>
    </div>
    <p v-if="error" class="error-text" role="alert">{{ error }}</p>
  </article>
</template>
