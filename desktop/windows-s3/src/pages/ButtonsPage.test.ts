import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import { flushPromises, mount, type VueWrapper } from "@vue/test-utils";
import ButtonsPage from "./ButtonsPage.vue";

type EdgeHandler = (edge: { button: string; isPressed: boolean }) => void;
type GestureHandler = (gesture: { button: string; trigger: string }) => void;
type ShortcutCaptureHandler = (edge: { key: string; isPressed: boolean }) => void;

let edgeHandler: EdgeHandler | null = null;
let gestureHandler: GestureHandler | null = null;
let shortcutCaptureHandler: ShortcutCaptureHandler | null = null;

/** 高亮时序取证用的前端日志 spy（保留真实的 reasonToken 等实现）。 */
const { reportFrontendEventSpy } = vi.hoisted(() => ({
  reportFrontendEventSpy: vi.fn<(event: Record<string, unknown>) => void>(),
}));

vi.mock("../lib/frontend-diagnostics", async (importOriginal) => {
  const actual = await importOriginal<typeof import("../lib/frontend-diagnostics")>();
  return {
    ...actual,
    reportFrontendEvent: (event: Record<string, unknown>) => reportFrontendEventSpy(event),
  };
});

vi.mock("../lib/bridge", async (importOriginal) => {
  const actual = await importOriginal<typeof import("../lib/bridge")>();
  return {
    ...actual,
    getButtonMappings: vi.fn(async () => ({
      enabled: true,
      actions: {
        ok: {
          single: { type: "shortcut", chord: { keys: ["enter"] } },
          double: { type: "disabled" },
          long: { type: "disabled" },
        },
      },
    })),
    getButtonMappingSnapshot: vi.fn(async () => ({
      enabled: true,
      gateActive: true,
      listenerActive: true,
      swallowedEdges: 3,
      leakedDowns: 0,
      firedGestures: 1,
      lastFired: null,
      lastError: null,
    })),
    saveButtonMappings: vi.fn(async (mappings: unknown) => mappings),
    activateAppForLearning: vi.fn(async () => undefined),
    learnFocusTarget: vi.fn(async () => ({
      controlType: "Edit",
      automationId: "chat-input",
      className: "RichEdit",
    })),
    learnFocusPoint: vi.fn(async () => ({
      xRatio: 0.5,
      yRatio: 0.82,
      windowWidth: 1200,
      windowHeight: 800,
    })),
    testAppFocus: vi.fn(async () => undefined),
    exportButtonMappingConfiguration: vi.fn(async () => true),
    importButtonMappingConfiguration: vi.fn(async () => ({
      enabled: false,
      actions: {
        power: {
          single: { type: "shortcut", chord: { keys: ["escape"] } },
          double: { type: "disabled" },
          long: { type: "disabled" },
        },
      },
    })),
    resetButtonMappings: vi.fn(async () => ({ enabled: true, actions: {} })),
    // 组合动作分组候选（2026-10-15）：默认给一条动作，个别用例覆盖为空库。
    getCombinedActions: vi.fn(async () => ({
      formatVersion: 1,
      definitions: [
        {
          schemaVersion: "0.1",
          macroId: "macro.aaaaaaa1",
          version: "1.0.0",
          name: "打开记事本并聚焦",
          steps: [
            { stepId: "step.01", action: "open_application", parameters: { target: "notepad" } },
          ],
        },
      ],
    })),
    scanRegisteredApps: vi.fn(async () => [
      { name: "Registered Example", path: "shell:AppsFolder\\Example!App" },
    ]),
    testButtonMapping: vi.fn(async () => ({
      available: true,
      submittedBatches: 1,
      submittedEvents: 2,
      lastError: null,
    })),
    subscribeButtonEdges: vi.fn(async (handler: EdgeHandler) => {
      edgeHandler = handler;
      return () => {};
    }),
    subscribeButtonGestures: vi.fn(async (handler: GestureHandler) => {
      gestureHandler = handler;
      return () => {};
    }),
    startShortcutCapture: vi.fn(async () => undefined),
    stopShortcutCapture: vi.fn(async () => undefined),
    subscribeShortcutCaptureEdges: vi.fn(async (handler: ShortcutCaptureHandler) => {
      shortcutCaptureHandler = handler;
      return () => {};
    }),
    // 三键捕获：默认「未授权，开启会触发 UAC」；个别用例用 mockResolvedValue 覆盖。
    // authorizationRequired 与 Rust enable_capture 的授权判定同源
    // （任务未注册，或安装/升级写下了重授权标记）。
    getRc003TaskStatus: vi.fn(async () => ({
      installed: false,
      authorizationRequired: true,
      enabled: false,
      helperPath: null,
      lastError: null,
    })),
    enableRc003Capture: vi.fn(async () => ({
      installed: true,
      authorizationRequired: false,
      enabled: true,
      helperPath: null,
      lastError: null,
    })),
    disableRc003Capture: vi.fn(async () => ({
      // 关闭不移除任务：授权保留（与真实语义一致）。
      installed: true,
      authorizationRequired: false,
      enabled: false,
      helperPath: null,
      lastError: null,
    })),
    getRc003BridgeSnapshot: vi.fn(async () => ({
      phase: "stopped",
      port: 0,
      helperPid: 0,
      acceptedTotal: 0,
      deniedTotal: 0,
      replacedTotal: 0,
      edgesApplied: 0,
      usagesDropped: 0,
      malformedTotal: 0,
      watchdogReleaseTotal: 0,
      pressedUsages: [],
      lastRxAgeMs: null,
      targetGeneration: 0,
      targetUsages: [],
      ownedUsages: [],
      captureHealth: "off",
      captureUnavailable: "none",
      captureRevoked: "none",
    })),
    // 遥控器信息卡片上的「重新连接」：扫描已配对设备 + 连接（默认空列表，用例内按需覆盖）。
    scanPairedRemotes: vi.fn(async () => []),
    connectRemote: vi.fn(async () => runtime.platform.connection),
    // 本机对「全按键支持」的支持情况：默认可用（x64 真机 / 受支持的平台），
    // 不可用的分支由「全按键支持在本机不可用（issue #206）」用例覆盖。
    getCaptureSupport: vi.fn(async () => ({
      nativeArch: "x64" as const,
      helperExpected: "sayall-helper.exe",
      available: true,
      reason: null,
    })),
    // 常用语组件可用性：默认不可用（公开构建口径）；可用分支由专门用例覆盖。
    getCommonPhrasePanelSnapshot: vi.fn(async () => ({
      available: false,
      access: "unavailable" as const,
      canUse: false,
      notice: null,
      entries: [],
    })),
    // 「锁定当前按键」偏好（2026-10-18 持久化）：默认开启；用例内按需覆盖。
    getLockMappingSelection: vi.fn(async () => true),
    saveLockMappingSelection: vi.fn(async (locked: boolean) => locked),
  };
});

