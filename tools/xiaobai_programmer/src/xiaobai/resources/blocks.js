/* 小白编程自定义 Blockly 块（全部离线，无 CDN）。块类型与 Python 侧 program_ast.py 白名单一一对应。 */
(() => {
  const B = Blockly.Blocks;
  const C = Blockly.common || Blockly;

  const defs = [
    // 单电机
    { type: "xiaobai_motor_time", msg: "左/右电机 以 %1 %2 转动 %3 秒", args: [
        { type: "field_dropdown", name: "MOTOR", options: [["左", "LEFT"], ["右", "RIGHT"]] },
        { type: "field_dropdown", name: "DIR", options: [["正转", "FORWARD"], ["反转", "BACKWARD"]] },
        { type: "input_value", name: "SECONDS", check: "Number" }] },
    { type: "xiaobai_motor_run", msg: "左/右电机 持续 %1 %2", args: [
        { type: "field_dropdown", name: "MOTOR", options: [["左", "LEFT"], ["右", "RIGHT"]] },
        { type: "field_dropdown", name: "DIR", options: [["正转", "FORWARD"], ["反转", "BACKWARD"]] }] },
    { type: "xiaobai_motor_stop", msg: "刹停 %1 电机", args: [
        { type: "field_dropdown", name: "MOTOR", options: [["左", "LEFT"], ["右", "RIGHT"]] }] },
    { type: "xiaobai_motor_power", msg: "单电机功率 %1 档", args: [
        { type: "field_dropdown", name: "LEVEL", options: [["1", "1"], ["2", "2"], ["3", "3"]] }] },
    // 组合电机
    { type: "xiaobai_move_time", msg: "%1 %2 秒", args: [
        { type: "field_dropdown", name: "MOVE", options: [["前进", "FORWARD"], ["左转", "LEFT"], ["右转", "RIGHT"], ["后退", "BACKWARD"]] },
        { type: "input_value", name: "SECONDS", check: "Number" }] },
    { type: "xiaobai_move_run", msg: "持续 %1", args: [
        { type: "field_dropdown", name: "MOVE", options: [["前进", "FORWARD"], ["左转", "LEFT"], ["右转", "RIGHT"], ["后退", "BACKWARD"]] }] },
    { type: "xiaobai_move_stop", msg: "刹停双电机", args: [] },
    { type: "xiaobai_move_power", msg: "组合电机功率 %1 档", args: [
        { type: "field_dropdown", name: "LEVEL", options: [["1", "1"], ["2", "2"], ["3", "3"]] }] },
    // 感应
    { type: "xiaobai_wait_ir", msg: "等待 %1 红外 %2 %3", args: [
        { type: "field_dropdown", name: "CHANNEL", options: [["左", "LEFT"], ["中", "CENTER"], ["右", "RIGHT"]] },
        { type: "field_dropdown", name: "CMP", options: [["大于", "GT"], ["小于", "LT"]] },
        { type: "input_value", name: "THRESHOLD", check: "Number" }] },
    { type: "xiaobai_ir_value", msg: "%1 红外值", output: "Number", args: [
        { type: "field_dropdown", name: "CHANNEL", options: [["左", "LEFT"], ["中", "CENTER"], ["右", "RIGHT"]] }] },
    { type: "xiaobai_battery", msg: "电量 mV", output: "Number", args: [] },
    { type: "xiaobai_device_mode", msg: "设备模式号", output: "Number", args: [] },
    // 显示 / 语音
    { type: "xiaobai_show_eye", msg: "显示表情 %1", args: [
        { type: "input_value", name: "EYE", check: "Number" }] },
    { type: "xiaobai_show_num", msg: "显示数字 %1", args: [
        { type: "input_value", name: "NUM", check: "Number" }] },
    { type: "xiaobai_show_off", msg: "关闭显示", args: [] },
    { type: "xiaobai_play_voice", msg: "播放词条 P%1", args: [
        { type: "input_value", name: "ITEM", check: "Number" }] },
    { type: "xiaobai_wait_voice", msg: "等待词条 ASR_%1", args: [
        { type: "input_value", name: "ITEM", check: "Number" }] },
    // 控制
    { type: "xiaobai_wait", msg: "等待 %1 秒（电脑本地）", args: [
        { type: "input_value", name: "SECONDS", check: "Number" }] },
    { type: "xiaobai_stop_program", msg: "停止程序", args: [] },
  ];

  for (const def of defs) {
    B[def.type] = {
      init() {
        this.appendDummyInput().appendField(def.msg.replace(/%\d/g, "").trim());
        for (const arg of def.args) {
          if (arg.type === "field_dropdown") {
            this.appendDummyInput().appendField(new Blockly.FieldDropdown(arg.options), arg.name);
          } else if (arg.type === "input_value") {
            this.appendValueInput(arg.name).setCheck(arg.check || null);
          }
        }
        this.setPreviousStatement(true, null);
        if (def.output) {
          this.setOutput(true, def.output);
          this.setPreviousStatement(false);
          this.setNextStatement(false);
        } else {
          this.setNextStatement(true, null);
        }
        this.setColour(def.output ? 160 : 230);
        this.setToolboxCategory("小白");
      },
    };
  }
})();
