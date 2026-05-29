# Hovercar GD32 串口命令指南

## 概述

该固件支持通过串口（USART2 PB10/PB11, 115200 baud）实现完整的系统控制和监测。所有命令不区分大小写。

---

## 电机控制命令

### 1. 使能/禁用电机
```
motor enable     - 启用电机
motor disable    - 禁用电机
```

### 2. 单独控制电机 PWM
```
motor left <pwm>     - 设置左电机 PWM 值 (-1000~1000)
motor right <pwm>    - 设置右电机 PWM 值 (-1000~1000)
motor both <pwm>     - 同时设置两个电机 PWM 值 (-1000~1000)
```

**示例:**
```
motor left 500       // 左电机半速前进
motor right -300     // 右电机反向速度
motor both 1000      // 两电机全速前进
```

---

## 系统状态查询

### 1. 完整系统状态
```
status               - 显示完整系统状态
```

显示内容：
- 系统状态码 (0=初始化, 1=就绪, 2=运行, 3=错误)
- 电池电压 (V)
- 电机电流 (A)
- 左/右电机速度 (RPM) 和霍尔传感器状态
- 油门和转向值
- 电机使能状态

### 2. ADC 原始数据
```
adc                  - 显示 ADC 采样值
```

显示内容：
- 电池电压 ADC 值
- 电流检测 ADC 值
- 温度 ADC 值

### 3. 电池电压
```
battery              - 显示电池电压和状态
```

状态指示：
- 正常
- 低压警告 (< BATTERY_LOW_VOLTAGE)
- 严重低压 (< BATTERY_DEAD_VOLTAGE)

### 4. 电机电流
```
current              - 显示总电流
```

显示警告（如果超过限制）

### 5. 电机速度
```
speed                - 显示左右电机速度
```

### 6. 霍尔传感器状态
```
hall                 - 显示霍尔传感器原始状态和换相位置
```

显示内容：
- 左右霍尔传感器状态值 (0x00-0xFF)
- 换相位置 (1-6)

---

## 蜂鸣器控制

---

## GPIO 控制命令

支持任意 GPIO 引脚读取和设置，格式如下：

```
gpio read PA3         - 读取 PA3 的原始 GPIO 值
gpio write PA3 1      - 将 PA3 设为推挽输出并写入 1（取值 0 或 1）
```

注：引脚格式为 `PXn`，例如 `PA0`、`PB5`。命令会自动启用相应 GPIO 时钟并显示原始读数。

---

## PWM 控制命令

可以为所有默认的 PWM 定时器设定频率与占空比（注意并非所有引脚都支持 PWM）：

```
pwm all <hz> [duty]   - 将所有可用 PWM 定时器设为 <hz> 频率，占空比可选（0-100，默认50）
```

也支持单个引脚的 PWM 输出，当前仅实现 PA3（TIM1_CH3）：

```
pwm PA3 [hz] [duty]   - 将 PA3 设为 PWM 输出，hz 默认为 1，duty 默认为 50
```

示例：
```
pwm all 1000 50       # 将 PWM 设为 1kHz，50% 占空比
pwm PA3 1 50          # 将 PA3 设为 1Hz 50% PWM 输出
```

注意：该命令会重新配置 TIM1 CH3 和 PA3 的输出模式，面向调试和单通道 PWM 控制用途。

---

## 简写 GPIO 命令

支持更短的读写命令：

```
r PA3       # 等价于 `gpio read PA3`
r A3        # 等价于 `gpio read PA3`
w A3 1      # 等价于 `gpio write PA3 1`
```

命令接受 `PXn` 或 `Xn` 格式（例如 `PA3`、`A3`、`pb5`），不区分大小写。



### 1. 多次蜂鸣
```
beep <n>            - 蜂鸣 n 次 (1-10)
```

**示例:**
```
beep 3              // 蜂鸣 3 次
```

### 2. 蜂鸣器模式
```
buzzer <pattern>    - 播放特定蜂鸣器模式 (1-10)
```

**预定义模式:**
- 1: 单次蜂鸣（确认音）
- 2: 双蜂鸣（就绪音）
- 3: 三蜂鸣（警告音）
- 4: 四蜂鸣（关机音）
- 5-10: 其他模式

---

## 系统管理

### 1. 系统复位
```
reset               - 系统软复位
```

