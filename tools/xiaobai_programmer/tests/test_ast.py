"""Blockly 工作区 → 受控 AST 的解析/校验测试（无 eval 白名单）。"""
from __future__ import annotations

import pytest  # pyright: ignore[reportMissingImports]  -- 运行环境已安装，分析器解析不到 user-site

from xiaobai.program_ast import (
    MAX_STATEMENTS,
    Program,
    ProgramSchemaError,
    parse_workspace,
)


def num(value) -> dict:
    return {"block": {"type": "math_number", "fields": {"NUM": str(value)}}}


def stmt(btype: str, fields: dict | None = None, inputs: dict | None = None,
         next_block: dict | None = None) -> dict:
    block: dict = {"type": btype}
    if fields:
        block["fields"] = fields
    if inputs:
        block["inputs"] = inputs
    if next_block:
        block["next"] = {"block": next_block}
    return block


def workspace(*blocks: dict) -> dict:
    return {"blocks": {"languageVersion": 0, "blocks": list(blocks)}}



def test_motor_and_move_actions():
    program = parse_workspace(workspace(
        stmt("xiaobai_motor_time", {"MOTOR": "LEFT", "DIR": "FORWARD"}, {"SECONDS": num(2.5)}),
        stmt("xiaobai_move_time", {"MOVE": "BACKWARD"}, {"SECONDS": num(0.01)}),
        stmt("xiaobai_motor_power", {"LEVEL": "2"}),
        stmt("xiaobai_move_stop"),
    ))
    assert program.statements[0] == {
        "type": "motor_time", "motor": 0, "dir": 1, "ms": 2500,
    }
    assert program.statements[1] == {"type": "move_time", "move": 4, "ms": 10}
    assert program.statements[2] == {"type": "motor_power", "level": 2}
    assert program.statements[3] == {"type": "move_stop"}


def test_display_and_voice_actions():
    program = parse_workspace(workspace(
        stmt("xiaobai_show_eye", {}, {"EYE": num(10)}),
        stmt("xiaobai_show_num", {}, {"NUM": num(100)}),
        stmt("xiaobai_show_off"),
        stmt("xiaobai_play_voice", {}, {"ITEM": num(3)}),
        stmt("xiaobai_wait_voice", {}, {"ITEM": num(1)}),
        stmt("xiaobai_wait_ir", {"CHANNEL": "RIGHT", "CMP": "LT"}, {"THRESHOLD": num(50)}),
        stmt("xiaobai_wait", {}, {"SECONDS": num(1.5)}),
    ))
    types = [s["type"] for s in program.statements]
    assert types == [
        "show_eye", "show_num", "show_off", "play_voice", "wait_voice", "wait_ir", "wait",
    ]
    assert program.statements[5] == {
        "type": "wait_ir", "channel": 2, "cmp": 1, "threshold": 50,
    }
    assert program.statements[6]["seconds"] == 1.5


def test_repeat_and_next_chain():
    body = stmt("xiaobai_show_off", next_block=stmt("xiaobai_play_voice", {}, {"ITEM": num(1)}))
    program = parse_workspace(workspace(
        stmt("controls_repeat_ext", {}, {"TIMES": num(3), "DO": {"block": body}}),
    ))
    assert program.statements[0]["type"] == "repeat"
    assert program.statements[0]["times"] == 3
    assert [s["type"] for s in program.statements[0]["body"]] == ["show_off", "play_voice"]


def test_if_with_ir_condition():
    cond = {
        "block": {
            "type": "logic_compare",
            "fields": {"OP": "GT"},
            "inputs": {
                "A": {"block": {"type": "xiaobai_ir_value", "fields": {"CHANNEL": "CENTER"}}},
                "B": num(30),
            },
        }
    }
    program = parse_workspace(workspace(
        stmt(
            "controls_if",
            {},
            {
                "IF0": cond,
                "DO0": {"block": stmt("xiaobai_move_run", {"MOVE": "FORWARD"})},
                "ELSE": {"block": stmt("xiaobai_move_stop")},
            },
        )
    ))
    st = program.statements[0]
    assert st["type"] == "if"
    assert st["cond"]["op"] == ">"
    assert st["cond"]["a"] == {"type": "ir", "channel": 1}
    assert st["then"][0]["type"] == "move_run"
    assert st["else"][0]["type"] == "move_stop"


def test_while_until_boolean():
    program = parse_workspace(workspace(
        stmt("controls_whileUntil", {"MODE": "UNTIL"},
             {"BOOL": {"block": {"type": "logic_boolean", "fields": {"BOOL": "TRUE"}}},
              "DO": {"block": stmt("xiaobai_show_off")}}),
    ))
    st = program.statements[0]
    assert st["type"] == "while" and st["until"] is True
    assert st["cond"] == {"type": "bool", "value": True}


@pytest.mark.parametrize("block, message", [
    (stmt("xiaobai_unknown_block"), "不支持"),
    (stmt("xiaobai_show_num", {}, {"NUM": num(101)}), "0-100"),
    (stmt("xiaobai_show_eye", {}, {"EYE": num(11)}), "1-10"),
    (stmt("xiaobai_wait_ir", {"CHANNEL": "CENTER", "CMP": "GT"}, {"THRESHOLD": num(101)}), "0-100"),
    (stmt("xiaobai_motor_time", {"MOTOR": "LEFT", "DIR": "FORWARD"}, {"SECONDS": num(0)}), "时长"),
    (stmt("xiaobai_motor_time", {"MOTOR": "MIDDLE", "DIR": "FORWARD"},
          {"SECONDS": num(1)}), "非法"),
    (stmt("xiaobai_wait", {}, {"SECONDS": num(-1)}), "0-600"),
])
def test_invalid_parameters_rejected(block, message):
    with pytest.raises(ProgramSchemaError) as excinfo:
        parse_workspace(workspace(block))
    assert message in str(excinfo.value)


def test_statement_count_limit():
    chain = stmt("xiaobai_show_off")
    for _ in range(MAX_STATEMENTS + 10):
        chain = stmt("xiaobai_show_off", next_block=chain)
    with pytest.raises(ProgramSchemaError):
        parse_workspace(workspace(chain))


def test_program_roundtrip_dict():
    program = Program(statements=[{"type": "show_off"}])
    restored = Program.from_dict(program.to_dict())
    assert restored.statements == program.statements
