// main/ui_picker.h —— 主页打卡事项选择弹窗
#pragma once

#include <stdbool.h>

// 在主页之上弹出事项选择窗（done=本次标记的目标状态：true 完成 / false 未完成）
void ui_picker_show(bool done);

// 关闭弹窗（取消或确认后由调用方调用）
void ui_picker_hide(void);

// 移动选择：delta=-1 上移 / +1 下移（自动夹在范围内），返回是否移动
bool ui_picker_move(int delta);

// 当前选中的事项索引
int ui_picker_selected(void);
