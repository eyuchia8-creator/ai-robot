#ifndef _STATE_MACHINE_H_
#define _STATE_MACHINE_H_

#include "common_types.h"

/* ===================================================================
 * StateMachine — 交互状态机模块
 *
 * 驱动语音机器人的核心交互流程:
 *   IDLE → RECORDING → PROCESSING → RESULT/ERROR → IDLE
 *
 * 状态转换由事件驱动, 每个状态的 enter/update/exit 在此模块中实现。
 * =================================================================== */

/* 状态转换表最大条数 */
#define STATE_TRANSITION_MAX 32

/* 状态超时常量 (秒) */
#define RECORDING_TIMEOUT_SEC  60   /* 录音最大时长 */
#define RESULT_DISPLAY_SEC      5   /* 结果显示时长 */
#define ERROR_DISPLAY_SEC       3   /* 错误显示时长 */

/* 录音缓冲区最大字节数 (16kHz * 2bytes * 60s = 1.92MB) */
#define MAX_RECORD_BYTES (16000 * 2 * 60)

/* 状态处理函数类型 */
typedef int (*StateHandler)(struct StateMachine* sm);

/* 状态转换规则 */
typedef struct {
    StateEnum from_state;
    EventEnum event;
    StateEnum to_state;
} StateTransition;

/* StateMachine 句柄 */
typedef struct StateMachine {
    StateEnum       current_state;                      /* 当前状态 */
    StateEnum       prev_state;                         /* 前一状态 */
    StateContext    ctx;                                /* 聚合所有模块引用 */
    StateTransition transitions[STATE_TRANSITION_MAX];  /* 状态转换表 */
    int             transition_count;                   /* 转换表条目数 */
    int             state_enter_time;                   /* 进入当前状态的时间戳 (秒) */
} StateMachine;

/* ---- API ---- */

/**
 * state_machine_init — 初始化状态机
 *
 * 1. 设置 initial state = STATE_IDLE
 * 2. 复制 StateContext
 * 3. 注册所有状态转换规则
 * 4. 记录进入时间戳
 *
 * @param sm  StateMachine 实例指针
 * @param ctx StateContext 指针 (包含所有模块引用)
 * @return    ERR_OK (0) 成功, <0 错误码
 */
int state_machine_init(StateMachine* sm, StateContext* ctx);

/**
 * state_machine_handle_event — 处理状态转换事件
 *
 * 在转换表中查找匹配 (from_state, event)，执行旧状态的 exit 处理，
 * 切换状态，执行新状态的 enter 处理。
 *
 * @param sm    StateMachine 实例指针
 * @param event 触发的事件
 * @return      ERR_OK (0) 转换成功, <0 无匹配转换
 */
int state_machine_handle_event(StateMachine* sm, EventEnum event);

/**
 * state_machine_run — 每帧调用，处理当前状态的 update 逻辑
 *
 * 包括: 按键轮询、录音采集、WiFi 检查、HTTP 上传、超时检测。
 *
 * @param sm  StateMachine 实例指针
 * @return    ERR_OK (0) 成功, <0 错误码
 */
int state_machine_run(StateMachine* sm);

/**
 * state_machine_get_state — 获取当前状态
 *
 * @param sm  StateMachine 实例指针
 * @return    当前 StateEnum 值
 */
StateEnum state_machine_get_state(StateMachine* sm);

/**
 * state_machine_deinit — 释放状态机资源
 *
 * @param sm  StateMachine 实例指针
 * @return    ERR_OK (0) 成功
 */
int state_machine_deinit(StateMachine* sm);

#endif /* _STATE_MACHINE_H_ */
