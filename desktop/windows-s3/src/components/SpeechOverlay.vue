<script setup lang="ts">
import { computed, nextTick, onMounted, onUnmounted, ref } from "vue";
import { getVolcengineStatus, type VolcengineStatus } from "../lib/bridge";
import { t } from "../lib/i18n";
const status = ref<VolcengineStatus>({generation:0,phase:"ready",text:"",lastError:null});
const text = ref<HTMLDivElement | null>(null);
let timer: ReturnType<typeof setInterval> | undefined;
let disposed = false, polling = false;
const label = computed(() => {
  switch(status.value.phase) {
    case "connecting": return t("正在连接语音服务…");
    case "listening": return t("正在听写…");
    case "recognizing": return t("正在识别…");
    case "completed": return t("文字已发送到输入框");
    case "copied": return t("已复制到剪贴板");
    case "copied_and_inserted": return t("已复制并输入到当前窗口");
    case "copy_required": return t("识别完成，请复制文字");
    case "failed": return t("语音识别失败");
    case "empty": return t("没有识别到语音");
    default: return t("等待按住说话");
  }
});
const active = computed(() => ["connecting","listening","recognizing"].includes(status.value.phase));
async function refresh() {
  if (polling || disposed) return; polling = true;
  try { const value = await getVolcengineStatus(); if (!disposed) { status.value = value; await nextTick(); text.value?.scrollTo({top:text.value.scrollHeight}); } }
  catch { /* Keep the latest valid display until the host hides the window. */ }
  finally { polling = false; }
}
onMounted(() => { void refresh(); timer = setInterval(refresh,100); });
onUnmounted(() => { disposed = true; if(timer)clearInterval(timer); });
</script>
<template>
  <div class="speech-bubble" :class="{active,failed:status.phase==='failed'}">
    <div class="indicator"><i></i><i></i><i></i><i></i></div>
    <div class="copy"><div ref="text" class="transcript">{{ status.text || status.lastError || label }}</div><div class="caption"><span>{{ label }}</span><span v-if="status.text">{{ status.text.length }} {{ t("字") }}</span></div></div>
  </div>
</template>
<style>
:root{font-family:"Segoe UI","Microsoft YaHei",sans-serif;color:#2a303b;font-synthesis:none}*{box-sizing:border-box}html,body,#app{margin:0;width:100%;height:100%;overflow:hidden;background:transparent}.speech-bubble{height:calc(100% - 24px);margin:12px;padding:14px 18px;display:flex;gap:12px;align-items:center;background:rgba(252,252,254,.94);border:1px solid rgba(255,255,255,.7);border-radius:18px;box-shadow:0 5px 12px rgba(20,25,40,.13)}.copy{min-width:0;flex:1}.transcript{font-size:17px;line-height:1.5;max-height:70px;overflow:hidden;white-space:pre-wrap;overflow-wrap:anywhere}.caption{font-size:12px;color:#858a94;display:flex;justify-content:space-between;gap:12px;margin-top:5px}.indicator{width:32px;height:34px;display:flex;align-items:center;justify-content:center;gap:4px;flex-shrink:0}.indicator i{width:4px;height:12px;border-radius:4px;background:#757fef}.active .indicator i{animation:pulse 1s ease-in-out infinite}.indicator i:nth-child(2){height:27px;animation-delay:.13s}.indicator i:nth-child(3){height:20px;animation-delay:.26s}.indicator i:nth-child(4){animation-delay:.39s}.failed .indicator i{background:#c26464}@keyframes pulse{50%{transform:scaleY(.4);opacity:.5}}@media(prefers-reduced-motion:reduce){.active .indicator i{animation:none}}
</style>
