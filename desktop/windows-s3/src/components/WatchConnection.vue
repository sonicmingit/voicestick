<script setup lang="ts">
import { invoke } from "@tauri-apps/api/core";
import { onMounted, onUnmounted, ref } from "vue";

type Status = { paired: boolean; enabled: boolean; online: boolean; serial: string; keys: [number, number] | null; pending: boolean; ble_enabled: boolean; ble_online: boolean };
const state = ref<Status>({ paired: false, enabled: false, online: false, serial: "", keys: null, pending: false, ble_enabled: false, ble_online: false });
const actions = ["回车", "撤销", "粘贴"];
const address = ref("");
const tab=ref<"ble"|"adb">("ble");
const pairCode=ref("");
let initialized=false;
const error = ref("");
const busy = ref(false);
let timer: ReturnType<typeof setInterval>;
let generation = 0;
async function refresh() {
  const request = generation;
  if (busy.value) return;
  try {
    const snapshot = await invoke<Status>("watch_status");
    if (request === generation) {
      state.value = snapshot;
      if (!initialized) {
        if (snapshot.enabled && !snapshot.ble_enabled) tab.value="adb";
        initialized=true;
      }
    }
  } catch { /* The plain browser preview has no native receiver. */ }
}
function chooseTab(choice:"ble"|"adb") {
  tab.value=choice; initialized=true; error.value=""; pairCode.value="";
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
async function forget() {
  generation++; busy.value=true; error.value="";
  try { state.value=await invoke<Status>("forget_watch"); }
  catch(e){ error.value=String(e); }
  finally{ busy.value=false; }
}
async function toggleBle() {
  generation++; busy.value=true; error.value="";
  try {
    state.value=await invoke<Status>("watch_ble_mode",{
      enable:!state.value.ble_enabled, code:!state.value.paired ? pairCode.value : null
    });
    pairCode.value="";
  }
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
    <p class="muted">手表听写使用火山豆包 API。下方的输入法选择用于 StickS3；文字输出方式与手表共用。</p>
    <div class="chip-row" aria-label="手表连接方式">
      <button v-for="choice in (['ble','adb'] as const)" :key="choice"
        type="button" :class="tab===choice?'primary-button':'secondary-button'" :aria-pressed="tab===choice"
        @click="chooseTab(choice)">{{ choice==='ble'?'蓝牙 BLE':'无线 ADB' }}</button>
    </div>
    <form v-if="tab==='ble'" class="chip-row" @submit.prevent="toggleBle">
      <template v-if="!state.paired">
        <label for="watch-code">蓝牙配对码</label>
        <input id="watch-code" v-model="pairCode" type="password" autocomplete="off" spellcheck="false"
          placeholder="手表顶部设置 → 查看蓝牙配对码" :disabled="busy" />
      </template>
      <button class="primary-button" :disabled="busy || (!state.paired && !pairCode.trim())">
        {{ state.ble_enabled ? "关闭蓝牙接收" : "连接 BLE" }}
      </button>
      <small>{{ state.ble_online ? "BLE 已连接" : "在表上选择「使用蓝牙 BLE」，并保持应用在前台。" }}</small>
    </form>
    <form v-else class="chip-row" @submit.prevent="change(false)">
      <label for="watch-address">无线调试地址</label>
      <input id="watch-address" v-model="address" :placeholder="state.serial || '留空自动查找已授权手表'" :disabled="busy" />
      <button class="primary-button" :disabled="busy">{{ busy ? "处理中…" : "连接 ADB" }}</button>
    </form>
    <div class="chip-row">
      <button v-if="state.enabled" class="secondary-button" type="button" :disabled="busy" @click="change(true)">断开手表</button>
      <button v-if="state.paired && !state.enabled" type="button" class="secondary-button" :disabled="busy" @click="forget">清除配对</button>
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
