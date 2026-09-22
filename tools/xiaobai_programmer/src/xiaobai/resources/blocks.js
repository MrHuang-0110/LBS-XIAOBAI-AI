/* 小白编程自定义积木：使用 Blockly 官方 JSON 定义（defineBlocksWithJsonArray）。
 * 类型必须与 src/xiaobai/program_ast.py 的受控白名单一一对应，改一处要同步另一处。
 * 依赖：blockly_compressed.js + blocks_compressed.js + zh-hans.js（全部本地离线）。 */
(() => {
  const defineBlocks =
    (Blockly.common && Blockly.common.defineBlocksWithJsonArray) ||
    Blockly.defineBlocksWithJsonArray;

  const DIR = [
    ["正转", "FORWARD"],
    ["反转", "BACKWARD"],
  ];
  const MOTOR = [
    ["左", "LEFT"],
    ["右", "RIGHT"],
  ];
  const MOVE = [
    ["前进", "FORWARD"],
    ["左转", "LEFT"],
    ["右转", "RIGHT"],
    ["后退", "BACKWARD"],
  ];
  const CHANNEL = [
    ["左", "LEFT"],
    ["中", "CENTER"],
    ["右", "RIGHT"],
  ];
  const CMP = [
    ["大于", "GT"],
    ["小于", "LT"],
  ];
  const LEVEL = [
    ["1", "1"],
    ["2", "2"],
    ["3", "3"],
  ];

  const statement = { previousStatement: null, nextStatement: null };

  defineBlocks([
    // ================= 单电机 =================
    {
      type: "xiaobai_motor_time",
      message0: "电机 %1 %2 转动 %3 秒",
      args0: [
        { type: "field_dropdown", name: "MOTOR", options: MOTOR },
        { type: "field_dropdown", name: "DIR", options: DIR },
        { type: "input_value", name: "SECONDS", check: "Number" },
      ],
      ...statement,
      colour: 230,
      tooltip: "指定电机转动，设备计时，到时刹停并回报完成",
    },
    {
      type: "xiaobai_motor_run",
      message0: "电机 %1 持续 %2",
      args0: [
        { type: "field_dropdown", name: "MOTOR", options: MOTOR },
        { type: "field_dropdown", name: "DIR", options: DIR },
      ],
      ...statement,
      colour: 230,
      tooltip: "指定电机持续转动（无需回报），下一条单电机指令只影响该电机",
    },
    {
      type: "xiaobai_motor_stop",
      message0: "刹停 %1 电机",
      args0: [{ type: "field_dropdown", name: "MOTOR", options: MOTOR }],
      ...statement,
      colour: 230,
      tooltip: "短刹指定电机（双输入高），随后滑行",
    },
    {
      type: "xiaobai_motor_power",
      message0: "单电机功率设为 %1 档",
      args0: [{ type: "field_dropdown", name: "LEVEL", options: LEVEL }],
      ...statement,
      colour: 230,
      tooltip: "1/2/3 档 = 40% / 70% / 100%，默认 3 档",
    },
    // ================= 组合电机 =================
    {
      type: "xiaobai_move_time",
      message0: "%1 %2 秒",
      args0: [
        { type: "field_dropdown", name: "MOVE", options: MOVE },
        { type: "input_value", name: "SECONDS", check: "Number" },
      ],
      ...statement,
      colour: 230,
      tooltip: "组合动作，设备计时，到时刹停双电机并回报完成",
    },
    {
      type: "xiaobai_move_run",
      message0: "持续 %1",
      args0: [{ type: "field_dropdown", name: "MOVE", options: MOVE }],
      ...statement,
      colour: 230,
      tooltip: "按方向持续移动（无需回报）",
    },
    {
      type: "xiaobai_move_stop",
      message0: "刹停双电机",
      ...statement,
      colour: 230,
      tooltip: "短刹左、右两个电机",
    },
    {
      type: "xiaobai_move_power",
      message0: "组合电机功率设为 %1 档",
      args0: [{ type: "field_dropdown", name: "LEVEL", options: LEVEL }],
      ...statement,
      colour: 230,
      tooltip: "1/2/3 档 = 40% / 70% / 100%，默认 3 档",
    },
    // ================= 感应 =================
    {
      type: "xiaobai_wait_ir",
      message0: "等待 %1 红外值 %2 %3",
      args0: [
        { type: "field_dropdown", name: "CHANNEL", options: CHANNEL },
        { type: "field_dropdown", name: "CMP", options: CMP },
        { type: "input_value", name: "THRESHOLD", check: "Number" },
      ],
      ...statement,
      colour: 120,
      tooltip: "0=远/反射弱，100=近/反射强；设备连续 3 次满足后回报完成",
    },
    {
      type: "xiaobai_ir_value",
      message0: "%1 红外值",
      args0: [{ type: "field_dropdown", name: "CHANNEL", options: CHANNEL }],
      output: "Number",
      colour: 120,
      tooltip: "读取指定通道当前红外值（0–100），用于条件判断",
    },
    {
      type: "xiaobai_battery",
      message0: "电量（mV）",
      output: "Number",
      colour: 120,
      tooltip: "查询电池电压，单位 mV",
    },
    {
      type: "xiaobai_device_mode",
      message0: "设备模式号",
      output: "Number",
      colour: 120,
      tooltip: "0 语音 / 1 动力 / 2 感应 / 3 遥控 / 4 编程",
    },
    // ================= 显示 / 语音 =================
    {
      type: "xiaobai_show_eye",
      message0: "显示表情 EYE_%1",
      args0: [{ type: "input_value", name: "EYE", check: "Number" }],
      ...statement,
      colour: 210,
      tooltip:
        "1=待机 2=开心 3=生气 4=伤心 5=惊讶 6=眨眼 7=喜欢 8=晕眩 9=困倦 10=好奇",
    },
    {
      type: "xiaobai_show_num",
      message0: "显示数字 %1",
      args0: [{ type: "input_value", name: "NUM", check: "Number" }],
      ...statement,
      colour: 210,
      tooltip: "显示 0–100（可接红外值/电量/模式等数值，超出范围按 0/100 限幅）；0–99 补零成两位，左右眼居中",
    },
    {
      type: "xiaobai_show_off",
      message0: "关闭显示",
      ...statement,
      colour: 210,
      tooltip: "关闭当前表情或数字显示",
    },
    {
      type: "xiaobai_play_voice",
      message0: "播放词条 P%1",
      args0: [{ type: "input_value", name: "ITEM", check: "Number" }],
      ...statement,
      colour: 210,
      tooltip: "播放 P01–P10 预置语音",
    },
    {
      type: "xiaobai_wait_voice",
      message0: "等待词条 ASR_%1",
      args0: [{ type: "input_value", name: "ITEM", check: "Number" }],
      ...statement,
      colour: 210,
      tooltip: "等待识别到 ASR_01–ASR_10，识别到后回报完成",
    },
    // ================= 控制 =================
    {
      type: "xiaobai_wait",
      message0: "等待 %1 秒（电脑本地）",
      args0: [{ type: "input_value", name: "SECONDS", check: "Number" }],
      ...statement,
      colour: 330,
      tooltip: "由上位机计时，不占用设备任务",
    },
    {
      type: "xiaobai_stop_program",
      message0: "停止程序",
      ...statement,
      colour: 330,
      tooltip: "取消设备任务、刹停双电机，保留编程模式",
    },
  ]);
})();
