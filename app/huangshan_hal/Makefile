############################################################################
# Huangshan Pi hardware interface application
############################################################################

include $(APPDIR)/Make.defs

PROGNAME  = huangshan_hal_demo
PRIORITY  = SCHED_PRIORITY_DEFAULT
STACKSIZE = 4096
MODULE    = $(CONFIG_LVX_USE_HUANGSHAN_HAL)

MAINSRC = huangshan_hal.c huangshan_hal_demo.c

include $(APPDIR)/Application.mk
