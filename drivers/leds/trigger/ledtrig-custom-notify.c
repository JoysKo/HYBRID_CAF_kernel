// SPDX-License-Identifier: GPL-2.0-only
// LED Trigger for custom notifications - Mi Max 3
// Вся логика принятия решений — в ядре
// by JoysKo & DeepSeek, 2026

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/leds.h>
#include <linux/sysfs.h>
#include <linux/timer.h>
#include <linux/power_supply.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

// ==================== ПЕРЕМЕННЫЕ ====================
static struct led_trigger *notify_trigger;
static struct led_classdev *current_led;
static struct power_supply *battery_psy;
static struct notifier_block battery_notifier;
static DEFINE_SPINLOCK(led_lock);

// Состояние от userspace (0-9)
static int state_A = 0;  // звонок
static int state_B = 0;  // SMS
static int state_C = 0;  // уведомления
static int state_D = 0;  // уровень заряда от скрипта (с учётом тумблеров)

// Параметры батареи от ядра
static int battery_capacity = 100;

// Режимы работы
enum led_mode {
    MODE_OFF,
    MODE_STEADY,
    MODE_DUAL_BLINK,
};
static enum led_mode current_mode = MODE_OFF;

// Параметры для режимов
static int steady_brightness = 0;
static int dual_high = 255;
static int dual_low = 0;
static int dual_on = 500;
static int dual_off = 500;
static struct timer_list dual_timer;
static int dual_state = 0;

// ==================== ФУНКЦИИ УПРАВЛЕНИЯ LED ====================
static void set_led_brightness(int brightness)
{
    unsigned long flags;
    spin_lock_irqsave(&led_lock, flags);
    if (current_led)
        led_set_brightness(current_led, brightness);
    spin_unlock_irqrestore(&led_lock, flags);
}

static void stop_dual_blink(void)
{
    unsigned long flags;
    spin_lock_irqsave(&led_lock, flags);
    if (current_mode == MODE_DUAL_BLINK) {
        del_timer_sync(&dual_timer);
        dual_state = 0;
    }
    spin_unlock_irqrestore(&led_lock, flags);
}

static void stop_all_modes(void)
{
    stop_dual_blink();
    current_mode = MODE_OFF;
}

// ==================== ТАЙМЕР ДЛЯ DUAL BLINK ====================
static void dual_blink_timer(unsigned long data)
{
    unsigned long flags;
    int should_run = 0;
    int new_brightness;
    
    spin_lock_irqsave(&led_lock, flags);
    if (current_led && current_mode == MODE_DUAL_BLINK) {
        should_run = 1;
        if (dual_state) {
            new_brightness = dual_low;
            mod_timer(&dual_timer, jiffies + msecs_to_jiffies(dual_off));
        } else {
            new_brightness = dual_high;
            mod_timer(&dual_timer, jiffies + msecs_to_jiffies(dual_on));
        }
        dual_state = !dual_state;
    }
    spin_unlock_irqrestore(&led_lock, flags);
    
    if (should_run)
        set_led_brightness(new_brightness);
}

static void start_dual_blink(int high, int low)
{
    stop_all_modes();
    dual_high = high;
    dual_low = low;
    dual_on = 500;
    dual_off = 500;
    dual_state = 0;
    current_mode = MODE_DUAL_BLINK;
    
    init_timer(&dual_timer);
    dual_timer.function = dual_blink_timer;
    dual_timer.data = 0;
    
    set_led_brightness(dual_high);
    mod_timer(&dual_timer, jiffies + msecs_to_jiffies(dual_on));
}

static void start_steady(int brightness)
{
    stop_all_modes();
    steady_brightness = brightness;
    current_mode = MODE_STEADY;
    set_led_brightness(steady_brightness);
}

static void start_off(void)
{
    stop_all_modes();
    set_led_brightness(LED_OFF);
}

