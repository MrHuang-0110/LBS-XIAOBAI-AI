/* Blockly 页面：工具箱 + 运行/停止/保存/加载 与 QWebChannel 桥。 */
(() => {
  const TOOLBOX = {
    kind: "categoryToolbox",
    contents: [
      { kind: "category", name: "电机", colour: "230",
        contents: ["xiaobai_motor_time", "xiaobai_motor_run", "xiaobai_motor_stop",
                   "xiaobai_motor_power", "xiaobai_move_time", "xiaobai_move_run",
                   "xiaobai_move_stop", "xiaobai_move_power",
                   { kind: "label", text: "功率/停止类无需等待完成" }]
          .map((t) => (typeof t === "string" ? { kind: "block", type: t } : t)) },
      { kind: "category", name: "感应", colour: "120",
        contents: [{ kind: "block", type: "xiaobai_wait_ir" },
                   { kind: "block", type: "xiaobai_ir_value" },
                   { kind: "block", type: "xiaobai_battery" },
                   { kind: "block", type: "xiaobai_device_mode" }] },
      { kind: "category", name: "显示/语音", colour: "210",
        contents: [{ kind: "block", type: "xiaobai_show_eye" },
                   { kind: "block", type: "xiaobai_show_num" },
                   { kind: "block", type: "xiaobai_show_off" },
                   { kind: "block", type: "xiaobai_play_voice" },
                   { kind: "block", type: "xiaobai_wait_voice" }] },
      { kind: "category", name: "控制", colour: "330",
        contents: [{ kind: "block", type: "controls_repeat_ext" },
                   { kind: "block", type: "controls_if" },
                   { kind: "block", type: "controls_whileUntil" },
                   { kind: "block", type: "xiaobai_wait" },
                   { kind: "block", type: "xiaobai_stop_program" }] },
      { kind: "category", name: "逻辑/数学", colour: "290",
        contents: [{ kind: "block", type: "logic_compare" },
                   { kind: "block", type: "logic_operation" },
                   { kind: "block", type: "logic_negate" },
                   { kind: "block", type: "logic_boolean" },
                   { kind: "block", type: "math_number" },
                   { kind: "block", type: "math_arithmetic" }] },
    ],
  };

  const workspace = Blockly.inject("blocklyDiv", {
    toolbox: TOOLBOX,
    grid: { spacing: 20, length: 3, colour: "#ddd", snap: true },
    zoom: { controls: true, wheel: true, startScale: 0.9 },
    trashcan: true,
  });
  window.xiaobaiWorkspace = workspace;

  let bridge = null;
  new QWebChannel(qt.webChannelTransport, (channel) => {
    bridge = channel.objects.bridge;
    document.getElementById("status").textContent = "已就绪（" + bridge.protocol_version() + "）";
  });

  window.xiaobaiRun = () => {
    if (!bridge) { alert("桥未就绪"); return; }
    const json = JSON.stringify(Blockly.serialization.workspaces.save(workspace));
    bridge.run_workspace(json);
  };
  window.xiaobaiLoad = (workspaceJson) => {
    let data = workspaceJson;
    if (typeof workspaceJson === "string") {
      try {
        data = JSON.parse(workspaceJson);
      } catch (err) {
        alert("工程数据不是合法 JSON：" + err.message);
        return;
      }
    }
    Blockly.serialization.workspaces.load(data, workspace);
  };
  window.xiaobaiStop = () => { if (bridge) bridge.stop_program(); };
})();
