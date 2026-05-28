/*
 * hovercar-gd32 - 遥控器输入处理（真实硬件 ADC 模式）
 * GD32F103RCT6 MCU 基于 ADC 的真实遥控器输入
 */

#include "defines.h"
#include "gd32f10x.h"
#include <stdio.h>
#include <string.h>
#include "firmware_api.h"

// 内部函数声明
static void init_ppm_input(void);
static void init_pwm_input(void);
static void init_adc_input(void);
static void update_ppm_input(void);
static void update_pwm_input(void);
static void update_adc_input(void);
static int16_t parse_channel_value(const char* str);
static int16_t map_adc_to_channel(int16_t adc_value);

// ============================================
// 全局变量
// ============================================

// 遥控器通道值
static int16_t remote_channels[8] = {0};
static uint32_t last_remote_update = 0;
static FlagStatus remote_connected = RESET;
static uint8_t remote_mode = REMOTE_MODE_NONE;

static RemoteConfig remote_config = {
    .mode = REMOTE_MODE_ADC,
    .num_channels = 8,
    .min_value = 0,
    .max_value = 4095,
    .center_value = 2048,
    .deadband = 50
};

// ============================================
// 公共函数
// ============================================

/**
 * @brief 初始化遥控器输入
 * 仅支持 ADC 真实硬件模式
 */
void Remote_Init(uint8_t mode) {
    // 统一初始化遥控器状态
    remote_mode = REMOTE_MODE_NONE;
    remote_connected = RESET;
    last_remote_update = 0;
    memset(remote_channels, 0, sizeof(remote_channels));

    if (mode != REMOTE_MODE_ADC) {
        printf("Warning: unsupported remote mode %d, defaulting to ADC hardware mode\n", mode);
    }

    init_adc_input();
    remote_mode = REMOTE_MODE_ADC;
    printf("Remote init complete: ADC hardware input mode\n");
}

/**
 * @brief 更新遥控器输入
 */
void Remote_Update(void) {
    static uint32_t last_update_time = 0;
    uint32_t current_time = GetSystemTicks();

    // 更新频率限制 (10ms)
    if (current_time - last_update_time < 10) {
        return;
    }
    last_update_time = current_time;

    if (remote_mode == REMOTE_MODE_ADC) {
        // ADC 采样由 DMA 自动更新到 adc_buffer
        // adc_buffer.battery → 转向（PA1）
        // adc_buffer.current → 油门（PC2）
        
        int16_t raw_steer = adc_buffer.battery;
        int16_t raw_throttle = adc_buffer.current;
        
        // 映射到 -1000~1000 范围
        remote_channels[0] = map_adc_to_channel(raw_steer);      // 转向
        remote_channels[2] = map_adc_to_channel(raw_throttle);   // 油门
        
        // 其他通道默认值
        remote_channels[1] = 0;     // 升降
        remote_channels[3] = 0;     // 偏航
        remote_channels[4] = 1000;  // 使能开关常开
        remote_channels[5] = 0;     // 模式开关
        remote_channels[6] = 0;     // 辅助 1
        remote_channels[7] = 0;     // 辅助 2
        
        last_remote_update = current_time;
        
        // 超时检测
        if (current_time - last_remote_update > REMOTE_TIMEOUT_MS) {
            remote_connected = RESET;
            // 超时时清零通道值（电机停止）
            for (int i = 0; i < 8; i++) {
                remote_channels[i] = 0;
            }
        } else {
            remote_connected = SET;
        }
    }
}

/**
 * @brief 获取遥控器通道值
 * @param channel 通道号 (0-7)
 * @return 通道值 (-1000~1000)
 */
int16_t Remote_GetChannel(uint8_t channel) {
    if (channel >= 8) {
        return 0;
    }
    
    if (!remote_connected) {
        return 0;
    }
    
    return remote_channels[channel];
}

/**
 * @brief 获取油门值
 * @return 油门值 (-1000~1000)
 */
int16_t Remote_GetThrottle(void) {
    // 通道2通常为油门
    return Remote_GetChannel(2);
}

/**
 * @brief 获取转向值
 * @return 转向值 (-1000~1000)
 */
int16_t Remote_GetSteering(void) {
    // 通道0通常为转向
    return Remote_GetChannel(0);
}

/**
 * @brief 获取使能状态
 * @return 使能状态
 */
FlagStatus Remote_GetEnable(void) {
    // 通道4通常为使能开关
    int16_t channel4 = Remote_GetChannel(4);
    return (channel4 > 500) ? SET : RESET;
}

/**
 * @brief 检查遥控器是否连接
 * @return 连接状态
 */
FlagStatus Remote_IsConnected(void) {
    return remote_connected;
}

/**
 * @brief 设置遥控器配置
 * @param config 配置参数
 */
void Remote_SetConfig(RemoteConfig* config) {
    if (config != NULL) {
        memcpy(&remote_config, config, sizeof(RemoteConfig));
    }
}

/**
 * @brief 获取遥控器配置
 * @return 配置指针
 */
RemoteConfig* Remote_GetConfig(void) {
    return &remote_config;
}

// ============================================
// 内部函数
// ============================================

/**
 * @brief 初始化PPM输入
 */
