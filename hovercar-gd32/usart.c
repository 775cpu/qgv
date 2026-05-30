/*
 * hovercar-gd32 - 串口调试模块
 * 用于输出调试信息和接收命令
 */

#include "defines.h"
#include "gd32f10x.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "firmware_api.h"
#include "buzzer.h"

// ============================================
// 全局变量
// ============================================

// 串口接收缓冲区
static uint8_t rx_buffer[256];
static uint16_t rx_index = 0;
static FlagStatus rx_ready = RESET;
static uint32_t rx_timestamp = 0;  // 接收时间戳

// 串口发送缓冲区
static uint8_t tx_buffer[256];
static uint16_t tx_index = 0;
static uint16_t tx_length = 0;

static void process_command(const char* command);

// 外部声明（来自 main.c 和 bldc.c）
extern VehicleControl vehicle;
extern uint8_t system_state;
extern MotorState left_motor;
extern MotorState right_motor;
extern adc_buf_t adc_buffer;

// ============================================
// 公共函数
// ============================================
#include <stdint.h>

void USART_Init(uint32_t baudrate)
{
    baudrate=baudrate; // 避免未使用参数的编译警告
    /* 1. 对应 a1: set *(uint32_t*)0x40021018 = *(uint32_t*)0x40021018 | 0x00000008 */
    /* 作用: 使能 GPIOB 时钟 (APB2ENR Bit 3) */
    *(volatile uint32_t*)0x40021018 |= 0x00000008;

    /* 2. 对应 a1: set *(uint32_t*)0x4002101C = *(uint32_t*)0x4002101C | 0x00040000 */
    /* 作用: 使能 USART3 时钟 (APB1ENR Bit 18) */
    *(volatile uint32_t*)0x4002101C |= 0x00040000;

    /* 3. 对应 a1: set *(uint32_t*)0x4000480C = 0x0000 */
    /* 作用: 清除 USART3_CR1，关闭串口准备配置 */
    *(volatile uint32_t*)0x4000480C = 0x0000;

    /* 4. 对应 a1: set *(uint32_t*)0x40010C04 = (*(uint32_t*)0x40010C04 & ~0xFFFF00) | 0x4B00 */
    /* 作用: 配置 GPIOB_CRH。PB10配置为复用推挽输出(0xB)，PB11配置为浮空输入(0x4) */
    *(volatile uint32_t*)0x40010C04 = (*(volatile uint32_t*)0x40010C04 & ~0xFFFF00) | 0x4B00;

    /* 5. 对应 a1: set *(uint32_t*)0x40004808 = 312 */
    /* 作用: 写入波特率寄存器 (BRR)。 */
    /* 注意：a1 中直接写死了 312 (通常对应 36MHz PCLK 下的 115200 波特率)。*/
    /* 为了严格对应，这里默认写入 312。若要让 baudrate 参数生效，可替换为: 
       *(volatile uint32_t*)0x40004808 = (36000000 + baudrate / 2) / baudrate; */
    *(volatile uint32_t*)0x40004808 = 312; 

    /* 6. 对应 a1: set *(uint32_t*)0x4000480C = 0x200C */
    /* 作用: 配置 USART3_CR1，开启 USART(Bit13), TX(Bit3), RX(Bit2) */
    /* 注意：0x200C = 0x2000(USART_ENABLE) | 0x0008(TX_ENABLE) | 0x0004(RX_ENABLE) */
    /* 需要添加接收中断使能位：Bit5 (RXNEIE) = 0x0020 */
    /* 所以最终值应为：0x202C = 0x2000 | 0x0020 | 0x0008 | 0x0004 */
    *(volatile uint32_t*)0x4000480C = 0x202C;
    
    /* 7. 使能 NVIC 中断 */
    /* 使用 DEBUG_USART_IRQn 宏定义，在 defines.h 中定义为 USART2_IRQn */
    NVIC->ISER[DEBUG_USART_IRQn >> 5] = 1 << (DEBUG_USART_IRQn & 0x1F);
}


/**
 * @brief 发送字符串
 * @param str 要发送的字符串
 */
void USART_SendString(const char* str) {
    if (str == NULL) {
        return;
    }
    
    uint32_t len = strlen(str);
    for (uint32_t i = 0; i < len; i++) {
        usart_data_transmit(DEBUG_USART, (uint8_t)str[i]);
        while (usart_flag_get(DEBUG_USART, USART_FLAG_TBE) == RESET);
    }
}

/**
 * @brief 发送数据
 * @param data 数据指针
 * @param length 数据长度
 */
void USART_SendData(const uint8_t* data, uint16_t length) {
    if (data == NULL || length == 0) {
        return;
    }
    
    for (uint16_t i = 0; i < length; i++) {
        usart_data_transmit(DEBUG_USART, data[i]);
        while (usart_flag_get(DEBUG_USART, USART_FLAG_TBE) == RESET);
    }
}

/**
 * @brief 检查是否有接收数据
 * @return 接收状态
 */
FlagStatus USART_DataAvailable(void) {
    return rx_ready;
}

