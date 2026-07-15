# Fork Development Notes

> 本文档记录 **nightfall-anew/PicoW-usb2bt-audio** 相对上游原版的改进，供后续会话微调 / 加功能时快速对齐上下文。  
> 上游：`wasdwasd0105/USBPods-Pico2W`（与旧名 `PicoW-usb2bt-audio` 同源）。  
> 默认工作分支：`main`。

---

## 1. 项目是什么

USB 主机 → **Pico（UAC2 声卡）** → **蓝牙耳机/音箱（A2DP Source + AVRCP）**。

| 方向 | 协议 / 角色 |
|------|-------------|
| 主机 → Pico | USB Audio Class 2（PCM 48 kHz stereo） |
| Pico → 耳机 | A2DP Source（SBC / AAC / AAC-ELD / LDAC） |
| 耳机 ↔ Pico | AVRCP（音量 absolute volume；按键 pass-through） |
| Pico → 主机 | USB HID Consumer Control（播放/暂停/上下曲等）**【本 fork 新增】** |

主机侧产品名一般为 `TinyUSB BT`。

---

## 2. 相对原版：已落地的改进

### 2.1 耳机媒体键 → 主机播放器（方案 A：AVRCP → USB HID）

**动机：** 原版 AVRCP Play/Pause 只做「静音 / 恢复音量」，不能控制主机上的音乐软件。

**做法：**

1. 启用 TinyUSB HID（`CFG_TUD_HID = 1`）。
2. 配置描述符增加 Consumer Control 接口（与 Audio IAD 分离）。
3. 耳机 AVRCP Target 收到按键后，队列发送 HID 报告。

| AVRCP 操作 | 主机侧行为 |
|------------|------------|
| Play / Pause | HID Play/Pause `0xCD` |
| Stop | HID Stop |
| Forward / Backward | Next / Previous track |
| Mute | HID Mute |
| Volume Up / Down | **不走 HID**，走 AVRCP Absolute Volume + 通知 USB 音量（见下） |

**关键文件：**

- `tusb_config.h` — `CFG_TUD_HID`
- `src/tinyusb/usb_descriptors.h` / `.c` — `ITF_NUM_HID`、report desc、EP `0x83`
- `src/tinyusb/usb_hid_media.c` / `.h` — 队列 + press/release
- `src/btstack/btstack_avdtp_source.c` — `avrcp_target_packet_handler` 映射

**实现注意：**

- **不要**在 BTstack 源文件里 `#include "class/hid/hid.h"`：会与 `btstack_hid.h` 的 `hid_report_type_t` 冲突。usage 常量写在 `usb_hid_media.h`。
- HID 发送只在 USB 定时器路径（`tinyusb_task`）里做，与 `tud_task` 同上下文，避免主循环竞态。
- 报告无 Report ID；`tud_hid_report(0, &usage, 2)`，再发 `0` 做 release。

### 2.2 主机音量 ↔ 耳机 Absolute Volume

原版已有双向同步骨架；本 fork 加固了：

- `set_bt_volume()`：校验 `avrcp_cid`、限幅、日志。
- 耳机 Volume Up/Down pass-through → 调整 `media_tracker.volume` + `avrcp_controller_set_absolute_volume` + `_bt_sink_volume_changed` 通知 USB。
- UAC 音量中断与 HID 都在 `tinyusb_task()` 中驱动（与 `tud_task` 同路径）。

主机调音量：UAC Feature Unit → `need_change_bt_volume` → `set_bt_volume` → AVRCP absolute volume。

### 2.3 配对 / 扫描更稳

原版 COD 过滤过严（要求 Rendering \| Audio service + Audio major），很多 TWS 扫到也不连。

| 项 | 原版 | 本 fork |
|----|------|---------|
| Inquiry COD | `0x200000\|0x040000\|0x000400` 全匹配 | Major=Audio **或** Audio service bit |
| Bondable | 未显式开 | `gap_set_bondable_mode(1)` |
| 开机 | 无 MAC 才扫描 | 无 **flash 有效 MAC** 才扫描；有 MAC 则双/三闪待机 |
| 长按 BOOTSEL | 清当前 MAC + 扫描 | 清 **全部 link key** + MAC + 扫描 |
| 开机自动重连 | 无 | **不做**（曾导致一直双闪、像“配不上”） |

**LED 含义（勿混淆）：**

| LED | 含义 |
|-----|------|
| **双闪**（两下亮、停一下） | 槽位 A 待机，**不是**配对 |
| **三闪** | 槽位 B 待机 |
| **均匀快闪** | 配对扫描中 |
| 慢闪（周期约 1s 级） | 正在推流（占空比与 codec 相关） |

**按键：**

| 操作 | 未连接 | 已连接 |
|------|--------|--------|
| 短按 | 重连当前槽设备 | 音量 + |
| 双击 | 切换设备槽 A/B | 音量 − |
| 长按 | 重新配对（清记录 + 扫描） | 同左 |

