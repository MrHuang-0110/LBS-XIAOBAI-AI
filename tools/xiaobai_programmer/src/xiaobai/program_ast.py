"""Blockly 工作区 JSON → 经 schema 校验的内部 AST。

安全约束（硬性）：
- 只接受白名单块类型；未知块直接报错，绝不执行任意代码。
- 不生成、不 eval Python/JavaScript；工作区 JSON 只被遍历解析。
- 参数全部范围校验；循环次数/嵌套深度/语句总数设上限，防跑飞。
"""
from __future__ import annotations

import math
from dataclasses import dataclass, field
from typing import Any, Iterable

MAX_DEPTH = 8
MAX_STATEMENTS = 500
MAX_REPEAT = 1000
MAX_WAIT_SECONDS = 600.0
MAX_ACTION_SECONDS = 3600.0
MIN_ACTION_MS = 10
MAX_WHILE_ITERATIONS = 10000

MOTOR_FIELDS = {"LEFT": 0, "RIGHT": 1}
DIR_FIELDS = {"FORWARD": 1, "BACKWARD": 2}
MOVE_FIELDS = {"FORWARD": 1, "LEFT": 2, "RIGHT": 3, "BACKWARD": 4}
CHANNEL_FIELDS = {"LEFT": 0, "CENTER": 1, "RIGHT": 2}
CMP_FIELDS = {"GT": 0, "LT": 1}

_ACTION_TYPES = {
    "xiaobai_motor_time",
    "xiaobai_motor_run",
    "xiaobai_motor_stop",
    "xiaobai_motor_power",
    "xiaobai_move_time",
    "xiaobai_move_run",
    "xiaobai_move_stop",
    "xiaobai_move_power",
    "xiaobai_wait_ir",
    "xiaobai_show_eye",
    "xiaobai_show_num",
    "xiaobai_show_off",
    "xiaobai_play_voice",
    "xiaobai_wait_voice",
    "xiaobai_wait",
    "xiaobai_stop_program",
}

_ACTION_PREFIX = "xiaobai_"


class ProgramSchemaError(ValueError):
    """工作区不符合受控 schema。"""


@dataclass
class Program:
    """受控 AST：statements 为语句列表，可直接序列化进 .xbprog。"""

    statements: list[dict] = field(default_factory=list)

    def to_dict(self) -> dict:
        return {"format": "xiaobai-program-ast", "version": 1, "statements": self.statements}

    @classmethod
    def from_dict(cls, data: dict) -> "Program":
        if data.get("format") != "xiaobai-program-ast":
            raise ProgramSchemaError("不是小白程序 AST")
        stmts = data.get("statements")
        if not isinstance(stmts, list):
            raise ProgramSchemaError("statements 必须是列表")
        return cls(statements=stmts)

    def count_statements(self) -> int:
        def _count(items: list[dict]) -> int:
            total = 0
            for s in items:
                total += 1
                if isinstance(s.get("body"), list):
                    total += _count(s["body"])
                if isinstance(s.get("else"), list):
                    total += _count(s["else"])
            return total

        return _count(self.statements)


# ---------------------------------------------------------------- 基础解析


def _require_dict(value: Any, what: str) -> dict:
    if not isinstance(value, dict):
        raise ProgramSchemaError(f"{what} 必须是对象")
    return value


def _field(block: dict, name: str, allowed: dict, what: str) -> Any:
    raw = _require_dict(block.get("fields", {}), f"{what}.fields").get(name)
    if raw not in allowed:
        raise ProgramSchemaError(f"{what}: 字段 {name}={raw!r} 非法（可选 {sorted(allowed)}）")
    return allowed[raw]


