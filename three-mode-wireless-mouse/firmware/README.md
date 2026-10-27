# 固件说明（CH585M 应用层节选）

这里只有**应用层**，不含沁恒官方 SDK（BLE 库、StdPeriph 驱动、USB 协议栈、`HidDev`/`Batt` 服务）。编译方法见仓库根 README 第 7 节。

## 依赖的 SDK 模块

| 模块 | 用途 |
| --- | --- |
| `CH58x_BLE_LIB` + `CH58xBLE_LIB.h` | BLE 协议栈、GAP/GATT、TMOS 调度 |
| `StdPeriphDriver` | GPIO / SPI0 / R8_USB 寄存器操作、`DelayUs` |
| `HidDev` / `hidmouseservice` | BLE HID 服务与报告通知 |
| `Batt` / `battservice` | 电池服务 |
| `usb_handler.c` / `usb_descriptors.c` | 自定义 USB HID 端点与报告描述符（**未随附**） |

## 文件

| 文件 | 行数 | 说明 |
| --- | --- | --- |
| `hidmouse_main.c` | 87 | 时钟/USB/BLE/HID 初始化，TMOS 主循环，`USB_IRQHandler` |
| `hidmouse.c` | 486 | PAW3395 驱动、按键扫描、三模切换、报告分发、BLE 状态回调 |
| `hidmouse.h` | 62 | 报告结构体、HID 回调表声明 |

## 关键点

- **编码**：原始文件是 GBK，仓库里已转 UTF-8，内容未改；
- **SPI**：`SPI0_MasterInit(SPI_Mode_0, 24000000, FALSE, NULL)`，片选 `PA12`；
- **轮询**：`tmos_start_task(hidEmuTaskId, MOUSE_POLL_EVT, 8)`，8 个 TMOS tick 一次；
- **报告结构**（顺序不可变，HID 规定）：

```c
__packed struct mouseHID_t
{
    uint8_t buttons;   // bit0 左 / bit1 右 / bit2 中
    int16_t x;
    int16_t y;
    int8_t  wheel;
};
```

- **已知未完成**：`wheel` 恒为 0（编码器逻辑未接入）、2.4G 状态回调为空、2.4G 配对/跳频未在节选中实现。

## 移植提示

如果只想在别的板子上复现"三模切换 + PAW3395"：

1. 保留 `HidEmu_Init()` 的初始化顺序：GPIO → SPI → 传感器上电 → GAP 参数 → Hid/Batt 服务 → 默认模式判定；
2. `apply_current_mode()` 是模式切换的唯一入口，新增模式只需要改这个函数和 `hidEmuSendMouseReport()`；
3. 传感器读数据固定走 `PAW3395_Motion_Burst()`（12 字节连读），比逐寄存器读快一个数量级。
