# Flash 桥接组件

位置：

- `components/periph/flash/Inc/periph_flash.h`
- `components/periph/flash/Src/periph_flash.c`

## 作用

这个组件用来把不同 MCU 的内部 Flash 差异统一收敛到一套可复用接口中。

桥接层主要处理的差异包括：

- 解锁/上锁流程
- 擦除粒度
- 支持的写入宽度
- 原始读取方式

## 桥接回调

桥接层可以按目标平台能力实现下面任意组合：

- `unlock`
- `lock`
- `erase`
- `read`
- `write_u8`
- `write_u16`
- `write_u32`
- `write_u64`

上层一般只调用 `periph_flash_write()`，组件内部会根据当前地址对齐情况和已经安装的函数指针，
自动选择当前可用的最宽合法写入方式。