**警告**: 此命令将重启系统，需要 1 秒确认时间。

### 2. 帮助信息
```
help                - 显示所有命令帮助
?                   - 快速帮助
```

---

## 命令示例

### 场景 1: 测试左电机
```
// 1. 启用电机
motor enable

// 2. 设置左电机速度
motor left 500

// 3. 检查速度和电流
status
speed
current

// 4. 停止
motor left 0
motor disable
```

### 场景 2: 完整系统诊断
```
help                    // 查看所有命令
status                  // 整体状态
battery                 // 电池状况
hall                    // 霍尔传感器检查
adc                     // ADC 采样值诊断
```

### 场景 3: 双电机控制
```
// 前进
motor both 800

// 左转 (左电机减速，右电机保持)
motor left 400
motor right 800

// 原地右转 (左前进，右反向)
motor left 500
motor right -500

// 停止
motor both 0
```

---

## 参数说明

### PWM 值范围: -1000 ~ 1000
- **0**: 停止
- **1~1000**: 正向转动（速度按值递增）
- **-1~-1000**: 反向转动（速度按值递增）

### 电池电压阈值（defines.h 中可配置）
- **BATTERY_LOW_VOLTAGE**: 低压警告阈值
- **BATTERY_DEAD_VOLTAGE**: 关机电压阈值
- **CURRENT_LIMIT**: 电流限制值

### 霍尔传感器状态编码（二进制）
- Bit 0: Hall U (低电平 = 1)
- Bit 1: Hall V (低电平 = 1)
- Bit 2: Hall W (低电平 = 1)
- 有效状态: 0x01-0x06 (6 步换相)

---

## 故障排查

### 问题：电机不动作
1. 检查 `status` 中 "Motors Enabled" 是否为 "YES"
2. 使用 `motor enable` 启用电机
3. 检查 PWM 值是否在 -1000~1000 范围内
4. 用 `motor left 100` 测试小速度

### 问题：电流过高
1. 检查 `current` 命令输出
2. 如超限，系统会自动禁用电机
3. 检查是否有机械卡阻

### 问题：霍尔传感器无信号
1. 用 `hall` 命令检查状态值
2. 正常状态应在 0x01-0x06 之间
3. 如为 0x00 或 0x07，检查传感器连接

### 问题：无法接收串口数据
1. 检查波特率是否为 115200 bps
2. 确认使用 PB10(TX) 和 PB11(RX)
3. 检查 USB-TTL 模块连接

---

## 串口协议

### 连接参数
- **端口**: USART2 (PB10/PB11)
- **波特率**: 115200 bps
- **数据位**: 8
- **停止位**: 1
- **校验位**: 无
- **流控**: 无

### 格式
- 命令以 `\r\n` 或 `\n` 结尾
- 响应以 `\r\n` 结尾
- 命令提示符: `> `
- 回显已启用（键入内容会显示）
- 支持退格键 (0x08 或 0x7F)

### 通信示例
```
$> motor enable\r\n
> motor enable
Motors enabled

$> status\r\n
> status

=== SYSTEM STATUS ===
State: 1 (0=Init,1=Ready,2=Running,3=Error)
Battery: 36.50V
Current: 0.50A
Left Speed: 0 RPM, Hall: 3
Right Speed: 0 RPM, Hall: 3
Throttle: 0, Steering: 0
Motors Enabled: YES
====================

$>
```

---

## 集成说明

### 主要函数
- `USART_Init(baudrate)` - 串口初始化
- `USART_SendString(str)` - 发送字符串
- `USART_ReadData(buffer, len)` - 读取接收数据
- `USART_Printf(format, ...)` - 格式化输出

### 外部依赖
- `firmware_api.h` - 电机控制 API
- `buzzer.h` - 蜂鸣器控制
- `defines.h` - 系统配置

### 可扩展功能
在 `usart.c` 的 `process_command()` 函数中添加新命令，例如：
```c
else if (strncmp(cmd_copy, "custom", 6) == 0) {
    // 自定义命令处理
}
```

---

## 版本信息

- **固件版本**: 1.0.0
- **MCU**: GD32F103RCT6
- **系统时钟**: 72 MHz
- **PWM 频率**: 16 kHz
- **串口**: USART2 @ 115200 baud

---

## 许可证

该文档适用于 hovercar-gd32 项目的一部分。

