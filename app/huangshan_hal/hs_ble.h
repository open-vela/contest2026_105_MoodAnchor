/****************************************************************************
 * app/huangshan_hal/hs_ble.h
 *
 * BLE peripheral (GATT server) support for the Huangshan Pi board.
 *
 * The SiFli SF32LB52 BSP exposes the on-chip Bluetooth controller as an H:4
 * HCI transport at /dev/ttyHCI0.  hs_ble_host.c binds the NuttX Bluetooth
 * host stack (nuttx/wireless/bluetooth) to that transport, hs_ble_gatt.c
 * implements the MoodAnchor peripheral on top of it:
 *
 *   - advertising name 是非钟-<xxxx>  (xxxx = last bytes of the BD_ADDR)
 *   - custom 128-bit service d38a0001-1234-5678-9abc-def012345678
 *       event characteristic   (Notify, 11 byte event packet)
 *       control characteristic (Write)
 *   - Device Information Service (name / model / firmware / PnP ID)
 *   - Battery Service
 *
 * The phone side (Android) is a plain GATT central, so there is no private
 * protocol on top of the standard GATT/ATT procedures.
 *
 ****************************************************************************/

#ifndef __APP_HUANGSHAN_HAL_HS_BLE_H
#define __APP_HUANGSHAN_HAL_HS_BLE_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Event packet layout (11 bytes, little endian) as agreed with the Android
 * application:
 *
 *   byte 0 : protocol version
 *   byte 1 : event type
 *   byte 2 : sequence number (low)
 *   byte 3 : sequence number (high)
 *   byte 4 : timestamp (low)  - milliseconds
 *   byte 5 : timestamp
 *   byte 6 : timestamp
 *   byte 7 : timestamp (high)
 *   byte 8 : risk level
 *   byte 9 : confidence
 *   byte 10: flags
 */

#define HS_BLE_EVENT_LEN        11
#define HS_BLE_EVENT_VERSION    0x01

/* Live sensor sample, written to the data characteristic (...0004):
 *
 *   byte 0    : version (0x01)
 *   byte 1    : flags (see HS_BLE_DATA_* below)
 *   byte 2-3  : skin conductance in mV   (uint16, little endian)
 *   byte 4    : microphone level, 0..100
 *   byte 5    : battery percent, 0xff = unknown
 *   byte 6    : heart rate in bpm        (uint8, 0 = invalid)
 *   byte 7    : SpO2 in %                (uint8, 0 = invalid)
 *   byte 8-9  : acceleration X in mg     (int16, little endian)
 *   byte 10-11: acceleration Y in mg     (int16, little endian)
 *   byte 12-13: acceleration Z in mg     (int16, little endian)
 *   byte 14-15: rotation magnitude, tenths of a degree/s (uint16, LE)
 *
 * The microphone level is a relative figure, not a sound pressure level: mic
 * bias, sensitivity and gain all differ between units, so it is only
 * meaningful against its own recent history.
 */

#define HS_BLE_DATA_LEN          16
#define HS_BLE_DATA_VERSION      0x01

#define HS_BLE_DATA_GSR_VALID    0x01
#define HS_BLE_DATA_MIC_VALID    0x02
#define HS_BLE_DATA_BAT_VALID    0x04
#define HS_BLE_DATA_HR_VALID     0x08
#define HS_BLE_DATA_SPO2_VALID   0x10
#define HS_BLE_DATA_IMU_VALID    0x20

/****************************************************************************
 * Fused mood state (characteristic ...0005)
 *
 * This is the headline the phone acts on: whether the wearer currently looks
 * agitated, decided by fusing three independent sensors.  The raw values
 * behind the decision travel separately on the data characteristic.
 *
 *   byte 0    : version (0x01)
 *   byte 1    : state, HS_BLE_MOOD_CALM / HS_BLE_MOOD_AGITATED
 *   byte 2    : fused confidence, 0..100
 *   byte 3    : flags (see HS_BLE_MOOD_* below)
 *   byte 4-7  : timestamp, milliseconds (uint32, little endian)
 *   byte 8    : IMU score, 0..100
 *   byte 9    : microphone score, 0..100
 *   byte 10   : skin conductance score, 0..100
 *   byte 11   : reserved (0)
 *
 * The three scores are reported alongside the verdict so the phone can see
 * *which* sensor drove it, and so a disagreement between the two sides is
 * easy to diagnose.
 */

