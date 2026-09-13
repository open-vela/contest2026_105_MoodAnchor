/****************************************************************************
 * app/huangshan_hal/hs_ble_host.c
 *
 * Bind the NuttX Bluetooth host stack (nuttx/wireless/bluetooth) to the
 * SiFli on-chip controller.
 *
 * The vendor BSP registers /dev/ttyHCI0 as an H:4 endpoint towards the
 * controller (vendor/sifli/chips/sf32lb52/sf32lb52_bth4.c).  This file
 * implements the missing host side:
 *
 *   open      - open the transport, start the receive thread
 *   send      - add the H:4 type byte and write the packet
 *   receive   - parse the H:4 stream and feed bt_netdev_receive()
 *
 * While the bytes pass through, a few controller events are snooped so the
 * application can learn the controller address (advertising name suffix)
 * and the connection handle/interval.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <debug.h>
#include <pthread.h>

#include <nuttx/wireless/bluetooth/bt_buf.h>
#include <nuttx/wireless/bluetooth/bt_driver.h>
#include <nuttx/wireless/bluetooth/bt_hci.h>
#include <nuttx/wireless/bluetooth/bt_uart.h>
#include <nuttx/mutex.h>

#include "huangshan_hal.h"
#include "hs_ble.h"

#ifdef CONFIG_WIRELESS_BLUETOOTH_HOST

/* Not yet exported from bt_driver.h: releases g_btdev.btdev. */
void bt_driver_unset(FAR struct bt_driver_s *btdev);

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* bt_receive() dispatches into HCI/L2CAP/GATT, which needs considerably more
 * stack than the transport loop itself.  A stack overflow here corrupts
 * g_h4.fd and makes every later write() fail with EBADF.
 */
