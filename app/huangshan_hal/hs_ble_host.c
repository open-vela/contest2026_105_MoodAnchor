/****************************************************************************
 * app/huangshan_hal/hs_ble_host.c
 *
 * Bind the NuttX Bluetooth host stack to the SiFli on-chip controller
 * through the official NuttX HCI-UART driver stack:
 *
 *   /dev/ttyHCI0 (uart_bth4, vendor)  ->  bt_uart_shim (lower half)
 *   -> bt_uart generic upper half (official H:4 framing) -> bt_driver_s
 *   -> bt_netdev_register()
 *
 * All H:4 framing, event/ACL headers, buffer management and fd handling
 * are done by the official NuttX drivers.  This file only performs
 * registration (with retries for the slow LCPU cold start) and exposes a
 * few status helpers for the application layers.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <debug.h>

#include <nuttx/mutex.h>
#include <nuttx/wireless/bluetooth/bt_driver.h>
#include <nuttx/wireless/bluetooth/bt_uart.h>
#include <nuttx/wireless/bluetooth/bt_uart_shim.h>

#include "huangshan_hal.h"
#include "hs_ble.h"

#ifdef CONFIG_WIRELESS_BLUETOOTH_HOST

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* Not yet exported from bt_driver.h: releases g_btdev.btdev. */
void bt_driver_unset(FAR struct bt_driver_s *btdev);

/* btuart_create() lives in drivers/wireless/bluetooth/bt_uart_generic.c and
 * is not (yet) declared in a public header.
 */
int btuart_create(FAR const struct btuart_lowerhalf_s *lower,
                  FAR struct bt_driver_s **driver);

static FAR struct bt_driver_s *g_drv;
static bool     g_host_started;
static bool     g_host_ready;

/* Serializes hs_ble_host_start() across the UI worker and CLI callers.
 * Without it, two concurrent bt_netdev_register() runs corrupt the shared
 * HCI state (double free in bt_buf.c).
 */
static mutex_t  g_host_lock = NXMUTEX_INITIALIZER;

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int hs_ble_host_start(void)
{
  FAR struct btuart_lowerhalf_s *lower;
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

  /* The LCPU controller is reset independently of the SoC and can take well
   * over a second to answer the first commands after a cold power-up, which
   * shows up as sync-command timeouts during bt_initialize().  Release the
   * transport, give the LCPU a moment and retry.
   */

  for (attempt = 0; attempt < 3; attempt++)
    {
      lower = btuart_shim_getdevice(HS_BLE_DEVICE);
      if (lower == NULL)
        {
          ret = -ENODEV;
        }
      else
        {
          ret = btuart_create(lower, &g_drv);
          if (ret == 0)
            {
              ret = bt_netdev_register(g_drv);
            }
        }

      if (ret >= 0)
        {
          break;
        }

      wlerr("ERROR: bt host bring-up failed: %d (attempt %d)\n",
            ret, attempt + 1);

      if (g_drv != NULL)
        {
          if (g_drv->close != NULL)
            {
              g_drv->close(g_drv);
            }

          g_drv = NULL;
        }

      bt_driver_unset(NULL);

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
  /* Raw H:4 access is no longer available (and no longer needed): the
   * official HCI-UART driver owns the transport.
   */

  return -ENOTCONN;
}

const uint8_t *hs_ble_host_bdaddr(void)
{
  /* The controller address is no longer snooped from the H4 stream.
   * The GATT layer falls back to a fixed name suffix.
   */

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