/**
 * @brief 读取接收数据
 * @param buffer 输出缓冲区
 * @param max_length 最大长度
 * @return 实际读取长度
 */
uint16_t USART_ReadData(uint8_t* buffer, uint16_t max_length) {
    if (buffer == NULL || max_length == 0 || rx_ready == RESET) {
        return 0;
    }
    
    uint16_t length = (rx_index < max_length) ? rx_index : max_length;
    memcpy(buffer, rx_buffer, length);
    
    // 清空缓冲区
    rx_index = 0;
    rx_ready = RESET;
    
    return length;
}

/**
 * @brief 格式化输出
 * @param format 格式字符串
 * @param ... 参数
 */
void USART_Printf(const char* format, ...) {
    char buffer[128];
    va_list args;
    
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    USART_SendString(buffer);
}

/**
 * @brief 发送调试信息
 * @param info 调试信息结构
 */
void USART_SendDebugInfo(DebugInfo* info) {
    if (info == NULL) {
        return;
    }
    
    char buffer[256];
    snprintf(buffer, sizeof(buffer),
             "State:%d Battery:%.1fV Current:%.1fA Left:%d/%d Right:%d/%d Ctrl:T:%d S:%d\n",
             info->system_state,
             info->battery_voltage,
             info->total_current,
             (int)info->left_speed,
             info->left_hall_state,
             (int)info->right_speed,
             info->right_hall_state,
             info->throttle,
             info->steering);
    
    USART_SendString(buffer);
}

// ============================================
// 内部函数
// ============================================

/**
 * @brief 处理接收到的字符
 * @param ch 接收到的字符
 */
static void process_received_char(uint8_t ch) {
    // 简单的命令行处理
    if (ch == '\r' || ch == '\n') {
        if (rx_index > 0) {
            rx_buffer[rx_index] = '\0';  // 添加字符串结束符
            rx_ready = SET;
            
            // 获取当前时间戳 (毫秒，基于 SysTick)
            // SysTick 配置为 1ms 中断，VAL 寄存器记录当前计数值
            uint32_t ms_counter = (SystemCoreClock / 1000) - SysTick->VAL;
            
            // 立即发送时间戳 echo
            USART_Printf("\r\n[%10u ms] RX: ", ms_counter);
            USART_SendString((char*)rx_buffer);
            USART_SendString("\r\n> ");
            
            // 处理命令
            process_command((char*)rx_buffer);
        }
        rx_index = 0;
    } else if (ch == 0x08 || ch == 0x7F) {  // 退格或删除
        if (rx_index > 0) {
            rx_index--;
            USART_SendString("\b \b");  // 回显退格
        }
    } else if (rx_index < sizeof(rx_buffer) - 1) {
        rx_buffer[rx_index++] = ch;
        usart_data_transmit(DEBUG_USART, ch);  // 回显字符
        while (usart_flag_get(DEBUG_USART, USART_FLAG_TBE) == RESET);
    }
}

// 命令帮助函数
static int16_t parse_pwm_value(const char* str) {
    int16_t value = atoi(str);
    return CLAMP(value, -1000, 1000);
}





// 解析形如 "PA3" 的引脚描述，返回 1=成功，0=失败
static int parse_pin_string(const char* s, uint32_t* out_port, uint16_t* out_pin) {
    if (!s) return 0;
    // Accept formats: PA3, pa3, A3, a3
    const char* p = s;
    // skip leading spaces
    while (*p == ' ') p++;

    char portch = 0;
    const char* numptr = NULL;
    if ((p[0] == 'P' || p[0] == 'p') && (p[1] >= 'A' && p[1] <= 'z')) {
        portch = p[1];
        numptr = p + 2;
    } else if ((p[0] >= 'A' && p[0] <= 'z')) {
        portch = p[0];
        numptr = p + 1;
    } else {
        return 0;
    }

    if (portch >= 'a' && portch <= 'z') portch -= ('a' - 'A');
    if (portch < 'A' || portch > 'G') return 0;

    int pin = atoi(numptr);
    if (pin < 0 || pin > 15) return 0;

    switch (portch) {
        case 'A': *out_port = (uint32_t)GPIOA; rcu_periph_clock_enable(RCU_GPIOA); break;
        case 'B': *out_port = (uint32_t)GPIOB; rcu_periph_clock_enable(RCU_GPIOB); break;
        case 'C': *out_port = (uint32_t)GPIOC; rcu_periph_clock_enable(RCU_GPIOC); break;
        case 'D': *out_port = (uint32_t)GPIOD; rcu_periph_clock_enable(RCU_GPIOD); break;
        case 'E': *out_port = (uint32_t)GPIOE; rcu_periph_clock_enable(RCU_GPIOE); break;
        default: return 0;
    }

    *out_pin = (uint16_t)(1U << pin);
    return 1;
}

