"""工程文件 .xbprog 读写（工作区 + 受控 AST 一起保存）。

文件是纯 JSON，便于审阅/版本管理：
{
  "format": "xiaobai-program",
  "version": 1,
  "name": "示例",
  "saved_at": "2026-09-16T12:00:00",
  "workspace": { ...Blockly serialization... },
  "program": { "format": "xiaobai-program-ast", ... }
}
加载时会重新解析工作区，拒绝被手工篡改/夹带未知块的 AST。
"""
from __future__ import annotations

import json
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from typing import Any

from xiaobai.program_ast import Program, ProgramSchemaError, parse_workspace

FORMAT = "xiaobai-program"
VERSION = 1


class ProjectFileError(ValueError):
    """工程文件缺失/格式非法。"""


@dataclass
class Project:
    name: str
    workspace: dict
    program: Program

    def to_dict(self) -> dict:
        return {
            "format": FORMAT,
            "version": VERSION,
            "name": self.name,
            "saved_at": datetime.now().isoformat(timespec="seconds"),
            "workspace": self.workspace,
            "program": self.program.to_dict(),
        }


def save_project(path: Path, name: str, workspace: dict, program: Program) -> None:
    """把工作区与 AST 写入 .xbprog。"""
    if not isinstance(workspace, dict):
        raise ProjectFileError("workspace 必须是 Blockly 序列化对象")
    project = Project(name=name, workspace=workspace, program=program)
    try:
        path.write_text(
            json.dumps(project.to_dict(), ensure_ascii=False, indent=2), encoding="utf-8"
        )
    except OSError as exc:
        raise ProjectFileError(f"无法写入 {path}：{exc}") from exc


def load_project(path: Path) -> Project:
    """读取 .xbprog，重新校验工作区与 AST。"""
    try:
        raw = path.read_text(encoding="utf-8")
    except OSError as exc:
        raise ProjectFileError(f"无法读取 {path}：{exc}") from exc
    try:
        data: Any = json.loads(raw)
    except json.JSONDecodeError as exc:
        raise ProjectFileError(f"{path} 不是合法 JSON：{exc}") from exc

    if not isinstance(data, dict) or data.get("format") != FORMAT:
        raise ProjectFileError(f"{path} 不是小白工程文件（format != {FORMAT}）")
    if data.get("version") != VERSION:
        raise ProjectFileError(f"工程版本不支持：{data.get('version')!r}（期望 {VERSION}）")

    workspace = data.get("workspace")
    if not isinstance(workspace, dict):
        raise ProjectFileError("工程缺少 workspace")

    # 以工作区为权威重新解析：AST 被篡改时直接拒绝
    try:
        program = parse_workspace(workspace)
    except ProgramSchemaError as exc:
        raise ProjectFileError(f"工作区校验失败：{exc}") from exc

    return Project(name=str(data.get("name", path.stem)), workspace=workspace, program=program)
