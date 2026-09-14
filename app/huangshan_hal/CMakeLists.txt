# Huangshan Pi hardware interface library and demo application.

if(CONFIG_LVX_USE_HUANGSHAN_HAL)
  nuttx_add_application(
    NAME huangshan_hal_demo
    SRCS huangshan_hal.c huangshan_hal_demo.c hs_ble_host.c hs_ble_gatt.c
         hs_ppg.c
    STACKSIZE 4096)
endif()

# MoodAnchor application on the openvela graphics framework.

if(CONFIG_LVX_USE_DEMO_MOOD_ANCHOR)
  nuttx_add_application(
    NAME mood_anchor
    PRIORITY 100
    STACKSIZE 40960
    MODULE ${CONFIG_LVX_USE_DEMO_MOOD_ANCHOR}
    DEPENDS lvgl
    SRCS mood_anchor_main.c huangshan_hal.c hs_ble_host.c hs_ble_gatt.c
         hs_ppg.c)
endif()
