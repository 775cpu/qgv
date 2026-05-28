/*
 * hovercar-gd32 - 硬件初始化
 * GD32F103RCT6 MCU 初始化
 */

#include "defines.h"
#include "gd32f10x.h"
#include <stdio.h>
#include "firmware_api.h"

// ============================================
// 全局变量
// ============================================
volatile uint32_t system_ticks = 0;  // 系统滴答计数

// 内部函数原型
static void My_SystemInit(void);
static void init_pwm_pins(void);
static void init_hall_pins(void);
static void init_control_pins(void);
static void init_adc_pins(void);
static void init_left_motor_timer(void);
static void init_right_motor_timer(void);
static void init_timer3(void);
static void init_timer4(void);
static void init_adc_peripheral(void);
static void init_adc_dma(void);

// ============================================
// 公共函数
// ============================================

/**
 * @brief 系统时钟初始化
 */
void My_SystemInit(void) {
    /* Minimal boot-time system clock init using IRC8M -> PLL 72MHz.
       This is called from the local startup file before main(), avoiding
       the library SystemInit path that requires HXTAL. */
    rcu_osci_on(RCU_IRC8M);
    while (rcu_osci_stab_wait(RCU_IRC8M) == ERROR);

    rcu_ahb_clock_config(RCU_AHB_CKSYS_DIV1);
    rcu_apb1_clock_config(RCU_APB1_CKAHB_DIV2);
    rcu_apb2_clock_config(RCU_APB2_CKAHB_DIV1);

    rcu_pll_config(RCU_PLLSRC_IRC8M_DIV2, RCU_PLL_MUL18);
    rcu_osci_on(RCU_PLL_CK);
    while (rcu_osci_stab_wait(RCU_PLL_CK) == ERROR);

    fmc_wscnt_set(2);
    rcu_system_clock_source_config(RCU_CKSYSSRC_PLL);
    while (rcu_system_clock_source_get() != RCU_SCSS_PLL);

    SystemCoreClock = SYSTEM_CORE_CLOCK;
}

void SystemClock_Init(void) {
    // 1. 使能IRC8M内部时钟
    rcu_osci_on(RCU_IRC8M);
    while (rcu_osci_stab_wait(RCU_IRC8M) == ERROR);
    
    // 2. 配置AHB、APB1、APB2分频
    rcu_ahb_clock_config(RCU_AHB_CKSYS_DIV1);    // AHB = 72MHz
    rcu_apb1_clock_config(RCU_APB1_CKAHB_DIV2);  // APB1 = 36MHz
    rcu_apb2_clock_config(RCU_APB2_CKAHB_DIV1);  // APB2 = 72MHz
    
    // 3. 配置PLL: IRC8M/2 * 18 = 72MHz
    rcu_pll_config(RCU_PLLSRC_IRC8M_DIV2, RCU_PLL_MUL18);
    
    // 4. 使能PLL
    rcu_osci_on(RCU_PLL_CK);
    while (rcu_osci_stab_wait(RCU_PLL_CK) == ERROR);
    
    // 5. 配置Flash等待状态 (72MHz需要2个等待状态)
    fmc_wscnt_set(2);
    
    // 6. 切换系统时钟到PLL
    rcu_system_clock_source_config(RCU_CKSYSSRC_PLL);
    while (rcu_system_clock_source_get() != RCU_SCSS_PLL);
    
    // 7. 更新SystemCoreClock变量
    SystemCoreClock = SYSTEM_CORE_CLOCK;
    
    printf("System clock init complete: %lu Hz\n", SystemCoreClock);
}

/**
 * @brief SysTick初始化
 */
void SysTick_Init(void) {
    // 配置SysTick为1ms中断
    if (SysTick_Config(SystemCoreClock / 1000)) {
        printf("SysTick config failed\n");
        while (1);
    }
    
    // 设置SysTick中断优先级
    NVIC_SetPriority(SysTick_IRQn, 0x00);
    
    printf("SysTick init complete\n");
}

/**
 * @brief GPIO初始化
 */
