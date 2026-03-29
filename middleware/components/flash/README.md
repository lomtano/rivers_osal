# Flash Component

这个目录放的是独立于 OSAL 核心层的内部 Flash 抽象组件。

## 设计目标

- 用桥接模式屏蔽不同 MCU SDK 的差异
- 支持解锁、加锁、擦除
- 支持按宽度编程：`u8 / u16 / u32 / u64`
- 提供一个通用 `periph_flash_write()`，自动选择当前地址下可用且对齐的最大写宽度

## 目录

- `Inc/periph_flash.h`
- `Src/periph_flash.c`

## 典型移植工作

你只需要实现这些桥接回调中的有效部分：

```c
unlock
lock
erase
read
write_u8
write_u16
write_u32
write_u64
```

说明：

- 扇区大小、页大小、bank 规则都放进 `erase()` 里处理
- 哪些写宽度可用，就实现哪些函数指针
- `periph_flash_write()` 会自动挑最宽的合法写入方式

## 推荐用法

```c
periph_flash_unlock(flash);
periph_flash_erase(flash, address, length);
periph_flash_write(flash, address, data, length);
periph_flash_lock(flash);
```