双设备槽：flash 记 2 个 MAC；连上蓝牙后不能再切槽，需拔插 USB 后双击切换。

### 2.4 BTstack / 连接层修复

| 问题 | 修复 |
|------|------|
| HCI 两个 handler 共用一个 `btstack_packet_callback_registration_t`，后者覆盖前者 | 拆成两个 registration 对象 |
| `MAX_NR_L2CAP_CHANNELS=4`，A2DP 流 + AVRCP 易挤爆 | 改为 **6**，services **4** |
| A2DP 与 AVRCP Target SDP 都用 handle `0x10002` | A2DP=`0x10001`，Target=`0x10002`，Controller=`0x10003` |
| LDAC config 缓冲写成 9 字节，assert | 改为 **8** 字节 |
| USB ISO 关流后 `ep_buf_ctrl` 仍 AVAIL，再开 panic | `tud_audio_set_itf_close_EP_cb` 里清 `usb_dpram->ep_buf_ctrl[1].out` |
| SSP 数字确认 | 自动 `gap_ssp_confirmation_response` |

### 2.5 双板支持（单仓库）

| 板子 | `PICO_BOARD` | 说明 |
|------|--------------|------|
| 官方 **Pico 2 W** | `pico2_w`（默认） | CYW43 原厂脚位 |
| **Waveshare RP2350B-Plus-W** | `waveshare_rp2350b_plus_w` | 自定义 `boards/waveshare_rp2350b_plus_w.h`：RP2350B（勿定义 `PICO_RP2350A`）、16MB flash、RM2 在 **GP36–39** |

CMake 注册：`list(APPEND PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_LIST_DIR}/boards)`。

**禁止交叉烧录**（Waveshare 固件烧官方板 → 蓝牙起不来，音量/媒体键全挂）。

历史上独立分支 `fix/cyw43-pins-rp2350b` 的板级内容已合入 `main`；日常只维护 `main` 即可。

### 2.6 LDAC 码率（当前工作区）

| 等级 | 约码率 | 宏 |
|------|--------|-----|
| **HQ（当前）** | ~909 / 990 kbps | `LDACBT_EQMID_HQ` |
| SQ（原版默认） | ~606 / 660 | `LDACBT_EQMID_SQ` |
| MQ | ~303 / 330 | `LDACBT_EQMID_MQ` |

位置：`src/btstack/btstack_avdtp_source.c` 内 `ldacBT_init_handle_encode(..., LDACBT_EQMID_HQ, ...)`。  
改 SQ/MQ 时同步关注注释里的 `audio_timer_interval` 建议（HQ 已用 `1`）。

> 若该改动尚未 commit，提交前请 `git status` 确认。

---

## 3. 架构与数据流（加功能时对照）

```
┌─────────────┐   UAC2 PCM    ┌──────────────────┐   A2DP     ┌──────────┐
│ USB Host    │ ────────────► │ Pico firmware    │ ─────────► │ Headset  │
│ player/OS   │ ◄─ HID media ─│                  │ ◄─ AVRCP ─ │          │
│             │ ◄─ UAC vol ── │  core0: encode   │  abs vol   │          │
└─────────────┘               │  + btstack + USB │            └──────────┘
                              └──────────────────┘
```

- **推流：** USB isochronous → slot 队列 → SBC/AAC/AAC-ELD/LDAC 编码 → AVDTP media。
- **Codec 优先级（连接时）：** AAC-ELD > LDAC > AAC > SBC（`set_next_codec`）。
- **AVRCP：** 流建立后 `avrcp_connect`；Target 收按键，Controller 发音量。

---

## 4. 关键路径地图

| 路径 | 职责 |
|------|------|
| `src/main.c` | 时钟、USB/BT 初始化、BOOTSEL 单击/双击/长按、看门狗 |
| `src/btstack/btstack_hci.c` | Inquiry、COD、配对/扫描、link key、槽位 MAC |
| `src/btstack/btstack_avdtp_source.c` | A2DP/AVDTP、编解码、AVRCP、音量、媒体键映射 |
| `src/tinyusb/uac.c` | UAC 回调、推流检测、音量同步任务 |
| `src/tinyusb/usb_descriptors.*` | UAC + HID 描述符 |
| `src/tinyusb/usb_hid_media.*` | HID 媒体键队列 |
| `src/pico_w_led.c` | LED 模式（双闪/三闪/配对/推流） |
| `boards/waveshare_rp2350b_plus_w.h` | Waveshare 板定义 |
| `btstack_config.h` | 连接数、L2CAP、flow control |
| `tusb_config.h` | USB class 开关 |
| `CMakeLists.txt` | 板型、源文件、依赖（ldac / fdk-aac） |
| `aac-eld-apple.md` | AirPods AAC-ELD 协议笔记（原项目） |

---

## 5. 构建与产物