// 读取任意引脚值（原始GPIO读数）并输出
static void cmd_gpio_read(const char* arg) {
    uint32_t port; uint16_t pin;
    if (!parse_pin_string(arg, &port, &pin)) {
        USART_SendString("gpio read usage: gpio read PA3\r\n");
        return;
    }
    uint8_t raw = GPIO_INPUT_BIT_GET((uint32_t)port, pin);
    USART_Printf("GPIO %s: raw=%d (pin mask 0x%04X)\r\n", arg, raw, pin);
}

// 将任意引脚设为输出并写入 0/1
static void cmd_gpio_write(const char* arg, const char* valstr) {
    uint32_t port; uint16_t pin;
    if (!parse_pin_string(arg, &port, &pin)) {
        USART_SendString("gpio write usage: gpio write PA3 1\r\n");
        return;
    }
    int v = atoi(valstr);
    // 配置为推挽输出
    gpio_init((uint32_t)port, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, pin);
    if (v) gpio_bit_set((uint32_t)port, pin); else gpio_bit_reset((uint32_t)port, pin);
    USART_Printf("GPIO %s set to %d\r\n", arg, v ? 1 : 0);
}

static int calculate_pwm_timer_params(int hz, uint32_t* prescaler_out, uint32_t* period_out) {
    if (hz <= 0 || prescaler_out == NULL || period_out == NULL) {
        return 0;
    }

    uint32_t timer_clock = SYSTEM_CORE_CLOCK;
    uint32_t total_ticks = timer_clock / (uint32_t)hz;
    if (total_ticks == 0) {
        return 0;
    }

    uint32_t prescaler = (total_ticks - 1U) / 0x10000U;
    if (prescaler > 0xFFFFU) {
        return 0;
    }

    uint32_t period = total_ticks / (prescaler + 1U);
    if (period == 0 || period > 0xFFFFU) {
        return 0;
    }

    *prescaler_out = prescaler;
    *period_out = period;
    return 1;
}

// 设置所有可用 PWM 定时器频率和占空比（默认 50%）
static void cmd_pwm_all(const char* hzstr, const char* dutystr) {
    int hz = atoi(hzstr);
    int duty = dutystr ? atoi(dutystr) : 50;
    if (hz <= 0 || duty < 0 || duty > 100) {
        USART_SendString("pwm all usage: pwm all <hz> [duty_percent]\r\n");
        return;
    }

    uint32_t prescaler;
    uint32_t period;
    if (!calculate_pwm_timer_params(hz, &prescaler, &period)) {
        USART_Printf("Requested frequency out of range: %d Hz\r\n", hz);
        return;
    }

    uint32_t pulse = (((uint32_t)period + 1U) * (uint32_t)duty) / 100U;
    if (pulse > period) {
        pulse = period;
    }

    // Ensure timer and GPIO/AF clocks enabled and reset TIM1 as user script
    rcu_periph_reset_enable(RCU_TIMER1RST);
    rcu_periph_reset_disable(RCU_TIMER1RST);
    rcu_periph_clock_enable(RCU_TIMER1);
    rcu_periph_clock_enable(RCU_GPIOA);
    rcu_periph_clock_enable(RCU_AF);
    // Ensure TIM1 uses default AF mapping for its channels
    volatile uint32_t* AFIO_MAPR = (uint32_t*)0x40010004;
    *AFIO_MAPR &= ~(3 << 8);

    timer_parameter_struct timer_init_struct;
    timer_oc_parameter_struct oc_init_struct;

    // 通用定时器初始化结构
    timer_deinit(TIMER1);
    timer_init_struct.prescaler = (uint16_t)prescaler;
    timer_init_struct.alignedmode = TIMER_COUNTER_CENTER_BOTH;
    timer_init_struct.counterdirection = TIMER_COUNTER_UP;
    timer_init_struct.period = (uint16_t)period;
    timer_init_struct.clockdivision = TIMER_CKDIV_DIV1;
    timer_init_struct.repetitioncounter = 0;
    timer_init(TIMER1, &timer_init_struct);

    oc_init_struct.outputstate = TIMER_CCX_ENABLE;
    oc_init_struct.outputnstate = TIMER_CCXN_ENABLE;
    oc_init_struct.ocpolarity = TIMER_OC_POLARITY_HIGH;
    oc_init_struct.ocnpolarity = TIMER_OCN_POLARITY_LOW;
    oc_init_struct.ocidlestate = TIMER_OC_IDLE_STATE_LOW;
    oc_init_struct.ocnidlestate = TIMER_OCN_IDLE_STATE_HIGH;

    timer_channel_output_config(TIMER1, TIMER_CH_1, &oc_init_struct);
    timer_channel_output_config(TIMER1, TIMER_CH_2, &oc_init_struct);
    timer_channel_output_config(TIMER1, TIMER_CH_3, &oc_init_struct);

    timer_channel_output_mode_config(TIMER1, TIMER_CH_1, TIMER_OC_MODE_PWM1);
    timer_channel_output_mode_config(TIMER1, TIMER_CH_2, TIMER_OC_MODE_PWM1);
    timer_channel_output_mode_config(TIMER1, TIMER_CH_3, TIMER_OC_MODE_PWM1);

    timer_channel_output_pulse_value_config(TIMER1, TIMER_CH_1, (uint16_t)pulse);
    timer_channel_output_pulse_value_config(TIMER1, TIMER_CH_2, (uint16_t)pulse);
    timer_channel_output_pulse_value_config(TIMER1, TIMER_CH_3, (uint16_t)pulse);
    timer_enable(TIMER1);

    // TIM3 CH3
    timer_deinit(TIMER3);
    timer_init_struct.period = (uint16_t)period;
    timer_init(TIMER3, &timer_init_struct);
    timer_channel_output_config(TIMER3, TIMER_CH_3, &oc_init_struct);
    timer_channel_output_mode_config(TIMER3, TIMER_CH_3, TIMER_OC_MODE_PWM1);
    timer_channel_output_pulse_value_config(TIMER3, TIMER_CH_3, (uint16_t)pulse);
    timer_enable(TIMER3);

    // TIM4 CH1, CH3
    timer_deinit(TIMER4);
    timer_init_struct.period = (uint16_t)period;
    timer_init(TIMER4, &timer_init_struct);
    timer_channel_output_config(TIMER4, TIMER_CH_1, &oc_init_struct);
    timer_channel_output_config(TIMER4, TIMER_CH_3, &oc_init_struct);
    timer_channel_output_mode_config(TIMER4, TIMER_CH_1, TIMER_OC_MODE_PWM1);
    timer_channel_output_mode_config(TIMER4, TIMER_CH_3, TIMER_OC_MODE_PWM1);
    timer_channel_output_pulse_value_config(TIMER4, TIMER_CH_1, (uint16_t)pulse);
    timer_channel_output_pulse_value_config(TIMER4, TIMER_CH_3, (uint16_t)pulse);
    timer_enable(TIMER4);

    USART_Printf("PWM set: %d Hz, duty %d%% (psc=%lu, period=%lu, pulse=%lu)\r\n", hz, duty, prescaler, period, pulse);
}

