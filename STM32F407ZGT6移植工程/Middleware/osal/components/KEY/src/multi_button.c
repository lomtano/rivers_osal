/*
 * Copyright (c) 2016 Zibin Zheng <znbin@qq.com>
 * All rights reserved
 */

#include "multi_button.h"

#define EVENT_CB(ev)   if(handle->cb[ev])handle->cb[ev]((void*)handle) /* 如果这个事件挂了回调，就立刻回调。 */
#define PRESS_REPEAT_MAX_NUM  15 /*!< The maximum value of the repeat counter */

/* 所有已经 start() 的按键都会挂在这条全局扫描链表上。 */
static struct Button* head_handle = NULL;

static void button_handler(struct Button* handle);

/**
  * @brief  Initializes the button struct handle.
  * @param  handle: the button handle struct.
  * @param  pin_level: read the HAL GPIO of the connected button level.
  * @param  active_level: pressed GPIO level.
  * @param  button_id: the button id.
  * @retval None
  */
void button_init(struct Button* handle, uint8_t(*pin_level)(uint8_t), uint8_t active_level, uint8_t button_id)
{
	/* 先清空整个控制块，避免旧状态残留影响状态机。 */
	memset(handle, 0, sizeof(struct Button));
	handle->event = (uint8_t)NONE_PRESS;
	handle->hal_button_Level = pin_level;
	/* 初始电平取“未按下”状态，也就是 active_level 的反相。 */
	handle->button_level = !active_level;
	handle->active_level = active_level;
	handle->button_id = button_id;
}

/**
  * @brief  Attach the button event callback function.
  * @param  handle: the button handle struct.
  * @param  event: trigger event type.
  * @param  cb: callback function.
  * @retval None
  */
void button_attach(struct Button* handle, PressEvent event, BtnCallback cb)
{
	/* 每类事件只保留一个回调，后设置的会覆盖前一个。 */
	handle->cb[event] = cb;
}

/**
  * @brief  Inquire the button event happen.
  * @param  handle: the button handle struct.
  * @retval button event.
  */
PressEvent get_button_event(struct Button* handle)
{
	return (PressEvent)(handle->event);
}

/**
  * @brief  Button driver core function, driver state machine.
  * @param  handle: the button handle struct.
  * @retval None
  */