import {
  activateAppForLearning,
  connectRemote,
  exportButtonMappingConfiguration,
  getButtonMappings,
  getButtonMappingSnapshot,
  getCaptureSupport,
  getCombinedActions,
  getCommonPhrasePanelSnapshot,
  enableRc003Capture,
  disableRc003Capture,
  getLockMappingSelection,
  getRc003BridgeSnapshot,
  getRc003TaskStatus,
  importButtonMappingConfiguration,
  learnFocusPoint,
  learnFocusTarget,
  saveLockMappingSelection,
  scanPairedRemotes,
  subscribeButtonEdges,
  subscribeButtonGestures,
  saveButtonMappings,
  startShortcutCapture,
  stopShortcutCapture,
} from "../lib/bridge";
import type {
  ButtonMappings,
  CaptureSupport,
  PairedRemote,
  Rc003BridgeSnapshot,
  RuntimeSnapshot,
} from "../lib/bridge";

const runtime: RuntimeSnapshot = {
  appVersion: "0.1.0",
  platform: {
    platform: "windows",
    windowsApiAvailable: true,
    bleScanAvailable: true,
    bleVoiceReady: true,
    wasapiReady: false,
    rawInputReady: true,
    sendInputReady: true,
    verificationStatus: "测试",
    connection: {
      phase: "ready",
      remoteName: "小米蓝牙语音遥控器",
      remoteModel: "stick_s3",
      capabilities: null,
      voiceState: "idle",
      decodedSamples: 0,
      generation: 0,
      reconnectAttempt: 0,
      powerNotificationsAvailable: false,
      lastError: null,
    },
    audio: {
      phase: "unsupported",
      selectedEndpointId: null,
      selectedEndpointName: null,
      queuedSamples: 0,
      submittedSamples: 0,
      generation: 0,
      lastError: null,
    },
    rawInput: {
      phase: "ready",
      matchedDeviceCount: 1,
      rawEventCount: 0,
      semanticEdgeCount: 0,
      lastButton: null,
      lastIsPressed: null,
      activeButtons: [],
      lastError: null,
      staleRemoteEventCount: 0,
    },
    buttonMapping: {
      enabled: true,
      gateActive: true,
      listenerActive: true,
      swallowedEdges: 3,
      leakedDowns: 0,
      firedGestures: 1,
      lastFired: null,
      lastError: null,
    },
  },
};