// 为单个引脚配置 PWM（目前实现 PA3 -> TIM1_CH3）
static void cmd_pwm_pin(const char* pinstr, int hz, int duty) {
    // 1. 解析引脚（保持你的逻辑）
    uint32_t port; uint16_t pin;
    if (!parse_pin_string(pinstr, &port, &pin)) {
        USART_Printf("Unsupported pin: %s\r\n", pinstr);
        return;
    }

    // 严格校验是否为 PA3
    if ((uint32_t)GPIOA != port || pin != GPIO_PIN_3) {
        USART_Printf("pwm pin: only PA3 currently supported\r\n");
        return;
    }

    // 2. 计算定时器参数
    uint32_t prescaler;
    uint32_t period;
    if (!calculate_pwm_timer_params(hz, &prescaler, &period)) {
        USART_Printf("Requested frequency out of range: %d Hz\r\n", hz);
        return;
    }
    uint32_t pulse = (((uint32_t)period + 1U) * (uint32_t)duty) / 100U;

    // 3. 严格按照 GDB 成功的逻辑配置
    // 复位 TIMER1
    rcu_periph_reset_enable(RCU_TIMER1RST);
    rcu_periph_reset_disable(RCU_TIMER1RST);
    
    // 开启时钟
    rcu_periph_clock_enable(RCU_TIMER1);
    rcu_periph_clock_enable(RCU_GPIOA);
    rcu_periph_clock_enable(RCU_AF);
    
    // 清除 AFIO_MAPR 的 Bit 9:8 (TIMER1 重映射控制位)
    volatile uint32_t* AFIO_MAPR = (uint32_t*)0x40010004;
    *AFIO_MAPR &= ~(3 << 8);

    // 4. 配置 PA3 为复用推挽输出 (AF_PP)
    gpio_init(GPIOA, GPIO_MODE_AF_PP, GPIO_OSPEED_50MHZ, GPIO_PIN_3);

    // 5. 直接寄存器配置，严格匹配 GDB 脚本
    volatile uint32_t* TIMER1_CTL0   = (uint32_t*)(TIMER1 + 0x00);
    volatile uint32_t* TIMER1_SWEVG  = (uint32_t*)(TIMER1 + 0x14);
    volatile uint32_t* TIMER1_CHCTL2 = (uint32_t*)(TIMER1 + 0x20);
    volatile uint32_t* TIMER1_CHCTL1 = (uint32_t*)(TIMER1 + 0x1C);
    volatile uint32_t* TIMER1_CNT    = (uint32_t*)(TIMER1 + 0x24);
    volatile uint32_t* TIMER1_PSC    = (uint32_t*)(TIMER1 + 0x28);
    volatile uint32_t* TIMER1_CAR    = (uint32_t*)(TIMER1 + 0x2C);
    volatile uint32_t* TIMER1_CH3CV  = (uint32_t*)(TIMER1 + 0x40);
    
    // 设置计数器、预分频器、自动重载值
    *TIMER1_CNT   = 0;
    *TIMER1_PSC   = (uint32_t)prescaler;
    *TIMER1_CAR   = (uint32_t)period;
    *TIMER1_CH3CV = (uint32_t)pulse;
    
    // 配置通道3为PWM模式1
    *TIMER1_CHCTL1 = (*TIMER1_CHCTL1 & ~0xFF) | 0x68;  // CH3配置为PWM模式1
    
    // 使能通道3输出
    *TIMER1_CHCTL2 &= ~(1 << 13);  // 清除CH3N输出使能
    *TIMER1_CHCTL2 |= (1 << 12);   // 设置CH3输出使能
    
    // 生成软件更新事件
    *TIMER1_SWEVG = (1 << 0);
    
    // 使能定时器
    *TIMER1_CTL0 |= (1 << 0);

    USART_Printf("PA3 (TIMER1_CH3) PWM active: %d Hz, duty %d%% (psc=%lu, period=%lu, pulse=%lu)\r\n", 
                 hz, duty, prescaler, period, pulse);
}