def _num_input(block: dict, name: str, what: str, default: float | None = None) -> float:
    inputs = _require_dict(block.get("inputs", {}), f"{what}.inputs")
    port = inputs.get(name)
    if port is None:
        if default is not None:
            return default
        raise ProgramSchemaError(f"{what}: 缺少输入 {name}")
    inner = _require_dict(port.get("shadow") or port.get("block"), f"{what}.{name}")
    if inner.get("type") != "math_number":
        raise ProgramSchemaError(f"{what}: 输入 {name} 必须是数字")
    raw = _require_dict(inner.get("fields", {}), f"{what}.{name}.fields").get("NUM")
    try:
        value = float(str(raw).strip())
    except (TypeError, ValueError) as exc:
        raise ProgramSchemaError(f"{what}: 输入 {name} 不是数字：{raw!r}") from exc
    if not math.isfinite(value):
        raise ProgramSchemaError(f"{what}: 输入 {name} 非有限数")
    return value


def _child_block(port: dict | None, what: str) -> dict | None:
    """从输入端口（{"block": {...}} 或 {"shadow": {...}}）取内部块。"""
    if port is None:
        return None
    inner = port.get("block") or port.get("shadow")
    if inner is None:
        return None
    return _require_dict(inner, what)


def _extract_seconds(seconds: float, what: str) -> int:
    if seconds < MIN_ACTION_MS / 1000.0 or seconds > MAX_ACTION_SECONDS:
        raise ProgramSchemaError(
            f"{what}: 时长必须在 {MIN_ACTION_MS / 1000.0}-{MAX_ACTION_SECONDS} 秒"
        )
    return max(MIN_ACTION_MS, _round_int(seconds * 1000.0, what))


def _round_int(value: float, what: str) -> int:
    """四舍五入转 int；输入必须是有限数（NaN/inf 已在解析处拦截）。"""
    try:
        number = int(round(value))
    except (OverflowError, ValueError) as exc:
        raise ProgramSchemaError(f"{what}: 数值超出范围：{value!r}") from exc
    return number


def _extract_int(value: float, lo: int, hi: int, what: str) -> int:
    number = _round_int(value, what)
    if number < lo or number > hi:
        raise ProgramSchemaError(f"{what}: 数值必须在 {lo}-{hi}")
    return number


# ---------------------------------------------------------------- 表达式


def parse_expression(block: dict | None, depth: int) -> dict:
    if block is None:
        raise ProgramSchemaError("条件为空")
    if depth > MAX_DEPTH:
        raise ProgramSchemaError("表达式嵌套过深")

    btype = block.get("type")
    if btype == "math_number":
        return {"type": "num", "value": _num_input({"inputs": {"N": {"shadow": block}}}, "N", "数字")}
    if btype == "logic_boolean":
        value = _field(block, "BOOL", {"TRUE": True, "FALSE": False}, "布尔")
        return {"type": "bool", "value": bool(value)}
    if btype == "logic_negate":
        inner = _child_block(
            _require_dict(block.get("inputs", {}), "逻辑非.inputs").get("BOOL"), "逻辑非"
        )
        return {"type": "not", "operand": parse_expression(inner, depth + 1)}
    if btype in ("logic_operation",):
        op = _field(block, "OP", {"AND": "and", "OR": "or"}, "逻辑运算")
        inputs = _require_dict(block.get("inputs", {}), "逻辑运算.inputs")
        a = parse_expression(_child_block(inputs.get("A"), "逻辑运算.A"), depth + 1)
        b = parse_expression(_child_block(inputs.get("B"), "逻辑运算.B"), depth + 1)
        return {"type": op, "a": a, "b": b}
    if btype == "logic_compare":
        op = _field(
            block,
            "OP",
            {"EQ": "==", "NEQ": "!=", "LT": "<", "LTE": "<=", "GT": ">", "GTE": ">="},
            "比较",
        )
        inputs = _require_dict(block.get("inputs", {}), "比较.inputs")
        a = parse_expression(_child_block(inputs.get("A"), "比较.A"), depth + 1)
        b = parse_expression(_child_block(inputs.get("B"), "比较.B"), depth + 1)
        return {"type": "cmp", "op": op, "a": a, "b": b}
    if btype == "math_arithmetic":
        op = _field(
            block, "OP", {"ADD": "+", "MINUS": "-", "MULTIPLY": "*", "DIVIDE": "/"}, "算术"
        )
        inputs = _require_dict(block.get("inputs", {}), "算术.inputs")
        a = parse_expression(_child_block(inputs.get("A"), "算术.A"), depth + 1)
        b = parse_expression(_child_block(inputs.get("B"), "算术.B"), depth + 1)
        return {"type": "math", "op": op, "a": a, "b": b}
    if btype == "xiaobai_ir_value":
        channel = _field(block, "CHANNEL", CHANNEL_FIELDS, "红外读数")
        return {"type": "ir", "channel": channel}
    if btype == "xiaobai_battery":
        return {"type": "battery"}
    if btype == "xiaobai_device_mode":
        return {"type": "mode"}

    raise ProgramSchemaError(f"不支持的条件/表达式块：{btype!r}")


