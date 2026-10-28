import { flushPromises, mount } from "@vue/test-utils";
import { afterEach, expect, it, vi } from "vitest";
import SpeechOverlay from "./SpeechOverlay.vue";
const mocks=vi.hoisted(()=>({getVolcengineStatus:vi.fn()}));
vi.mock("../lib/bridge",()=>mocks);
afterEach(()=>vi.useRealTimers());
it("updates partial text and displays clipboard completion without an editable input",async()=>{
  vi.useFakeTimers();
  mocks.getVolcengineStatus.mockResolvedValue({generation:1,phase:"listening",text:"第一段",lastError:null});
  const wrapper=mount(SpeechOverlay);await flushPromises();expect(wrapper.text()).toContain("第一段");
  mocks.getVolcengineStatus.mockResolvedValue({generation:1,phase:"listening",text:"第一段继续显示",lastError:null});
  await vi.advanceTimersByTimeAsync(100);await flushPromises();expect(wrapper.text()).toContain("第一段继续显示");
  mocks.getVolcengineStatus.mockResolvedValue({generation:1,phase:"copied_and_inserted",text:"完整识别结果",lastError:null});
  await vi.advanceTimersByTimeAsync(100);await flushPromises();expect(wrapper.text()).toContain("已复制并输入到当前窗口");expect(wrapper.findAll("input,textarea")).toHaveLength(0);wrapper.unmount();
});
