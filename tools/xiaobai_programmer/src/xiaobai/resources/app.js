/* Blockly 页面：工具箱 + 运行/停止/加载 与 QWebChannel 桥（离线，无 CDN）。 */
(() => {
  const err = (message) => {
    const el = document.getElementById("err");
    if (el) {
      el.style.display = "block";
      el.textContent = "页面错误：" + message;
    }
  };

  const num = (value) => ({ shadow: { type: "math_number", fields: { NUM: value } } });
  const blk = (type, inputs, fields) => {
    const item = { kind: "block", type };
    if (inputs) item.inputs = inputs;
    if (fields) item.fields = fields;
    return item;
  };

  const TOOLBOX = {
    kind: "categoryToolbox",
    contents: [
      {
        kind: "category",
        name: "电机",
        colour: "230",
        contents: [
          blk("xiaobai_motor_time", { SECONDS: num(2) }),
          blk("xiaobai_motor_run"),
          blk("xiaobai_motor_stop"),
          blk("xiaobai_motor_power"),
          blk("xiaobai_move_time", { SECONDS: num(2) }),
          blk("xiaobai_move_run"),
          blk("xiaobai_move_stop"),
          blk("xiaobai_move_power"),
        ],
      },
      {
        kind: "category",
        name: "感应",
        colour: "120",
        contents: [
          blk("xiaobai_wait_ir", { THRESHOLD: num(30) }),
          blk("xiaobai_ir_value"),
          blk("xiaobai_battery"),
          blk("xiaobai_device_mode"),
        ],
      },
      {
        kind: "category",
        name: "显示/语音",
        colour: "210",
        contents: [
          blk("xiaobai_show_eye", { EYE: num(1) }),
          blk("xiaobai_show_num", { NUM: num(0) }),
          blk("xiaobai_show_off"),
          blk("xiaobai_play_voice", { ITEM: num(1) }),
          blk("xiaobai_wait_voice", { ITEM: num(1) }),
        ],
      },
      {
        kind: "category",
        name: "控制",
        colour: "330",
        contents: [
          blk("controls_repeat_ext", { TIMES: num(3) }),
          blk("controls_if"),
          blk("controls_whileUntil"),
          blk("xiaobai_wait", { SECONDS: num(1) }),
          blk("xiaobai_stop_program"),
        ],
      },
      {
        kind: "category",
        name: "逻辑/数学",
        colour: "290",
        contents: [
          blk("logic_compare", { A: num(0), B: num(0) }),
          blk("logic_operation"),
          blk("logic_negate"),
          blk("logic_boolean", null, { BOOL: "TRUE" }),
          blk("math_number", null, { NUM: 1 }),
          blk("math_arithmetic", { A: num(1), B: num(1) }),
        ],
      },
    ],
  };

  let workspace = null;
  try {
    workspace = Blockly.inject("blocklyDiv", {
      toolbox: TOOLBOX,
      grid: { spacing: 20, length: 3, colour: "#ddd", snap: true },
      zoom: { controls: true, wheel: true, startScale: 0.9 },
      trashcan: true,
    });
  } catch (e) {
    err("Blockly 初始化失败：" + e.message);
    return;
  }
  window.xiaobaiWorkspace = workspace;

  let bridge = null;
  if (typeof QWebChannel === "function" && window.qt && qt.webChannelTransport) {
    new QWebChannel(qt.webChannelTransport, (channel) => {
      bridge = channel.objects.bridge;
      const status = document.getElementById("status");
      if (status) status.textContent = "已就绪（" + bridge.protocol_version() + "）";
    });
  } else {
    const status = document.getElementById("status");
    if (status) status.textContent = "页面已加载，但与上位机的桥未连接";
  }

  window.xiaobaiRun = () => {
    if (!bridge) {
      err("与上位机的桥未连接，无法运行");
      return;
    }
    const json = JSON.stringify(Blockly.serialization.workspaces.save(workspace));
    bridge.run_workspace(json);
  };

  window.xiaobaiLoad = (workspaceJson) => {
    let data = workspaceJson;
    if (typeof workspaceJson === "string") {
      try {
        data = JSON.parse(workspaceJson);
      } catch (e) {
        err("工程数据不是合法 JSON：" + e.message);
        return;
      }
    }
    Blockly.serialization.workspaces.load(data, workspace);
  };

  window.xiaobaiStop = () => {
    if (bridge) bridge.stop_program();
  };

  window.xiaobaiClear = () => {
    workspace.clear();
  };
})();
