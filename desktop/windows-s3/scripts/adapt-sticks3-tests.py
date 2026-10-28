"""Migrate hardware-specific UI assertions; retain originals for upstream comparison."""
from pathlib import Path
root = Path(__file__).resolve().parents[1]
archive = root / "Testing/upstream-ui"
archive.mkdir(exist_ok=True)
for name in ["ButtonsPage", "ConnectionPage"]:
    path = root / f"src/pages/{name}.test.ts"
    original = path.read_text(encoding="utf-8")
    (archive / f"{name}.test.ts.reference").write_text(original, encoding="utf-8")
    if name == "ButtonsPage":
        # The original test fixture/mocks are shared; Xiaomi's 13-key/UAC cases
        # are replaced with tests of the real two-key product below.
        prefix = original[:original.index('describe("buttons mapping page"')]
        prefix = prefix.replace('remoteModel: "rc003"', 'remoteModel: "stick_s3"')
        prefix += '''describe("StickS3 button mapping", () => {
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
'''
        path.write_text(prefix, encoding="utf-8")
    else:
        # Enhanced-capture Helper is specific to Xiaomi. It is absent from the
        # StickS3 UI, where the firmware provides the keyboard channel.
        chunks = original.split('\ndescribe(')
        chunks = [chunk for chunk in chunks if not chunk.startswith(('"connection page rc003 capture switch"', '"connection page capture support unavailable'))]
        changed = '\ndescribe('.join(chunks)
        changed = changed.replace('要开启“支持更多输入工具”', '通过 StickS3 键盘通道启动')
        changed = changed.replace('已配对的遥控器', '已配对的 StickS3')
        path.write_text(changed, encoding="utf-8")
for name in ["src/lib/profile.test.ts", "src/pages/MacrosPage.test.ts"]:
    path=root/name
    text=path.read_text(encoding="utf-8").replace('"确定"','"正面按键"').replace('确定 ·','正面按键 ·').replace('菜单 ·','侧面按键 ·')
    path.write_text(text,encoding="utf-8")
path=root/"src/pages/SettingsPage.test.ts"
text=path.read_text(encoding="utf-8").replace("无线麦 SayAll","VoiceStick S3")
path.write_text(text,encoding="utf-8")
print("StickS3 UI assertions migrated; Xiaomi-specific originals archived.")
