#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
repo_dir=$(cd "$script_dir/.." && pwd)
root=${OPENVELA_ROOT:-$(cd "$repo_dir/.." && pwd)}
nuttx="$root/nuttx"
sifli="$root/vendor/sifli"
[[ -d "$nuttx" && -d "$sifli" ]] || { echo "run repo sync first" >&2; exit 2; }

ln -sfn "$sifli/chips/sf32lb52/ipc_queue" "$nuttx/arch/arm/src/ipc_queue"
ln -sfn "$sifli/chips/drivers" "$nuttx/arch/arm/drivers"
ln -sfn "$sifli/boards/sf32lb52/lckfb_huangshan_pi/scripts/Make.defs" "$nuttx/Make.defs"

python3 - "$sifli/boards/sf32lb52/lckfb_huangshan_pi/configs/nsh/defconfig" <<'PY'
from pathlib import Path
import re
import sys
p=Path(sys.argv[1]); s=p.read_text()
replacements = {
    'CONFIG_ALLSYMS=y': '# CONFIG_ALLSYMS is not set',
    'CONFIG_CXX_LOCALIZATION=y': '# CONFIG_CXX_LOCALIZATION is not set',
    'CONFIG_HAVE_CXX=y': '# CONFIG_HAVE_CXX is not set',
    'CONFIG_LIBCXX=y': '# CONFIG_LIBCXX is not set',
    'CONFIG_EXAMPLES_LVGLDEMO=y': '# CONFIG_EXAMPLES_LVGLDEMO is not set',
    'CONFIG_INTERPRETERS_QUICKJS=y': '# CONFIG_INTERPRETERS_QUICKJS is not set',
    'CONFIG_LIB_PNG=y': '# CONFIG_LIB_PNG is not set',
    'CONFIG_LV_USE_LIBPNG=y': '# CONFIG_LV_USE_LIBPNG is not set',
    'CONFIG_UNQLITE=y': '# CONFIG_UNQLITE is not set',
    'CONFIG_UTILS_CURL=y': '# CONFIG_UTILS_CURL is not set',
    'CONFIG_BSP_USING_SPI1=y': '# CONFIG_BSP_USING_SPI1 is not set',
    'CONFIG_MMCSD=y': '# CONFIG_MMCSD is not set',
    'CONFIG_SPI=y': '# CONFIG_SPI is not set',
    'CONFIG_SPI_DRIVER=y': '# CONFIG_SPI_DRIVER is not set',
}
for old, new in replacements.items(): s=s.replace(old, new)
def add(line, after=None):
    global s
    if line not in s:
        s += '\n' + line + '\n'
for line in ('CONFIG_ARCH_ARMV8M=y','CONFIG_ARCH_CORTEXM33=y','CONFIG_ARM_SYSTICK=y','CONFIG_UART_BTH4=y','CONFIG_LV_CONF_SKIP=y','CONFIG_LVX_USE_HUANGSHAN_HAL=y'):
    add(line)
p.write_text(s)
PY

python3 - "$nuttx/arch/arm/src/arm_m/Make.defs" <<'PY'
from pathlib import Path
import sys
p=Path(sys.argv[1]); p.write_text(p.read_text().replace('ifeq ($(CONFIG_ARM_M_SYSTICK),y)', 'ifeq ($(CONFIG_ARM_SYSTICK),y)'))
PY

python3 - "$sifli/boards/sf32lb52/lckfb_huangshan_pi/scripts/Make.defs" <<'PY'
from pathlib import Path
import sys
p=Path(sys.argv[1]); s=p.read_text()
s=s.replace('include $(TOPDIR)/arch/arm/src/armv7-m/Toolchain.defs', 'include $(TOPDIR)/arch/arm/src/armv8-m/Toolchain.defs')
anchor='include $(TOPDIR)/arch/arm/src/armv8-m/Toolchain.defs'
extra='''\nARCHINCLUDES += -I$(TOPDIR)/../vendor/sifli/chips/drivers/Include\nARCHINCLUDES += -I$(TOPDIR)/../vendor/sifli/chips/drivers/cmsis/Include\nARCHINCLUDES += -I$(TOPDIR)/../vendor/sifli/chips/drivers/cmsis/sf32lb52x\nARCHINCLUDES += -I$(TOPDIR)/../vendor/sifli/chips/boards/include/config/sf32lb52x\nARCHINCLUDES += -I$(TOPDIR)/../vendor/sifli/chips/external/CMSIS/Include\nARCHINCLUDES += -I$(TOPDIR)/../vendor/sifli/boards/sf32lb52/lckfb_huangshan_pi/include\nARCHINCLUDES += -I$(TOPDIR)/../vendor/sifli/chips/sf32lb52/include\nARCHDEFINES += -DSOC_BF0_HCPU -DSF32LB52X -DUSE_HAL_DRIVER\n'''
if 'chips/drivers/Include' not in s: s=s.replace(anchor, anchor+extra)
s=s.replace(' -pipe -Werror=return-type -Werror', ' -pipe -Werror=return-type')
p.write_text(s)
PY

