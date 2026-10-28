import { flushPromises, mount, type VueWrapper } from "@vue/test-utils";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import ConnectionPage from "./ConnectionPage.vue";
const mocks = vi.hoisted(() => ({ getVolcengineConfig: vi.fn(), saveVolcengineConfig: vi.fn(), testVolcengineConnection: vi.fn(), getVolcengineStatus: vi.fn(), getConnectionSnapshot: vi.fn(), saveSpeechPreferences: vi.fn(), scanPairedRemotes: vi.fn() }));
vi.mock("../lib/bridge", async importOriginal => ({ ...(await importOriginal<typeof import("../lib/bridge")>()), ...mocks }));
let wrapper: VueWrapper | null = null;
beforeEach(() => {
  vi.clearAllMocks();
  mocks.getVolcengineConfig.mockResolvedValue({ keyConfigured: true, resourceId: "volc.seedasr.sauc.duration", hotwords: ["StickS3"], voiceMode: "volcengine", outputMode: "focused_app", floatingWindow: true, overlayPosition: "center" });
  mocks.saveVolcengineConfig.mockResolvedValue({ keyConfigured: true, resourceId: "volc.seedasr.sauc.duration", hotwords: [], voiceMode: "volcengine", outputMode: "focused_app", floatingWindow: true, overlayPosition: "center" });
  mocks.getVolcengineStatus.mockResolvedValue({ generation: 1, phase: "ready", text: "", lastError: null });
  mocks.getConnectionSnapshot.mockResolvedValue({ phase: "idle", remoteName: null, remoteModel: "unknown", lastError: null });
  mocks.scanPairedRemotes.mockResolvedValue([]);
  mocks.testVolcengineConnection.mockResolvedValue(undefined);
  mocks.saveSpeechPreferences.mockImplementation(async (voiceMode, outputMode, floatingWindow, overlayPosition) => ({ keyConfigured: true, resourceId: "volc.seedasr.sauc.duration", hotwords: ["StickS3"], voiceMode, outputMode, floatingWindow, overlayPosition }));
});
afterEach(() => { wrapper?.unmount(); wrapper = null; });
async function render() { wrapper = mount(ConnectionPage, { props: { runtime: null } }); await flushPromises(); return wrapper; }
describe("direct API connection setup", () => {
  it("shows saved configuration without ever returning or displaying a key", async () => {
    const page = await render();
    expect(page.text()).toContain("火山豆包 API");
    expect(page.findAll(".tool-card")).toHaveLength(4);
    expect(page.text()).toContain("豆包输入法");
    expect(page.text()).toContain("微信输入法");
    expect(page.text()).not.toContain("Vokie");
    expect(page.text()).not.toContain("CABLE");

    expect(page.get<HTMLInputElement>("#volcengine-key").element.type).toBe("password");
    expect(page.get<HTMLInputElement>("#volcengine-key").element.value).toBe("");
    expect(page.get<HTMLTextAreaElement>("#volcengine-hotwords").element.value).toBe("StickS3");
  });
  it("keeps the saved key when blank and normalizes hotwords", async () => {
    const page = await render();
    await page.get("#volcengine-hotwords").setValue("甲，乙\n 丙 ");
    await page.get("form").trigger("submit"); await flushPromises();
    expect(mocks.saveVolcengineConfig).toHaveBeenCalledWith(null, "volc.seedasr.sauc.duration", ["甲", "乙", "丙"]);
    expect(page.text()).toContain("API 配置已保存");
  });
  it("clears a new key after saving and requires saving before testing it", async () => {
    const page = await render(); await page.get("#volcengine-key").setValue("fixture-key");
    const button = page.findAll("button").find(button => button.text() === "测试 API 连接")!;
    expect(button.attributes("disabled")).toBeDefined();
    await page.get("form").trigger("submit"); await flushPromises();
    expect(mocks.saveVolcengineConfig).toHaveBeenCalledWith("fixture-key", "volc.seedasr.sauc.duration", ["StickS3"]);
    expect(page.get<HTMLInputElement>("#volcengine-key").element.value).toBe("");
  });
  it("shows a sanitized API failure and permits retry", async () => {
    mocks.testVolcengineConnection.mockRejectedValueOnce("语音服务连接失败（HTTP 403），请检查 API Key、资源和额度");
    const page = await render(); const button = page.findAll("button").find(button => button.text() === "测试 API 连接")!;
    await button.trigger("click"); await flushPromises(); expect(page.text()).toContain("HTTP 403");
    await button.trigger("click"); await flushPromises(); expect(page.text()).toContain("API 连接正常");
  });
  it("preserves recognized text for copying when the target focus changed", async () => {
    mocks.getVolcengineStatus.mockResolvedValue({ generation: 2, phase: "copy_required", text: "测试识别结果", lastError: "输入焦点已改变" });
    const page = await render(); expect(page.text()).toContain("测试识别结果"); expect(page.text()).toContain("识别完成，请复制文字"); expect(page.text()).toContain("输入焦点已改变");
  });
  it("restores input method setup without replacing the API configuration", async () => {
    const page = await render();
    await page.findAll(".tool-card").find(card => card.text().includes("豆包输入法"))!.trigger("click");
    await flushPromises();
    expect(mocks.saveSpeechPreferences).toHaveBeenCalledWith("doubao", "focused_app", true, "center");
    expect(page.find("#ime-endpoint").exists()).toBe(true);
    expect(page.find("#ime-hotkey").exists()).toBe(true);
    expect(page.find("#volcengine-key").exists()).toBe(false);
    await page.findAll(".tool-card").find(card => card.text().includes("火山豆包 API"))!.trigger("click");
    await flushPromises(); expect(page.find("#volcengine-key").exists()).toBe(true);
  });
  it("persists clipboard output and floating window preferences independently", async () => {
    const page = await render();
    await page.get("#speech-output").setValue("clipboard_and_input"); await flushPromises();
    expect(mocks.saveSpeechPreferences).toHaveBeenLastCalledWith("volcengine", "clipboard_and_input", true, "center");
    await page.get("#overlay-position").setValue("bottom_right"); await flushPromises();
    expect(mocks.saveSpeechPreferences).toHaveBeenLastCalledWith("volcengine", "clipboard_and_input", true, "bottom_right");
    await page.get(".overlay-toggle input").setValue(false); await flushPromises();
    expect(mocks.saveSpeechPreferences).toHaveBeenLastCalledWith("volcengine", "clipboard_and_input", false, "bottom_right");
  });

});