#define HS_BLE_STATUS_LEN        12
#define HS_BLE_STATUS_VERSION    0x01

#define HS_BLE_MOOD_CALM         0x00
#define HS_BLE_MOOD_AGITATED     0x01

#define HS_BLE_MOOD_GSR_READY    0x01    /* baseline captured, pads on skin */
#define HS_BLE_MOOD_IMU_POSITIVE 0x02
#define HS_BLE_MOOD_MIC_POSITIVE 0x04
#define HS_BLE_MOOD_GSR_POSITIVE 0x08

#define HS_BLE_STATUS_BAT_UNKNOWN 0xff

/****************************************************************************
 * Event types (byte 1)
 ****************************************************************************/

#define HS_BLE_EV_NONE          0x00
#define HS_BLE_EV_FALL          0x01    /* fall detected */
#define HS_BLE_EV_SOS           0x02    /* SOS button */
#define HS_BLE_EV_HR_ABNORMAL   0x03    /* abnormal heart rate */
#define HS_BLE_EV_SPO2_LOW      0x04    /* low blood oxygen */
#define HS_BLE_EV_SLEEP         0x05    /* sleep event */
#define HS_BLE_EV_LOW_BATTERY   0x06
#define HS_BLE_EV_SELFTEST      0xff    /* bench/self test event */

/* Risk levels (byte 8) */

#define HS_BLE_RISK_LOW         0
#define HS_BLE_RISK_MEDIUM      1
#define HS_BLE_RISK_HIGH        2

/* Flags (byte 10) */

#define HS_BLE_FLAG_ACK_REQ     0x01    /* phone shall answer with the control char */
#define HS_BLE_FLAG_SIMULATED   0x80    /* event generated by the CLI for testing */

/* Advertising intervals (milliseconds) */

#define HS_BLE_ADV_INT_IDLE     200     /* normal advertising interval */
#define HS_BLE_ADV_INT_FAST_MIN 100     /* right after an event */
#define HS_BLE_ADV_INT_FAST_MAX 200
#define HS_BLE_ADV_BOOST_SECS   10      /* keep the fast interval for so long */

/* Control characteristic commands (first byte of a control write) */

#define HS_BLE_CTL_ACK          0x01    /* acknowledge an event */
#define HS_BLE_CTL_CLEAR        0x02    /* clear the pending event */
#define HS_BLE_CTL_INTERVAL     0x03    /* followed by one byte: report period s */

/* Size of the control characteristic value buffer */

#define HS_BLE_CTRL_MAX         16

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: hs_ble_host_start
 *
 * Description:
 *   Open /dev/ttyHCI0, register the H:4 transport with the NuttX Bluetooth
 *   host stack and let the stack initialise the controller, register its
 *   default GAP/GATT services and start advertising.
 *
 * Returned Value:
 *   Zero on success, a negated errno value on failure.  Calling this twice
 *   returns OK without touching the controller again.
 *
 ****************************************************************************/

int hs_ble_host_start(void);

/****************************************************************************
 * Name: hs_ble_host_ready
 *
 * Description:
 *   Return true once the host stack has been started successfully.
 *
 ****************************************************************************/

bool hs_ble_host_ready(void);

/****************************************************************************
 * Name: hs_ble_host_hci_raw
 *
 * Description:
 *   Send one raw HCI command through the same transport the host stack uses.
 *   This is used for the few controller level settings the stack does not
 *   expose (advertising data/parameters) and for diagnostics.  The command
 *   must only be issued while the stack has no command in flight.
 *
 * Input Parameters:
 *   opcode - HCI opcode (BT_HCI_OP_*)
 *   params - Command parameters (may be NULL when plen is zero)
 *   plen   - Number of parameter bytes
 *
 * Returned Value:
 *   Zero on success, a negated errno value on failure.
 *
 ****************************************************************************/

