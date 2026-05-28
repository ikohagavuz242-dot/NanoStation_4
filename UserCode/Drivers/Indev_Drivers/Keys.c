#include "Keys.h"

#include "cmsis_os2.h"
#include "lv_hal_indev.h"
#include "lv_group.h"
#include "os_handles.h"
#include "stm32f4xx_hal.h"
#include "UserTask.h"

KeyStruct_t keys[KEY_NUM];
static KeyEventMsg_t msg;

static uint8_t Key_ReadLevel(IndevTypeDef key){
    switch(key) {
        case KEY_MODE:      return HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_12) == GPIO_PIN_RESET ? 1 : 0;
        case KEY_UP:        return HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_13) == GPIO_PIN_RESET ? 1 : 0;
        case KEY_DOWN:      return HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_14) == GPIO_PIN_RESET ? 1 : 0;
        case KEY_SET:       return HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_15) == GPIO_PIN_RESET ? 1 : 0;
        case KEY_PWR:       return HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_4)  == GPIO_PIN_RESET ? 1 : 0;
        default: return 0;
    }
}

void Key_Init(void)
{
    for (int i = 0; i < KEY_NUM; i++) {
        keys[i].state         = KEY_STATE_IDLE;
        keys[i].level         = 0;
        keys[i].last_level    = 0;
        keys[i].press_cnt     = 0;
        keys[i].long_hold_cnt = 0;
    }
}

void Key_Scan(void)
{
    for (uint8_t i = 0; i < KEY_NUM; i++)
    {
        KeyStruct_t *k = &keys[i];
        uint8_t raw = Key_ReadLevel((IndevTypeDef)i);

        // 消抖
        if (raw == k->last_level)
        {
            if (raw == 1)   // 按下方向
            {
                if (k->press_cnt < 0xFFFF) k->press_cnt++;
            }
            else            // 释放方向
            {
                k->press_cnt = 0;
                k->long_hold_cnt = 0;
            }
        }
        else
        {
            k->press_cnt = 0;
            k->long_hold_cnt = 0;
        }
        k->last_level = raw;

        // 稳定则进入状态机
        if (k->press_cnt >= DEBOUNCE_CYCLES)
        {
            k->level = 1;

            if (k->state == KEY_STATE_IDLE)
            {
                k->state = KEY_STATE_PRESSED;
                k->press_cnt = 0;           // 重置计数，准备计数长按时间
            }

            if (k->state == KEY_STATE_PRESSED)
            {
                k->press_cnt++;

                // 到长按阈值的时候 -> 触发长按事件
                if (k->press_cnt >= LONG_PRESS_CYCLES)
                {
                    msg.key   = (IndevTypeDef)i;
                    msg.event = KEY_EVENT_LONG_PRESS;
                    osMessageQueuePut(KeyEventQueueHandle, &msg, 0, 0);
                    WakeUp();   // 唤醒设备

                    k->state = KEY_STATE_LONG_HOLD;
                    k->long_hold_cnt = 0;
                }
            }

            if (k->state == KEY_STATE_LONG_HOLD)
            {
                k->long_hold_cnt++;

                // 长按连续事件触发
                if (k->long_hold_cnt >= LONG_HOLD_INTERVAL)
                {
                    msg.key   = (IndevTypeDef)i;
                    msg.event = KEY_EVENT_LONG_HOLD;
                    osMessageQueuePut(KeyEventQueueHandle, &msg, 0, 0);
                    WakeUp();   // 唤醒设备

                    k->long_hold_cnt = 0;
                }
            }
        }
        // 按键释放
        else if (k->level == 1 && raw == 0)
        {
            // 短按释放 -> 单击事件
            if (k->state == KEY_STATE_PRESSED &&
                k->press_cnt < LONG_PRESS_CYCLES)
            {
                msg.key   = (IndevTypeDef)i;
                msg.event = KEY_EVENT_CLICK;
                osMessageQueuePut(KeyEventQueueHandle, &msg, 0, 0);
                WakeUp();   // 唤醒设备
            }

            // 回到空闲状态
            k->state = KEY_STATE_IDLE;
            k->level = 0;
            k->press_cnt = 0;
            k->long_hold_cnt = 0;
        }
    }
}

void Key_Reset(void)
{
    Key_Init();
}

// ────────────────────────────────────────────────
//    LVGL 键盘输入设备回调（直接读GPIO，无消抖）
// ────────────────────────────────────────────────

static uint32_t btn_to_lv_key(IndevTypeDef btn)
{
    switch (btn) {
        case KEY_MODE: return LV_KEY_ESC;
        case KEY_UP:   return LV_KEY_UP;
        case KEY_DOWN: return LV_KEY_DOWN;
        case KEY_SET:  return LV_KEY_ENTER;
        case KEY_PWR:  return LV_KEY_HOME;
        default:       return 0;
    }
}

void keypad_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    static uint8_t last_any = 0;

    uint8_t raw[KEY_NUM];
    raw[0] = Key_ReadLevel(KEY_MODE);
    raw[1] = Key_ReadLevel(KEY_UP);
    raw[2] = Key_ReadLevel(KEY_DOWN);
    raw[3] = Key_ReadLevel(KEY_SET);
    raw[4] = Key_ReadLevel(KEY_PWR);

    data->state = LV_INDEV_STATE_RELEASED;
    data->key   = 0;

    for (uint8_t i = 0; i < KEY_NUM; i++) {
        if (raw[i]) {
            data->state = LV_INDEV_STATE_PRESSED;
            data->key   = btn_to_lv_key((IndevTypeDef)i);
            break;
        }
    }

    // 按下边沿触发蜂鸣和唤醒
    uint8_t any = (raw[0] || raw[1] || raw[2] || raw[3] || raw[4]) ? 1 : 0;
    if (any && !last_any) {
        StartBeezer(0);
        WakeUp();
    }
    last_any = any;
}