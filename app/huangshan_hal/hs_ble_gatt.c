/****************************************************************************
 * app/huangshan_hal/hs_ble_gatt.c
 *
 * MoodAnchor GATT server.
 *
 * Layout of the installed database (replaces the default table installed by
 * the host stack, so the mandatory GAP/GATT services are included here):
 *
 *   0x0001 Generic Access (0x1800)        - device name, appearance
 *   0x0006 Generic Attribute (0x1801)     - service changed
 *   0x0010 MoodAnchor service (128 bit)   - event (Notify), control (Write)
 *   0x0020 Device Information (0x180a)    - name/model/fw/serial/pnp
 *   0x0030 Battery Service (0x180f)       - battery level (Read + Notify)
 *
 * The custom service UUID is d38a0001-1234-5678-9abc-def012345678 with the
 * event characteristic at ...0002 and the control characteristic at ...0003.
 *
 * Advertising (name, service UUID and interval) is driven with raw HCI
 * commands through hs_ble_host_hci_raw() because the NuttX host stack only
 * offers the default advertising data to applications.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <pthread.h>

#include <nuttx/wireless/bluetooth/bt_core.h>
#include <nuttx/wireless/bluetooth/bt_gatt.h>
#include <nuttx/wireless/bluetooth/bt_hci.h>
#include <nuttx/wireless/bluetooth/bt_uuid.h>

#include "hs_ble.h"

#ifdef CONFIG_WIRELESS_BLUETOOTH_HOST

/* These host advertising helpers are implemented by bt_hcicore.c but are
 * intentionally not installed as public headers.  Keep the declaration local
 * so this application uses the same serialized HCI command path as the host
 * stack instead of racing it with raw H:4 writes. */
extern int bt_start_advertising(uint8_t type,
                                FAR const struct bt_eir_s *ad,
                                FAR const struct bt_eir_s *sd);
extern int bt_stop_advertising(void);

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Attribute handles */

#define HS_H_GAP_SVC            0x0001
#define HS_H_GAP_NAME_CHRC      0x0002
#define HS_H_GAP_NAME_VAL       0x0003
#define HS_H_GAP_APPEAR_CHRC    0x0004
#define HS_H_GAP_APPEAR_VAL     0x0005

#define HS_H_GATT_SVC           0x0006
#define HS_H_GATT_SC_CHRC       0x0007
#define HS_H_GATT_SC_VAL        0x0008
#define HS_H_GATT_SC_CCC        0x0009

#define HS_H_MOOD_SVC           0x0010
#define HS_H_EVENT_CHRC         0x0011
#define HS_H_EVENT_VAL          0x0012
#define HS_H_EVENT_CCC          0x0013
#define HS_H_CTRL_CHRC          0x0014
#define HS_H_CTRL_VAL           0x0015
#define HS_H_DATA_CHRC          0x0016
#define HS_H_DATA_VAL           0x0017
#define HS_H_DATA_CCC           0x0018

#define HS_H_STATUS_CHRC        0x0019
#define HS_H_STATUS_VAL         0x001a
#define HS_H_STATUS_CCC         0x001b

#define HS_H_DIS_SVC            0x0020
#define HS_H_DIS_MANUF_CHRC     0x0021
#define HS_H_DIS_MANUF_VAL      0x0022
#define HS_H_DIS_MODEL_CHRC     0x0023
#define HS_H_DIS_MODEL_VAL      0x0024
#define HS_H_DIS_FW_CHRC        0x0025
#define HS_H_DIS_FW_VAL         0x0026
#define HS_H_DIS_SN_CHRC        0x0027
#define HS_H_DIS_SN_VAL         0x0028
#define HS_H_DIS_PNP_CHRC       0x0029
#define HS_H_DIS_PNP_VAL        0x002A

#define HS_H_BAS_SVC            0x0030
#define HS_H_BAS_LEVEL_CHRC     0x0031
#define HS_H_BAS_LEVEL_VAL      0x0032
#define HS_H_BAS_LEVEL_CCC      0x0033

#define HS_BLE_NAME_PREFIX      "是非钟-"
#define HS_BLE_NAME_SIZE        24

#define HS_BLE_ADV_MAX          31

/* LE advertising parameter defaults */

#define HS_BLE_ADV_TYPE         0x00    /* ADV_IND, connectable undirected */
#define HS_BLE_ADV_OWNADDR      0x00    /* public address */
#define HS_BLE_ADV_CHANMAP      0x07
#define HS_BLE_ADV_FILTER       0x00

#define HS_BLE_INTERVAL_MIN     0x0020  /* 20 ms */
#define HS_BLE_INTERVAL_MAX     0x4000  /* 10.24 s */

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct hs_ble_advparam_s
{
  uint8_t  min[2];
  uint8_t  max[2];
  uint8_t  type;
  uint8_t  own_addr_type;
  uint8_t  peer_addr_type;
  uint8_t  peer_addr[6];
  uint8_t  channel_map;
  uint8_t  filter_policy;
};