int hs_ble_host_hci_raw(uint16_t opcode, const void *params, uint8_t plen);

/****************************************************************************
 * Name: hs_ble_host_bdaddr
 *
 * Description:
 *   Return the controller address captured while the stack was initialised.
 *   Returns NULL if it has not been seen yet.
 *
 ****************************************************************************/

const uint8_t *hs_ble_host_bdaddr(void);

/****************************************************************************
 * Name: hs_ble_host_conn_handle
 *
 * Description:
 *   Return the handle of the active LE connection (0xffff when there is no
 *   connection).  The value is snooped from the LE connection complete
 *   events that pass through the transport.
 *
 ****************************************************************************/

uint16_t hs_ble_host_conn_handle(void);

/****************************************************************************
 * Name: hs_ble_host_conn_interval
 *
 * Description:
 *   Return the connection interval in 1.25 ms units of the active
 *   connection (0 when there is none).
 *
 ****************************************************************************/

uint16_t hs_ble_host_conn_interval(void);

/****************************************************************************
 * Name: hs_ble_gatt_start
 *
 * Description:
 *   Install the MoodAnchor GATT database and start advertising with the
 *   vendor service UUID, the 是非钟-<xxxx> name and the configured
 *   advertising interval.  The host stack must have been started first.
 *
 * Input Parameters:
 *   suffix - Two byte hex suffix used in the advertising name, NULL selects
 *            the suffix derived from the controller address.
 *
 ****************************************************************************/

int hs_ble_gatt_start(const char *suffix);

/****************************************************************************
 * Name: hs_ble_gatt_name
 *
 * Description:
 *   Return the advertising/GAP device name (是非钟-<xxxx>).
 *
 ****************************************************************************/

const char *hs_ble_gatt_name(void);

/****************************************************************************
 * Name: hs_ble_adv_set_interval
 *
 * Description:
 *   Change the advertising interval.  The interval is given in
 *   milliseconds and is clamped to the Bluetooth LE range (20 ms - 10.24 s).
 *
 ****************************************************************************/

int hs_ble_adv_set_interval(uint16_t min_ms, uint16_t max_ms);

/****************************************************************************
 * Name: hs_ble_adv_enable
 *
 * Description:
 *   Turn advertising on or off.  Used by the on-screen BLE switch: the host
 *   stack and the GATT database stay up, only the advertising state changes,
 *   so flipping the switch again is immediate.
 *
 * Returned Value:
 *   Zero on success, a negated errno value when the host is not running.
 *
 ****************************************************************************/

int hs_ble_adv_enable(bool enable);

/****************************************************************************
 * Name: hs_ble_gatt_ready
 *
 * Description:
 *   True once the MoodAnchor GATT database has been installed.
 *
 ****************************************************************************/

bool hs_ble_gatt_ready(void);

/****************************************************************************
 * Name: hs_ble_gatt_peer_connected
 *
 * Description:
 *   True once a peer has subscribed to the event characteristic's CCC.
 *   Used by the UI as the "phone connected" signal.
 *
 ****************************************************************************/

bool hs_ble_gatt_peer_connected(void);

/****************************************************************************
 * Name: hs_ble_event_notify
 *
 * Description:
 *   Build the 11 byte event packet, keep it as the current characteristic
 *   value and notify every subscribed peer.  Advertising is switched to the
 *   fast interval for HS_BLE_ADV_BOOST_SECS seconds.
 *
 * Returned Value:
 *   The sequence number of the packet on success, a negated errno value if
 *   the host or the GATT database is not available.
 *
 ****************************************************************************/

int hs_ble_event_notify(uint8_t type, uint8_t risk, uint8_t confidence,
                        uint8_t flags);

/****************************************************************************
 * Name: hs_ble_event_last
 *
 * Description:
 *   Return a pointer to the last event packet (HS_BLE_EVENT_LEN bytes).
 *
 ****************************************************************************/

