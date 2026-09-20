"""工程文件 .xbprog 保存/加载测试。"""
from __future__ import annotations

import json
from pathlib import Path

import pytest  # pyright: ignore[reportMissingImports]  -- 运行环境已安装，分析器解析不到 user-site

from xiaobai.program_ast import parse_workspace
from xiaobai.project_io import (
    FORMAT,
    ProjectFileError,
    load_project,
    save_project,
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



def _program():
    return parse_workspace(workspace(
        stmt("xiaobai_motor_time", {"MOTOR": "RIGHT", "DIR": "BACKWARD"},
             {"SECONDS": num(0.5)}),
        stmt("xiaobai_show_num", {}, {"NUM": num(7)}),
    ))


def test_save_load_roundtrip(tmp_path: Path):
    path = tmp_path / "demo.xbprog"
    program = _program()
    save_project(path, "示例", workspace(
        stmt("xiaobai_motor_time", {"MOTOR": "RIGHT", "DIR": "BACKWARD"},
             {"SECONDS": num(0.5)}),
        stmt("xiaobai_show_num", {}, {"NUM": num(7)}),
    ), program)

    loaded = load_project(path)
    assert loaded.name == "示例"
    assert loaded.program.statements == program.statements
    data = json.loads(path.read_text(encoding="utf-8"))
    assert data["format"] == FORMAT and data["version"] == 1


def test_load_rejects_wrong_format(tmp_path: Path):
    path = tmp_path / "bad.xbprog"
    path.write_text(json.dumps({"format": "other", "version": 1}), encoding="utf-8")
    with pytest.raises(ProjectFileError):
        load_project(path)


def test_load_rejects_bad_workspace(tmp_path: Path):
    path = tmp_path / "bad2.xbprog"
    path.write_text(
        json.dumps({
            "format": FORMAT,
            "version": 1,
            "name": "x",
            "workspace": {"blocks": {"blocks": [{"type": "xiaobai_motor_time"}]}},
        }),
        encoding="utf-8",
    )
    with pytest.raises(ProjectFileError):
        load_project(path)


def test_load_missing_file(tmp_path: Path):
    with pytest.raises(ProjectFileError):
        load_project(tmp_path / "nope.xbprog")