// ==================== РАСЧЁТ D_KERNEL ИЗ РЕАЛЬНОЙ БАТАРЕИ ====================
static int get_D_kernel(void)
{
    int cap = battery_capacity;
    if (cap <= 0) return 0;
    if (cap <= 10) return 1;
    if (cap <= 20) return 2;
    if (cap <= 30) return 3;
    if (cap <= 40) return 4;
    if (cap <= 50) return 5;
    if (cap <= 60) return 6;
    if (cap <= 70) return 7;
    if (cap <= 80) return 8;
    return 9;
}

// ==================== РАСЧЁТ ЯРКОСТИ ЗАРЯДКИ ====================
static int calculate_charge_brightness(void)
{
    int D_kernel = get_D_kernel();
    int percent;
    int brightness;
    
    // Базовая яркость от реального заряда (30-90%)
    percent = 100 - (D_kernel - 1) * 7;
    if (percent > 90) percent = 90;
    if (percent < 30) percent = 30;
    brightness = 255 * percent / 100;
    
    // Коррекция: если D_script < D_kernel, уменьшаем яркость
    if (state_D > 0 && state_D < D_kernel) {
        int diff = D_kernel - state_D;
        int reduction = diff * 10;  // 10%, 20%, 30%...
        if (reduction > 80) reduction = 80;
        brightness = brightness * (100 - reduction) / 100;
    }
    
    if (brightness < 0) brightness = 0;
    if (brightness > 255) brightness = 255;
    
    return brightness;
}

// ==================== ГЛАВНАЯ ЛОГИКА ПРИНЯТИЯ РЕШЕНИЙ ====================
static void update_led_from_state(void)
{
    int has_notify = (state_A + state_B + state_C);
    int notify_brightness = 0;
    int charge_brightness = 0;
    
    // Яркость уведомлений (максимальная из A,B,C) 0-9 → 0-255
    if (state_A > 0) notify_brightness = state_A;
    else if (state_B > 0) notify_brightness = state_B;
    else if (state_C > 0) notify_brightness = state_C;
    notify_brightness = notify_brightness * 255 / 9;
    
    // === ПРИНЯТИЕ РЕШЕНИЙ ===
    if (has_notify > 0) {
        // Есть уведомления
        if (state_D > 0) {
            // Зарядка + уведомления → dual blink (high=notify, low=charge)
            charge_brightness = calculate_charge_brightness();
            start_dual_blink(notify_brightness, charge_brightness);
        } else {
            // Нет зарядки → мигание между notify_brightness и 0
            start_dual_blink(notify_brightness, 0);
        }
    } else if (state_D > 0) {
        // Только зарядка
        charge_brightness = calculate_charge_brightness();
        start_steady(charge_brightness);
    } else {
        // Всё выключено
        start_off();
    }
}

// ==================== SYSFS: ПРИЁМ СОСТОЯНИЯ ОТ USERSPACE ====================
static ssize_t state_store(struct device *dev,
    struct device_attribute *attr, const char *buf, size_t size)
{
    int A, B, C, D;
    if (sscanf(buf, "%d %d %d %d", &A, &B, &C, &D) != 4)
        return -EINVAL;
    
    // Ограничиваем значения 0-9
    if (A < 0) A = 0;
    if (A > 9) A = 9;
    if (B < 0) B = 0;
    if (B > 9) B = 9;
    if (C < 0) C = 0;
    if (C > 9) C = 9;
    if (D < 0) D = 0;
    if (D > 9) D = 9;
    
    state_A = A;
    state_B = B;
    state_C = C;
    state_D = D;
    
    update_led_from_state();
    
    return size;
}
static DEVICE_ATTR_WO(state);

// ==================== SYSFS: OFF (аварийное выключение) ====================
static ssize_t off_store(struct device *dev,
    struct device_attribute *attr, const char *buf, size_t size)
{
    state_A = state_B = state_C = state_D = 0;
    update_led_from_state();
    return size;
}
static DEVICE_ATTR_WO(off);