static void button_handler(struct Button* handle)
{
	uint8_t read_gpio_level = handle->hal_button_Level(handle->button_id);

	/* 只要状态机已经进入按下相关状态，就让 ticks 持续累加。 */
	if((handle->state) > 0) handle->ticks++;

	/*------------button debounce handle---------------*/
	if(read_gpio_level != handle->button_level) { /* 新采样电平与上一次稳定电平不同。 */
		/* 必须连续多次读到同样的新电平，才真正承认电平发生变化。 */
		if(++(handle->debounce_cnt) >= DEBOUNCE_TICKS) {
			handle->button_level = read_gpio_level;
			handle->debounce_cnt = 0;
		}
	} else { /* 电平没变，消抖计数清零。 */
		handle->debounce_cnt = 0;
	}

	/*-----------------State machine-------------------*/
	switch (handle->state) {
	case 0:
		/* 状态 0：空闲态，等待第一次按下。 */
		if(handle->button_level == handle->active_level) {	/* 检测到按下。 */
			handle->event = (uint8_t)PRESS_DOWN;
			EVENT_CB(PRESS_DOWN);
			handle->ticks = 0;
			/* 第一次按下记作第 1 次点击。 */
			handle->repeat = 1;
			handle->state = 1;
		} else {
			handle->event = (uint8_t)NONE_PRESS;
		}
		break;

	case 1:
		/* 状态 1：已经按下，等待松开或演变成长按。 */
		if(handle->button_level != handle->active_level) { /* 松开。 */
			handle->event = (uint8_t)PRESS_UP;
			EVENT_CB(PRESS_UP);
			handle->ticks = 0;
			/* 进入“等待是否还有下一次点击”的窗口。 */
			handle->state = 2;
		} else if(handle->ticks > LONG_TICKS) {
			/* 持续按下超过长按阈值，进入长按态。 */
			handle->event = (uint8_t)LONG_PRESS_START;
			EVENT_CB(LONG_PRESS_START);
			handle->state = 5;
		}
		break;

	case 2:
		/* 状态 2：刚松开，处于短时间双击判定窗口。 */
		if(handle->button_level == handle->active_level) { /* 窗口内再次按下。 */
			handle->event = (uint8_t)PRESS_DOWN;
			EVENT_CB(PRESS_DOWN);
			if(handle->repeat != PRESS_REPEAT_MAX_NUM) {
				handle->repeat++;
			}
			EVENT_CB(PRESS_REPEAT); /* 告诉上层这是一次重复点击。 */
			handle->ticks = 0;
			handle->state = 3;
		} else if(handle->ticks > SHORT_TICKS) { /* 窗口结束，还没出现下一次点击。 */
			if(handle->repeat == 1) {
				handle->event = (uint8_t)SINGLE_CLICK;
				EVENT_CB(SINGLE_CLICK);
			} else if(handle->repeat == 2) {
				handle->event = (uint8_t)DOUBLE_CLICK;
				EVENT_CB(DOUBLE_CLICK);
			}
			/* 一轮点击序列结束，回到空闲态。 */
			handle->state = 0;
		}
		break;

	case 3:
		/* 状态 3：第二次按下过程中，等待松开。 */
		if(handle->button_level != handle->active_level) { /* 第二次松开。 */
			handle->event = (uint8_t)PRESS_UP;
			EVENT_CB(PRESS_UP);
			if(handle->ticks < SHORT_TICKS) {
				handle->ticks = 0;
				handle->state = 2; /* 再次进入双击/多击窗口。 */
			} else {
				/* 按住太久，已经不算连续短击，直接结束。 */
				handle->state = 0;
			}
		} else if(handle->ticks > SHORT_TICKS) { /* 第二次按下时间已超出短击窗口。 */
			handle->state = 1;
		}
		break;

	case 5:
		/* 状态 5：长按保持态。 */
		if(handle->button_level == handle->active_level) {
			/* 仍在持续按下时，周期性上报 LONG_PRESS_HOLD。 */
			handle->event = (uint8_t)LONG_PRESS_HOLD;
			EVENT_CB(LONG_PRESS_HOLD);
		} else { /* 长按后松开。 */
			handle->event = (uint8_t)PRESS_UP;
			EVENT_CB(PRESS_UP);
			handle->state = 0; /* 回到空闲态。 */
		}
		break;
	default:
		/* 理论上不会走到这里；为了安全起见，异常状态直接复位。 */
		handle->state = 0;
		break;
	}
}

/**
  * @brief  Start the button work, add the handle into work list.
  * @param  handle: target handle struct.
  * @retval 0: succeed. -1: already exist.
  */
int button_start(struct Button* handle)
{
	struct Button* target = head_handle;
	while(target) {
		if(target == handle) return -1;	/* 已经在扫描链表里了，避免重复加入。 */
		target = target->next;
	}
	/* 头插到全局扫描链表，后续 button_ticks() 就会周期性扫描它。 */
	handle->next = head_handle;
	head_handle = handle;
	return 0;
}

/**
  * @brief  Stop the button work, remove the handle off work list.
  * @param  handle: target handle struct.
  * @retval None
  */
void button_stop(struct Button* handle)
{
	struct Button** curr;
	for(curr = &head_handle; *curr; ) {
		struct Button* entry = *curr;
		if(entry == handle) {
			/* 找到目标节点后，把它从单向链表里摘掉。 */
			*curr = entry->next;
			return;
		} else {
			curr = &entry->next;
		}
	}
}

/**
  * @brief  background ticks, timer repeat invoking interval 5ms.
  * @param  None.
  * @retval None
  */
void button_ticks(void)
{
	struct Button* target;
	for(target=head_handle; target; target=target->next) {
		/* 每次调用都按链表顺序扫描所有活跃按键。 */
		button_handler(target);
	}
}

