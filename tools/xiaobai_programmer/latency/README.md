# 真机延时诊断记录（ECB02）

方法：Windows + Bleak，`tools/ble_probe.py --latency`，每场景 120 次 QUERY_STATUS，
`queue_wait` = 等写锁的软件排队（验收指标），`guard_wait` = 拿到锁后等待 30ms 收发保护窗，
`write` = `write_gatt_char(response=True)` 调用耗时，`rtt` = 提交到匹配 D2。
两轮使用同一主机、适配器、设备地址与固件，仅后台策略不同：

| 轮次 | 命令 | JSON |
| --- | --- | --- |
| legacy（优化前） | `--bg-policy legacy` | `legacy_v2.json` |
| adaptive（优化后） | `--bg-policy adaptive` | `adaptive_v2.json` |

## 2026-09-22 结果（设备 EC:B0:DA:20:2F:8F）

| 场景 | 心跳数(旧→新) | queue p50 | queue p95 | queue max | rtt p50 | rtt p95 | 丢帧 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| idle | 0→0 | 0→0 ms | 0→0 ms | 0→0 ms | 235→235 | 297→250 | 0 |
| heartbeat_300ms | 119→40 | 125→0 ms | 187→125 ms | 188→172 ms | 359→250 | 422→406 | 0 |
| poll_1s | 119→40 | 125→109 ms | 313→297 ms | 422→359 ms | 422→359 | 594→531 | 0 |
| mixed_200ms | 119→59 | 297→172 ms | 359→359 ms | 375→422 ms | 547→437 | 609→594 | 0 |

- 前台动作期间的心跳数下降约 57%–66%；纯心跳并发时 queue p50 从 125ms 降到 0ms。
- 全部 960 次前台采样 0 丢帧、0 次中途断连；保护间隔与带确认写保持不变。
- 本机 ECB02 链路的 `write_gatt_char(response=True)` 本身约 125–141ms（连接间隔/模块转发决定），
  因此「queue P95 ≤20ms」的绝对阈值在当前 BLE 栈下只有在无并发（idle）时可达；
  600ms 硬上限心跳与前台请求相遇时仍会排队一次在途写。该开销属于 BLE/模块层，
  不在本次「消除冗余后台竞争」的改动范围内。

复测命令：

```bat
tools\xiaobai_programmer\.venv\Scripts\python.exe tools\xiaobai_programmer\tools\ble_probe.py ^
  --address <设备地址> --latency --samples 120 --bg-policy adaptive ^
  --save tools\xiaobai_programmer\latency\adaptive_v2.json --label adaptive
```