#define HS_BLE_RX_STACKSIZE   8192
#define HS_BLE_H4_BUFSIZE     600
#define HS_BLE_NO_CONN        0xffff

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct hs_ble_h4_s
{
  struct bt_driver_s drv;       /* Must stay first (casted to/from drv) */

  /* fd tables are per task group in NuttX.  rx_fd is opened by the
   * application (valid for the receive thread), while tx_fd is opened
   * lazily by whatever task sends - hci_tx_kthread is a kernel thread and
   * cannot see the application's fds (write() would fail with EBADF).
   */
  int                rx_fd;
  int                tx_fd;
  volatile bool      running;
  pthread_t          thread;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct hs_ble_h4_s g_h4 =
{
  .rx_fd = -1,
  .tx_fd = -1,
};

static bool     g_host_started;
static bool     g_host_ready;

/* Serializes hs_ble_host_start() across the UI worker and CLI callers.
 * Without it, two concurrent bt_netdev_register() runs corrupt the shared
 * HCI state (double free in bt_buf.c, ASSERT at POOL_BUFFER_DYNAMIC).
 */
static mutex_t  g_host_lock = NXMUTEX_INITIALIZER;

static uint8_t  g_bdaddr[6];
static bool     g_bdaddr_valid;

static uint16_t g_conn_handle = HS_BLE_NO_CONN;
static uint16_t g_conn_interval;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: hs_ble_h4_snoop
 *
 * Description:
 *   Look at controller events on their way to the host stack.  Only the
 *   controller address, the connection handle and the connection interval
 *   are extracted; everything is passed to the stack unchanged.
 *
 ****************************************************************************/

static void hs_ble_h4_snoop(uint8_t evt_code, FAR const uint8_t *pkt,
                            size_t len)
{
  /* pkt points at the event parameters only (no event code, no length) */

  if (evt_code == BT_HCI_EVT_CMD_COMPLETE)
    {
      uint16_t opcode;

      /* params: ncmd(1) + opcode(2) + status(1) + return parameters */

      if (len < 4)
        {
          return;
        }

      opcode = (uint16_t)pkt[1] | ((uint16_t)pkt[2] << 8);

      if (opcode == BT_HCI_OP_READ_BD_ADDR && len >= 10)
        {
          memcpy(g_bdaddr, &pkt[4], sizeof(g_bdaddr));
          g_bdaddr_valid = true;

          wlinfo("BLE: controller address %02x:%02x:%02x:%02x:%02x:%02x\n",
                 g_bdaddr[5], g_bdaddr[4], g_bdaddr[3], g_bdaddr[2],
                 g_bdaddr[1], g_bdaddr[0]);
        }
    }
  else if (evt_code == BT_HCI_EVT_LE_META_EVENT)
    {
      /* params: subevent(1) + subevent parameters */

      if (pkt[0] == BT_HCI_EVT_LE_CONN_COMPLETE && len >= 4)
        {
          uint8_t status = pkt[1];

          if (status == 0)
            {
              g_conn_handle   = (uint16_t)pkt[2] | ((uint16_t)pkt[3] << 8);
              g_conn_interval = 0;
              if (len >= 14)
                {
                  g_conn_interval = (uint16_t)pkt[12] |
                                    ((uint16_t)pkt[13] << 8);
                }

              wlinfo("BLE: connected handle 0x%04x interval %u\n",
                     g_conn_handle, g_conn_interval);
            }
        }
      else if (pkt[0] == BT_HCI_EVT_LE_CONN_UPDATE_COMPLETE && len >= 7)
        {
          g_conn_handle   = (uint16_t)pkt[1] | ((uint16_t)pkt[2] << 8);
          g_conn_interval = (uint16_t)pkt[3] | ((uint16_t)pkt[4] << 8);

          wlinfo("BLE: connection updated handle 0x%04x interval %u\n",
                 g_conn_handle, g_conn_interval);
        }
    }
  else if (evt_code == BT_HCI_EVT_DISCONN_COMPLETE)
    {
      g_conn_handle   = HS_BLE_NO_CONN;
      g_conn_interval = 0;
      wlinfo("BLE: disconnected\n");
    }
}

/****************************************************************************
 * Name: hs_ble_h4_read_full
 *
 * Description:
 *   Read exactly len bytes from the transport (short reads are possible
 *   because the vendor driver delivers one H:4 packet at a time).
 *
 ****************************************************************************/

static int hs_ble_h4_read_full(int fd, FAR uint8_t *buf, size_t len)
{
  size_t done = 0;

  while (done < len)
    {
      ssize_t nread = read(fd, &buf[done], len - done);

      if (nread == 0)
        {
          return -ENODATA;
        }

      if (nread < 0)
        {
          if (errno == EINTR || errno == EAGAIN)
            {
              continue;
            }

          return -errno;
        }

      done += (size_t)nread;
    }

  return OK;
}

/****************************************************************************
 * Name: hs_ble_h4_rxthread
 *
 * Description:
 *   Drain the H:4 transport and hand the packets over to the host stack.
 *
 ****************************************************************************/

static FAR void *hs_ble_h4_rxthread(FAR void *arg)
{
  FAR struct hs_ble_h4_s *priv = (FAR struct hs_ble_h4_s *)arg;
  static uint8_t pkt[HS_BLE_H4_BUFSIZE];
  uint8_t hdr[5];
  size_t hdrlen;
  size_t plen;
  enum bt_buf_type_e type;
  int ret;

  printf("[h4] rx thread started: fd=%d running=%d tid=%lu receive=%p\n",
         priv->rx_fd, (int)priv->running, (unsigned long)pthread_self(),
         (void *)priv->drv.receive);

  while (priv->running)
    {
      ret = hs_ble_h4_read_full(priv->rx_fd, &hdr[0], 1);
      if (ret < 0)
        {
          printf("[h4] rx thread: type read failed (%d), exiting\n", ret);
          break;
        }

      switch (hdr[0])
        {
          case H4_EVT:
            hdrlen = 2;            /* event code + parameter length */
            type   = BT_EVT;
            break;

          case H4_ACL:
            hdrlen = 4;            /* handle (2) + data length (2) */
            type   = BT_ACL_IN;
            break;

          case H4_ISO:
            hdrlen = 4;
            type   = BT_ISO_IN;
            break;

          default:
            continue;
        }

      /* Read the remaining header bytes (hdrlen of them, so hdr[1..hdrlen]) */

      ret = hs_ble_h4_read_full(priv->rx_fd, &hdr[1], hdrlen);
      if (ret < 0)
        {
          printf("[h4] rx thread: header read failed (%d)\n", ret);
          break;
        }

      if (hdr[0] == H4_ACL || hdr[0] == H4_ISO)
        {
          plen = (size_t)hdr[3] | ((size_t)hdr[4] << 8);

          if (hdr[0] == H4_ISO)
            {
              plen &= 0x3fff;
            }
        }
      else
        {
          plen = hdr[2];
        }

      if (plen > sizeof(pkt))
        {
          wlerr("ERROR: oversized HCI packet (%zu)\n", plen);
          break;
        }

      ret = hs_ble_h4_read_full(priv->rx_fd, pkt, plen);
      if (ret < 0)
        {
          printf("[h4] rx thread: payload read failed (%d)\n", ret);
          break;
        }

      if (priv->drv.receive == NULL)
        {
          printf("[h4] ERROR: receive is NULL, dropping type=%02x len=%u\n",
                 hdr[0], (unsigned)plen);
          continue;
        }

      if (type == BT_EVT)
        {
          /* NuttX expects the complete HCI event payload:
           * event code + parameter length + parameters
           */
          uint8_t evt[HS_BLE_H4_BUFSIZE + 2];

          hs_ble_h4_snoop(hdr[1], pkt, plen);

          evt[0] = hdr[1];
          evt[1] = (uint8_t)plen;
          memcpy(&evt[2], pkt, plen);

          priv->drv.receive(&priv->drv, type, evt, plen + 2);
          continue;
        }

      if (type == BT_ACL_IN || type == BT_ISO_IN)
        {
          /* NuttX expects the ACL/ISO header (handle + data length, the
           * 4 bytes after the H:4 type byte) followed by the payload.
           * Passing only the payload made hci_acl() parse garbage handles,
           * drop every L2CAP/ATT/SMP packet and time out the phone.
           */
          uint8_t acl[HS_BLE_H4_BUFSIZE + 4];

          memcpy(acl, &hdr[1], 4);
          memcpy(&acl[4], pkt, plen);

          priv->drv.receive(&priv->drv, type, acl, plen + 4);
          continue;
        }

      priv->drv.receive(&priv->drv, type, pkt, plen);
    }

  return NULL;
}

/****************************************************************************
 * Name: hs_ble_h4_send
 ****************************************************************************/

static int hs_ble_h4_send(FAR struct bt_driver_s *drv,
                       enum bt_buf_type_e type, FAR void *data, size_t len)
{
  FAR struct hs_ble_h4_s *priv = (FAR struct hs_ble_h4_s *)drv;
  FAR uint8_t *hdr = (FAR uint8_t *)data - drv->head_reserve;
  size_t total = len + drv->head_reserve;
  size_t done  = 0;
  ssize_t nwritten;

  switch (type)
    {
      case BT_CMD:
        hdr[0] = H4_CMD;
        break;

      case BT_ACL_OUT:
        hdr[0] = H4_ACL;
        break;

      case BT_ISO_OUT:
        hdr[0] = H4_ISO;
        break;

      default:
        return -EINVAL;
    }

  /* The sender runs in a different task group (hci_tx_kthread is a kernel
   * thread with an empty fd table).  Open a transport fd lazily in the
   * calling thread's task group; uart_bth4 supports multiple opens and only
   * the first one runs the vendor controller init.
   */

  if (priv->tx_fd < 0)
    {
      priv->tx_fd = open(HS_BLE_DEVICE, O_RDWR);
      if (priv->tx_fd < 0)
        {
          printf("[h4] tx transport open failed: %d tid=%lu\n", errno,
                 (unsigned long)pthread_self());
          return -errno;
        }

      printf("[h4] tx transport open, fd=%d tid=%lu\n", priv->tx_fd,
             (unsigned long)pthread_self());
    }

  while (done < total)
    {
      nwritten = write(priv->tx_fd, hdr + done, total - done);
      if (nwritten < 0)
        {
          int e = errno;

          if (e == EINTR)
            {
              continue;
            }

          if (e == EBADF)
            {
              /* The transport was closed underneath us (host restart).
               * Reopen in this thread's task group and retry the packet.
               */

              priv->tx_fd = open(HS_BLE_DEVICE, O_RDWR);
              if (priv->tx_fd >= 0)
                {
                  printf("[h4] tx transport reopened, fd=%d\n",
                         priv->tx_fd);
                  continue;
                }

              e = errno;
            }

          printf("[h4] tx write failed: %d fd=%d tid=%lu\n",
                 e, priv->tx_fd, (unsigned long)pthread_self());
          return -e;
        }

      done += (size_t)nwritten;
    }

  return OK;
}

/****************************************************************************
 * Name: hs_ble_h4_open
 ****************************************************************************/

static int hs_ble_h4_open(FAR struct bt_driver_s *drv)
{
  FAR struct hs_ble_h4_s *priv = (FAR struct hs_ble_h4_s *)drv;
  pthread_attr_t attr;
  int ret;

  /* NOTE: an extra open()/close() pair used to be done here to force the
   * vendor driver through controller_deinit().  That turned out to poison the
   * transport: the second open() returned a valid fd, but every write() on it
   * failed with EBADF (errno 9), so HCI_RESET never reached the controller and
   * the host stack timed out with -ETIMEDOUT.
   *
   * The raw path (huangshan_hal_demo ble, huangshan_hal.c) opens /dev/ttyHCI0
   * exactly once and works, so do exactly the same here.
   */

  priv->rx_fd = open(HS_BLE_DEVICE, O_RDWR);
  if (priv->rx_fd < 0)
    {
      wlerr("ERROR: cannot open %s: %d\n", HS_BLE_DEVICE, errno);
      return -errno;
    }

  printf("[h4] transport open, fd=%d\n", priv->rx_fd);

  priv->running = true;
  printf("[h4] open: fd=%d priv=%p drv=%p tid=%lu\n", priv->rx_fd,
         (void *)priv, (void *)drv, (unsigned long)pthread_self());

  ret = pthread_attr_init(&attr);
  if (ret == 0)
    {
      ret = pthread_attr_setstacksize(&attr, HS_BLE_RX_STACKSIZE);
    }

  if (ret == 0)
    {
      ret = pthread_create(&priv->thread, &attr, hs_ble_h4_rxthread, priv);
    }

  pthread_attr_destroy(&attr);

  if (ret != 0)
    {
      priv->running = false;
      close(priv->rx_fd);
      priv->rx_fd = -1;
      wlerr("ERROR: cannot start HCI receive thread: %d\n", ret);
      return -ret;
    }

  printf("[h4] open: rx thread spawned, running=%d fd=%d\n",
         (int)priv->running, priv->rx_fd);
  return OK;
}

/****************************************************************************
 * Name: hs_ble_h4_close
 ****************************************************************************/

static void hs_ble_h4_close(FAR struct bt_driver_s *drv)
{
  FAR struct hs_ble_h4_s *priv = (FAR struct hs_ble_h4_s *)drv;

  printf("[h4] close called: rx_fd=%d tx_fd=%d running=%d tid=%lu\n",
         priv->rx_fd, priv->tx_fd, (int)priv->running,
         (unsigned long)pthread_self());

  priv->running = false;

  if (priv->rx_fd >= 0)
    {
      close(priv->rx_fd);
      priv->rx_fd = -1;
    }

  /* tx_fd lives in the kernel thread's task group and cannot be closed from
   * here; it is invalidated via the EBADF self-heal path in send().
   */
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int hs_ble_host_start(void)
{
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

  memset(&g_h4.drv, 0, sizeof(g_h4.drv));
  g_h4.drv.head_reserve = H4_HEADER_SIZE;
  g_h4.drv.open         = hs_ble_h4_open;
  g_h4.drv.send         = hs_ble_h4_send;
  g_h4.drv.close        = hs_ble_h4_close;

  g_host_started = true;

  /* bt_netdev_register() calls open() (which starts the receive thread) and
   * then initialises the controller, registers the default GAP/GATT
   * services and starts advertising.
   *
   * The LCPU occasionally drops the first commands after wake-up, which
   * shows up as a sync-command timeout during bt_initialize().  Release
   * the transport, give the LCPU a moment and retry once.
   */

  for (attempt = 0; attempt < 3; attempt++)
    {
      ret = bt_netdev_register(&g_h4.drv);
      if (ret >= 0)
        {
          break;
        }

      wlerr("ERROR: bt_netdev_register failed: %d (attempt %d)\n",
            ret, attempt + 1);

      /* Release the transport: closing it makes the vendor driver run its
       * controller_deinit(), so switching the UI switch off and on again
       * starts from a clean controller state instead of failing forever.
       */

      hs_ble_h4_close(&g_h4.drv);
      bt_driver_unset(&g_h4.drv);

      if (attempt < 2)
        {
          /* Cold power-up of the LCPU controller can take well over a
           * second; keep retrying with generous spacing.
           */

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
  uint8_t buf[4 + 255];
  size_t total = (size_t)plen + 4;
  size_t done  = 0;
  ssize_t nwritten;

  if (!g_host_ready || g_h4.rx_fd < 0)
    {
      return -ENOTCONN;
    }

  buf[0] = H4_CMD;
  buf[1] = (uint8_t)(opcode & 0xff);
  buf[2] = (uint8_t)(opcode >> 8);
  buf[3] = plen;

  if (plen > 0 && params != NULL)
    {
      memcpy(&buf[4], params, plen);
    }

  while (done < total)
    {
      nwritten = write(g_h4.rx_fd, &buf[done], total - done);
      if (nwritten < 0)
        {
          if (errno == EINTR)
            {
              continue;
            }

          return -errno;
        }

      done += (size_t)nwritten;
    }

  return OK;
}

const uint8_t *hs_ble_host_bdaddr(void)
{
  return g_bdaddr_valid ? g_bdaddr : NULL;
}

uint16_t hs_ble_host_conn_handle(void)
{
  return g_conn_handle;
}

uint16_t hs_ble_host_conn_interval(void)
{
  return g_conn_interval;
}

#endif /* CONFIG_WIRELESS_BLUETOOTH_HOST */