async function mountPage(model: "rc001" | "rc003" | "unknown" = "rc003"): Promise<VueWrapper> {
  const snapshot =
    model === "rc003"
      ? runtime
      : {
          ...runtime,
          platform: {
            ...runtime.platform,
            connection: { ...runtime.platform.connection, remoteModel: model },
          },
        };
  const wrapper = mount(ButtonsPage, { props: { runtime: snapshot } });
  await vi.waitFor(() => {
    if (!edgeHandler || !gestureHandler) throw new Error("事件订阅未完成");
  });
  mountedPages.push(wrapper);
  return wrapper;
}

/** 通过「扫描本机应用」加入 mock 的 Registered Example（自定义应用）。 */
async function addScannedApp(page: VueWrapper): Promise<void> {
  await page
    .findAll("button")
    .find((button) => button.text() === "扫描本机应用")!
    .trigger("click");
  await flushPromises();
  await page.get('input[aria-label="全选当前结果"]').setValue(true);
  await page.get(".registered-apps-dialog .primary-button").trigger("click");
  await flushPromises();
}

/**
 * 本文件挂载过的页面，用例结束统一卸载。
 *
 * 为什么必须卸载：页面里有一个每秒对账的 interval（读任务状态与桥接相位），
 * 留在挂载态的页面会继续按**当前用例的 mock** 轮询并回写 localStorage 缓存——
 * 上一用例遗留的开关值（enabled=false）会覆盖本用例刚写下的缓存，制造跨用例
 * 串扰。2026-10-10 实测：缓存用例偶发读到 { enabled:false, phase:'connected' }
 * （本文件新增用例改变时序后稳定复现）。
 */
const mountedPages: VueWrapper[] = [];
afterEach(() => {
  while (mountedPages.length) {
    const page = mountedPages.pop()!;
    try {
      page.unmount();
    } catch {
      // 用例里已自行卸载：忽略重复卸载。
    }
  }
});