void GPIO_Init(void) {
    // 使能GPIO时钟
    rcu_periph_clock_enable(RCU_GPIOA);
    rcu_periph_clock_enable(RCU_GPIOB);
    rcu_periph_clock_enable(RCU_GPIOC);
    rcu_periph_clock_enable(RCU_AF);  // 复用功能时钟
    
    // 禁用JTAG，启用SWD (释放PA15, PB3, PB4)
    gpio_pin_remap_config(GPIO_SWJ_SWDPENABLE_REMAP, ENABLE);
    
    printf("GPIO clocks enabled\n");
    
    // 1. 初始化PWM输出引脚
    init_pwm_pins();
    
    // 2. 初始化霍尔传感器引脚
    init_hall_pins();
    
    // 3. 初始化控制引脚
    init_control_pins();
    
    // 4. 初始化ADC引脚
    init_adc_pins();
    
    printf("GPIO init complete\n");
}

/**
 * @brief PWM定时器初始化
 */
void PWM_Timer_Init(void) {
    // 1. 使能定时器时钟
    rcu_periph_clock_enable(RCU_TIMER1);  // 左电机
    rcu_periph_clock_enable(RCU_TIMER3);  // 右电机U相
    rcu_periph_clock_enable(RCU_TIMER4);  // 右电机V,W相
    
    // 2. 初始化左电机定时器 (TIM1)
    init_left_motor_timer();
    
    // 3. 初始化右电机定时器 (TIM3, TIM4)
    init_right_motor_timer();
    
    printf("PWM timer init complete\n");
}

/**
 * @brief ADC初始化
 */
void ADC_Init(void) {
    // 1. 使能ADC和DMA时钟
    rcu_periph_clock_enable(RCU_ADC0);
    rcu_periph_clock_enable(RCU_DMA0);
    
    // 2. 配置ADC时钟 (APB2/6 = 72MHz/6 = 12MHz)
    rcu_adc_clock_config(RCU_CKADC_CKAPB2_DIV6);
    
    // 3. 初始化ADC
    init_adc_peripheral();
    
    // 4. 初始化DMA
    init_adc_dma();
    
    printf("ADC init complete\n");
}

/**
 * @brief 看门狗初始化
 */
void Watchdog_Init(void) {
    // 检查是否看门狗复位
    if (RESET != rcu_flag_get(RCU_FLAG_FWDGTRST)) {
        rcu_all_reset_flag_clear();
        printf("Watchdog reset detected\n");
    }
    
    // 配置看门狗: 40kHz/16, 4096计数 = 1.6384秒
    if (fwdgt_config(0x0FFF, FWDGT_PSC_DIV16) != SUCCESS) {
        printf("Watchdog config failed\n");
        while (1);
    }
    
    // 使能看门狗
    fwdgt_enable();
    
    printf("Watchdog init complete\n");
}

/**
 * @brief 延时函数
 * @param ms 毫秒数
 */
void Delay(uint32_t ms) {
    uint32_t start_ticks = system_ticks;
    while ((system_ticks - start_ticks) < ms) {
        __NOP();
    }
}

/**
 * @brief 获取系统滴答计数
 * @return 系统滴答计数
 */
uint32_t GetSystemTicks(void) {
    return system_ticks;
}

// ============================================
// 内部函数
// ============================================

/**
 * @brief 初始化PWM引脚
 */
static void init_pwm_pins(void) {
    // 左电机PWM引脚 (TIM1)
    gpio_init(LEFT_MOTOR_UH_PORT, GPIO_MODE_AF_PP, GPIO_OSPEED_50MHZ, LEFT_MOTOR_UH_PIN | LEFT_MOTOR_VH_PIN | LEFT_MOTOR_WH_PIN);
    gpio_init(LEFT_MOTOR_UL_PORT, GPIO_MODE_AF_PP, GPIO_OSPEED_50MHZ, LEFT_MOTOR_UL_PIN | LEFT_MOTOR_VL_PIN | LEFT_MOTOR_WL_PIN);

    // 右电机PWM引脚
    // U相 (TIM3_CH3)
    gpio_init(RIGHT_MOTOR_UH_PORT, GPIO_MODE_AF_PP, GPIO_OSPEED_50MHZ, RIGHT_MOTOR_UH_PIN);
    gpio_init(RIGHT_MOTOR_UL_PORT, GPIO_MODE_AF_PP, GPIO_OSPEED_50MHZ, RIGHT_MOTOR_UL_PIN);

    // V,W相 (TIM4)
    gpio_init(RIGHT_MOTOR_VH_PORT, GPIO_MODE_AF_PP, GPIO_OSPEED_50MHZ, RIGHT_MOTOR_VH_PIN | RIGHT_MOTOR_WH_PIN);
    gpio_init(RIGHT_MOTOR_VL_PORT, GPIO_MODE_AF_PP, GPIO_OSPEED_50MHZ, RIGHT_MOTOR_VL_PIN | RIGHT_MOTOR_WL_PIN);
}

