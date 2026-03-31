#ifndef OSAL_H
#define OSAL_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    OSAL_OK = 0,
    OSAL_ERROR = 1,
    OSAL_ERR_TIMEOUT = 2,
    OSAL_ERR_RESOURCE = 3,
    OSAL_ERR_PARAM = 4,
    OSAL_ERR_NOMEM = 5,
    OSAL_ERR_ISR = 6,
    OSAL_RESERVED = 0x7FFFFFFF
} osal_status_t;

/*
 * ---------------------------------------------------------------------------
 * OSAL 缁熶竴绛夊緟璇箟
 * ---------------------------------------------------------------------------
 * 1. timeout = 0U锛氫笉绛夊緟锛岀珛鍗宠繑鍥炪€? * 2. timeout = N  锛氭渶澶氱瓑寰?N 姣銆? * 3. timeout = OSAL_WAIT_FOREVER锛氭案涔呯瓑寰咃紝鐩村埌璧勬簮婊¤冻鎴栬鏄惧紡鍙栨秷銆? */
#ifndef OSAL_WAIT_FOREVER
#define OSAL_WAIT_FOREVER 0xFFFFFFFFUL
#endif

/*
 * ---------------------------------------------------------------------------
 * OSAL 鍔熻兘瑁佸壀閰嶇疆
 * ---------------------------------------------------------------------------
 * 1. 榛樿鍏ㄩ儴寮€鍚紝寮€绠卞嵆鍙娇鐢ㄣ€?
 * 2. mem / irq / task / 鍩虹 timer 鏃跺熀 灞炰簬鍐呮牳鏍稿績鑳藉姏锛岄粯璁ゅ父寮€銆?
 * 3. queue / event / mutex / 杞欢瀹氭椂鍣?/ USART 缁勪欢 / Flash 缁勪欢 灞炰簬鍙€変欢銆?
 * 4. 濡傞渶瑕佸壀锛岃鍦ㄥ寘鍚?osal.h 涔嬪墠鏀瑰啓杩欎簺瀹忥紝鎴栫洿鎺ュ湪鏈枃浠朵腑淇敼榛樿鍊笺€?
 */
#ifndef OSAL_CFG_ENABLE_QUEUE
#define OSAL_CFG_ENABLE_QUEUE 1
#endif

#ifndef OSAL_CFG_ENABLE_EVENT
#define OSAL_CFG_ENABLE_EVENT 1
#endif

#ifndef OSAL_CFG_ENABLE_MUTEX
#define OSAL_CFG_ENABLE_MUTEX 1
#endif

#ifndef OSAL_CFG_ENABLE_SW_TIMER
#define OSAL_CFG_ENABLE_SW_TIMER 1
#endif

#ifndef OSAL_CFG_ENABLE_USART
#define OSAL_CFG_ENABLE_USART 1
#endif

#ifndef OSAL_CFG_ENABLE_FLASH
#define OSAL_CFG_ENABLE_FLASH 1
#endif

/*
 * ---------------------------------------------------------------------------
 * OSAL 璇婃柇閰嶇疆
 * ---------------------------------------------------------------------------
 * 1. OSAL_CFG_ENABLE_DEBUG = 0 鏃讹紝璇婃柇瀹忓叏閮ㄤ负绌烘搷浣溿€?
 * 2. OSAL_CFG_ENABLE_DEBUG = 1 鏃讹紝OSAL_DEBUG_HOOK(module, message) 浼氬湪鍙娴嬬殑
 *    闈炴硶鍙ユ焺銆侀噸澶嶉噴鏀俱€侀噸澶嶇粦瀹氥€侀敊璇笂涓嬫枃璋冪敤绛夊満鏅笅琚Е鍙戙€?
 * 3. 鎺ㄨ崘鍦ㄥ簲鐢ㄥ眰瀹氫箟绫讳技涓嬮潰鐨勯挬瀛愶紝鍐嶅寘鍚?osal.h锛? *      #define OSAL_CFG_ENABLE_DEBUG 1
 *      #define OSAL_DEBUG_HOOK(module, message) \
 *          printf("[OSAL/%s] %s\r\n", module, message)
 * 4. OSAL 榛樿涓嶇粦瀹氫换浣曡緭鍑哄悗绔紱RTT銆乁SART銆佸崐涓绘満绛夊潎鐢辩敤鎴疯嚜琛屽喅瀹氥€? * 5. 濡傛灉浣犳病鏈夎嚜瀹氫箟 OSAL_DEBUG_HOOK锛屽嵆浣挎墦寮€ debug锛岀郴缁熷眰涔熶笉浼氫富鍔ㄨ緭鍑恒€? */