beforeEach(() => {
  edgeHandler = null;
  gestureHandler = null;
  shortcutCaptureHandler = null;
  vi.mocked(getButtonMappings).mockClear();
  vi.mocked(subscribeButtonEdges).mockClear();
  vi.mocked(subscribeButtonGestures).mockClear();
  reportFrontendEventSpy.mockClear();
  vi.mocked(saveButtonMappings).mockClear();
  // mockClear 只清调用记录，**不清实现**：学习输入框的默认实现要在每个用例里重置。
  vi.mocked(activateAppForLearning).mockClear();
  vi.mocked(activateAppForLearning).mockResolvedValue(undefined);
  vi.mocked(learnFocusTarget).mockClear();
  vi.mocked(learnFocusTarget).mockResolvedValue({
    controlType: "Edit",
    automationId: "chat-input",
    className: "RichEdit",
  });
  vi.mocked(learnFocusPoint).mockClear();
  vi.mocked(learnFocusPoint).mockResolvedValue({
    xRatio: 0.5,
    yRatio: 0.82,
    windowWidth: 1200,
    windowHeight: 800,
  });
  vi.mocked(exportButtonMappingConfiguration).mockClear();
  vi.mocked(importButtonMappingConfiguration).mockClear();
  vi.mocked(startShortcutCapture).mockClear();
  vi.mocked(stopShortcutCapture).mockClear();
  // mockClear 只清调用记录，**不清实现**：前面用例留下的 mockRejectedValue /
  // mockResolvedValue 会渗进后面的用例，必须显式重置。
  localStorage.clear();
  vi.mocked(getRc003TaskStatus).mockResolvedValue({
    installed: false,
    authorizationRequired: true,
    enabled: false,
    helperPath: null,
    lastError: null,
  });
  vi.mocked(enableRc003Capture).mockResolvedValue({
    installed: true,
    authorizationRequired: false,
    enabled: true,
    helperPath: null,
    lastError: null,
  });
  vi.mocked(disableRc003Capture).mockResolvedValue({
    installed: true,
    authorizationRequired: false,
    enabled: false,
    helperPath: null,
    lastError: null,
  });
  // 桥接快照同样要重置实现（mockClear 不清实现，见上）。
  vi.mocked(getRc003BridgeSnapshot).mockResolvedValue(bridgeSnapshot("stopped"));
  // 「全按键支持」的支持情况同样要重置（默认可用，回归基线）。
  vi.mocked(getCaptureSupport).mockResolvedValue(SUPPORTED_CAPTURE);
  // 「重新连接」用的扫描/连接：同样重置实现，避免 mockRejectedValue / 空实现渗进后续用例。
  vi.mocked(scanPairedRemotes).mockReset();
  vi.mocked(scanPairedRemotes).mockResolvedValue([]);
  vi.mocked(connectRemote).mockClear();
  // 常用语可用性：mockClear 不清实现，显式复位为公开构建口径（不可用）。
  vi.mocked(getCommonPhrasePanelSnapshot).mockReset();
  vi.mocked(getCommonPhrasePanelSnapshot).mockResolvedValue({
    available: false,
    access: "unavailable",
    canUse: false,
    previewMode: false,
    notice: null,
    entries: [],
  });
  // 「锁定当前按键」偏好（2026-10-18）：同样显式重置实现（mockReset 清实现，再给默认值）。
  vi.mocked(getLockMappingSelection).mockReset();
  vi.mocked(getLockMappingSelection).mockResolvedValue(true);
  vi.mocked(saveLockMappingSelection).mockReset();
  vi.mocked(saveLockMappingSelection).mockImplementation(async (locked: boolean) => locked);
});

/** 构造一份指定相位的桥接快照（其余计数字段对 UI 无关，取零值）。 */
function bridgeSnapshot(
  phase: Rc003BridgeSnapshot["phase"],
  captureHealth: Rc003BridgeSnapshot["captureHealth"] = "off",
  captureRevoked: Rc003BridgeSnapshot["captureRevoked"] = "none",
  captureUnavailable: Rc003BridgeSnapshot["captureUnavailable"] = "none",
): Rc003BridgeSnapshot {
  return {
    phase,
    port: 0,
    helperPid: 0,
    acceptedTotal: 0,
    deniedTotal: 0,
    replacedTotal: 0,
    edgesApplied: 0,
    usagesDropped: 0,
    malformedTotal: 0,
    watchdogReleaseTotal: 0,
    pressedUsages: [],
    lastRxAgeMs: null,
    targetGeneration: 0,
    targetUsages: [],
    ownedUsages: [],
    captureHealth,
    captureUnavailable,
    captureRevoked,
  };
}

/** 「全按键支持」所在的开关行（页面有多个 toggle-row，必须按文本定位）。 */
function captureRow(page: VueWrapper) {
  return page
    .findAll(".toggle-row")
    .filter((row) => row.text().includes("全按键支持"))[0];
}

/** 2026-09-28 用户定稿：三键关闭态提示（不再带「提示：」前缀）。 */
const TRI_KEY_HINT = "返回 / 音量+ / 音量−需要开启全按键支持才能使用";

/** 2026-10-03 用户定稿：关闭态三键卡片置灰禁用后的悬停提示。 */
const TRI_KEY_GATED_TITLE = `${TRI_KEY_HINT}，开启后恢复正常`;

/** 2026-09-28 用户定稿：开关悬停提示随开关状态切换。 */
const CAPTURE_SWITCH_OFF_TITLE =
  "开启后支持使用返回 / 音量+ / 音量−，其他按键将同步优化";
const CAPTURE_SWITCH_ON_TITLE = "关闭后返回 / 音量+ / 音量−将不可映射";

