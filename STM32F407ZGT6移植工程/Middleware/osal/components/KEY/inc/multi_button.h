/*
 * Copyright (c) 2016 Zibin Zheng <znbin@qq.com>
 * All rights reserved
 */

#ifndef _MULTI_BUTTON_H_
#define _MULTI_BUTTON_H_
#include "main.h"
#include <stdint.h>
#include <string.h>

/* 下面这组参数决定按键扫描与判定阈值，可按项目需要调整。 */
#define TICKS_INTERVAL    5	/* 按键扫描周期，单位 ms。button_ticks() 应按这个周期被调用。 */
#define DEBOUNCE_TICKS    3	/* 消抖需要连续稳定多少个扫描周期。MAX 7。 */
#define SHORT_TICKS       (300 /TICKS_INTERVAL)   /* 短按/单击判定窗口。 */
#define LONG_TICKS        (1000 /TICKS_INTERVAL)  /* 长按起始判定窗口。 */


typedef void (*BtnCallback)(void*);

typedef enum {
	PRESS_DOWN = 0,
	PRESS_UP,
	PRESS_REPEAT,
	SINGLE_CLICK,
	DOUBLE_CLICK,
	LONG_PRESS_START,
	LONG_PRESS_HOLD,
	number_of_event,
	NONE_PRESS
}PressEvent;

typedef struct Button {
	uint16_t ticks;                              /* 当前状态已持续的扫描计数。 */
	uint8_t  repeat : 4;                        /* 连击计数，最多 15 次。 */
	uint8_t  event : 4;                         /* 最近一次产生的事件类型。 */
	uint8_t  state : 3;                         /* 按键状态机当前状态。 */
	uint8_t  debounce_cnt : 3;                  /* 当前消抖稳定计数。 */
	uint8_t  active_level : 1;                  /* 什么电平算“按下”。 */
	uint8_t  button_level : 1;                  /* 最近一次稳定采样到的电平。 */
	uint8_t  button_id;                         /* 用户给这个按键分配的编号。 */
	uint8_t  (*hal_button_Level)(uint8_t button_id_); /* 板级读引脚函数。 */
	BtnCallback  cb[number_of_event];           /* 各类按键事件对应的回调。 */
	struct Button* next;                        /* 活动按键链表指针。 */
}Button;

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化按键控制块。这里只做字段初始化，不会自动加入扫描链表。 */
void button_init(struct Button* handle, uint8_t(*pin_level)(uint8_t), uint8_t active_level, uint8_t button_id);
/* 给某类按键事件挂一个回调函数。 */
void button_attach(struct Button* handle, PressEvent event, BtnCallback cb);
/* 读取最近一次产生的按键事件。 */
PressEvent get_button_event(struct Button* handle);
/* 把这个按键加入全局扫描链表。返回 0 表示成功，-1 表示已经在链表里。 */
int  button_start(struct Button* handle);
/* 把这个按键从全局扫描链表中移除。 */
void button_stop(struct Button* handle);
/* 按固定周期调用的后台扫描入口，通常放在 5ms 周期任务或定时器中。 */
void button_ticks(void);

#ifdef __cplusplus
}
#endif

#endif