// ==================== ГРУППА АТРИБУТОВ ====================
static struct attribute *notify_attrs[] = {
    &dev_attr_state.attr,
    &dev_attr_off.attr,
    NULL,
};

static struct attribute_group notify_attr_group = {
    .attrs = notify_attrs,
};

// ==================== CALLBACK ОТ POWER_SUPPLY ====================
static int battery_notifier_cb(struct notifier_block *nb,
                                unsigned long event, void *data)
{
    struct power_supply *psy = data;
    union power_supply_propval val;
    int old_capacity = battery_capacity;
    
    if (!psy || strcmp(psy->desc->name, "battery") != 0)
        return NOTIFY_DONE;
    
    if (power_supply_get_property(psy, POWER_SUPPLY_PROP_CAPACITY, &val) == 0)
        battery_capacity = val.intval;
    
    if (old_capacity != battery_capacity)
        update_led_from_state();
    
    return NOTIFY_OK;
}

// ==================== АКТИВАЦИЯ ТРИГГЕРА ====================
static void notify_trig_activate(struct led_classdev *led_cdev)
{
    int ret;
    unsigned long flags;
    
    spin_lock_irqsave(&led_lock, flags);
    current_led = led_cdev;
    spin_unlock_irqrestore(&led_lock, flags);
    
    ret = sysfs_create_group(&led_cdev->dev->kobj, &notify_attr_group);
    if (ret)
        dev_err(led_cdev->dev, "Failed to create sysfs group: %d\n", ret);
    
    pr_info("custom_notify: activated on %s\n", led_cdev->name);
}

// ==================== ДЕАКТИВАЦИЯ ТРИГГЕРА ====================
static void notify_trig_deactivate(struct led_classdev *led_cdev)
{
    unsigned long flags;
    
    start_off();
    sysfs_remove_group(&led_cdev->dev->kobj, &notify_attr_group);
    
    spin_lock_irqsave(&led_lock, flags);
    current_led = NULL;
    spin_unlock_irqrestore(&led_lock, flags);
    
    pr_info("custom_notify: deactivated\n");
}

// ==================== СТРУКТУРА ТРИГГЕРА ====================
static struct led_trigger notify_led_trigger = {
    .name     = "custom_notify",
    .activate = notify_trig_activate,
    .deactivate = notify_trig_deactivate,
};

// ==================== ИНИЦИАЛИЗАЦИЯ МОДУЛЯ ====================
static int __init notify_trig_init(void)
{
    int ret;
    
    ret = led_trigger_register(&notify_led_trigger);
    if (ret) {
        pr_err("custom_notify: failed to register trigger: %d\n", ret);
        return ret;
    }
    
    battery_psy = power_supply_get_by_name("battery");
    if (battery_psy) {
        battery_notifier.notifier_call = battery_notifier_cb;
        ret = power_supply_reg_notifier(&battery_notifier);
        if (ret)
            pr_warn("custom_notify: failed to register battery notifier: %d\n", ret);
        else
            pr_info("custom_notify: registered battery notifier\n");
    } else {
        pr_warn("custom_notify: battery power supply not found\n");
    }
    
    pr_info("custom_notify: module loaded v3.0\n");
    return 0;
}

// ==================== ВЫХОД ИЗ МОДУЛЯ ====================
static void __exit notify_trig_exit(void)
{
    if (battery_psy) {
        power_supply_unreg_notifier(&battery_notifier);
        power_supply_put(battery_psy);
    }
    
    led_trigger_unregister(&notify_led_trigger);
    pr_info("custom_notify: module unloaded\n");
}

module_init(notify_trig_init);
module_exit(notify_trig_exit);

MODULE_AUTHOR("JoysKo & DeepSeek");
MODULE_DESCRIPTION("Custom notification LED trigger");
MODULE_LICENSE("GPL");