/** 本机支持「全按键支持」（x64 等受支持平台）：回归基线。 */
const SUPPORTED_CAPTURE: CaptureSupport = {
  nativeArch: "x64",
  helperExpected: "sayall-helper.exe",
  available: true,
  reason: null,
};

/** 本机不支持（Windows 11 ARM64，issue #206）：界面必须置灰并说明原因。 */
const UNSUPPORTED_CAPTURE: CaptureSupport = {
  nativeArch: "arm64",
  helperExpected: "sayall-helper-arm64.exe",
  available: false,
  reason: "arch_unsupported",
};

/** 2026-10-07 定稿：本机不可用时的开关悬停提示（原因 + 恢复方式）。 */
const CAPTURE_UNSUPPORTED_TITLE =
  "全按键支持在这台电脑上暂不可用；新版本支持后会恢复正常。";
/** 2026-10-07 定稿：开关旁的短说明（≤ 40 字）。 */
const CAPTURE_UNSUPPORTED_NOTE = "这台电脑暂不支持全按键支持，其他按键不受影响。";
/** 2026-10-07 定稿：三键卡片/格子的不可用悬停提示（不写「开启后恢复正常」）。 */
const TRI_KEY_UNSUPPORTED_TITLE =
  "返回 / 音量+ / 音量−需要全按键支持；这台电脑暂不支持全按键支持，新版本支持后可配置。";

/** 三键捕获开启前的确认弹窗（未弹出时为 undefined）。 */
function confirmDialog(page: VueWrapper) {
  return page
    .findAll("dialog")
    .find((dialog) => dialog.classes().includes("capture-confirm-dialog"));
}

describe("StickS3 button mapping", () => {
  it("renders exactly two physical keys and six configurable gestures", async () => {
    const page = await mountPage();
    expect(page.findAll(".mapping-card")).toHaveLength(2);
    expect(page.findAll(".mapping-cell")).toHaveLength(6);
    expect(page.find(".remote-photo img").attributes("src")).toBe("/sticks3.png");
    expect(page.text()).toContain("正面按键");
    expect(page.text()).toContain("侧面按键");
    expect(page.find(".voice-card").exists()).toBe(false);
    expect(page.findAll(".toggle-row").some(row => row.text().includes("全按键支持"))).toBe(false);
  });
  it("offers hold-to-talk on either long-press slot and persists the choice", async () => {
    const page = await mountPage();
    await page.findAll(".mapping-card")[1]!.findAll(".mapping-cell")[2]!.trigger("click");
    await flushPromises();
    const action = page.findAll(".action-section button").find(button => button.text() === "按住说话");
    expect(action).toBeDefined();
    await action!.trigger("click");
    await flushPromises();
    expect(vi.mocked(saveButtonMappings).mock.lastCall![0].actions.menu?.long).toEqual({ type: "voice_hold" });
  });
  it("does not offer a recording action in a click slot", async () => {
    const page = await mountPage();
    await page.findAll(".mapping-card")[0]!.findAll(".mapping-cell")[0]!.trigger("click");
    await flushPromises();
    expect(page.findAll(".action-section button").some(button => button.text() === "按住说话")).toBe(false);
  });
  it("retains upstream shortcut and application choices in the editor", async () => {
    const page = await mountPage();
    await page.findAll(".mapping-cell")[0]!.trigger("click");
    await flushPromises();
    expect(page.text()).toContain("扫描本机应用");
    expect(page.text()).toContain("聚焦输入框");
    expect(page.findAll(".preset-grid button").length).toBeGreaterThan(10);
  });
  it("shows a saved recording binding", async () => {
    vi.mocked(getButtonMappings).mockResolvedValueOnce({ enabled: true, actions: {
      ok: { single: { type: "disabled" }, double: { type: "disabled" }, long: { type: "voice_hold" } },
    } });
    const page = await mountPage();
    expect(page.findAll(".mapping-cell")[2]!.text()).toContain("按住说话");
  });
  it("keeps export, import and reset controls", async () => {
    const page = await mountPage();
    expect(page.text()).toContain("导出");
    expect(page.text()).toContain("导入");
    expect(page.text()).toContain("恢复默认");
  });
});
