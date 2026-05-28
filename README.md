---

## 目录

- [硬件规格](#硬件规格)
- [功能特性](#功能特性)
- [硬件架构](#硬件架构)
- [软件架构](#软件架构)
- [目录结构](#目录结构)
- [构建说明](#构建说明)
- [校准指南](#校准指南)
- [许可证](#许可证)

---

## 硬件规格

| 项目 | 参数 |
|------|------|
| **主控** | STM32F407VET6 (Cortex-M4, 168MHz, FPU) |
| **Flash** | 512 KB |
| **RAM** | 192 KB (含 64 KB CCMRAM) |
| **显示屏** | 3.2 寸 TFT-LCD (ST7789 驱动, SPI + FSMC 接口, 320×240) |
| **外部存储** | SPI Flash (字库/校准数据), FATFS 文件系统 |
| **输入** | 5 向按键 + 旋转编码器 (含按键) |
| **USB** | USB CDC 虚拟串口 |
| **供电** | USB PD (CH224K 诱骗芯片) |

### 外设与接口

| 外设 | 芯片/电路 | 接口 | 用途 |
|------|-----------|------|------|
| 电流/功率监测 | TI INA226 | I2C1 (0x40) | 输入电压/电流/功率实时监测 |
| 温度传感器 | TI TMP102 | I2C1 | DCDC 模块温度监测 |
| DAC ×2 | Microchip MCP4725 | I2C1 / I2C3 | DC 偏置 + 信号发生器输出 |
| ADC 采集 | STM32 片上 ADC1 | 3 通道 DMA 扫描 | 数控电源 + 万用表数据采集 |
| 编码器 | 旋转编码器 (TIM4) | 定时器编码器模式 | 菜单导航 / 参数调节 |
| 程控放大器 | 模拟开关 + 运放 | GPIO | 示波器 8 级程控放大 (×1 ~ ×128) |
| 输出控制 | MOS 管开关 | GPIO | 数控电源输出 / 泄放 / 接地控制 |

---

## 功能特性

### 1. 数控电源 (DPS)

- 恒压 / 恒流双模式输出, 硬件 PID 电流环控制
- 实时显示输出电压、电流、功率、累计能量、运行时长
- 5 组快速预设电压/电流值
- 输入电压 / DCDC 温度 / 风扇转速监测
- 屏幕亮度自适应休眠

### 2. 数字示波器 (DSO)

- 8 级程控放大: ×1 / ×2 / ×4 / ×8 / ×16 / ×32 / ×64 / ×128
- 直流 / 交流耦合切换
- ×1 / ×5 输入衰减
- 可调时基与触发
- 波形冻结 (HOLD) 与测量光标

### 3. 任意波形发生器 (AWG)

- 正弦波 / 方波 / 三角波 / 锯齿波输出
- MCP4725 12-bit DAC 输出
- 频率、幅值、直流偏置独立可调

### 4. 数字万用表 (DMM)

- **电压测量**: 直流 ±40V, 自动量程
- **电流测量**: 自动量程
- **电阻测量**: 200Ω / 30KΩ 两档切换
- **二极管/通断测试**: 二极管正向压降 + 蜂鸣导通检测
- 数据保持 (HOLD) 功能
- 过采样 (12→14bit) + 软件排序去毛刺 + IIR 数字滤波

### 5. 系统功能

- LVGL 图形界面, 多应用切换
- 屏幕亮度 / 休眠时间 / 显示翻转可调
- 蜂鸣器音量 / 时长可调
- 温控风扇 (启转温度可设)
- USB 虚拟串口通信
- 校准参数 SPI Flash 持久化存储

---

## 硬件架构

```
                        STM32F407VET6
                   ┌──────────────────────┐
    SPI Flash  ←──┤ SPI2                 │
    ST7789 LCD ←──┤ SPI1 + FSMC          │
    5× Keys    ←──┤ GPIO (PB12-15, PE4)  │
    Encoder    ←──┤ TIM4 (Encoder Mode)  │
    INA226     ←──┤ I2C1 (0x40)          │
    TMP102     ←──┤ I2C1                 │
    MCP4725 #1 ←──┤ I2C1 (DC Bias)      │
    MCP4725 #2 ←──┤ I2C3 (OSC Output)   │
    USB CDC    ←──┤ USB OTG FS           │
    ADC1       ←──┤ 3ch DMA (DPS + DMM) │
    TIM8       ──→│ ADC 触发源           │
    TIM1 CH2   ──→│ LCD 背光 PWM         │
    TIM9 CH1   ──→│ 蜂鸣器 PWM           │
                   └──────────────────────┘
```

| GPIO | 功能 |
|------|------|
| PB12 ~ PB15 | 按键 MODE / UP / DOWN / SET |
| PE4 | 按键 PWR |
| PC6 | 编码器按键 |
| PC0 | 系统状态 LED |
| PC10 / PC11 | 数控电源输出控制 |
| PD2 / PD3 | 电源开关 / 泄放控制 |
| PE1 / PE2 | 万用表电阻档位切换 |

---

## 软件架构

### RTOS 任务概览

| 任务名 | 优先级 | 栈大小 | 功能 |
|--------|--------|--------|------|
| **InitTask** | Realtime7 | 1536B | 上电初始化: 外设检测 → 加载配置 → 字体 → 启动应用切换 |
| **SensorTask** | Realtime | 768B | 每 125ms: 读取 INA226 (输入 V/A/W), 风扇/休眠管理 |
| **LcdFlushTask** | High1 | 2048B | LCD 异步刷新消费者 (LVGL + 手动绘图统一调度) |
| **PageSelectTask** | High | 2048B | 应用切换调度: 挂起当前 APP → 恢复目标 APP |
| **IndevDetectTask** | Normal4 | 512B | 10ms 周期: 按键消抖状态机 + 编码器扫描 + 蜂鸣器 PWM 控制 |
| **LvglCoreTask** | Normal | 3072B | LVGL 主循环 (lv_task_handler + lv_timer_handler) |
| **DpsCoreTask** | Normal | 2048B | 数控电源主循环: UI 刷新 + 按键处理 |
| **PIDTask** | Normal1 | 1024B | 1ms 周期: 电流环 PID 运算 → MCP4725 DAC 输出 |
| **DmmCoreTask** | Normal | 2048B | 万用表主循环: ADC 回调 → 滤波 → 数据显示 + 按键处理 |
| **DsoCoreTask** | Normal | 2048B | 示波器主循环: 波形采集 → 渲染 |
| **AwgTask** | Normal | 1024B | 信号发生器控制与参数管理 |
| **CalibrateTask** | Normal | 2048B | 各功能模块校准流程 |
| **SerialCoreTask** | Realtime1 | 1024B | USB CDC 串口命令解析与响应 |

### 中断与回调

| 中断源 | 回调函数 | 功能 |
|--------|----------|------|
| ADC1 转换完成 | `CB_DPS_ADC_ConvCpltCallback` | 数控电源: 电压/电流/PID 计算 |
| ADC1 转换完成 | `CB_DMM_ADC_ConvCpltCallback` | 万用表: 3 通道数据解交错→排序去毛刺→过采样→校准→IIR 滤波 |
| TIM8 触发 | (ADC 触发源) | 定时触发 ADC 采样 (DPS: 64KHz, DMM: 20KHz) |

### 应用切换流程

```
  [App 选择界面 (LVGL)]
           │
     ┌─────┼─────────┬─────────┬─────────┐
     ▼     ▼         ▼         ▼         ▼
  DPS     DSO       AWG       DMM      校准
(数控电源)(示波器)(信号发生器)(万用表)

  切换过程:
  PageSelectTask 收到 AppSwitchQueue 消息
    → SuspendTask(当前APP)    // 反初始化外设, 挂起任务
    → ResumeTask(目标APP)     // 初始化外设, 恢复任务, 绘制UI
```

### 输入事件路由

```
  [硬件层]                    [FreeRTOS 层]              [应用层]
  ─────────                  ─────────────              ────────
  Key_Scan()       ──→  KeyEventQueueHandle  ──→  DpsCoreTask
  Encoder_Scan()   ──→  (容量: 8)             ──→  DmmCoreTask
  (IndevDetectTask)                             ──→  DsoCoreTask
                                                ──→  AwgTask
  encoder_read_cb() ──→  LVGL indev driver  ──→  LvglCoreTask
  keypad_read_cb()      (LVGL 独占模式)
```

- **LVGL 模式下**: 按键/编码器通过 `lv_port_indev` 直接输入, `IndevDetectTask` 暂停扫描
- **APP 模式下**: `IndevDetectTask` 恢复扫描, 事件通过 `KeyEventQueueHandle` 分发给当前 APP

### 数据流 (万用表示例)

```
  ADC1 采样 (TIM8 触发 @20KHz)
       │
       ▼
  DMA 循环缓冲 (600 个 uint16_t)
       │
       ▼
  回调: 解交错 → 3 通道 × 200 点
       │
       ▼
  qsort 排序 → 去头尾 4 个极值 → 192 点求和
       │
       ▼
  过采样 (12→14bit) → 校准公式 → IIR 低通滤波
       │
       ▼
  DMM_Voltage / DMM_Current / DMM_Resistance_Voltage
       │
       ▼
  DmmCoreTask: FormatFloat → lcd_draw_string → LcdFlushTask → ST7789
```

---

## 目录结构

```
XM_Power_Kit-master/
├── Core/                    # STM32CubeMX 生成
│   ├── Inc/                 #   main.h, gpio.h, adc.h, i2c.h, tim.h ...
│   ├── Src/                 #   main.c, gpio.c, freertos.c, stm32f4xx_it.c ...
│   └── startup_stm32f407xx.s
├── Drivers/                 # CMSIS + STM32F4xx HAL 库
├── UserCode/                # ★ 用户应用代码
│   ├── Algorithm/           #   算法模块
│   │   └── calibrate/       #     各 APP 校准算法 (DPS / DMM / AWG)
│   ├── Callback/            #   LVGL 回调 (按钮事件 / 设置界面)
│   ├── Drivers/             #   外设驱动
│   │   ├── CH224A/          #     USB PD 诱骗芯片
│   │   ├── FLASH/           #     SPI Flash 读写
│   │   ├── INA226/          #     TI INA226 电流监测
│   │   ├── Indev_Drivers/   #     按键 + 编码器驱动
│   │   ├── mcp4725/         #     MCP4725 DAC
│   │   ├── TFT_LCD/         #     ST7789 LCD 驱动 + 图形原语
│   │   └── TMP102/          #     TI TMP102 温度传感器
│   ├── Manager/             #   管理模块
│   │   ├── SwitchManager    #     硬件开关控制 (电源/万用表档位/示波器增益)
│   │   └── UserDefineManage #     用户参数持久化 (SPI Flash)
│   └── Task/                #   FreeRTOS 任务
│       ├── APP/             #     各 APP 核心任务
│       │   ├── DpsCoreTask  #       数控电源
│       │   ├── DsoTask/     #       示波器
│       │   ├── AwgTask      #       信号发生器
│       │   ├── DmmCoreTask  #       万用表
│       │   ├── CalibrateTask#       校准流程
│       │   ├── LvglCoreTask #       LVGL 图形界面
│       │   └── YourAPP      #       用户自定义 APP 模板
│       ├── InitTask.c       #     系统初始化
│       ├── SensorTask.c     #     传感器采集 (INA226)
│       ├── PageSelectTask.c #     应用切换调度
│       ├── IndevDetectTask.c#     输入设备扫描
│       └── UserTask.h       #     公共头文件 (类型/函数声明)
├── lvgl/                    # LVGL 8.x 图形库
├── lvgl_app/                # LVGL GUI Guider 生成的界面
├── FATFS/                   # FAT 文件系统 (SPI Flash 字库)
├── USB_DEVICE/              # USB CDC 虚拟串口
├── Middlewares/             # FreeRTOS + STM32 USB 库
├── cmake/                   # CMake 子构建 (STM32CubeMX)
├── CMakeLists.txt           # 顶层 CMake 构建
├── CMakePresets.json        # CMake 预设
├── STM32F407XX_FLASH.ld     # 链接脚本
└── XM_Power_Kit_V7.ioc      # STM32CubeMX 项目配置
```

---

## 构建说明

### 工具链要求

| 工具 | 版本要求 |
|------|----------|
| **ARM GCC 工具链** | `arm-none-eabi-gcc` 10.3+ |
| **CMake** | 3.22+ |
| **STM32CubeMX** | (可选) 用于修改 HAL 配置 |
| **烧录工具** | STM32CubeProgrammer / OpenOCD / J-Flash |

### 编译

```bash
# 配置 (使用预设)
cmake --preset default

# 编译
cmake --build build/ -j$(nproc)
```

产物:
- `build/XM_Power_Kit.elf` — ELF 调试文件
- `build/XM_Power_Kit.hex` — HEX 烧录文件

### 烧录

```bash
# STM32CubeProgrammer
STM32_Programmer_CLI -c port=SWD -w build/XM_Power_Kit.hex -v -rst

# OpenOCD
openocd -f interface/stlink.cfg -f target/stm32f4x.cfg \
  -c "program build/XM_Power_Kit.hex verify reset exit"
```

### 编译选项

- **C 标准**: C11
- **优化**: `-O3`
- **浮点**: 硬件 FPU (`fpv4-sp-d16`, `-mfloat-abi=hard`)
- **printf**: 启用浮点支持 (`-u _printf_float`)

### 内存布局

| 区域 | 起始地址 | 大小 | 用途 |
|------|----------|------|------|
| FLASH | 0x08000000 | 512 KB | 代码 + 只读数据 |
| SRAM1 | 0x20000000 | 112 KB | 通用数据 |
| SRAM2 | 0x2001C000 | 16 KB | 通用数据 |
| CCMRAM | 0x10000000 | 64 KB | FreeRTOS 堆 + LVGL 内存池 |

---

## 校准指南

所有校准参数存储在 SPI Flash 的 `uparam.bin` 文件中，每个参数 4 字节。

### 数控电源校准

| 参数 | 说明 |
|------|------|
| `DPS_Voltage_Original` | 电压 ADC 零点 (LSB) |
| `DPS_Voltage_Factor` | 电压分压比系数 |
| `DPS_Current_Original` | 电流 ADC 零点 (LSB) |
| `DPS_Current_Factor` | 电流采样系数 (I = U/R/放大倍数) |

### 万用表校准

| 模式 | 参数 | 说明 |
|------|------|------|
| 电压 | `DMM_Voltage_Original` | ADC 零点 |
| 电压 | `DMM_Voltage_Factor_B` / `_R` | 黑表笔 / 红表笔比例系数 |
| 电流 | `DMM_Current_Original` | ADC 零点 |
| 电流 | `DMM_Current_Factor` | 电流系数 |
| 电阻 200Ω | `DMM_Res_R200` + `_Original` + `_Voltage` | 基准电阻 / 零点 / 空载电压 |
| 电阻 2KΩ | `DMM_Res_R2K` + `_Original` + `_Voltage` | 基准电阻 / 零点 / 空载电压 |

### 示波器校准

| 参数 | 说明 |
|------|------|
| `OSC_Original` ~ `OSC_Original_X128` | 各放大档位 ADC 零点 |
| `OSC_Factor` | 输入分压比 |
| `OSC_AMP_X2` ~ `OSC_AMP_X128` | 各档位实际放大倍率 |

---

## 开发说明

### 添加自定义 APP

参考 `UserCode/Task/APP/YourAPP.c` 模板, 实现:

1. `Start_XXXCoreTask()` — 任务入口 (自挂起等待唤醒)
2. `Suspend_XXXTask()` — 反初始化外设, 挂起任务, 调用 `Suspend_IndevDetectTask()`
3. `Resume_XXXTask()` — 初始化外设, 绘制 UI, 调用 `Resume_IndevDetectTask()`, 恢复任务
4. 在 `PageSelectTask.c` 的 `SuspendTask/ResumeTask` 中注册
5. 在 `os_handles.h` 中声明任务句柄
6. 在 `freertos.c` 中创建任务实例

### 关键约定

- **I2C 总线**: 使用 `IIC1_MutexHandle` / `IIC3_MutexHandle` 保护多任务访问
- **SPI Flash**: 使用 `Flash_MutexHandle` 保护读写操作
- **LCD 绘图**: 非 LVGL APP 通过 `lcd_draw_*` API → `LcdMsgQueueHandle` → `LcdFlushTask` 统一刷新
- **输入扫描**: 非 LVGL APP 必须调用 `Resume_IndevDetectTask()` 启用按键/编码器扫描

---