const uint8_t *hs_ble_event_last(void);

/****************************************************************************
 * Name: hs_ble_event_seq
 *
 * Description:
 *   Return the sequence number of the last event packet.
 *
 ****************************************************************************/

uint16_t hs_ble_event_seq(void);

/****************************************************************************
 * Name: hs_ble_control_last
 *
 * Description:
 *   Copy the most recent control characteristic write into buf.
 *
 * Returned Value:
 *   Number of bytes copied (0 when nothing was written yet).
 *
 ****************************************************************************/

size_t hs_ble_control_last(uint8_t *buf, size_t buflen);

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* Everything that goes into one data packet.  Grouped into a structure
 * rather than passed as eight positional arguments, because half of them are
 * validity flags and mixing those up silently is the easiest mistake to make
 * on this interface.
 */

struct hs_ble_sample_s
{
  bool     gsr_valid;
  uint16_t gsr_mv;

  bool     mic_valid;
  uint8_t  mic_level;           /* 0..100, relative */

  bool     bat_valid;
  uint8_t  battery;             /* HS_BLE_STATUS_BAT_UNKNOWN when unknown */

  bool     hr_valid;
  uint8_t  hr_bpm;

  bool     spo2_valid;
  uint8_t  spo2;

  bool     imu_valid;
  int16_t  accel_mg[3];
  uint16_t gyro_dps10;          /* rotation magnitude, tenths of a degree/s */
};

/* The fused verdict plus the evidence behind it. */

struct hs_ble_mood_s
{
  bool    agitated;
  uint8_t confidence;           /* 0..100 */
  uint8_t flags;                /* HS_BLE_MOOD_* */
  uint8_t imu_score;            /* 0..100, per-sensor evidence */
  uint8_t mic_score;
  uint8_t gsr_score;
};

/****************************************************************************
 * Name: hs_ble_data_notify
 *
 * Description:
 *   Publish one live sensor sample on the data characteristic (Notify).
 *   Unlike the 11 byte event packet this is a continuous stream, so the
 *   phone only needs to subscribe once.  Nothing happens when no peer has
 *   subscribed yet.
 *
 * Input Parameters:
 *   sample - the values to send; the flags are derived from it, and any
 *            field whose validity flag is false is sent as zero
 *
 * Returned Value:
 *   Zero on success, a negated errno value when the host or the GATT
 *   database is not ready.
 *
 ****************************************************************************/

int hs_ble_data_notify(const struct hs_ble_sample_s *sample);

/****************************************************************************
 * Name: hs_ble_data_last
 *
 * Description:
 *   Return a pointer to the last sample packet (HS_BLE_DATA_LEN bytes).
 *
 ****************************************************************************/

const uint8_t *hs_ble_data_last(void);

/****************************************************************************
 * Name: hs_ble_status_notify
 *
 * Description:
 *   Publish the fused mood packet (HS_BLE_STATUS_LEN bytes) on the status
 *   characteristic (Notify).  Nothing happens when no peer subscribed yet.
 *
 * Input Parameters:
 *   mood - the verdict and the per-sensor scores behind it
 *
 * Returned Value:
 *   Zero on success, a negated errno value otherwise.
 *
 ****************************************************************************/

int hs_ble_status_notify(const struct hs_ble_mood_s *mood);

/****************************************************************************
 * Name: hs_ble_status_last
 *
 * Description:
 *   Return a pointer to the last status packet (HS_BLE_STATUS_LEN bytes).
 *
 ****************************************************************************/

const uint8_t *hs_ble_status_last(void);

/****************************************************************************
 * Name: hs_ble_adv_service
 *
 * Description:
 *   Housekeeping for the advertising state machine.  The controller stops
 *   advertising when a phone connects and does not resume it after the link
 *   is dropped, so this must be polled from a normal thread to re-arm the
 *   advertiser and keep the device discoverable.
 *
 ****************************************************************************/

void hs_ble_adv_service(void);

#endif /* __APP_HUANGSHAN_HAL_HS_BLE_H */