#ifndef OSAL_CFG_ENABLE_DEBUG
#define OSAL_CFG_ENABLE_DEBUG 0
#endif

#ifndef OSAL_DEBUG_HOOK
#define OSAL_DEBUG_HOOK(module, message) ((void)0)
#endif

#if OSAL_CFG_ENABLE_DEBUG
#define OSAL_DEBUG_REPORT(module, message) do { OSAL_DEBUG_HOOK((module), (message)); } while (0)
#else
#define OSAL_DEBUG_REPORT(module, message) ((void)0)
#endif

/*
 * ---------------------------------------------------------------------------
 * OSAL 鍙ユ焺璧勬簮濂戠害
 * ---------------------------------------------------------------------------
 * 1. create / alloc 鎴愬姛鍚庯紝璧勬簮鎵€鏈夋潈褰掕皟鐢ㄦ柟銆?
 * 2. delete / destroy / free 鎴愬姛鍚庯紝鍘熷彞鏌勬垨鎸囬拡绔嬪嵆澶辨晥锛屼笉鍏佽缁х画浣跨敤銆?
 * 3. delete(NULL) / destroy(NULL) / free(NULL) 榛樿瑙嗕负瀹夊叏绌烘搷浣溿€?
 * 4. 閲嶅 delete銆侀噸澶?destroy銆侀檲鏃у彞鏌勮闂睘浜庤皟鐢ㄦ柟閿欒銆?
 * 5. release 鏋勫缓涓嬶紝OSAL 浼樺厛淇濇寔杞婚噺锛岃兘闈欓粯杩斿洖鐨勫湴鏂归€氬父浼氶潤榛樿繑鍥炪€?
 * 6. debug 鏋勫缓涓嬶紝鍑℃槸瀹炵幇灞傝兘澶熸娴嬪埌鐨勯噸澶嶉噴鏀俱€侀噸澶嶇粦瀹氥€侀潪娉曞彞鏌勩€?
 *    閿欒涓婁笅鏂囪皟鐢紝閮戒細閫氳繃 OSAL_DEBUG_HOOK 缁欏嚭璇婃柇淇℃伅銆?
 * 7. 鍙湁鍚嶅瓧鏄惧紡甯?from_isr锛屾垨澶存枃浠惰兘鍔涚煩闃垫槑纭爣娉ㄢ€滀换鍔℃€?ISR鈥濈殑鎺ュ彛锛?
 *    鎵嶅缓璁湪 ISR 涓娇鐢ㄣ€?
 */

/**
 * @brief 鍒濆鍖?OSAL 绯荤粺灞傘€?
 * @note 璇ユ帴鍙ｄ細璋冪敤骞冲彴灞傚垵濮嬪寲閽╁瓙锛屽苟鑷姩鍚屾褰撳墠 Tick 璁℃暟婧愰厤缃€?
 */
void osal_init(void);

/**
 * @brief 鍦ㄥ懆鏈熸€?Tick 涓柇閲岃皟鐢ㄧ殑 OSAL 閫氱敤涓柇鍏ュ彛銆?
 * @note 鎺ㄨ崘鐩存帴鍦?SysTick_Handler() 鎴栧叾浠栫郴缁熸椂鍩轰腑鏂腑璋冪敤瀹冦€?
 */
void osal_tick_handler(void);

#include "osal_task.h"
#include "osal_mem.h"
#include "osal_irq.h"
#include "osal_timer.h"
#include "osal_platform.h"

#if OSAL_CFG_ENABLE_QUEUE
#include "osal_queue.h"
#endif

#if OSAL_CFG_ENABLE_EVENT
#include "osal_event.h"
#endif

#if OSAL_CFG_ENABLE_MUTEX
#include "osal_mutex.h"
#endif

#ifdef __cplusplus
}
#endif

#endif /* OSAL_H */