static void init_ppm_input(void) {
    // PPM输入通常使用定时器输入捕获
    // 这里需要根据具体硬件配置
    
    printf("PPM input init\n");
    
    // 示例配置 - 需要根据实际硬件修改
    // 1. 配置GPIO为输入
    // 2. 配置定时器为输入捕获模式
    // 3. 配置中断
    
    // 暂时使用模拟输入
    remote_mode = REMOTE_MODE_NONE;
}

/**
 * @brief 初始化PWM输入
 */
static void init_pwm_input(void) {
    // PWM输入使用ADC或输入捕获
    // 这里需要根据具体硬件配置
    
    printf("PWM input init\n");
    
    // 示例配置 - 需要根据实际硬件修改
    // 1. 配置ADC通道
    // 2. 或配置定时器输入捕获
    
    // 暂时使用模拟输入
    remote_mode = REMOTE_MODE_NONE;
}

/**
 * @brief 初始化ADC输入
 */
static void init_adc_input(void) {
    // ADC输入用于模拟遥控器
    // 需要配置ADC通道
    
    printf("ADC input init\n");
    
    // ADC已经在setup.c中初始化
    // 这里只需要配置通道映射
    
    remote_config.min_value = 0;
    remote_config.max_value = 4095;
    remote_config.center_value = 2048;
    remote_config.deadband = 50;
}

/**
 * @brief 更新PPM输入
 */
static void update_ppm_input(void) {
    // 从定时器读取PPM值
    // 这里需要实现具体的PPM解码
    // 硬件版当前不使用模拟输入
}

/**
 * @brief 更新PWM输入
 */
static void update_pwm_input(void) {
    // 从ADC或输入捕获读取PWM值
    // 硬件版当前不使用模拟输入
}

/**
 * @brief 更新ADC输入
 */
static void update_adc_input(void) {
    // 从ADC读取值并转换为通道值
    
    // 假设ADC0为转向，ADC1为油门
    int16_t raw_steer = adc_buffer.battery;  // 使用电池ADC作为示例
    int16_t raw_throttle = adc_buffer.current; // 使用电流ADC作为示例
    
    // 转换为-1000~1000范围
    remote_channels[0] = MAP(raw_steer, 
                            remote_config.min_value, 
                            remote_config.max_value, 
                            -1000, 1000);
    
    remote_channels[2] = MAP(raw_throttle,
                            remote_config.min_value,
                            remote_config.max_value,
                            -1000, 1000);
    
    // 死区处理
    if (ABS(remote_channels[0]) < remote_config.deadband) {
        remote_channels[0] = 0;
    }
    if (ABS(remote_channels[2]) < remote_config.deadband) {
        remote_channels[2] = 0;
    }
    
    // 模拟其他通道
    remote_channels[4] = 1000;  // 使能通道常开
    remote_channels[5] = 0;     // 模式通道
    remote_channels[6] = 0;     // 辅助通道1
    remote_channels[7] = 0;     // 辅助通道2
    
    last_remote_update = GetSystemTicks();
}

/**
 * @brief 更新模拟输入（用于测试）
 */
static void update_simulated_input(void) {
    static uint32_t sim_counter = 0;
    static int16_t sim_throttle = 0;
    static int16_t sim_steering = 0;
    
    // 模拟遥控器输入
    sim_counter++;
    
    // 简单的正弦波模拟
    if (sim_counter % 200 < 100) {
        sim_throttle = 500;  // 前进
        sim_steering = 200;  // 轻微右转
    } else {
        sim_throttle = -300; // 后退
        sim_steering = -150; // 轻微左转
    }
    
    // 添加一些随机变化
    if (sim_counter % 50 == 0) {
        sim_steering += (sim_counter % 100) - 50;
        sim_steering = CLAMP(sim_steering, -800, 800);
    }
    
    // 设置通道值
    remote_channels[0] = sim_steering;    // 转向
    remote_channels[1] = 0;               // 升降
    remote_channels[2] = sim_throttle;    // 油门
    remote_channels[3] = 0;               // 偏航
    remote_channels[4] = 1000;            // 使能开关
    remote_channels[5] = 0;               // 模式开关
    remote_channels[6] = 0;               // 辅助1
    remote_channels[7] = 0;               // 辅助2
    
    last_remote_update = GetSystemTicks();
    
    // 每5秒输出一次调试信息
    if (sim_counter % 1000 == 0) {
        printf("Simulated remote: throttle=%d, steering=%d\n", sim_throttle, sim_steering);
    }
}

/**
 * @brief 将原始值转换为标准范围
 * @param raw 原始值
 * @param min 最小值
 * @param max 最大值
 * @param center 中心值
 * @return 转换后的值 (-1000~1000)
 */
static int16_t convert_to_standard(int16_t raw, int16_t min, int16_t max, int16_t center) {
    if (raw < center) {
        return MAP(raw, min, center, -1000, 0);
    } else {
        return MAP(raw, center, max, 0, 1000);
    }
}

static int16_t map_adc_to_channel(int16_t adc_value) {
    return convert_to_standard(
        adc_value,
        remote_config.min_value,
        remote_config.max_value,
        remote_config.center_value
    );
}