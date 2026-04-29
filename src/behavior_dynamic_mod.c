#define DT_DRV_COMPAT zmk_behavior_dynamic_mod

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/slist.h>

#include <drivers/behavior.h>
#include <dt-bindings/zmk/keys.h>
#include <zmk/behavior.h>
#include <zmk/endpoints.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/hid.h>
#include <zmk/keymap.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

struct behavior_dynamic_mod_config {
    uint8_t layer;
    uint16_t mod_keycode;
    uint8_t trigger_count;
    uint32_t trigger_positions[];
};

struct behavior_dynamic_mod_data {
    sys_snode_t node;
    const struct device *dev;
    bool active;
    bool modifier_mode;
    uint32_t position;
};

static sys_slist_t dynamic_mod_list;

static int behavior_dynamic_mod_init(const struct device *dev) {
    struct behavior_dynamic_mod_data *data = dev->data;
    data->dev = dev;
    data->active = false;
    data->modifier_mode = false;
    sys_slist_append(&dynamic_mod_list, &data->node);
    return 0;
}

static int on_dynamic_mod_pressed(struct zmk_behavior_binding *binding,
                                   struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    struct behavior_dynamic_mod_data *data = dev->data;
    const struct behavior_dynamic_mod_config *config = dev->config;

    data->active = true;
    data->modifier_mode = false;
    data->position = event.position;

    zmk_keymap_layer_activate(config->layer);
    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_dynamic_mod_released(struct zmk_behavior_binding *binding,
                                    struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    struct behavior_dynamic_mod_data *data = dev->data;
    const struct behavior_dynamic_mod_config *config = dev->config;

    if (data->modifier_mode) {
        zmk_hid_keyboard_release(config->mod_keycode);
        zmk_endpoints_send_report(HID_USAGE_KEY);
    } else {
        zmk_keymap_layer_deactivate(config->layer);
    }

    data->active = false;
    data->modifier_mode = false;
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_dynamic_mod_driver_api = {
    .binding_pressed = on_dynamic_mod_pressed,
    .binding_released = on_dynamic_mod_released,
};

static int dynamic_mod_position_listener(const zmk_event_t *eh) {
    struct zmk_position_state_changed *ev = as_zmk_position_state_changed(eh);

    if (ev == NULL || !ev->state) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    struct behavior_dynamic_mod_data *item;

    SYS_SLIST_FOR_EACH_CONTAINER(&dynamic_mod_list, item, node) {
        if (!item->active || item->modifier_mode) {
            continue;
        }
        if (item->position == ev->position) {
            continue;
        }

        const struct behavior_dynamic_mod_config *config = item->dev->config;

        for (int i = 0; i < config->trigger_count; i++) {
            if (config->trigger_positions[i] == ev->position) {
                LOG_DBG("Trigger pos %d pressed, switching to modifier mode", ev->position);

                item->modifier_mode = true;
                zmk_keymap_layer_deactivate(config->layer);
                zmk_hid_keyboard_press(config->mod_keycode);
                zmk_endpoints_send_report(HID_USAGE_KEY);
                break;
            }
        }
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(dynamic_mod_position_listener, dynamic_mod_position_listener);
ZMK_SUBSCRIPTION(dynamic_mod_position_listener, zmk_position_state_changed);

#define DYN_MOD_INST(n)                                                                            \
    static struct behavior_dynamic_mod_data behavior_dynamic_mod_data_##n = {};                    \
    static const struct behavior_dynamic_mod_config behavior_dynamic_mod_config_##n = {            \
        .layer = DT_INST_PROP(n, layer),                                                           \
        .mod_keycode = DT_INST_PROP(n, mod),                                                       \
        .trigger_count = DT_INST_PROP_LEN(n, trigger_positions),                                   \
        .trigger_positions = DT_INST_PROP(n, trigger_positions),                                   \
    };                                                                                             \
    DEVICE_DT_INST_DEFINE(n, behavior_dynamic_mod_init, NULL,                                    \
                          &behavior_dynamic_mod_data_##n, &behavior_dynamic_mod_config_##n,        \
                          POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,                        \
                          &behavior_dynamic_mod_driver_api);

DT_INST_FOREACH_STATUS_OKAY(DYN_MOD_INST)
