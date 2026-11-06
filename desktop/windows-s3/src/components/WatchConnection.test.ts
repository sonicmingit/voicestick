import { mount, flushPromises } from "@vue/test-utils";
import { describe, it, expect, vi } from "vitest";
import { invoke } from "@tauri-apps/api/core";
import WatchConnection from "./WatchConnection.vue";
vi.mock("@tauri-apps/api/core", () => ({ invoke: vi.fn() }));
const idle = {paired:false, enabled:false, online:false, keys:null, serial:"", pending:false, ble_enabled:false, ble_online:false};
describe("watch transport selection", () => {
  it("pairs over BLE without scanning ADB", async () => {
    const mock=vi.mocked(invoke); mock.mockReset().mockResolvedValue(idle);
    const page=mount(WatchConnection);
    try {
      await flushPromises();
      await page.get("#watch-code").setValue("0011 2233 4455 6677 8899 AABB CCDD EEFF");
      await page.get("form").trigger("submit"); await flushPromises();
      expect(mock).toHaveBeenCalledWith("watch_ble_mode",{enable:true,code:"0011 2233 4455 6677 8899 AABB CCDD EEFF"});
      expect(mock.mock.calls.some(([command]) => command==="connect_watch")).toBe(false);
    } finally {page.unmount();}
  });
  it("allows selecting ADB without entering a BLE code", async () => {
    const mock=vi.mocked(invoke);mock.mockReset().mockResolvedValue(idle);
    const page=mount(WatchConnection);
    try {
      await flushPromises();
      await page.findAll("button").find(b=>b.text()==="无线 ADB")!.trigger("click");
      await page.get("#watch-address").setValue("192.168.1.10:30001");
      await page.get("form").trigger("submit");await flushPromises();
      expect(mock).toHaveBeenCalledWith("connect_watch",{address:"192.168.1.10:30001"});
      expect(mock.mock.calls.some(([command])=>command==="watch_ble_mode")).toBe(false);
    } finally {page.unmount();}
  });
});
