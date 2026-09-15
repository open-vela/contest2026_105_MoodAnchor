# Huangshan Pi hardware interface library and demo application.

# Microphone capture lives in the chip directory: bf0_hal.h refuses to
# compile without the core selection macros (SOC_BF0_HCPU, SF32LB52X) and the
# CMSIS headers, which only the kernel's `arch` target provides.  All the
# application needs from there is this one header.

get_filename_component(HS_HAL_REAL_DIR ${CMAKE_CURRENT_LIST_DIR} REALPATH)

set(HS_HAL_INCLUDE_DIRS
    ${HS_HAL_REAL_DIR}/../../../vendor/sifli/chips/sf32lb52/include)

if(CONFIG_LVX_USE_HUANGSHAN_HAL)
  nuttx_add_application(
    NAME huangshan_hal_demo
    SRCS huangshan_hal.c huangshan_hal_demo.c hs_ble_host.c hs_ble_gatt.c
         hs_mic.c hs_mood.c
    STACKSIZE 4096)

  target_include_directories(apps_huangshan_hal_demo PRIVATE
                             ${HS_HAL_INCLUDE_DIRS})
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
         hs_mic.c hs_mood.c)

  target_include_directories(apps_mood_anchor PRIVATE ${HS_HAL_INCLUDE_DIRS})
endif()
