# 🚀 Hovercar-GD32 串口控制快速入门

## 五秒快速开始

### 1️⃣ 连接硬件
- USB-TTL 模块连接到 **PB10 (TX)** 和 **PB11 (RX)**
- 波特率: **115200 bps**

### 2️⃣ 打开串口终端
```bash
# Linux/Mac
screen /dev/ttyUSB0 115200

# 或使用 minicom
minicom -D /dev/ttyUSB0 -b 115200
```

### 3️⃣ 输入命令
```
help                # 查看所有命令
motor enable        # 启用电机
motor both 500      # 两个电机半速前进
status              # 查看系统状态
```

---

## 🎮 最常用的 5 个命令

| 命令 | 功能 | 示例 |
|------|------|------|
| `help` | 显示帮助 | `help` ↵ |
| `motor enable` | 启用电机 | `motor enable` ↵ |
| `motor both <pwm>` | 控制电机速度 | `motor both 800` ↵ |
| `status` | 查看状态 | `status` ↵ |
| `motor disable` | 禁用电机 | `motor disable` ↵ |

---

## 📊 控制示例

### 🔴 完整工作流
```
$ help
=== HOVERCAR SERIAL COMMANDS ===
Motor Control:
  motor enable              - Enable motors
  motor disable             - Disable motors
  ...

$ motor enable
Motors enabled

$ motor both 600
Both motors PWM: 600

$ status
=== SYSTEM STATUS ===
State: 2 (0=Init,1=Ready,2=Running,3=Error)
Battery: 36.50V
Current: 2.30A
Left Speed: 250 RPM, Hall: 3
Right Speed: 245 RPM, Hall: 3
Throttle: 0, Steering: 0
Motors Enabled: YES
====================

$ motor left 300
Left motor PWM: 300

$ motor right 800
Right motor PWM: 800

$ hall
Left Hall State: 0x03 (3)
Right Hall State: 0x03 (3)
Left Motor Position: 4
Right Motor Position: 4

$ motor both 0
Both motors PWM: 0

$ motor disable
Motors disabled
```

---

## ⚙️ PWM 速度参考

```
PWM 值        | 效果
-1000 ~ -501  | 最大反向 ← 逐级减速 → 停止
-500 ~ -101   | 中等反向
-100 ~ -1     | 轻微反向
      0       | 停止 (默认)
      1 ~ 100 | 轻微前进
    101 ~ 500 | 中等前进  
    501 ~ 1000| 最大前进
```

---

## 🔧 故障快速排查

### ❌ 电机不转
```bash
# 1. 查看状态
status

# 2. 检查电池
battery

# 3. 启用电机
motor enable

# 4. 测试小速度
motor both 100
```

### ❌ 串口无反应
```bash
# 检查连接: PB10 (TX) → RXD, PB11 (RX) → TXD
# 检查波特率: 必须是 115200
# 检查流控: 无流控
# 在终端尝试: motor enable
```

### ❌ 电流过高
```bash
# 查看电流
current

# 如果 > 20A，系统自动停止电机
# 检查机械是否卡阻
```

---

## 📱 高级命令总览

### 🔋 电池管理
```bash
battery                 # 显示电压
current                 # 显示电流
adc                     # 原始 ADC 值
```

### 🎯 电机诊断  
```bash
speed                   # 转速诊断
hall                    # 霍尔传感器诊断
motor left 100          # 单独测试左电机
motor right 100         # 单独测试右电机
```

### 🔊 蜂鸣器
```bash
beep 3                  # 蜂鸣 3 次
buzzer 2                # 播放就绪音
```

---

## 📋 完整命令列表

**电机控制:**
- `motor enable` | `motor disable`
- `motor left <pwm>` | `motor right <pwm>` | `motor both <pwm>`

**状态查询:**
- `status` | `battery` | `current` | `speed` | `hall` | `adc`

**蜂鸣器:**
- `beep <n>` | `buzzer <pattern>`

**系统:**
- `reset` | `help` | `?`

---

## 💡 温馨提示

✅ **命令不区分大小写** (MOTOR 等同于 motor)  
✅ **支持退格键** (删除错误输入)  
✅ **参数范围自动检查** (超出范围自动限制)  
✅ **实时状态输出** (每个命令都有反馈)  
✅ **格式化显示** (易读的表格和列表)

---

## 🎯 典型应用场景

### 🏃 前进
```
motor both 800
```

### 🔄 原地转圈 (右转)
```
motor left 500
motor right -500
```

### 🚗 S 弯
```
motor left 800        # 右边速度快
motor right 500       # 左边速度慢
```

### 🛑 紧急停止
```
motor disable
```

---

## 🔗 更多信息

详细文档: [SERIAL_COMMANDS.md](./SERIAL_COMMANDS.md)  
实现说明: [IMPLEMENTATION_SUMMARY.md](./IMPLEMENTATION_SUMMARY.md)  
硬件配置: [defines.h](./defines.h)

---

## ⏱️ 性能指标

| 指标 | 值 |
|------|-----|
| 波特率 | 115200 bps |
| 命令处理延迟 | < 1 ms |
| 电机响应时间 | 即时 (~5 ms) |
| 缓冲区大小 | 256 字节 |
| 最大命令长度 | 255 字符 |

---

**🚀 开始控制你的 Hovercar!**  
*输入 `help` 查看完整命令列表*

