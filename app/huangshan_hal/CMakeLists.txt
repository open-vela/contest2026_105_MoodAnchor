# Huangshan Pi hardware interface library and demo application.

if(CONFIG_LVX_USE_HUANGSHAN_HAL)
  nuttx_add_application(
    NAME huangshan_hal_demo
    SRCS huangshan_hal.c huangshan_hal_demo.c
    STACKSIZE 4096)
endif()
