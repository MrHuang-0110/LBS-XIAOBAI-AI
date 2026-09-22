"""UI 轮询/会话策略测试：状态页之外零轮询、进页立即查一次且不重入。"""
from __future__ import annotations

from xiaobai.ui.policy import (
    SESSION_ENTER,
    SESSION_HEARTBEAT,
    SESSION_SKIP,
    STATUS_POLL_INTERVAL_MS,
    ProgramSessionPolicy,
    StatusPollPolicy,
)


def test_status_poll_only_on_status_tab() -> None:
    policy = StatusPollPolicy()
    assert policy.on_tab_changed("直接控制") is False
    assert policy.timer_interval_ms() == 0
    # 直接控制页不产生周期 QUERY_STATUS
    assert policy.should_query(
        connected=True, runner_running=False, query_pending=False
    ) is False

    # 进入状态页：立即查询一次 + 启动 1s 定时器
    assert policy.on_tab_changed("状态") is True
    assert policy.timer_interval_ms() == STATUS_POLL_INTERVAL_MS
    assert policy.should_query(
        connected=True, runner_running=False, query_pending=False
    ) is True

    # 同一页重复回调不再“立即查询”，但定时器仍按周期查询
    assert policy.on_tab_changed("状态") is False
    assert policy.should_query(
        connected=True, runner_running=False, query_pending=False
    ) is True

    # 离开状态页：停表且不再查询
    assert policy.on_tab_changed("Blockly 编程") is False
    assert policy.timer_interval_ms() == 0
    assert policy.should_query(
        connected=True, runner_running=False, query_pending=False
    ) is False


def test_status_poll_gates_connection_running_and_pending() -> None:
    policy = StatusPollPolicy()
    policy.on_tab_changed("状态")
    assert not policy.should_query(connected=False, runner_running=False, query_pending=False)
    assert not policy.should_query(connected=True, runner_running=True, query_pending=False)
    assert not policy.should_query(connected=True, runner_running=False, query_pending=True)
    assert policy.should_query(connected=True, runner_running=False, query_pending=False)


def test_program_session_single_enter_per_connection() -> None:
    policy = ProgramSessionPolicy()
    assert policy.decide(connected=False, aborted=False) == SESSION_SKIP
    assert policy.active is False                         # 未连接不占位
    assert policy.decide(connected=True, aborted=False) == SESSION_ENTER
    assert policy.decide(connected=True, aborted=False) == SESSION_HEARTBEAT
    assert policy.decide(connected=False, aborted=False) == SESSION_SKIP
    policy.on_enter_failed()
    assert policy.decide(connected=True, aborted=False) == SESSION_ENTER  # 发送失败可重试
    policy.reset()
    assert policy.decide(connected=True, aborted=False) == SESSION_ENTER  # 断连/切遥控后重建


def test_program_session_reenters_after_stop_or_abort() -> None:
    policy = ProgramSessionPolicy()
    assert policy.decide(connected=True, aborted=False) == SESSION_ENTER
    assert policy.decide(connected=True, aborted=False) == SESSION_HEARTBEAT
    # 停止程序/设备中止后：设备可能已退回旧模式，下次动作必须重新 ENTER_PROGRAM
    assert policy.decide(connected=True, aborted=True) == SESSION_ENTER
    assert policy.decide(connected=True, aborted=False) == SESSION_HEARTBEAT