/**
 * @brief 初始化霍尔传感器引脚
 */
static void init_hall_pins(void) {
    // 左电机霍尔
    gpio_init(LEFT_HALL_U_PORT, GPIO_MODE_IN_FLOATING, GPIO_OSPEED_50MHZ, LEFT_HALL_U_PIN | LEFT_HALL_V_PIN | LEFT_HALL_W_PIN);

    // 右电机霍尔
    gpio_init(RIGHT_HALL_U_PORT, GPIO_MODE_IN_FLOATING, GPIO_OSPEED_50MHZ, RIGHT_HALL_U_PIN | RIGHT_HALL_V_PIN | RIGHT_HALL_W_PIN);
}

/**
 * @brief 初始化控制引脚
 */
static void init_control_pins(void) {
    // 使能引脚
    gpio_init(ENABLE_PORT, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, ENABLE_PIN);
    gpio_bit_reset(ENABLE_PORT, ENABLE_PIN);  // 默认禁用

    // 蜂鸣器
    gpio_init(BUZZER_PORT, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, BUZZER_PIN);
    gpio_bit_reset(BUZZER_PORT, BUZZER_PIN);  // 默认关闭

    // LED
    gpio_init(LED_PORT, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, LED_PIN);
    gpio_bit_set(LED_PORT, LED_PIN);  // 默认点亮
}

/**
 * @brief 初始化ADC引脚
 */
static void init_adc_pins(void) {
    // 电池电压ADC
    gpio_init(BATTERY_ADC_PORT, GPIO_MODE_AIN, GPIO_OSPEED_50MHZ, BATTERY_ADC_PIN);

    // 电流检测ADC
    gpio_init(CURRENT_ADC_PORT, GPIO_MODE_AIN, GPIO_OSPEED_50MHZ, CURRENT_ADC_PIN);

    // 左侧模拟油门控制 PA2/PA3
    gpio_init(THROTTLE_ADC_PORT, GPIO_MODE_AIN, GPIO_OSPEED_50MHZ,
              THROTTLE_ADC_PIN_1 | THROTTLE_ADC_PIN_2);
}

/**
 * @brief 初始化左电机定时器
 */