struct hs_ble_data_s
{
  uint8_t  len;
  uint8_t  data[HS_BLE_ADV_MAX];
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* Service and characteristic UUIDs --------------------------------------- */

static struct bt_uuid_s g_uuid_gap =
{
  .type  = BT_UUID_16,
  .u.u16 = BT_UUID_GAP,
};

static struct bt_uuid_s g_uuid_gap_name =
{
  .type  = BT_UUID_16,
  .u.u16 = BT_UUID_GAP_DEVICE_NAME,
};

static struct bt_uuid_s g_uuid_gap_appearance =
{
  .type  = BT_UUID_16,
  .u.u16 = BT_UUID_GAP_APPEARANCE,
};

static struct bt_uuid_s g_uuid_gatt =
{
  .type  = BT_UUID_16,
  .u.u16 = BT_UUID_GATT,
};

static struct bt_uuid_s g_uuid_gatt_sc =
{
  .type  = BT_UUID_16,
  .u.u16 = 0x2a05,                /* GATT Service Changed */
};

/* d38a0001-1234-5678-9abc-def012345678 (little endian on the air) */

static struct bt_uuid_s g_uuid_mood =
{
  .type = BT_UUID_128,
  .u.u128 =
  {
    0x01, 0x00, 0x8a, 0xd3, 0x34, 0x12, 0x78, 0x56,
    0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12, 0xf0, 0xde
  }
};

/* d38a0002-1234-5678-9abc-def012345678 */

static struct bt_uuid_s g_uuid_event =
{
  .type = BT_UUID_128,
  .u.u128 =
  {
    0x02, 0x00, 0x8a, 0xd3, 0x34, 0x12, 0x78, 0x56,
    0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12, 0xf0, 0xde
  }
};

/* d38a0003-1234-5678-9abc-def012345678 */

static struct bt_uuid_s g_uuid_control =
{
  .type = BT_UUID_128,
  .u.u128 =
  {
    0x03, 0x00, 0x8a, 0xd3, 0x34, 0x12, 0x78, 0x56,
    0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12, 0xf0, 0xde
  }
};

/* d38a0004-1234-5678-9abc-def012345678 (live sensor samples) */

static struct bt_uuid_s g_uuid_data =
{
  .type = BT_UUID_128,
  .u.u128 =
  {
    0x04, 0x00, 0x8a, 0xd3, 0x34, 0x12, 0x78, 0x56,
    0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12, 0xf0, 0xde
  }
};

/* d38a0005-1234-5678-9abc-def012345678 (full device status) */

static struct bt_uuid_s g_uuid_status =
{
  .type = BT_UUID_128,
  .u.u128 =
  {
    0x05, 0x00, 0x8a, 0xd3, 0x34, 0x12, 0x78, 0x56,
    0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12, 0xf0, 0xde
  }
};

static struct bt_uuid_s g_uuid_dis =
{
  .type  = BT_UUID_16,
  .u.u16 = BT_UUID_DIS,
};

static struct bt_uuid_s g_uuid_dis_manuf =
{
  .type  = BT_UUID_16,
  .u.u16 = BT_UUID_DIS_MANUFACTURER_NAME_STRING,
};

static struct bt_uuid_s g_uuid_dis_model =
{
  .type  = BT_UUID_16,
  .u.u16 = BT_UUID_DIS_MODEL_NUMBER_STRING,
};

static struct bt_uuid_s g_uuid_dis_fw =
{
  .type  = BT_UUID_16,
  .u.u16 = BT_UUID_DIS_FIRMWARE_REVISION_STRING,
};

static struct bt_uuid_s g_uuid_dis_sn =
{
  .type  = BT_UUID_16,
  .u.u16 = BT_UUID_DIS_SERIAL_NUMBER_STRING,
};

static struct bt_uuid_s g_uuid_dis_pnp =
{
  .type  = BT_UUID_16,
  .u.u16 = BT_UUID_DIS_PNP_ID,
};

static struct bt_uuid_s g_uuid_bas =
{
  .type  = BT_UUID_16,
  .u.u16 = BT_UUID_BAS,
};

static struct bt_uuid_s g_uuid_bas_level =
{
  .type  = BT_UUID_16,
  .u.u16 = BT_UUID_BAS_BATTERY_LEVEL,
};

/* Characteristic declarations --------------------------------------------- */

static struct bt_gatt_chrc_s g_chrc_gap_name =
{
  .properties   = BT_GATT_CHRC_READ,
  .value_handle = HS_H_GAP_NAME_VAL,
  .uuid         = &g_uuid_gap_name,
};

static struct bt_gatt_chrc_s g_chrc_gap_appearance =
{
  .properties   = BT_GATT_CHRC_READ,
  .value_handle = HS_H_GAP_APPEAR_VAL,
  .uuid         = &g_uuid_gap_appearance,
};

static struct bt_gatt_chrc_s g_chrc_gatt_sc =
{
  .properties   = BT_GATT_CHRC_INDICATE,
  .value_handle = HS_H_GATT_SC_VAL,
  .uuid         = &g_uuid_gatt_sc,
};

static struct bt_gatt_chrc_s g_chrc_event =
{
  .properties   = BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
  .value_handle = HS_H_EVENT_VAL,
  .uuid         = &g_uuid_event,
};

static struct bt_gatt_chrc_s g_chrc_control =
{
  .properties   = BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
  .value_handle = HS_H_CTRL_VAL,
  .uuid         = &g_uuid_control,
};

static struct bt_gatt_chrc_s g_chrc_data =
{
  .properties   = BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
  .value_handle = HS_H_DATA_VAL,
  .uuid         = &g_uuid_data,
};

static struct bt_gatt_chrc_s g_chrc_status =
{
  .properties   = BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
  .value_handle = HS_H_STATUS_VAL,
  .uuid         = &g_uuid_status,
};

static struct bt_gatt_chrc_s g_chrc_dis_manuf =
{
  .properties   = BT_GATT_CHRC_READ,
  .value_handle = HS_H_DIS_MANUF_VAL,
  .uuid         = &g_uuid_dis_manuf,
};

static struct bt_gatt_chrc_s g_chrc_dis_model =
{
  .properties   = BT_GATT_CHRC_READ,
  .value_handle = HS_H_DIS_MODEL_VAL,
  .uuid         = &g_uuid_dis_model,
};

static struct bt_gatt_chrc_s g_chrc_dis_fw =
{
  .properties   = BT_GATT_CHRC_READ,
  .value_handle = HS_H_DIS_FW_VAL,
  .uuid         = &g_uuid_dis_fw,
};

static struct bt_gatt_chrc_s g_chrc_dis_sn =
{
  .properties   = BT_GATT_CHRC_READ,
  .value_handle = HS_H_DIS_SN_VAL,
  .uuid         = &g_uuid_dis_sn,
};

static struct bt_gatt_chrc_s g_chrc_dis_pnp =
{
  .properties   = BT_GATT_CHRC_READ,
  .value_handle = HS_H_DIS_PNP_VAL,
  .uuid         = &g_uuid_dis_pnp,
};

static struct bt_gatt_chrc_s g_chrc_bas_level =
{
  .properties   = BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
  .value_handle = HS_H_BAS_LEVEL_VAL,
  .uuid         = &g_uuid_bas_level,
};

/* Client characteristic configurations ------------------------------------ */

static struct bt_gatt_ccc_cfg_s g_ccc_sc[1];
static struct bt_gatt_ccc_cfg_s g_ccc_event[1];
static struct bt_gatt_ccc_cfg_s g_ccc_data[1];
static struct bt_gatt_ccc_cfg_s g_ccc_status[1];
static struct bt_gatt_ccc_cfg_s g_ccc_battery[1];

/* Characteristic values --------------------------------------------------- */

static char     g_name[HS_BLE_NAME_SIZE] = HS_BLE_NAME_PREFIX "0000";
static uint16_t g_appearance;

static uint8_t  g_event_pkt[HS_BLE_EVENT_LEN];
static uint16_t g_event_seq;

static uint8_t  g_data_pkt[HS_BLE_DATA_LEN];
static uint8_t  g_status_pkt[HS_BLE_STATUS_LEN];

static uint8_t  g_ctrl[HS_BLE_CTRL_MAX];
static size_t   g_ctrl_len;
static uint8_t  g_report_period = 5;

static uint8_t  g_battery = 100;

static const char g_dis_manuf[] = "MoodAnchor Team";
static const char g_dis_model[] = "MoodAnchor-W1";
static const char g_dis_fw[]    = "1.0.0";

static char     g_dis_sn[8] = "000000";

/* Ugly but the PnP ID is a fixed 7 byte structure:
 * vendor id source (2 = USB), vendor id, product id, product version.
 */

static uint8_t  g_dis_pnp[7] =
{
  0x02, 0x34, 0x12, 0x01, 0x00, 0x00, 0x01
};

static bool     g_gatt_installed;
static bool     g_adv_enabled;
static pthread_mutex_t g_adv_lock = PTHREAD_MUTEX_INITIALIZER;

static uint16_t g_adv_int_min = HS_BLE_ADV_INT_IDLE * 8 / 5;
static uint16_t g_adv_int_max = HS_BLE_ADV_INT_IDLE * 8 / 5;

static int      g_boost_gen;

/* Advertising data -------------------------------------------------------- */

static uint8_t  g_adv[HS_BLE_ADV_MAX];
static uint8_t  g_adv_len;
static uint8_t  g_srsp[HS_BLE_ADV_MAX];
static uint8_t  g_srsp_len;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static volatile bool g_peer_connected;

bool hs_ble_gatt_peer_connected(void)
{
  return g_peer_connected;
}

static void hs_ble_ccc_cfg_changed(uint16_t value)
{
  /* The stack keeps the per-peer CCC configuration and bt_gatt_notify()
   * picks it up.  We only track whether some peer has subscribed (used as
   * the "phone connected" signal for the UI).
   */

  g_peer_connected = (value != 0);
}

/****************************************************************************
 * Name: hs_ble_read_*
 *
 * Description:
 *   Read callbacks for the characteristic values.
 *
 ****************************************************************************/

static int hs_ble_read_name(FAR struct bt_conn_s *conn,
                            FAR const struct bt_gatt_attr_s *attr,
                            FAR void *buf, uint8_t len, uint16_t offset)
{
  return bt_gatt_attr_read(conn, attr, buf, len, offset, g_name,
                           (uint8_t)strlen(g_name));
}

static int hs_ble_read_appearance(FAR struct bt_conn_s *conn,
                                  FAR const struct bt_gatt_attr_s *attr,
                                  FAR void *buf, uint8_t len, uint16_t offset)
{
  uint16_t appearance = g_appearance;

  return bt_gatt_attr_read(conn, attr, buf, len, offset, &appearance,
                           sizeof(appearance));
}

static int hs_ble_read_event(FAR struct bt_conn_s *conn,
                             FAR const struct bt_gatt_attr_s *attr,
                             FAR void *buf, uint8_t len, uint16_t offset)
{
  return bt_gatt_attr_read(conn, attr, buf, len, offset, g_event_pkt,
                           sizeof(g_event_pkt));
}

static int hs_ble_read_control(FAR struct bt_conn_s *conn,
                               FAR const struct bt_gatt_attr_s *attr,
                               FAR void *buf, uint8_t len, uint16_t offset)
{
  return bt_gatt_attr_read(conn, attr, buf, len, offset, g_ctrl,
                           (uint8_t)g_ctrl_len);
}

static int hs_ble_read_data(FAR struct bt_conn_s *conn,
                            FAR const struct bt_gatt_attr_s *attr,
                            FAR void *buf, uint8_t len, uint16_t offset)
{
  return bt_gatt_attr_read(conn, attr, buf, len, offset, g_data_pkt,
                           sizeof(g_data_pkt));
}

static int hs_ble_read_status(FAR struct bt_conn_s *conn,
                              FAR const struct bt_gatt_attr_s *attr,
                              FAR void *buf, uint8_t len, uint16_t offset)
{
  return bt_gatt_attr_read(conn, attr, buf, len, offset, g_status_pkt,
                           sizeof(g_status_pkt));
}

static int hs_ble_read_string(FAR struct bt_conn_s *conn,
                              FAR const struct bt_gatt_attr_s *attr,
                              FAR void *buf, uint8_t len, uint16_t offset)
{
  FAR const char *string = attr->user_data;

  return bt_gatt_attr_read(conn, attr, buf, len, offset, string,
                           (uint8_t)strlen(string));
}

static int hs_ble_read_battery(FAR struct bt_conn_s *conn,
                               FAR const struct bt_gatt_attr_s *attr,
                               FAR void *buf, uint8_t len, uint16_t offset)
{
  return bt_gatt_attr_read(conn, attr, buf, len, offset, &g_battery,
                           sizeof(g_battery));
}

static int hs_ble_read_pnp(FAR struct bt_conn_s *conn,
                           FAR const struct bt_gatt_attr_s *attr,
                           FAR void *buf, uint8_t len, uint16_t offset)
{
  return bt_gatt_attr_read(conn, attr, buf, len, offset, g_dis_pnp,
                           sizeof(g_dis_pnp));
}

static int hs_ble_read_service_changed(FAR struct bt_conn_s *conn,
                                       FAR const struct bt_gatt_attr_s *attr,
                                       FAR void *buf, uint8_t len,
                                       uint16_t offset)
{
  uint16_t value = 0x0000;

  return bt_gatt_attr_read(conn, attr, buf, len, offset, &value,
                           sizeof(value));
}

/****************************************************************************
 * Name: hs_ble_write_control
 *
 * Description:
 *   Write callback of the control characteristic.
 *
 ****************************************************************************/

static int hs_ble_write_control(FAR struct bt_conn_s *conn,
                                FAR const struct bt_gatt_attr_s *attr,
                                FAR const void *buf, uint8_t len,
                                uint16_t offset)
{
  if (offset != 0)
    {
      return -EINVAL;
    }

  if (len > HS_BLE_CTRL_MAX)
    {
      len = HS_BLE_CTRL_MAX;
    }

  memcpy(g_ctrl, buf, len);
  g_ctrl_len = len;

  if (len >= 1)
    {
      switch (g_ctrl[0])
        {
          case HS_BLE_CTL_ACK:
            printf("[BLE] phone ack, last seq=%u\n", g_event_seq);
            break;

          case HS_BLE_CTL_CLEAR:
            memset(g_event_pkt, 0, sizeof(g_event_pkt));
            printf("[BLE] phone cleared the pending event\n");
            break;

          case HS_BLE_CTL_INTERVAL:
            if (len >= 2)
              {
                g_report_period = g_ctrl[1];
                printf("[BLE] report period set to %u s\n", g_report_period);
              }
            break;

          default:
            printf("[BLE] control write: opcode 0x%02x len %u\n",
                   g_ctrl[0], (unsigned)len);
            break;
        }
    }

  return len;
}

/* GATT database ----------------------------------------------------------- */

static const struct bt_gatt_attr_s g_attrs[] =
{
  /* Generic Access */

  BT_GATT_PRIMARY_SERVICE(HS_H_GAP_SVC, &g_uuid_gap),
  BT_GATT_CHARACTERISTIC(HS_H_GAP_NAME_CHRC, &g_chrc_gap_name),
  BT_GATT_DESCRIPTOR(HS_H_GAP_NAME_VAL, &g_uuid_gap_name,
                     BT_GATT_PERM_READ, hs_ble_read_name, NULL, NULL),
  BT_GATT_CHARACTERISTIC(HS_H_GAP_APPEAR_CHRC, &g_chrc_gap_appearance),
  BT_GATT_DESCRIPTOR(HS_H_GAP_APPEAR_VAL, &g_uuid_gap_appearance,
                     BT_GATT_PERM_READ, hs_ble_read_appearance, NULL, NULL),

  /* Generic Attribute */

  BT_GATT_PRIMARY_SERVICE(HS_H_GATT_SVC, &g_uuid_gatt),
  BT_GATT_CHARACTERISTIC(HS_H_GATT_SC_CHRC, &g_chrc_gatt_sc),
  BT_GATT_DESCRIPTOR(HS_H_GATT_SC_VAL, &g_uuid_gatt_sc, BT_GATT_PERM_READ,
                     hs_ble_read_service_changed, NULL, NULL),
  BT_GATT_CCC(HS_H_GATT_SC_CCC, HS_H_GATT_SC_VAL, g_ccc_sc,
              hs_ble_ccc_cfg_changed),

  /* MoodAnchor service: event (Notify) and control (Write) */

  BT_GATT_PRIMARY_SERVICE(HS_H_MOOD_SVC, &g_uuid_mood),
  BT_GATT_CHARACTERISTIC(HS_H_EVENT_CHRC, &g_chrc_event),
  BT_GATT_DESCRIPTOR(HS_H_EVENT_VAL, &g_uuid_event, BT_GATT_PERM_READ,
                     hs_ble_read_event, NULL, NULL),
  BT_GATT_CCC(HS_H_EVENT_CCC, HS_H_EVENT_VAL, g_ccc_event,
              hs_ble_ccc_cfg_changed),
  BT_GATT_CHARACTERISTIC(HS_H_CTRL_CHRC, &g_chrc_control),
  BT_GATT_DESCRIPTOR(HS_H_CTRL_VAL, &g_uuid_control,
                     BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
                     hs_ble_read_control, hs_ble_write_control, NULL),
  BT_GATT_CHARACTERISTIC(HS_H_DATA_CHRC, &g_chrc_data),
  BT_GATT_DESCRIPTOR(HS_H_DATA_VAL, &g_uuid_data, BT_GATT_PERM_READ,
                     hs_ble_read_data, NULL, NULL),
  BT_GATT_CCC(HS_H_DATA_CCC, HS_H_DATA_VAL, g_ccc_data,
              hs_ble_ccc_cfg_changed),
  BT_GATT_CHARACTERISTIC(HS_H_STATUS_CHRC, &g_chrc_status),
  BT_GATT_DESCRIPTOR(HS_H_STATUS_VAL, &g_uuid_status, BT_GATT_PERM_READ,
                     hs_ble_read_status, NULL, NULL),
  BT_GATT_CCC(HS_H_STATUS_CCC, HS_H_STATUS_VAL, g_ccc_status,
              hs_ble_ccc_cfg_changed),

  /* Device Information Service */

  BT_GATT_PRIMARY_SERVICE(HS_H_DIS_SVC, &g_uuid_dis),
  BT_GATT_CHARACTERISTIC(HS_H_DIS_MANUF_CHRC, &g_chrc_dis_manuf),
  BT_GATT_DESCRIPTOR(HS_H_DIS_MANUF_VAL, &g_uuid_dis_manuf,
                     BT_GATT_PERM_READ, hs_ble_read_string, NULL,
                     (FAR void *)g_dis_manuf),
  BT_GATT_CHARACTERISTIC(HS_H_DIS_MODEL_CHRC, &g_chrc_dis_model),
  BT_GATT_DESCRIPTOR(HS_H_DIS_MODEL_VAL, &g_uuid_dis_model,
                     BT_GATT_PERM_READ, hs_ble_read_string, NULL,
                     (FAR void *)g_dis_model),
  BT_GATT_CHARACTERISTIC(HS_H_DIS_FW_CHRC, &g_chrc_dis_fw),
  BT_GATT_DESCRIPTOR(HS_H_DIS_FW_VAL, &g_uuid_dis_fw, BT_GATT_PERM_READ,
                     hs_ble_read_string, NULL, (FAR void *)g_dis_fw),
  BT_GATT_CHARACTERISTIC(HS_H_DIS_SN_CHRC, &g_chrc_dis_sn),
  BT_GATT_DESCRIPTOR(HS_H_DIS_SN_VAL, &g_uuid_dis_sn, BT_GATT_PERM_READ,
                     hs_ble_read_string, NULL, (FAR void *)g_dis_sn),
  BT_GATT_CHARACTERISTIC(HS_H_DIS_PNP_CHRC, &g_chrc_dis_pnp),
  BT_GATT_DESCRIPTOR(HS_H_DIS_PNP_VAL, &g_uuid_dis_pnp, BT_GATT_PERM_READ,
                     hs_ble_read_pnp, NULL, NULL),

  /* Battery Service */

  BT_GATT_PRIMARY_SERVICE(HS_H_BAS_SVC, &g_uuid_bas),
  BT_GATT_CHARACTERISTIC(HS_H_BAS_LEVEL_CHRC, &g_chrc_bas_level),
  BT_GATT_DESCRIPTOR(HS_H_BAS_LEVEL_VAL, &g_uuid_bas_level,
                     BT_GATT_PERM_READ, hs_ble_read_battery, NULL, NULL),
  BT_GATT_CCC(HS_H_BAS_LEVEL_CCC, HS_H_BAS_LEVEL_VAL, g_ccc_battery,
              hs_ble_ccc_cfg_changed),
};

/****************************************************************************
 * Name: hs_ble_interval_units
 ****************************************************************************/

static uint16_t hs_ble_interval_units(uint16_t ms)
{
  uint32_t units = ((uint32_t)ms * 8u) / 5u;

  if (units < HS_BLE_INTERVAL_MIN)
    {
      units = HS_BLE_INTERVAL_MIN;
    }
  else if (units > HS_BLE_INTERVAL_MAX)
    {
      units = HS_BLE_INTERVAL_MAX;
    }

  return (uint16_t)units;
}

/****************************************************************************
 * Name: hs_ble_build_advdata
 *
 * Description:
 *   Build the advertising data (flags + 128 bit service UUID) and the scan
 *   response (complete local name).
 *
 ****************************************************************************/

static void hs_ble_build_advdata(void)
{
  size_t nlen = strlen(g_name);
  size_t i = 0;

  if (nlen > HS_BLE_ADV_MAX - 2)
    {
      nlen = HS_BLE_ADV_MAX - 2;
    }

  g_adv[i++] = 2;
  g_adv[i++] = BT_EIR_FLAGS;
  g_adv[i++] = 0x06;              /* LE general discoverable, no BR/EDR */

  g_adv[i++] = 17;                /* 1 byte type + 16 bytes UUID */
  g_adv[i++] = BT_EIR_UUID128_ALL;
  memcpy(&g_adv[i], g_uuid_mood.u.u128, sizeof(g_uuid_mood.u.u128));
  i += sizeof(g_uuid_mood.u.u128);
  g_adv_len = (uint8_t)i;

  i = 0;
  g_srsp[i++] = (uint8_t)(nlen + 1);
  g_srsp[i++] = BT_EIR_NAME_COMPLETE;
  memcpy(&g_srsp[i], g_name, nlen);
  i += nlen;
  g_srsp_len = (uint8_t)i;
}

/****************************************************************************
 * Name: hs_ble_adv_apply
 *
 * Description:
 *   Push data, parameters and the enable flag to the controller.  The
 *   advertising data may not be changed while advertising is enabled, so
 *   the sequence always starts by disabling advertising.
 *
 ****************************************************************************/

static int hs_ble_adv_apply(void)
{
  struct bt_eir_s ad[3];
  struct bt_eir_s sd[2];
  size_t name_len;
  int ret;

  /* bt_start_advertising() updates global host state (g_btdev.adv_enable)
   * and is not re-entrant.  Notifications, UI callbacks and the BLE worker
   * can all request an interval/name update, so serialize the complete
   * stop/configure/start sequence. */
  pthread_mutex_lock(&g_adv_lock);

  /* Use the host command queue for every advertising command.  The previous
   * implementation wrote raw H:4 commands directly while the NuttX HCI TX
   * thread was still sending bt_add_services() commands.  That allowed the
   * two streams to reorder, causing default-name advertisements, mismatched
   * command-complete opcodes, and failed connections. */
  ret = bt_stop_advertising();
  if (ret < 0 && ret != -EALREADY)
    {
      pthread_mutex_unlock(&g_adv_lock);
      return ret;
    }

  /* Main advertising packet: Flags + the complete device name.  Many
   * Android system Bluetooth scanners only list peripherals whose main
   * advertising packet carries a local name (they never send a scan
   * request), so the name must live here rather than in the scan response.
   */

  memset(ad, 0, sizeof(ad));
  ad[0].len = 2;
  ad[0].type = BT_EIR_FLAGS;
  ad[0].data[0] = 0x06;

  name_len = strlen(g_name);
  if (name_len > sizeof(ad[1].data))
    {
      name_len = sizeof(ad[1].data);
    }

  ad[1].len = (uint8_t)(name_len + 1);   /* type byte + name */
  ad[1].type = BT_EIR_NAME_COMPLETE;
  memcpy(ad[1].data, g_name, name_len);

  /* Scan response packet: the 128 bit MoodAnchor service UUID. */

  memset(sd, 0, sizeof(sd));
  sd[0].len = 17;
  sd[0].type = BT_EIR_UUID128_ALL;
  memcpy(sd[0].data, g_uuid_mood.u.u128, sizeof(g_uuid_mood.u.u128));

  ret = bt_start_advertising(BT_LE_ADV_IND, ad, sd);
  if (ret < 0)
    {
      pthread_mutex_unlock(&g_adv_lock);
      return ret;
    }

  g_adv_enabled = true;
  pthread_mutex_unlock(&g_adv_lock);
  return OK;
}

/****************************************************************************
 * Name: hs_ble_boost_thread
 *
 * Description:
 *   Restore the idle advertising interval after an event.
 *
 ****************************************************************************/

static FAR void *hs_ble_boost_thread(FAR void *arg)
{
  int gen = (int)(intptr_t)arg;

  sleep(HS_BLE_ADV_BOOST_SECS);

  if (g_boost_gen == gen && g_adv_enabled)
    {
      hs_ble_adv_set_interval(HS_BLE_ADV_INT_IDLE, HS_BLE_ADV_INT_IDLE);
    }

  return NULL;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int hs_ble_gatt_start(const char *suffix)
{
  char hex[5];

  if (!hs_ble_host_ready())
    {
      return -ENOTCONN;
    }

  if (suffix != NULL && strlen(suffix) == 4)
    {
      snprintf(hex, sizeof(hex), "%s", suffix);
    }
  else
    {
      const uint8_t *addr = hs_ble_host_bdaddr();

      if (addr != NULL)
        {
          snprintf(hex, sizeof(hex), "%02x%02x", addr[1], addr[0]);
        }
      else
        {
          snprintf(hex, sizeof(hex), "0001");
        }
    }

  snprintf(g_name, sizeof(g_name), HS_BLE_NAME_PREFIX "%s", hex);
  snprintf(g_dis_sn, sizeof(g_dis_sn), "%s", hex);

  hs_ble_build_advdata();

  /* Install our database (replaces the one installed by the host stack) */

  bt_gatt_register(g_attrs, sizeof(g_attrs) / sizeof(g_attrs[0]));
  g_gatt_installed = true;

  if (hs_ble_adv_apply() < 0)
    {
      return -EIO;
    }

  printf("[BLE] advertising as \"%s\", service "
         "d38a0001-1234-5678-9abc-def012345678\n", g_name);
  return OK;
}

const char *hs_ble_gatt_name(void)
{
  return g_name;
}

int hs_ble_adv_enable(bool enable)
{
  int ret;

  if (!g_gatt_installed)
    {
      return -ENOTCONN;
    }

  if (enable)
    {
      ret = hs_ble_adv_apply();
    }
  else
    {
      pthread_mutex_lock(&g_adv_lock);
      ret = bt_stop_advertising();
      pthread_mutex_unlock(&g_adv_lock);
    }

  if (!enable && ret == -EALREADY)
    {
      ret = 0;
    }
  if (ret >= 0)
    {
      g_adv_enabled = enable;
      printf("[BLE] advertising %s\n", enable ? "enabled" : "disabled");
    }

  return ret;
}

bool hs_ble_gatt_ready(void)
{
  return g_gatt_installed;
}

int hs_ble_adv_set_interval(uint16_t min_ms, uint16_t max_ms)
{
  if (!g_gatt_installed)
    {
      return -ENOTCONN;
    }

  g_adv_int_min = hs_ble_interval_units(min_ms);
  g_adv_int_max = hs_ble_interval_units(max_ms);

  if (g_adv_int_max < g_adv_int_min)
    {
      g_adv_int_max = g_adv_int_min;
    }

  return hs_ble_adv_apply();
}

int hs_ble_event_notify(uint8_t type, uint8_t risk, uint8_t confidence,
                        uint8_t flags)
{
  struct timespec now;
  uint32_t timestamp;

  if (!g_gatt_installed)
    {
      return -ENOTCONN;
    }

  if (clock_gettime(CLOCK_MONOTONIC, &now) < 0)
    {
      timestamp = 0;
    }
  else
    {
      timestamp = (uint32_t)now.tv_sec * 1000u +
                  (uint32_t)(now.tv_nsec / 1000000);
    }

  g_event_seq++;

  g_event_pkt[0]  = HS_BLE_EVENT_VERSION;
  g_event_pkt[1]  = type;
  g_event_pkt[2]  = (uint8_t)(g_event_seq & 0xff);
  g_event_pkt[3]  = (uint8_t)(g_event_seq >> 8);
  g_event_pkt[4]  = (uint8_t)(timestamp & 0xff);
  g_event_pkt[5]  = (uint8_t)((timestamp >> 8) & 0xff);
  g_event_pkt[6]  = (uint8_t)((timestamp >> 16) & 0xff);
  g_event_pkt[7]  = (uint8_t)((timestamp >> 24) & 0xff);
  g_event_pkt[8]  = risk;
  g_event_pkt[9]  = confidence;
  g_event_pkt[10] = flags;

  bt_gatt_notify(HS_H_EVENT_VAL, g_event_pkt, sizeof(g_event_pkt));

  printf("[BLE] event seq=%u type=0x%02x risk=%u conf=%u flags=0x%02x%s\n",
         g_event_seq, type, risk, confidence, flags,
         hs_ble_host_conn_handle() == 0xffff ? " (no peer connected)" : "");

  /* Speed up advertising so a phone that just saw the event reconnects
   * quickly.
   */

  if (g_adv_enabled)
    {
      int gen = ++g_boost_gen;
      pthread_t tid;
      pthread_attr_t attr;

      if (hs_ble_adv_set_interval(HS_BLE_ADV_INT_FAST_MIN,
                                 HS_BLE_ADV_INT_FAST_MAX) == OK)
        {
          if (pthread_attr_init(&attr) == 0)
            {
              pthread_attr_setstacksize(&attr, 1024);
              if (pthread_create(&tid, &attr, hs_ble_boost_thread,
                                 (FAR void *)(intptr_t)gen) == 0)
                {
                  pthread_detach(tid);
                }

              pthread_attr_destroy(&attr);
            }
        }
    }

  return (int)g_event_seq;
}

int hs_ble_data_notify(uint16_t gsr_mv, uint8_t hr_bpm, uint8_t spo2,
                       uint8_t flags)
{
  if (!g_gatt_installed)
    {
      return -ENOTCONN;
    }

  g_data_pkt[0] = (uint8_t)(gsr_mv & 0xff);
  g_data_pkt[1] = (uint8_t)(gsr_mv >> 8);
  g_data_pkt[2] = hr_bpm;
  g_data_pkt[3] = spo2;
  g_data_pkt[4] = flags;
  g_data_pkt[5] = 0;

  /* No-op unless a peer has written the data CCC descriptor */

  bt_gatt_notify(HS_H_DATA_VAL, g_data_pkt, sizeof(g_data_pkt));
  return OK;
}

const uint8_t *hs_ble_data_last(void)
{
  return g_data_pkt;
}

int hs_ble_status_notify(uint16_t gsr_mv, uint8_t hr_bpm, uint8_t spo2,
                         const int16_t accel_mg[3], uint8_t battery,
                         uint8_t buttons, bool vibration, uint8_t flags)
{
  if (!g_gatt_installed)
    {
      return -ENOTCONN;
    }

  g_status_pkt[0] = HS_BLE_STATUS_VERSION;
  g_status_pkt[1] = flags;
  g_status_pkt[2] = (uint8_t)(gsr_mv & 0xff);
  g_status_pkt[3] = (uint8_t)(gsr_mv >> 8);
  g_status_pkt[4] = hr_bpm;
  g_status_pkt[5] = spo2;

  if (accel_mg != NULL)
    {
      uint8_t i;

      for (i = 0; i < 3; i++)
        {
          g_status_pkt[6 + i * 2] = (uint8_t)(accel_mg[i] & 0xff);
          g_status_pkt[7 + i * 2] = (uint8_t)((uint16_t)accel_mg[i] >> 8);
        }
    }
  else
    {
      memset(&g_status_pkt[6], 0, 6);
    }

  g_status_pkt[12] = battery;
  g_status_pkt[13] = buttons;
  g_status_pkt[14] = vibration ? 1 : 0;
  g_status_pkt[15] = 0;

  /* No-op unless a peer has written the status CCC descriptor */

  bt_gatt_notify(HS_H_STATUS_VAL, g_status_pkt, sizeof(g_status_pkt));
  return OK;
}

const uint8_t *hs_ble_status_last(void)
{
  return g_status_pkt;
}

const uint8_t *hs_ble_event_last(void)
{
  return g_event_pkt;
}

uint16_t hs_ble_event_seq(void)
{
  return g_event_seq;
}

size_t hs_ble_control_last(uint8_t *buf, size_t buflen)
{
  size_t len = g_ctrl_len;

  if (len > buflen)
    {
      len = buflen;
    }

  memcpy(buf, g_ctrl, len);
  return len;
}

#endif /* CONFIG_WIRELESS_BLUETOOTH_HOST */