python3 - "$sifli/chips/sf32lb52/Make.defs" <<'PY'
from pathlib import Path
import re
import sys
p=Path(sys.argv[1]); s=p.read_text()
s=s.replace('include armv7-m/Make.defs','include armv8-m/Make.defs')
s=re.sub(r'^CHIP_CSRCS \+= sifli_start\.c sifli_irq\.c sifli_uart\.c.*$', 'CHIP_CSRCS += sifli_start.c sifli_irq.c sifli_uart.c sifli_lowput.c', s, flags=re.M)
s=s.replace('CHIP_CSRCS += sifli_spi.c','''CHIP_CSRCS += sifli_gpio.c sifli_i2c.c sifli_spi.c
CHIP_CSRCS += ../drivers/cmsis/sf32lb52x/Templates/system_bf0_ap.c
CHIP_CSRCS += ../drivers/cmsis/sf32lb52x/bf0_pin_const.c
CHIP_CSRCS += ../drivers/cmsis/sf32lb52x/lcpu_config_type.c
CHIP_CSRCS += $(filter-out ../drivers/hal/bf0_hal_audcodec.c ../drivers/hal/bf0_hal_qspi.c ../drivers/hal/bf0_hal_qspi_ex.c,$(wildcard ../drivers/hal/*.c))
CFLAGS += -DHAL_TICK_PER_SECOND=1000''')
p.write_text(s)
PY

python3 - "$sifli/chips/sf32lb52/sifli_uart.c" <<'PY'
from pathlib import Path
import re
import sys
p=Path(sys.argv[1]); p.write_text(p.read_text().replace('  return ch;\n}\n\n#endif /* USE_SERIALDRIVER */','}\n\n#endif /* USE_SERIALDRIVER */'))
PY

python3 - "$sifli/boards/sf32lb52/lckfb_huangshan_pi/src/Makefile" <<'PY'
from pathlib import Path
import re
import sys
p=Path(sys.argv[1]); s=p.read_text()
s=re.sub(r'^CSRCS = sifli_ap\.c.*?(?=^RCSRCS)', 'CSRCS = sifli_ap.c sifli_gpio.c bsp_init.c bsp_pinmux.c bsp_power.c bsp_lcd_tp.c sf32lb52_buttons.c\nCSRCS += ../../drivers/lcd/sf32lb_lcd.c ../../drivers/lcd/co5300.c\nCSRCS += ../../drivers/input/ft6146.c\n\n', s, flags=re.M|re.S)
s=s.replace('\nifeq ($(CONFIG_ARCH_BUTTONS),y)\nCSRCS += sf32lb52_buttons.c\nendif\n','\n')
p.write_text(s)
PY

lfs_dir="$nuttx/fs/littlefs"
if [[ ! -f "$lfs_dir/littlefs/lfs.c" ]]; then
  mkdir -p "$lfs_dir/.download"
  curl --http1.1 --fail --location --retry 3 -o "$lfs_dir/.download/v2.5.1.tar.gz" https://codeload.github.com/littlefs-project/littlefs/tar.gz/refs/tags/v2.5.1
  tar -xzf "$lfs_dir/.download/v2.5.1.tar.gz" -C "$lfs_dir"
  mv "$lfs_dir/littlefs-2.5.1" "$lfs_dir/littlefs"
  (cd "$nuttx/fs" && git apply littlefs/lfs_util.patch && git apply littlefs/lfs_getpath.patch)
fi
touch "$lfs_dir/.littlefsunpack"
echo "Huangshan Pi source preparation complete: $root"
