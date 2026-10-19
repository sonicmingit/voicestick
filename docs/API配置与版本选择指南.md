# VoiceStick API 配置与火山语音版本选择指南

适用范围：VoiceStick Windows 桌面端设置界面，重点说明直接接入火山引擎的方式。

核对日期：2026-10-19。界面和调用行为按当前项目源码核对；服务价格、配额及开通方式按文中官方资料核对，实际购买以所属项目的控制台报价和合同为准。

## 1. 第一次使用应该怎么选

个人语音输入、偶尔开字幕，可以从 **豆包流式语音识别 2.0 小时版** 开始：

| 设置项 | 填写内容 |
| --- | --- |
| Provider | `Volcengine` |
| API Key | 豆包语音新版控制台中获取的 API Key |
| Resource ID | `volc.seedasr.sauc.duration` |
| Hotwords | 可以留空，或填写常用人名、产品名和专业词 |
| LLM Base URL / LLM API Key / LLM Model | 仅需要翻译时配置；只做语音转文字时可以暂不配置 |
| Debug | 日常使用可以不勾选，排查声音问题时再开启 |

保存后，将所用设备的 `Translation` 设为 `Original`，先测试能否正确转成原文，再配置翻译。

官方将 1.0 标为历史版本，建议新接入用户优先使用 2.0，已有 1.0 用户仍可继续使用。[来源：实时语音识别接口](https://docs.volcengine.com/docs/DoubaoVoice/bidirectional-streaming-automatic-speech-recognition-websocket?lang=zh)

## 2. 两套 API 分别负责什么

VoiceStick 的处理过程为：

```text
设备采集声音 → 语音识别 API（ASR）→ 原文
                                    ↓ 开启翻译时
                               大模型 API（LLM）→ 译文
```

| 配置区域 | 用途 | 什么时候需要 |
| --- | --- | --- |
| Provider、API Key、Resource ID | 将声音识别成文字 | 使用云端语音识别时需要 |
| LLM Base URL、LLM API Key、LLM Model | 将识别结果翻译为目标语言 | 设备启用 `Translate to ...` 时需要 |
| Hotwords | 提供容易识别错误的词；也作为翻译术语提示传给 LLM | 可选 |

这里的 `Provider` 选择的是语音识别服务商。下面的 LLM 服务独立配置，可以使用另一家支持相应接口的服务商。

上方 API Key 与 LLM API Key 要分别从对应服务获取，不要因为都来自火山引擎就假定两者可以互换。本文介绍的语音服务使用豆包语音凭证；LLM 凭证需按选定的大模型服务配置。

## 3. 1.0、2.0、小时版、并发版的区别

### 3.1 先区分两个维度

**1.0 / 2.0 是模型版本；小时版 / 并发版是计费与容量方案。** 因此界面里实际上有四种组合：

| 界面中的 Resource ID | 模型版本 | 方案 |
| --- | --- | --- |
| `volc.bigasr.sauc.duration` | 1.0 | 小时版 |
| `volc.bigasr.sauc.concurrent` | 1.0 | 并发版 |
| `volc.seedasr.sauc.duration` | 2.0 | 小时版 |
| `volc.seedasr.sauc.concurrent` | 2.0 | 并发版 |

`bigasr` 对应这里的 1.0，`seedasr` 对应 2.0；`duration` 和 `concurrent` 分别表示时长与并发方案。只能选择你已经开通、具有额度或付费权限的资源。[来源：语音识别资源 ID](https://docs.volcengine.com/docs/DoubaoVoice/unidirectional-streaming-automatic-speech-recognition-websocket?lang=zh)

### 3.2 1.0 小时版与 2.0 小时版

| 对比项 | 1.0 小时版 | 2.0 小时版 |
| --- | --- | --- |
| 定位 | 历史模型，供已有接入继续使用 | 新接入优先选择的版本 |
| 识别能力 | 旧一代模型能力 | 官方说明识别准确率与功能有所提升 |
| 计费维度 | 按识别音频时长 | 按识别音频时长 |
| VoiceStick 是否支持 | 支持，选择 `bigasr...duration` | 支持，选择 `seedasr...duration` |
| 选择依据 | 已有服务、有效资源或经过验证的业务效果 | 个人首次接入和新项目优先试用 |

官方没有在这里给出适用于所有场景的准确率或延迟提升比例。实际效果仍受口音、噪声、麦克风、网络和说话内容影响；可以用同一组常用语句比较最终错字和等待时间。[来源：官方版本比较](https://docs.volcengine.com/docs/6348/1807450)

模型升级也不意味着所有服务端新功能都能从 VoiceStick 界面开启。当前 Windows 客户端使用 `bigmodel_async` 接口，并开启 `enable_nonstream = true` 的二次识别；这个接口模式与小时版、并发版是不同的概念。项目实现细节见 [ASR 技术说明](火山引擎语音识别技术说明.md)。

### 3.3 小时版与并发版

| 对比项 | 小时版 | 纯并发版（以 2.0 为例） |
| --- | --- | --- |
| 买的是什么 | 音频识别用量 | 一段订购周期内可同时处理的请求容量 |
| 主要费用 | 实际识别时长，或预购时长包 | 按购买的并发路数包月 |
| 空闲时 | 没有识别用量就没有新增时长用量；已买资源包仍受有效期约束 | 包月费用不会因空闲自动减少 |
| 用得多时 | 识别时长增加，用量费用相应增加 | 官方说明在已购并发额度内不再另收时长费 |
| 能否同时识别多路 | 可以，但有配额限制 | 可以，上限取决于已购容量 |
| 常见适用方式 | 个人输入、间歇性使用、用量变化较大 | 持续运行、并发可预测且每路利用率高的业务 |

2.0 小时版支持资源包预付费和按量后付费；2.0 纯并发版需要预先购买并发容量。[来源：模型列表中的计费方案说明](https://docs.volcengine.com/docs/DoubaoVoice/model-list?lang=zh)

“并发”指同一时刻服务正在处理的请求数。例如有 10 人使用，但同一时刻只有 2 人在进行识别，所需容量取决于这 2 路及尚未处理结束的请求，而非累计注册人数。请求结束前仍可能占用容量，应按服务端实际配额统计判断。[来源：计费说明中的并发定义](https://docs.volcengine.com/docs/DoubaoVoice/Billinginstructions-21?lang=zh)

### 3.4 两个常见误解

- **小时版也支持并发。** “小时”表示音频时长的计量单位，不能据此判断只能连一台设备，或每次请求必须使用整整一小时。具体计量粒度与配额以平台规则为准。
- **并发版不代表更高的模型准确率或必然更低的延迟。** 在相同模型版本下，这个选项主要用于选择容量和付费方案；延迟仍需实测。

## 4. 价格参考与用量算例

### 4.1 核对到的公开价格

下表只摘录与本配置相关的常用价格，人民币计价，所列时长包有效期为 1 年。1.0 在官方价格页列于历史模型“大模型流式语音识别”项下；不要与另一个名为“流式语音识别”的旧产品混用价格。

| 服务 | 按量后付费 | 小额时长包参考 |
| --- | --- | --- |
| 1.0 小时版 | 4.5 元／音频小时 | 30 小时 132 元，折合 4.4 元／小时 |
| 2.0 小时版 | 1 元／音频小时 | 30 小时 28 元，折合约 0.93 元／小时 |
| 2.0 纯并发版 | 500 元／路／月，不再另收时长费 | 需购买并发容量，无默认并发 |

**注意另一项商品：小时版的并发扩容包。** 官方同时列有 2.0 并发增购包 100 元／路／月，并注明正式版默认 50 路。该扩容项增加小时版容量，仍有时长费用，不能按 500 元纯并发版的规则理解。试用配额、实际默认容量与可购买规格请在自己的控制台核实。

以上价格及容量来源：[官方计费说明](https://docs.volcengine.com/docs/DoubaoVoice/Billinginstructions-21?lang=zh)，核对日期 2026-10-19。1.0 并发版的现行报价本文未确认，不能直接套用 2.0 价格。

### 4.2 个人使用怎么估算

假设每天发送用于识别的音频累计 20 分钟，一个月使用 30 天：

```text
月识别时长 = 20 × 30 ÷ 60 = 10 小时
2.0 小时版按量费用 = 10 × 1 = 10 元
1.0 小时版按量费用 = 10 × 4.5 = 45 元
2.0 纯并发版购买 1 路 = 500 元／月
```

这是根据公开单价计算的示例，不是对实际账单的承诺；未计免费额度、优惠、扩容费及 LLM 翻译费。这里的 10 小时指音频识别用量，不是电脑开机时间。

### 4.3 什么时候才值得考虑纯并发版

只比较 2.0 按量单价与 1 路纯并发价：

```text
500 元 ÷ 1 元／小时 = 每路每月 500 小时
```

这个粗算意味着：一条容量每月实际承担约 500 小时识别时，才接近上述按量费用。若小时版享有更低资源包单价，临界用量还会增加。

多路业务应同时看峰值和利用率。例如总量 600 小时、峰值需要 10 路时，按上述公开价，纯并发容量是 `10 × 500 = 5000 元／月`；小时版时长费用是 `600 × 1 = 600 元`，另按实际需要计扩容费。因此不能只凭总用量超过 500 小时就购买并发版。

这些是基于表中价格的算术比较。个人间歇性语音输入通常从 2.0 小时版开始更合适；持续服务应结合实际峰值、购买规格和报价重新计算。

## 5. 在火山控制台准备语音服务

1. 注册并登录火山引擎，按控制台要求完成实名认证。
2. 进入豆包语音控制台，确认当前项目；首次使用可先查看默认项目 `default`。
3. 在“开通管理”或“API 服务中心”等对应入口，找到**豆包流式语音识别模型 2.0**。入口名称可能随控制台调整。
4. 选择试用或正式开通，核对是小时版还是纯并发版，以及剩余用量、配额或已购容量。
5. 在“API 管理 / API Key 管理”创建或获取 API Key，确认与所开通服务的项目、应用及权限范围匹配。
6. 回到 VoiceStick，选择 `Volcengine`，填写 Key，并选择对应的 Resource ID，点击 `Save`。

相关入口与官方操作说明：[新版快速入门](https://docs.volcengine.com/docs/DoubaoVoice/QuickStartNewConsole?lang=zh)、[控制台指引](https://docs.volcengine.com/docs/DoubaoVoice/console-guidance?lang=zh)、[默认项目 API Key 页面](https://console.volcengine.com/speech/new/setting/apikeys?projectName=default)。不同项目的资源与 Key 存在隔离，排查权限问题时应先核对所属项目。

当前 Windows 直接接入方式将上方 Key 放入 `X-Api-Key`，不提供旧版 App ID、Access Token 的组合输入项。也不要填写火山账号的通用 AccessKey ID / Secret，或把 LLM 服务的 Key 填到这里。[来源：语音 API Key 使用说明](https://docs.volcengine.com/docs/DoubaoVoice/APIKeyUsage?lang=zh)

创建 Key 不等于开通全部模型；下拉框出现某个 Resource ID，也不代表账户已经获得该资源的使用权限。

## 6. 设置界面逐项说明

| 字段 | 作用与填写方法 |
| --- | --- |
| Provider | 直接调用自己的火山语音服务时选 `Volcengine`。`VoiceStick Cloud` 使用对应云中转服务的凭证，不能直接照搬本指南的火山 Key。 |
| API Key | 当前所选语音服务的凭证。复制完整内容，避免带入空格、引号或换行。 |
| Resource ID | 指定语音模型版本及计费资源，例如 `volc.seedasr.sauc.duration`。 |
| Hotwords | 常用人名、品牌名、专业术语，以英文逗号或换行分隔；可留空。 |
| LLM Base URL | 翻译模型服务的接口基础地址。必须与下面的 Key 和模型属于同一套服务配置。 |
| LLM API Key | LLM 服务的凭证；仅在调用翻译时使用。 |
| LLM Model | 服务商要求的精确模型 ID，或其支持的推理接入点 ID。 |
| Save debug audio files | 将识别录音保存到本地，便于回听与排查问题。 |
| Audio Folder | 调试录音的保存位置；通过 `Browse...` 修改。 |
| Save / Cancel | 保存设置或放弃本次修改。 |

热词示例，可以直接按行填写：

```text
VoiceStick
火山引擎
M5Stack
你的产品名称
```

热词用于提供提示，无法保证每次都识别正确；优先加入确实经常说错的词。

调试录音默认目录为 `%LOCALAPPDATA%\VoiceStick\DebugAudio`。这是音频文件目录，与保存 API 配置的目录不同。

## 7. 配置翻译 LLM 与第三方服务

LLM 是大语言模型。当前 VoiceStick 中它负责将 ASR 已识别出的文字翻译成目标语言，例如“中文语音 → 中文识别结果 → 英文译文”。这里不是通用聊天入口，也不会因为填写了模型 Key 就自动开启润色或语音对话功能。

LLM 不限定为 OpenAI。当前客户端允许自定义接口地址、Key 和模型 ID，可以接入符合相应请求格式的第三方服务。上方 `Provider` 仍选择语音识别服务商；下面的 LLM 三项单独决定翻译使用哪家服务。

### 7.1 只需要语音转文字

将设备菜单中的 `Translation` 选择为 `Original`。此时当前 Windows 实现直接使用 ASR 文本，不调用翻译 LLM；LLM API Key 可以留空。

设置界面中出现默认 LLM 地址和模型名，也不会自行开启翻译。

### 7.2 需要翻译成其他语言

1. 先按第 5 节配置 ASR，并确认 `Original` 能正常输出。
2. 获取所选 LLM 服务的基础地址、API Key 与可用模型 ID。
3. 填好三个 LLM 字段并保存。
4. 在托盘菜单中进入对应的 `VS-XXXX` 设备子菜单，在 `Translation` 下选择 `Translate to ...` 的目标语言。
5. 说一段短句，检查得到的是所选语言的译文。

例如使用 OpenAI 官方 API 时，`LLM Base URL` 为 `https://api.openai.com/v1`，Key 与模型需要来自可用的 OpenAI API 服务。当前程序会自动补上 `/chat/completions`；如果地址已以该路径结尾，就不会重复添加。[来源：OpenAI Docs — Chat Completions](https://developers.openai.com/api/reference/resources/chat/subresources/completions/methods/create)

使用其他服务时，填写该服务文档提供的兼容接口基础地址。模型名必须使用该服务实际可用的 ID；截图和当前代码中的默认 `gpt-5.5` 只是预填配置，不能据此认定自己的账号具有调用权限，或任意兼容服务都支持它。

当前 Windows LLM 客户端发送 `Authorization: Bearer ...`、`messages` 和固定的 `temperature: 0`，并读取 `choices[0].message.content`。选择模型时，应确认服务接受这些请求字段并返回相应结构；仅支持其他 API 或拒绝这些参数的模型需要额外适配。

语音识别与 LLM 翻译是两次独立服务调用，翻译费不会被 ASR 时长包或纯并发费用自动覆盖。

### 7.3 小米 MiMo 配置示例

小米 MiMo 官方提供兼容 Chat Completions 的接口，并支持当前客户端使用的 Bearer 鉴权。下面以**按量付费 API** 为例：

| VoiceStick 字段 | 填写内容 |
| --- | --- |
| LLM Base URL | `https://api.xiaomimimo.com/v1` |
| LLM API Key | 从小米 MiMo 开放平台“API Keys”页面创建的按量付费 Key |
| LLM Model | 例如 `mimo-v2.6-flash`；使用你账号实际可用的模型 ID |

填写步骤：

1. 登录 [小米 MiMo 开放平台](https://platform.xiaomimimo.com/)，确认 API 服务权限和可用余额，创建 API Key。
2. 将上表三项填入 VoiceStick 的 LLM 配置，不要把小米 Key 填到上方火山语音的 `API Key` 中。
3. 保留已验证可用的火山 ASR 配置，点击 `Save` 保存。
4. 在对应设备的 `Translation` 菜单中选择 `Translate to ...`，例如翻译为英文。
5. 说一段中文，检查最终是否输出英文；选择 `Original` 则恢复输出识别原文。

程序自动补齐 `/chat/completions`，上表填写基础地址即可。小米的按量付费 Key 与 Token Plan 套餐 Key、专属地址相互独立，不可混用；若使用套餐，需另行核对套餐适用范围，并使用套餐页面提供的兼容接口地址和凭证。

接口形式与当前客户端匹配，但本文没有使用真实小米 Key 完成调用测试。部分 MiMo 型号默认开启思考模式，可能增加翻译等待时间；当前界面没有思考模式开关，若出现超时，需要结合所选型号行为排查。

来源：[小米首次调用 API](https://mimo.mi.com/docs/zh-CN/quick-start/summary/first-api-call)、[API 接入与凭证说明](https://mimo.mi.com/docs/zh-CN/quick-start/faq/api-integration)、[Chat Completions 兼容接口](https://mimo.mi.com/docs/zh-CN/api/chat/openai-api)，核对日期 2026-10-19。

### 7.4 能否把语音识别也换成小米

小米 LLM 配置只改变文字翻译这一步，无法替换上方火山语音的录音识别。当前界面内置的 ASR 服务商是 `Volcengine` 和 `VoiceStick Cloud`。如果要直接接入小米或其他厂商的语音识别，需要额外实现对应的 ASR 鉴权、音频请求及结果处理，不能只修改 LLM 地址或模型名。

## 8. 配置文件位置与示例

Windows 配置文件：

```text
%APPDATA%\VoiceStick\config.toml
```

一般通过设置界面修改即可。需要手动调整时，先退出程序并备份文件，再编辑相应字段；保留已有配对设备及其他设置。

以下为**字段示例**，不要用它覆盖完整配置。`REPLACE_WITH_...` 都是占位符。

```toml
asr_provider = "volcengine"
volcengine_api_key = "REPLACE_WITH_VOLCENGINE_SPEECH_API_KEY"
resource_id = "volc.seedasr.sauc.duration"
asr_hotwords = "VoiceStick,M5Stack,火山引擎"

# 以下三个字段仅在启用翻译时使用。
llm_base_url = "https://api.openai.com/v1"
llm_api_key = ""
llm_model = "REPLACE_WITH_SUPPORTED_MODEL_ID"

debug_audio_cache = false

[output]
target = "focused_app"
transform = "original"
translation_target = "en"
```

`focused_app` 表示向当前输入窗口输出，`subtitle` 表示字幕。`transform = "original"` 输出原文，`"translate"` 调用 LLM 翻译；`translation_target` 在翻译时生效，例如 `en` 或 `zh-Hans`。

设备可有独立的 `[device.<id>.output]` 覆盖设置。例如已设置设备 `C3D8` 的翻译选项时，修改全局 `[output]` 后还需要检查该设备的实际选项。

API Key 会写入本地配置文件。分享日志、配置或向仓库提交文件前，应移除真实 Key；设置界面显示为星号不代表配置文件内容已加密。

## 9. 保存后如何验证

1. 点击 `Save`，确认设备已配对并连接。
2. 将对应设备的 `Translation` 设为 `Original`。
3. 在记事本等普通文本输入框里说一句完整的话，检查有没有识别文字及最终输出。
4. 成功后，到火山控制台查看相应模型的调用用量；账单和统计可能存在延迟。
5. 如果需要翻译，再补齐 LLM 配置、选择目标语言，并单独测试翻译结果。

这样可以分别确认语音识别和翻译两条链路是否正常。

## 10. 常见问题

| 现象 | 优先排查 |
| --- | --- |
| 填了 Key，仍提示无权限或鉴权失败 | 是否为豆包语音新版 API Key；所属项目、应用、资源权限是否匹配；Key 是否被禁用或复制不完整。 |
| 小时版能用，改成并发版后失败 | 是否真正购买并开通对应版本的纯并发容量；改下拉框不会自动完成购买。 |
| 2.0 能用，1.0 失败，或相反 | 相应版本是否独立开通、是否有可用权限与额度；不要假定不同版本共享资源。 |
| 免费试用结束后不能使用 | 核对是否需要正式开通，以及余额、资源包和服务状态。 |
| 提示并发超限 | 检查同时处理的请求、其他共用该服务的程序，以及当前资源的容量。 |
| 能出原文，翻译时报 `Missing LLM API key` | 翻译模式已启用，但 LLM API Key 为空；填写 Key 或切回 `Original`。 |
| 翻译报 `Invalid LLM translation response` | 当前客户端可能没有取得预期的成功响应。检查地址、Key、模型权限、余额和参数兼容性；该提示本身不能区分具体服务端错误。 |
| 填写 LLM 后仍输出原文 | 检查所用设备是否仍选中 `Original`；配置 LLM 字段不会自动切换设备的翻译模式。 |
| 网络连接失败或长时间没有结果 | 检查当前网络是否可访问对应服务、系统网络设置，以及控制台服务状态。 |
| 人名或专业术语经常错 | 添加少量相关热词，并回听调试录音，确认是否有噪声、音量低或录音中断。 |
| 识别成功但没有进入输入框 | 检查输出目标是否为字幕，以及输入焦点；这一步发生在 API 识别之后。 |

## 11. 后续查阅

- [豆包语音新版快速入门](https://docs.volcengine.com/docs/DoubaoVoice/QuickStartNewConsole?lang=zh)：注册、开通和首次接入。
- [豆包语音控制台指引](https://docs.volcengine.com/docs/DoubaoVoice/console-guidance?lang=zh)：项目、应用和 Key 管理。
- [官方计费说明](https://docs.volcengine.com/docs/DoubaoVoice/Billinginstructions-21?lang=zh)：购买前核对价格和配额。
- [VoiceStick ASR 技术说明](火山引擎语音识别技术说明.md)：接口、协议及音频参数。
- [项目配置字段说明](../项目说明.md#本地配置)：配置项参考。
