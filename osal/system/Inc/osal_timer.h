#ifndef OSAL_TIMER_H
#define OSAL_TIMER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef OSAL_TIMER_MAX
#define OSAL_TIMER_MAX 16
#endif

/**
 * @brief 回退模式下的默认 OSAL Tick 周期，单位为微秒。
 * @note 当平台层没有提供硬件 Tick 计数源时，OSAL 会使用这个默认周期。
 */
#ifndef OSAL_TICK_PERIOD_US
#define OSAL_TICK_PERIOD_US 1000U
#endif

typedef void (*osal_timer_callback_t)(void *arg);
typedef struct osal_timer osal_timer_t;

/**
 * @brief 获取 OSAL 运行时间的微秒计数值。
 * @return 32 位回绕微秒计数。
 */
uint32_t osal_timer_get_uptime_us(void);

/**
 * @brief 获取 OSAL 运行时间的毫秒计数值。
 * @return 32 位回绕毫秒计数。
 */
uint32_t osal_timer_get_uptime_ms(void);

/**
 * @brief 获取 HAL 风格的毫秒 Tick。
 * @return 32 位回绕毫秒 Tick。
 */
uint32_t osal_timer_get_tick(void);

/**
 * @brief 忙等待指定的微秒数。
 * @param us 延时时长，单位为微秒。
 */
void osal_timer_delay_us(uint32_t us);

/**
 * @brief 忙等待指定的毫秒数。
 * @param ms 延时时长，单位为毫秒。
 */
void osal_timer_delay_ms(uint32_t ms);

/**
 * @brief 创建一个软件定时器。
 * @param timeout_us 定时器周期或单次超时时间，单位为微秒。
 * @param periodic true 表示周期定时器，false 表示单次定时器。
 * @param cb 定时到期后的回调函数。
 * @param arg 传递给回调函数的用户参数。
 * @return 成功返回定时器 ID，失败返回 -1。
 */
int osal_timer_create(uint32_t timeout_us, bool periodic, osal_timer_callback_t cb, void *arg);

/**
 * @brief 启动一个软件定时器。
 * @param timer_id 定时器 ID。
 * @return 启动成功返回 true。
 */
bool osal_timer_start(int timer_id);

/**
 * @brief 停止一个软件定时器。
 * @param timer_id 定时器 ID。
 */
void osal_timer_stop(int timer_id);

/**
 * @brief 删除一个软件定时器。
 * @param timer_id 定时器 ID。
 */
void osal_timer_delete(int timer_id);

/**
 * @brief 轮询软件定时器并执行已到期的回调。
 */
void osal_timer_poll(void);

#ifdef __cplusplus
}
#endif

#endif /* OSAL_TIMER_H */
