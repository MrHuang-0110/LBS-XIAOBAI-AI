"""UI 轮询/会话策略：纯逻辑，不依赖 Qt，便于无界面测试。

直接控制按钮的响应时间受后台流量影响：
- 状态查询只在“状态”页可见时周期发送，其它页面零轮询；
- 同一连接内只发送一次 ENTER_PROGRAM + 一次心跳启动，避免切页重复建会话。
"""
from __future__ import annotations

STATUS_TAB_TITLE = "状态"
PROGRAM_TAB_TITLES = ("直接控制", "Blockly 编程")
STATUS_POLL_INTERVAL_MS = 1000


class StatusPollPolicy:
    """状态页轮询判定：页面可见性 + 查询不重入。"""

    def __init__(self) -> None:
        self.page_title = ""

    def on_tab_changed(self, title: str) -> bool:
        """记录当前页；返回是否刚进入“状态”页（应立刻查询一次）。"""
        entered = title == STATUS_TAB_TITLE and self.page_title != STATUS_TAB_TITLE
        self.page_title = title
        return entered

    def timer_interval_ms(self) -> int:
        """定时器周期；非状态页返回 0（应停止定时器）。"""
        return STATUS_POLL_INTERVAL_MS if self.page_title == STATUS_TAB_TITLE else 0

    def should_query(self, *, connected: bool, runner_running: bool,
                     query_pending: bool) -> bool:
        """只有状态页可见、已连接、无程序运行且上一条查询已回时才发查询。"""
        return (
            self.page_title == STATUS_TAB_TITLE
            and connected
            and not runner_running
            and not query_pending
        )


SESSION_ENTER = "enter"          # 需要发送 ENTER_PROGRAM 建会话
SESSION_HEARTBEAT = "heartbeat"  # 会话已建立，只需确保心跳在跑
SESSION_SKIP = "skip"            # 未连接，什么都不做


class ProgramSessionPolicy:
    """编程会话建立判定：同一连接内只发一次 ENTER_PROGRAM。

    停止程序/设备中止（aborted）后，下次前台动作必须重新建会话，
    否则设备已退回旧模式，动作会被静默忽略。"""

    def __init__(self) -> None:
        self.active = False

    def decide(self, *, connected: bool, aborted: bool) -> str:
        if not connected:
            return SESSION_SKIP
        if self.active:
            if aborted:
                self.active = False          # 会话已失效，落下去重建
            else:
                return SESSION_HEARTBEAT
        self.active = True                   # 先占位，防止并发重复 ENTER_PROGRAM
        return SESSION_ENTER

    def on_enter_failed(self) -> None:
        self.active = False

    def reset(self) -> None:
        self.active = False