# ---------------------------------------------------------------- 语句


class _Parser:
    def __init__(self) -> None:
        self.total = 0

    def parse_chain(self, block: dict | None, depth: int) -> list[dict]:
        statements: list[dict] = []
        while block is not None:
            if depth > MAX_DEPTH:
                raise ProgramSchemaError("语句嵌套过深")
            self.total += 1
            if self.total > MAX_STATEMENTS:
                raise ProgramSchemaError(f"语句总数超过 {MAX_STATEMENTS}")
            statements.append(self.parse_statement(block, depth))
            block = _child_block(_require_dict(block.get("next", {}), "next"), "next")
        return statements

    def parse_statement(self, block: dict, depth: int) -> dict:
        btype = block.get("type")
        if btype in _ACTION_TYPES:
            return self._parse_action(block, btype, depth)
        if btype == "controls_repeat_ext":
            times = _extract_int(
                _num_input(block, "TIMES", "重复"), 0, MAX_REPEAT, "重复次数"
            )
            body = self._statement_body(block, "DO", depth)
            return {"type": "repeat", "times": times, "body": body}
        if btype == "controls_whileUntil":
            mode = _field(block, "MODE", {"WHILE": False, "UNTIL": True}, "循环")
            cond = parse_expression(
                _child_block(
                    _require_dict(block.get("inputs", {}), "循环.inputs").get("BOOL"), "循环"
                ),
                depth + 1,
            )
            body = self._statement_body(block, "DO", depth)
            return {"type": "while", "until": bool(mode), "cond": cond, "body": body}
        if btype == "controls_if":
            inputs = _require_dict(block.get("inputs", {}), "条件.inputs")
            cond = parse_expression(_child_block(inputs.get("IF0"), "条件.IF0"), depth + 1)
            then_body = self.parse_chain(_child_block(inputs.get("DO0"), "条件.DO0"), depth + 1)
            else_body = self.parse_chain(_child_block(inputs.get("ELSE"), "条件.ELSE"), depth + 1)
            return {"type": "if", "cond": cond, "then": then_body, "else": else_body}
        raise ProgramSchemaError(f"不支持的语句块：{btype!r}")

    def _statement_body(self, block: dict, name: str, depth: int) -> list[dict]:
        inputs = _require_dict(block.get("inputs", {}), f"{block.get('type')}.inputs")
        return self.parse_chain(_child_block(inputs.get(name), name), depth + 1)

    def _parse_action(self, block: dict, btype: str, depth: int) -> dict:
        base = btype[len(_ACTION_PREFIX):]
        if base == "motor_time":
            return {
                "type": "motor_time",
                "motor": _field(block, "MOTOR", MOTOR_FIELDS, "电机"),
                "dir": _field(block, "DIR", DIR_FIELDS, "方向"),
                "ms": _extract_seconds(_num_input(block, "SECONDS", "电机定时"), "电机定时"),
            }
        if base == "motor_run":
            return {
                "type": "motor_run",
                "motor": _field(block, "MOTOR", MOTOR_FIELDS, "电机"),
                "dir": _field(block, "DIR", DIR_FIELDS, "方向"),
            }
        if base == "motor_stop":
            return {"type": "motor_stop", "motor": _field(block, "MOTOR", MOTOR_FIELDS, "电机")}
        if base == "motor_power":
            level = _field(block, "LEVEL", {"1": 1, "2": 2, "3": 3}, "单电机功率")
            return {"type": "motor_power", "level": level}
        if base == "move_time":
            return {
                "type": "move_time",
                "move": _field(block, "MOVE", MOVE_FIELDS, "移动"),
                "ms": _extract_seconds(_num_input(block, "SECONDS", "组合定时"), "组合定时"),
            }
        if base == "move_run":
            return {"type": "move_run", "move": _field(block, "MOVE", MOVE_FIELDS, "移动")}
        if base == "move_stop":
            return {"type": "move_stop"}
        if base == "move_power":
            level = _field(block, "LEVEL", {"1": 1, "2": 2, "3": 3}, "组合功率")
            return {"type": "move_power", "level": level}
        if base == "wait_ir":
            return {
                "type": "wait_ir",
                "channel": _field(block, "CHANNEL", CHANNEL_FIELDS, "等待红外"),
                "cmp": _field(block, "CMP", CMP_FIELDS, "等待红外"),
                "threshold": _extract_int(
                    _num_input(block, "THRESHOLD", "等待红外"), 0, 100, "红外阈值"
                ),
            }
        if base == "show_eye":
            eye = _extract_int(_num_input(block, "EYE", "表情"), 1, 10, "表情编号")
            return {"type": "show_eye", "eye": eye}
        if base == "show_num":
            # 显示数字支持表达式（红外值/电量/模式等），运行时求值并限幅 0-100；
            # 字面量在解析期就校验范围，保持原有报错。
            inputs = _require_dict(block.get("inputs", {}), "显示数字.inputs")
            inner = _child_block(inputs.get("NUM"), "显示数字.NUM")
            if inner is None:
                raise ProgramSchemaError("显示数字: 缺少输入 NUM")
            expr = parse_expression(inner, depth + 1)
            if expr["type"] == "num":
                _extract_int(expr["value"], 0, 100, "显示数字")
            return {"type": "show_num", "value": expr}
        if base == "show_off":
            return {"type": "show_off"}
        if base == "play_voice":
            item = _extract_int(_num_input(block, "ITEM", "播放词条"), 1, 10, "词条编号")
            return {"type": "play_voice", "item": item}
        if base == "wait_voice":
            item = _extract_int(_num_input(block, "ITEM", "等待词条"), 1, 10, "词条编号")
            return {"type": "wait_voice", "item": item}
        if base == "wait":
            seconds = _num_input(block, "SECONDS", "本地等待")
            if not 0.0 <= seconds <= MAX_WAIT_SECONDS:
                raise ProgramSchemaError(f"本地等待必须在 0-{MAX_WAIT_SECONDS} 秒")
            return {"type": "wait", "seconds": seconds}
        if base == "stop_program":
            return {"type": "stop_program"}
        raise ProgramSchemaError(f"不支持的动作块：{btype!r}")


def parse_workspace(workspace_json: dict) -> Program:
    """把 Blockly 工作区 JSON 解析为受控 Program AST。"""
    if not isinstance(workspace_json, dict):
        raise ProgramSchemaError("工作区必须是 JSON 对象")
    blocks = workspace_json.get("blocks")
    if not isinstance(blocks, dict):
        raise ProgramSchemaError("工作区缺少 blocks（请用 Blockly serialization）")
    top = blocks.get("blocks")
    if top is None:
        top = []
    if not isinstance(top, list):
        raise ProgramSchemaError("blocks.blocks 必须是列表")

    parser = _Parser()
    statements: list[dict] = []
    for item in top:
        block = _require_dict(item, "顶层块")
        statements.extend(parser.parse_chain(block, 0))

    program = Program(statements=statements)
    if program.count_statements() > MAX_STATEMENTS:
        raise ProgramSchemaError(f"语句总数超过 {MAX_STATEMENTS}")
    return program
