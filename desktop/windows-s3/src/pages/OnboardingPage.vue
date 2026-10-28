<script setup lang="ts">
import ConnectionPage from "./ConnectionPage.vue";
import type { RuntimeSnapshot } from "../lib/bridge";
import { completeOnboarding } from "../lib/bridge";
import { t } from "../lib/i18n";
defineProps<{ runtime: RuntimeSnapshot | null }>();
const emit = defineEmits<{ completed: [] }>();
async function finish() { await completeOnboarding(); emit("completed"); }
</script>
<template><main class="content"><ConnectionPage :runtime="runtime" /><button class="primary-button" @click="finish">{{ t("完成设置") }}</button></main></template>