static void init_left_motor_timer(void) {
    timer_parameter_struct timer_init_struct;
    timer_break_parameter_struct break_init_struct;
    timer_oc_parameter_struct oc_init_struct;
    
    // 复位定时器
    timer_deinit(TIMER1);
    
    // 基本定时器配置
    timer_init_struct.prescaler = 0;
    timer_init_struct.alignedmode = TIMER_COUNTER_CENTER_BOTH;
    timer_init_struct.counterdirection = TIMER_COUNTER_UP;
    timer_init_struct.period = BLDC_TIMER_PERIOD;
    timer_init_struct.clockdivision = TIMER_CKDIV_DIV1;
    timer_init_struct.repetitioncounter = 0;
    timer_init(TIMER1, &timer_init_struct);
    
    // 输出比较配置
    oc_init_struct.outputstate = TIMER_CCX_ENABLE;
    oc_init_struct.outputnstate = TIMER_CCXN_ENABLE;
    oc_init_struct.ocpolarity = TIMER_OC_POLARITY_HIGH;
    oc_init_struct.ocnpolarity = TIMER_OCN_POLARITY_LOW;
    oc_init_struct.ocidlestate = TIMER_OC_IDLE_STATE_LOW;
    oc_init_struct.ocnidlestate = TIMER_OCN_IDLE_STATE_HIGH;
    
    timer_channel_output_config(TIMER1, TIMER_CH_1, &oc_init_struct);
    timer_channel_output_config(TIMER1, TIMER_CH_2, &oc_init_struct);
    timer_channel_output_config(TIMER1, TIMER_CH_3, &oc_init_struct);
    
    // 通道PWM模式配置
    timer_channel_output_mode_config(TIMER1, TIMER_CH_1, TIMER_OC_MODE_PWM1);
    timer_channel_output_mode_config(TIMER1, TIMER_CH_2, TIMER_OC_MODE_PWM1);
    timer_channel_output_mode_config(TIMER1, TIMER_CH_3, TIMER_OC_MODE_PWM1);
    
    // 死区时间配置
    break_init_struct.runoffstate = TIMER_ROS_STATE_ENABLE;
    break_init_struct.ideloffstate = TIMER_IOS_STATE_DISABLE;
    break_init_struct.deadtime = DEAD_TIME;
    break_init_struct.breakpolarity = TIMER_BREAK_POLARITY_LOW;
    break_init_struct.outputautostate = TIMER_OUTAUTO_ENABLE;
    break_init_struct.protectmode = TIMER_CCHP_PROT_OFF;
    break_init_struct.breakstate = TIMER_BREAK_DISABLE;
    
    timer_break_config(TIMER1, &break_init_struct);
    
    // 使能定时器
    timer_enable(TIMER1);
}

/**
 * @brief 初始化右电机定时器
 */
static void init_right_motor_timer(void) {
    // 初始化TIM3 (U相)
    init_timer3();
    
    // 初始化TIM4 (V,W相)
    init_timer4();
}

/**
 * @brief 初始化TIM3
 */
static void init_timer3(void) {
    timer_parameter_struct timer_init_struct;
    timer_oc_parameter_struct oc_init_struct;
    
    timer_deinit(TIMER3);
    
    timer_init_struct.prescaler = 0;
    timer_init_struct.alignedmode = TIMER_COUNTER_CENTER_BOTH;
    timer_init_struct.counterdirection = TIMER_COUNTER_UP;
    timer_init_struct.period = BLDC_TIMER_PERIOD;
    timer_init_struct.clockdivision = TIMER_CKDIV_DIV1;
    timer_init_struct.repetitioncounter = 0;
    timer_init(TIMER3, &timer_init_struct);
    
    oc_init_struct.outputstate = TIMER_CCX_ENABLE;
    oc_init_struct.outputnstate = TIMER_CCXN_ENABLE;
    oc_init_struct.ocpolarity = TIMER_OC_POLARITY_HIGH;
    oc_init_struct.ocnpolarity = TIMER_OCN_POLARITY_LOW;
    oc_init_struct.ocidlestate = TIMER_OC_IDLE_STATE_LOW;
    oc_init_struct.ocnidlestate = TIMER_OCN_IDLE_STATE_HIGH;
    
    timer_channel_output_config(TIMER3, TIMER_CH_3, &oc_init_struct);
    timer_channel_output_mode_config(TIMER3, TIMER_CH_3, TIMER_OC_MODE_PWM1);
    
    timer_enable(TIMER3);
}

/**
 * @brief 初始化TIM4
 */
