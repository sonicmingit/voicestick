import { createApp } from "vue";
import SpeechOverlay from "./components/SpeechOverlay.vue";
import { initializeLanguage, resolveLanguage } from "./lib/language";
import { effectiveLanguage } from "./lib/language-state";
window.addEventListener("storage", event => {
  if (event.key === "sayall-language-preference" && (event.newValue === "system" || event.newValue === "zh-Hans" || event.newValue === "en")) {
    effectiveLanguage.value = resolveLanguage(event.newValue);
    document.documentElement.lang = effectiveLanguage.value;
  }
});
void initializeLanguage().finally(() => createApp(SpeechOverlay).mount("#app"));
