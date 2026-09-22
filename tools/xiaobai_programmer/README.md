# 小白编程上位机（Windows Blockly 测试工具）

用离线 Blockly 拖积木编程，通过 BLE 把动作下发给小白主控执行；循环/条件/等待由本机执行。

> 协议、指令全集、积木映射与**发送周期/超时**详见 [`docs/上位机编程协议与用法.md`](../../docs/上位机编程协议与用法.md)。

## 环境

- Windows 10/11，**Python 3.11**（安装时勾选 *Add python.exe to PATH*）
- 首次运行需联网装依赖（PySide6、bleak）；国内网络可先运行
  `py -3.11 -m pip install -i https://pypi.tuna.tsinghua.edu.cn/simple -r requirements.txt`

## 启动

**方式 A（推荐）**：双击 `run_dev.bat`。它会自动建 `.venv`、装依赖、启动界面；失败会停住并显示原因。

**方式 B（手动）**：
```bat
cd /d E:\LBS-XIAOBAI-AI\tools\xiaobai_programmer
py -3.11 -m venv .venv
.venv\Scripts\python.exe -m pip install -r requirements.txt
set PYTHONPATH=%CD%\src
.venv\Scripts\python.exe -m xiaobai.ui.app
```

**排错**：如果双击闪退，先开一个 cmd 窗口再运行 `run_dev.bat`，报错就会留在窗口里。
最常见两种情况：① 没装 Python 3.11；② 依赖安装被网络拦截（用上面的清华源）。

## 使用

| 页面 | 说明 |
|---|---|
| 设备连接 | 扫描 BLE → 选 `Spark_AI` → 连接；首次连接自动发现并记住读写 UUID。不会连真机时点 **离线演示（模拟设备）** |
| 直接控制 | 单电机正反转/停、组合移动、功率档、关闭显示、**停止程序**（始终可用）；切到本页自动进入编程模式并启动心跳 |
| Blockly 编程 | 拖积木 → 运行/停止；切到本页会自动发 `ENTER_PROGRAM`（设备四灯跑马、播 ID 52） |
| 状态 | 只在本页可见时轮询：进页立即查一次，之后每 1s 刷新模式/三路红外/电量/任务/故障 |
| 协议日志 | 每条 C2/D2/D3 的收发记录，排障用 |

- 空闲时每 300ms 发一次心跳；前台动作刚发出时跳过冗余心跳，但显式心跳最长不超过 600ms（兼容旧固件 1s 超时）。
- 写队列按优先级调度：按钮/动作/停止（前台）插到心跳与状态查询（后台）之前；设备丢首包时会 200ms 重发 DONE，不会卡流程。
- 切页不会重复发 `ENTER_PROGRAM`；断开/重连会重建会话并清理心跳任务。
- 工程保存/加载为 `.xbprog`（内含 Blockly 工作区 + 校验后的 AST，加载时会重新校验）。
- 关闭编程页/断开连接：设备 1s 心跳超时后自动刹停并退出编程模式。

## 测试

```bat
cd /d E:\LBS-XIAOBAI-AI\tools\xiaobai_programmer
.venv\Scripts\python.exe -m pip install pytest
.venv\Scripts\python.exe -m pytest        # 61 项：协议黄金帧/AST/执行器/传输调度/工程读写/UI 策略/探针
```

## 打包 EXE（无 Python 的机器可直接运行）

```bat
.venv\Scripts\python.exe -m pip install pyinstaller
.venv\Scripts\pyinstaller.exe xiaobai_programmer.spec
```
产物：`dist\XiaoBaiProgrammer.exe`（离线 Blockly 已内嵌）。

## 目录

```
src/xiaobai/protocol.py    BLE 协议 v2 编解码 + 黄金帧
src/xiaobai/transport.py   bleak 传输 + UUID 持久化（FakeTransport 供测试）
src/xiaobai/executor.py    心跳 / DONE 等待 / 本地循环条件 / 事件处理
src/xiaobai/program_ast.py Blockly 工作区 → 受控 AST（白名单，无 eval）
src/xiaobai/simulator.py   模拟 MCU（测试与离线演示共用）
src/xiaobai/ui/            PySide6 界面 + QWebChannel 桥 + 轮询/会话策略（policy.py）
src/xiaobai/resources/     离线 Blockly 11.2 + 自定义积木（blocks.js/app.js）
tests/                     pytest（61 项）
```

## 排障工具：无界面 BLE 探针

`tools\ble_probe.py`（需要 Windows Python + `pip install bleak`），不依赖 PySide6：

```bat
cd /d E:\LBS-XIAOBAI-AI\tools\xiaobai_programmer
python tools\ble_probe.py                    # 扫描名字含 Spark 并连接，发 ENTER_PROGRAM + 心跳
python tools\ble_probe.py --seconds 12       # 观察收到的 D2/D3 帧
python tools\ble_probe.py --address AA:BB:.. # 直连指定地址
python tools\ble_probe.py --hunt 8           # 猎手模式：试探信号最强的 8 个设备，谁能按协议回帧就锁定
python tools\ble_probe.py --latency --samples 200 --save baseline.json --label baseline
                                             # 延时诊断：空闲/心跳/轮询/混合四场景统计（无电机动作）
python tools\ble_probe.py --latency --bg-policy legacy   # 固定 300ms 心跳（优化前基线）
python tools\ble_probe.py --latency --bg-policy adaptive # 前台发过则跳过冗余心跳（默认，新策略）
```

用途：判定写特征是否正确、设备是否在广播、固件是否为含 BLE v2 的版本。

`--latency` 统计每次前台 `QUERY_STATUS` 的写前排队、写入耗时与 D2 往返时间（min/P50/P95/max），
并给出 `P95≤20ms 且 max≤40ms` 的判定；前后两次运行用同一主机/适配器/位置，JSON 可直接对比。