static void init_timer4(void) {
    timer_parameter_struct timer_init_struct;
    timer_oc_parameter_struct oc_init_struct;
    
    timer_deinit(TIMER4);
    
    timer_init_struct.prescaler = 0;
    timer_init_struct.alignedmode = TIMER_COUNTER_CENTER_BOTH;
    timer_init_struct.counterdirection = TIMER_COUNTER_UP;
    timer_init_struct.period = BLDC_TIMER_PERIOD;
    timer_init_struct.clockdivision = TIMER_CKDIV_DIV1;
    timer_init_struct.repetitioncounter = 0;
    timer_init(TIMER4, &timer_init_struct);
    
    oc_init_struct.outputstate = TIMER_CCX_ENABLE;
    oc_init_struct.outputnstate = TIMER_CCXN_ENABLE;
    oc_init_struct.ocpolarity = TIMER_OC_POLARITY_HIGH;
    oc_init_struct.ocnpolarity = TIMER_OCN_POLARITY_LOW;
    oc_init_struct.ocidlestate = TIMER_OC_IDLE_STATE_LOW;
    oc_init_struct.ocnidlestate = TIMER_OCN_IDLE_STATE_HIGH;
    
    timer_channel_output_config(TIMER4, TIMER_CH_1, &oc_init_struct);
    timer_channel_output_config(TIMER4, TIMER_CH_3, &oc_init_struct);
    
    timer_channel_output_mode_config(TIMER4, TIMER_CH_1, TIMER_OC_MODE_PWM1);
    timer_channel_output_mode_config(TIMER4, TIMER_CH_3, TIMER_OC_MODE_PWM1);
    
    timer_enable(TIMER4);
}

/**
 * @brief 初始化ADC外设
 */
static void init_adc_peripheral(void) {
    // 复位ADC
    adc_deinit(ADC0);
    
    // ADC模式配置
    adc_mode_config(ADC_MODE_FREE);
    
    // 数据对齐
    adc_data_alignment_config(ADC0, ADC_DATAALIGN_RIGHT);
    
    // 扫描模式
    adc_special_function_config(ADC0, ADC_SCAN_MODE, ENABLE);
    
    // 外部触发禁用
    adc_external_trigger_config(ADC0, ADC_REGULAR_CHANNEL, DISABLE);
    
    // 使能ADC
    adc_enable(ADC0);
    
    // 校准ADC
    adc_calibration_enable(ADC0);
}

/**
 * @brief 初始化ADC DMA
 */
static void init_adc_dma(void) {
    dma_parameter_struct dma_init_struct;
    
    // 复位DMA通道
    dma_deinit(DMA0, DMA_CH0);
    
    // DMA配置
    dma_init_struct.direction = DMA_PERIPHERAL_TO_MEMORY;
    dma_init_struct.memory_addr = (uint32_t)&adc_buffer;
    dma_init_struct.memory_inc = DMA_MEMORY_INCREASE_ENABLE;
    dma_init_struct.memory_width = DMA_MEMORY_WIDTH_16BIT;
    dma_init_struct.number = 3;  // 3个ADC通道
    dma_init_struct.periph_addr = (uint32_t)&ADC_RDATA(ADC0);
    dma_init_struct.periph_inc = DMA_PERIPH_INCREASE_DISABLE;
    dma_init_struct.periph_width = DMA_PERIPHERAL_WIDTH_16BIT;
    dma_init_struct.priority = DMA_PRIORITY_HIGH;
    
    dma_init(DMA0, DMA_CH0, &dma_init_struct);
    
    // 循环模式
    dma_circulation_enable(DMA0, DMA_CH0);
    
    // 使能DMA
    dma_channel_enable(DMA0, DMA_CH0);
    
    // 使能ADC DMA
    adc_dma_mode_enable(ADC0);
}

// ============================================
// 中断处理函数
// ============================================

/**
 * @brief SysTick中断处理
 */
void SysTick_Handler(void) {
    system_ticks++;
    
    // 喂狗
    fwdgt_counter_reload();
}

/**
 * @brief TIM1更新中断 (左电机PWM)
 */
void TIMER1_UP_IRQHandler(void) {
    if (timer_interrupt_flag_get(TIMER1, TIMER_INT_FLAG_UP) != RESET) {
        timer_interrupt_flag_clear(TIMER1, TIMER_INT_FLAG_UP);
        
        // 触发ADC采样
        adc_software_trigger_enable(ADC0, ADC_REGULAR_CHANNEL);
    }
}

/**
 * @brief DMA中断 (ADC完成)
 */
void DMA0_Channel0_IRQHandler(void) {
    if (dma_interrupt_flag_get(DMA0, DMA_CH0, DMA_INT_FLAG_FTF) != RESET) {
        dma_interrupt_flag_clear(DMA0, DMA_CH0, DMA_INT_FLAG_FTF);
        
        // ADC采样完成，可以处理数据
        // 这里可以添加ADC数据处理代码
    }
}