依赖：Pico SDK **2.2.x** 推荐（Waveshare / RP2350B 脚位与扩展 GPIO）、`arm-none-eabi` 工具链、Ninja。

```bash
# 官方 Pico 2 W
cmake -S . -B build-pico2w -DPICO_BOARD=pico2_w -DPICO_SDK_PATH=$PICO_SDK_PATH
cmake --build build-pico2w

# Waveshare RP2350B-Plus-W
cmake -S . -B build-waveshare-rp2350b -DPICO_BOARD=waveshare_rp2350b_plus_w -DPICO_SDK_PATH=$PICO_SDK_PATH
cmake --build build-waveshare-rp2350b
```

| 板子 | UF2 目录 |
|------|----------|
| Pico 2 W | `build-pico2w/PicoW_USB_BT_Audio.uf2` |
| Waveshare | `build-waveshare-rp2350b/PicoW_USB_BT_Audio.uf2` |

`build*` 目录被 `.gitignore` 忽略，可随时删后重编。  
历史目录 `build/`、`build-hid-test/` 已废弃可删。

VS Code Pico 扩展：用 **Switch Board** / 设置 `PICO_BOARD`；Waveshare 需能找到 `boards/`。

---

## 6. 调试建议

- UART：GPIO0=TX，GPIO1=RX；`stdio` 已开。
- 有用日志关键字：
  - `BTstack up and running`
  - `Start scanning (pairing mode)` / `Device found` / `Bluetooth audio device detected`
  - `AVRCP: Channel ... successfully opened`
  - `set_bt_volume:` / `AVRCP volume up`
  - `AVRCP -> USB HID:` / `HID media press`
  - `LDAC HQ (~909 kbps)`
- 主机应枚举：**声卡 + HID 媒体设备**。媒体键需要系统有活动媒体会话（播放器在播或支持媒体键）。
- 配对一直**双闪**：多半是待机不是扫描 → **长按**进快闪；确认没烧错板固件。

---

## 7. 已知限制 / 后续可做

| 项 | 说明 |
|----|------|
| COD 过宽 | 扫描时可能先连到带 Audio service bit 的非耳机设备 |
| 信令失败即全量扫描 | 可改为退避重试再扫描 |
| HID 队列 | 连按只保留最后一键 |
| `is_muted` | 旧静音伪装路径基本闲置 |
| 双击切槽 | 连接后不可用，需断 USB |
| LDAC HQ | 链路/耳机不支持时可能卡顿或降级失败，可改回 SQ |
| CDC 调试 | 旧实验在 `usb-cdc-debug-fixes` 分支，未合 main |
| 上游同步 | 定期可 diff `wasdwasd0105/USBPods-Pico2W:main`，cherry-pick 有用提交 |

**加功能时优先扩展点：**

1. 新媒体键 → `usb_hid_media.h` usage + `avrcp_target_packet_handler` case。  
2. 新 codec / 码率 → `btstack_avdtp_source.c` 配置与 `set_next_codec` 优先级。  
3. 新板 → `boards/*.h` + `PICO_BOARD`，勿改默认 `pico2_w` 除非有意。  
4. 配对策略 → 只动 `btstack_hci.c` 的 inquiry/COD/bond 逻辑。

---

## 8. Git / 分支现状（精简后）

| 分支 | 状态 |
|------|------|
| **`main`** | 唯一日常分支：HID + 配对修复 + 双板 +（工作区）LDAC HQ |
| `feature/avrcp-hid-media-control` | 已合 main，可删 |
| `fix/cyw43-pins-rp2350b` | 板支持已合 main，可删 |
| `usb-cdc-debug-fixes` | CDC 调试实验，未合入 |
| 远程 | `origin` = `https://github.com/nightfall-anew/PicoW-usb2bt-audio.git` |

已删除的历史噪音：`fix/relax-cod-filter`（内容已进 main）、所有 `claude/*` 会话分支。

---

## 9. 给后续 Agent 的最短 checklist

1. 读本文件 + `git log -5 --oneline` + `git status`。  
2. 默认在 **`main`** 改；只推 `nightfall-anew` fork。  
3. 动 USB 描述符时保持 **Audio IAD 2 接口 + HID 独立**；核对 endpoint 不与 ISO 冲突。  
4. 动 AVRCP/HID 时避免 TinyUSB/BTstack HID 头文件混用。  
5. 改配对逻辑时区分 **双闪待机 vs 快闪扫描**，不要恢复「有 link key 就开机自动重连」除非同时处理失败回退。  
6. 出固件时写清 **板型**（`pico2_w` vs `waveshare_rp2350b_plus_w`）。  
7. 改完在目标板上验证：推流、主机音量、耳机暂停/上下曲、长按重配。

---

*文档对应 fork 演进至 2026-07 前后：HID 媒体键、配对/L2CAP/HCI 修复、Waveshare 板支持、LDAC HQ。有行为变更时请同步更新本节。*
