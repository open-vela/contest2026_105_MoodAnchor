if(CONFIG_EXAMPLES_MOOD_GATE)
  nuttx_add_application(
    MODULE
    ${CONFIG_EXAMPLES_MOOD_GATE}
    NAME
    ${CONFIG_EXAMPLES_MOOD_GATE_PROGNAME}
    SRCS
    mood_gate_main.c mood_gate.c mood_gate_model_params.c mood_gate_platform.c
    STACKSIZE
    ${CONFIG_EXAMPLES_MOOD_GATE_STACKSIZE}
    PRIORITY
    ${CONFIG_EXAMPLES_MOOD_GATE_PRIORITY})
endif()