/**
 * @brief 处理命令
 * @param command 命令字符串
 */
static void process_command(const char* command) {
    if (command == NULL || strlen(command) == 0) {
        return;
    }
    
    char cmd_copy[256];
    strncpy(cmd_copy, command, sizeof(cmd_copy) - 1);
    cmd_copy[sizeof(cmd_copy) - 1] = '\0';
    
    // 转换为小写以便比较
    for (int i = 0; cmd_copy[i]; i++) {
        if (cmd_copy[i] >= 'A' && cmd_copy[i] <= 'Z') {
            cmd_copy[i] += 32;
        }
    }
    
    // 命令处理
    if (strcmp(cmd_copy, "help") == 0 || strcmp(cmd_copy, "?") == 0) {
        USART_SendString("\r\n=== HOVERCAR SERIAL COMMANDS ===\r\n");
        USART_SendString("Motor Control:\r\n");
        USART_SendString("  motor enable              - Enable motors\r\n");
        USART_SendString("  motor disable             - Disable motors\r\n");
        USART_SendString("  motor left <pwm>          - Set left motor PWM (-1000~1000)\r\n");
        USART_SendString("  motor right <pwm>         - Set right motor PWM (-1000~1000)\r\n");
        USART_SendString("  motor both <pwm>          - Set both motors PWM (-1000~1000)\r\n");
        USART_SendString("\r\nSystem Status:\r\n");
        USART_SendString("  status                    - Show system status\r\n");
        USART_SendString("  adc                       - Show ADC values\r\n");
        USART_SendString("  battery                   - Show battery voltage\r\n");
        USART_SendString("  current                   - Show motor current\r\n");
        USART_SendString("  speed                     - Show motor speeds\r\n");
        USART_SendString("  hall                      - Show hall sensor states\r\n");
        USART_SendString("\r\nBuzzer Control:\r\n");
        USART_SendString("  beep <n>                  - Beep n times (1-10)\r\n");
        USART_SendString("  buzzer <pattern>          - Play buzzer pattern\r\n");
        USART_SendString("\r\nSystem:\r\n");
        USART_SendString("  reset                     - Reset system\r\n");
        USART_SendString("  help/?                    - Show this help\r\n");
        USART_SendString("================================\r\n");
    }
    
    else if (strncmp(cmd_copy, "motor ", 6) == 0) {
        const char* motor_cmd = command + 6;
        
        if (strncmp(motor_cmd, "enable", 6) == 0) {
            SetMotorsEnable(SET);
            vehicle.enable = SET;
            USART_SendString("Motors enabled\r\n");
        }
        else if (strncmp(motor_cmd, "disable", 7) == 0) {
            SetMotorsEnable(RESET);
            vehicle.enable = RESET;
            USART_SendString("Motors disabled\r\n");
        }
        else if (strncmp(motor_cmd, "left ", 5) == 0) {
            int16_t pwm = parse_pwm_value(motor_cmd + 5);
            SetLeftMotorPWM(pwm);
            USART_Printf("Left motor PWM: %d\r\n", pwm);
        }
        else if (strncmp(motor_cmd, "right ", 6) == 0) {
            int16_t pwm = parse_pwm_value(motor_cmd + 6);
            SetRightMotorPWM(pwm);
            USART_Printf("Right motor PWM: %d\r\n", pwm);
        }
        else if (strncmp(motor_cmd, "both ", 5) == 0) {
            int16_t pwm = parse_pwm_value(motor_cmd + 5);
            SetLeftMotorPWM(pwm);
            SetRightMotorPWM(pwm);
            USART_Printf("Both motors PWM: %d\r\n", pwm);
        }
        else {
            USART_SendString("Motor command: enable|disable|left|right|both\r\n");
        }
    }

        else if (strncmp(cmd_copy, "gpio ", 5) == 0) {
            const char* args = command + 5;
            while (*args == ' ') args++;
            if (strncasecmp(args, "read ", 5) == 0) {
                const char* pinstr = args + 5;
                while (*pinstr == ' ') pinstr++;
                cmd_gpio_read(pinstr);
            } else if (strncasecmp(args, "write ", 6) == 0) {
                const char* rest = args + 6;
                while (*rest == ' ') rest++;
                // split pin and value
                char pinbuf[16]; int i=0;
                while (*rest && *rest != ' ' && i < (int)sizeof(pinbuf)-1) pinbuf[i++] = *rest++;
                pinbuf[i] = '\0';
                while (*rest == ' ') rest++;
                cmd_gpio_write(pinbuf, rest);
            } else {
                USART_SendString("gpio commands:\r\n  gpio read PA3\r\n  gpio write PA3 1\r\n");
            }
        }
        else if (cmd_copy[0] == 'r' && cmd_copy[1] == ' ') {
            const char* arg = command + 2;
            while (*arg == ' ') arg++;
            char pinbuf[16]; int i=0;
            while (*arg && *arg != ' ' && i < (int)sizeof(pinbuf)-1) pinbuf[i++] = *arg++;
            pinbuf[i] = '\0';
            if (i == 0) {
                USART_SendString("r usage: r PA3 or r A3\r\n");
            } else {
                cmd_gpio_read(pinbuf);
            }
        }
        else if (cmd_copy[0] == 'w' && cmd_copy[1] == ' ') {
            const char* rest = command + 2;
            while (*rest == ' ') rest++;
            char pinbuf[16]; int i=0;
            while (*rest && *rest != ' ' && i < (int)sizeof(pinbuf)-1) pinbuf[i++] = *rest++;
            pinbuf[i] = '\0';
            while (*rest == ' ') rest++;
            if (i == 0 || *rest == 0) {
                USART_SendString("w usage: w PA3 1\r\n");
            } else {
                cmd_gpio_write(pinbuf, rest);
            }
        }
    
    else if (strcmp(cmd_copy, "status") == 0) {
        USART_SendString("\r\n=== SYSTEM STATUS ===\r\n");
        USART_Printf("State: %d (0=Init,1=Ready,2=Running,3=Error)\r\n", system_state);
        USART_Printf("Battery: %.2fV\r\n", vehicle.battery_voltage);
        USART_Printf("Current: %.2fA\r\n", GetTotalCurrent());
        USART_Printf("Left Speed: %.0f RPM, Hall: %d\r\n", GetLeftMotorSpeed(), left_motor.hall_state);
        USART_Printf("Right Speed: %.0f RPM, Hall: %d\r\n", GetRightMotorSpeed(), right_motor.hall_state);
        USART_Printf("Throttle: %d, Steering: %d\r\n", vehicle.throttle, vehicle.steering);
        USART_Printf("Motors Enabled: %s\r\n", vehicle.enable ? "YES" : "NO");
        USART_SendString("====================\r\n");
    }
    
    else if (strcmp(cmd_copy, "adc") == 0) {
        USART_SendString("\r\n=== ADC VALUES ===\r\n");
        USART_Printf("Battery ADC: %d (counts)\r\n", adc_buffer.battery);
        USART_Printf("Current ADC: %d (counts)\r\n", adc_buffer.current);
        USART_Printf("Temp ADC: %d (counts)\r\n", adc_buffer.temp);
        USART_SendString("==================\r\n");
    }
    
    else if (strcmp(cmd_copy, "battery") == 0) {
        USART_Printf("Battery Voltage: %.2fV\r\n", vehicle.battery_voltage);
        if (vehicle.battery_voltage < BATTERY_DEAD_VOLTAGE) {
            USART_SendString("WARNING: Battery critically low!\r\n");
        } else if (vehicle.battery_voltage < BATTERY_LOW_VOLTAGE) {
            USART_SendString("WARNING: Battery low!\r\n");
        } else {
            USART_SendString("Battery status: Normal\r\n");
        }
    }
    
    else if (strcmp(cmd_copy, "current") == 0) {
        float current = GetTotalCurrent();
        USART_Printf("Motor Current: %.2fA\r\n", current);
        if (current > CURRENT_LIMIT) {
            USART_SendString("WARNING: Current exceeds limit!\r\n");
        }
    }
    
    else if (strcmp(cmd_copy, "speed") == 0) {
        USART_Printf("Left Motor Speed: %.0f RPM\r\n", GetLeftMotorSpeed());
        USART_Printf("Right Motor Speed: %.0f RPM\r\n", GetRightMotorSpeed());
    }
    
    else if (strncmp(cmd_copy, "pwm ", 4) == 0) {
        const char* args = command + 4;
        while (*args == ' ') args++;
        // args may be: all <hz> [duty]  OR  <pin> [hz] [duty]
        if (strncasecmp(args, "all ", 4) == 0) {
            const char* params = args + 4;
            while (*params == ' ') params++;
            // parse hz and optional duty
            char hzbuf[16] = {0}; int i=0;
            while (*params && *params != ' ' && i < (int)sizeof(hzbuf)-1) hzbuf[i++] = *params++;
            hzbuf[i] = '\0';
            while (*params == ' ') params++;
            const char* dutystr = NULL;
            if (*params) dutystr = params;
            cmd_pwm_all(hzbuf, dutystr);
        } else {
            // treat first token as pin
            char pinbuf[16] = {0}; int i=0;
            const char* p = args;
            while (*p && *p != ' ' && i < (int)sizeof(pinbuf)-1) pinbuf[i++] = *p++;
            pinbuf[i] = '\0';
            while (*p == ' ') p++;
            int hz = 1;
            int duty = 50;
            if (*p) {
                char hzbuf[32] = {0}; int j = 0;
                while (*p && *p != ' ' && j < (int)sizeof(hzbuf) - 1) hzbuf[j++] = *p++;
                hzbuf[j] = '\0';
                if (hzbuf[0]) {
                    hz = atoi(hzbuf);
                }
                while (*p == ' ') p++;
                if (*p) {
                    duty = atoi(p);
                }
            }
            if (pinbuf[0]) cmd_pwm_pin(pinbuf, hz, duty);
            else USART_SendString("pwm commands:\r\n  pwm all <hz> [duty_percent]\r\n  pwm PA3 [hz] [duty_percent]\r\n");
        }
    }
    
    else if (strcmp(cmd_copy, "hall") == 0) {
        // 使用 bldc.c 中的 read_hall_sensors 函数读取霍尔传感器
        // 注意：read_hall_sensors 返回的是低电平有效的编码（0=高电平，1=低电平）
        uint8_t left_hall_raw = read_hall_sensors(
            LEFT_HALL_U_PORT, LEFT_HALL_U_PIN,
            LEFT_HALL_V_PORT, LEFT_HALL_V_PIN,
            LEFT_HALL_W_PORT, LEFT_HALL_W_PIN
        );
        
        uint8_t right_hall_raw = read_hall_sensors(
            RIGHT_HALL_U_PORT, RIGHT_HALL_U_PIN,
            RIGHT_HALL_V_PORT, RIGHT_HALL_V_PIN,
            RIGHT_HALL_W_PORT, RIGHT_HALL_W_PIN
        );
        
        // 转换为高电平有效的表示（1=高电平，0=低电平）用于显示
        uint8_t left_u = (left_hall_raw & 0x01) ? 0 : 1;
        uint8_t left_v = (left_hall_raw & 0x02) ? 0 : 1;
        uint8_t left_w = (left_hall_raw & 0x04) ? 0 : 1;
        uint8_t left_high_active = (left_u << 0) | (left_v << 1) | (left_w << 2);
        
        uint8_t right_u = (right_hall_raw & 0x01) ? 0 : 1;
        uint8_t right_v = (right_hall_raw & 0x02) ? 0 : 1;
        uint8_t right_w = (right_hall_raw & 0x04) ? 0 : 1;
        uint8_t right_high_active = (right_u << 0) | (right_v << 1) | (right_w << 2);
        
        USART_SendString("\r\n=== HALL SENSOR STATES ===\r\n");
        USART_SendString("Left Hall pins (低电平有效):\r\n");
        USART_Printf("  LEFT_HALL_U (PB5): raw=0x%02X, high_active=%u\r\n", 
                     (left_hall_raw & 0x01), left_u);
        USART_Printf("  LEFT_HALL_V (PB6): raw=0x%02X, high_active=%u\r\n", 
                     ((left_hall_raw >> 1) & 0x01), left_v);
        USART_Printf("  LEFT_HALL_W (PB7): raw=0x%02X, high_active=%u\r\n", 
                     ((left_hall_raw >> 2) & 0x01), left_w);
        USART_Printf("Left Raw State (低电平有效): 0x%02X (%d)\r\n", left_hall_raw, left_hall_raw);
        USART_Printf("Left High Active State: 0x%02X (%d)\r\n", left_high_active, left_high_active);
        
        // 霍尔状态解释
        USART_SendString("Left Hall Interpretation:\r\n");
        if (left_high_active == 0x00 || left_high_active == 0x07) {
            USART_SendString("  WARNING: Hardware fault (000/111) - Hall disconnected, shorted, or wiring error\r\n");
        } else if (left_high_active == 0x01 || left_high_active == 0x03 || left_high_active == 0x02 || 
                   left_high_active == 0x06 || left_high_active == 0x04 || left_high_active == 0x05) {
            USART_SendString("  NORMAL: Motor stationary, valid Hall state\r\n");
            // 单个霍尔输出解释
            if (left_high_active == 0x01) USART_SendString("    Hall U=1: Position aligned with N pole\r\n");
            if (left_high_active == 0x02) USART_SendString("    Hall V=1: Position aligned with N pole\r\n");
            if (left_high_active == 0x04) USART_SendString("    Hall W=1: Position aligned with N pole\r\n");
            if (left_u == 0) USART_SendString("    Hall U=0: Position aligned with S pole\r\n");
            if (left_v == 0) USART_SendString("    Hall V=0: Position aligned with S pole\r\n");
            if (left_w == 0) USART_SendString("    Hall W=0: Position aligned with S pole\r\n");
        }
        
        USART_Printf("Left Motor Position: %d\r\n", left_motor.position);
        
        USART_SendString("\r\nRight Hall pins (低电平有效):\r\n");
        USART_Printf("  RIGHT_HALL_U (PC10): raw=0x%02X, high_active=%u\r\n", 
                     (right_hall_raw & 0x01), right_u);
        USART_Printf("  RIGHT_HALL_V (PC11): raw=0x%02X, high_active=%u\r\n", 
                     ((right_hall_raw >> 1) & 0x01), right_v);
        USART_Printf("  RIGHT_HALL_W (PC12): raw=0x%02X, high_active=%u\r\n", 
                     ((right_hall_raw >> 2) & 0x01), right_w);
        USART_Printf("Right Raw State (低电平有效): 0x%02X (%d)\r\n", right_hall_raw, right_hall_raw);
        USART_Printf("Right High Active State: 0x%02X (%d)\r\n", right_high_active, right_high_active);
        
        // 霍尔状态解释
        USART_SendString("Right Hall Interpretation:\r\n");
        if (right_high_active == 0x00 || right_high_active == 0x07) {
            USART_SendString("  WARNING: Hardware fault (000/111) - Hall disconnected, shorted, or wiring error\r\n");
        } else if (right_high_active == 0x01 || right_high_active == 0x03 || right_high_active == 0x02 || 
                   right_high_active == 0x06 || right_high_active == 0x04 || right_high_active == 0x05) {
            USART_SendString("  NORMAL: Motor stationary, valid Hall state\r\n");
            // 单个霍尔输出解释
            if (right_high_active == 0x01) USART_SendString("    Hall U=1: Position aligned with N pole\r\n");
            if (right_high_active == 0x02) USART_SendString("    Hall V=1: Position aligned with N pole\r\n");
            if (right_high_active == 0x04) USART_SendString("    Hall W=1: Position aligned with N pole\r\n");
            if (right_u == 0) USART_SendString("    Hall U=0: Position aligned with S pole\r\n");
            if (right_v == 0) USART_SendString("    Hall V=0: Position aligned with S pole\r\n");
            if (right_w == 0) USART_SendString("    Hall W=0: Position aligned with S pole\r\n");
        }
        
        USART_Printf("Right Motor Position: %d\r\n", right_motor.position);
        
        // 显示全局变量状态用于比较
        USART_SendString("\r\n=== GLOBAL VARIABLE STATES (from bldc.c) ===\r\n");
        USART_Printf("left_motor.hall_state: 0x%02X\r\n", left_motor.hall_state);
        USART_Printf("right_motor.hall_state: 0x%02X\r\n", right_motor.hall_state);
    }
    
    else if (strncmp(cmd_copy, "beep", 4) == 0) {
        const char* param = command + 4;
        while (*param == ' ') param++;
        int beeps = atoi(param);
        if (beeps > 0 && beeps <= 10) {
            for (int i = 0; i < beeps; i++) {
                beep_pattern(1);
                Delay(100);
            }
            USART_Printf("Beep x%d\r\n", beeps);
        } else {
            USART_SendString("Beep count: 1-10\r\n");
        }
    }
    
    else if (strncmp(cmd_copy, "buzzer", 6) == 0) {
        const char* param = command + 6;
        while (*param == ' ') param++;
        int pattern = atoi(param);
        if (pattern > 0 && pattern <= 10) {
            beep_pattern(pattern);
            USART_Printf("Buzzer pattern: %d\r\n", pattern);
        } else {
            USART_SendString("Buzzer pattern: 1-10\r\n");
        }
    }
    
    else if (strcmp(cmd_copy, "reset") == 0) {
        USART_SendString("System reset in 1 second...\r\n");
        Delay(1000);
        NVIC_SystemReset();
    }
    
    else {
        USART_Printf("Unknown command: '%s'\r\n", command);
        USART_SendString("Type 'help' for available commands\r\n");
    }
}

// ============================================
// 中断处理函数
// ============================================

/**
 * @brief USART2 中断处理 (调试串口)
 */
void USART2_IRQHandler(void) {
    if (usart_interrupt_flag_get(DEBUG_USART, USART_INT_FLAG_RBNE) != RESET) {
        uint8_t ch = usart_data_receive(DEBUG_USART);
        process_received_char(ch);
        usart_interrupt_flag_clear(DEBUG_USART, USART_INT_FLAG_RBNE);
    }
    
    if (usart_interrupt_flag_get(DEBUG_USART, USART_INT_FLAG_TC) != RESET) {
        usart_interrupt_flag_clear(DEBUG_USART, USART_INT_FLAG_TC);
    }
}

// ============================================
// 重定向printf到串口
// ============================================

#ifdef __GNUC__
// 对于GCC编译器
int _write(int file, char *ptr, int len) {
    (void)file;
    USART_SendData((uint8_t*)ptr, len);
    return len;
}
#else
// 对于其他编译器
int fputc(int ch, FILE *f) {
    (void)f;
    usart_data_transmit(DEBUG_USART, (uint8_t)ch);
    while (usart_flag_get(DEBUG_USART, USART_FLAG_TBE) == RESET);
    return ch;
}
#endif