/****************************************************************************
 * app/huangshan_hal/hs_ble_host.c
 *
 * Bind the NuttX Bluetooth host stack directly to the SiFli vendor HCI
 * driver, following the standard openvela BTH4 driver architecture:
 *
 *   application -> bt_netdev_register(vendor bt_driver_s)
 *   vendor drv->open/send/close <-> LCPU IPC mailbox
 *   vendor receive callback -> bt_netdev_receive() -> host stack
 *
 * The vendor driver (sf32lb52_bth4.c) already implements H:4 framing on
 * send and hands complete HCI packets (event code / ACL header included)
 * to the stack on receive, so no character device, UART shim or extra H:4
 * layer is involved here.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <unistd.h>
#include <debug.h>

#include <nuttx/mutex.h>
#include <nuttx/wireless/bluetooth/bt_driver.h>

#include "huangshan_hal.h"
#include "hs_ble.h"

#ifdef CONFIG_WIRELESS_BLUETOOTH_HOST

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* Not yet exported from bt_driver.h: releases g_btdev.btdev. */
void bt_driver_unset(FAR struct bt_driver_s *btdev);

/* Vendor driver accessor added in vendor/sifli/chips/sf32lb52/sf32lb52_bth4.c */
FAR struct bt_driver_s *sf32lb52_bt_get_driver(void);

static bool     g_host_started;
static bool     g_host_ready;

/* Serializes hs_ble_host_start() across the UI worker and CLI callers:
 * concurrent bt_netdev_register() runs corrupt the shared HCI state.
 */
static mutex_t  g_host_lock = NXMUTEX_INITIALIZER;

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int hs_ble_host_start(void)
{
  FAR struct bt_driver_s *drv;
  int ret;
  int attempt;

  nxmutex_lock(&g_host_lock);

  if (g_host_ready)
    {
      nxmutex_unlock(&g_host_lock);
      return OK;
    }

  if (g_host_started)
    {
      nxmutex_unlock(&g_host_lock);
      return -EALREADY;
    }

  g_host_started = true;

  drv = sf32lb52_bt_get_driver();
  if (drv == NULL)
    {
      g_host_started = false;
      hs_ble_stage("no vendor driver");
      nxmutex_unlock(&g_host_lock);
      return -ENODEV;
    }

  for (attempt = 0; attempt < 3; attempt++)
    {
      ret = bt_netdev_register(drv);
      if (ret >= 0)
        {
          break;
        }

      wlerr("ERROR: bt_netdev_register failed: %d (attempt %d)\n",
            ret, attempt + 1);

      bt_driver_unset(drv);

      if (attempt < 2)
        {
          usleep(1000000);
        }
    }

  if (ret < 0)
    {
      g_host_started = false;
      nxmutex_unlock(&g_host_lock);
      return ret;
    }

  g_host_ready = true;
  nxmutex_unlock(&g_host_lock);
  printf("[BLE] NuttX host stack up, controller initialised\n");
  return OK;
}

bool hs_ble_host_ready(void)
{
  return g_host_ready;
}

int hs_ble_host_hci_raw(uint16_t opcode, const void *params, uint8_t plen)
{
  /* Raw H:4 access is not exposed any more: the vendor driver owns the
   * controller transport and the host stack owns the driver.
   */

  return -ENOTCONN;
}

const uint8_t *hs_ble_host_bdaddr(void)
{
  return NULL;
}

uint16_t hs_ble_host_conn_handle(void)
{
  return 0xffff;
}

uint16_t hs_ble_host_conn_interval(void)
{
  return 0;
}

#endif /* CONFIG_WIRELESS_BLUETOOTH_HOST */
