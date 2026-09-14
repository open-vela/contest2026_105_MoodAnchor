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

/* Live sensor sample plus the fused verdict, written to the data
 * characteristic (...0004):
 *
 *   byte 0-1  : skin conductance in mV   (uint16, little endian)
 *   byte 2    : heart rate in bpm        (uint8, 0 = invalid)
 *   byte 3    : SpO2 in %                (uint8, 0 = invalid)
 *   byte 4    : flags (see HS_BLE_DATA_* below)
 *   byte 5    : microphone level, 0..100
 *   byte 6    : battery percent, 0xff = unknown
 *   byte 7-8  : acceleration X in mg     (int16, little endian)
 *   byte 9-10 : acceleration Y in mg     (int16, little endian)
 *   byte 11-12: acceleration Z in mg     (int16, little endian)
 *   byte 13-14: rotation magnitude, tenths of a degree/s (uint16, LE)
 *   byte 15   : fused mood, bit 7 = agitated, bits 0..6 = confidence 0..100
 *
 * The order of the first five bytes is not ours to choose.  The Android
 * receiver reads this characteristic as
 *
 *     gsr = le16(value, 0); hr = value[2]; spo2 = value[3];
 *     flags = value[4];
 *
 * so those offsets are the contract.  Everything the receiver does not look
 * at - microphone, IMU, the verdict - goes after byte 4, where an
 * implementation that stops reading at the flags byte is unaffected.
 *
 * There is deliberately no version byte here: the receiver does not expect
 * one and reads value[0] as the low half of the GSR sample.  Adding fields
 * means adding them at the end.
 *
 * The microphone level is a relative figure, not a sound pressure level: mic
 * bias, sensitivity and gain all differ between units, so it is only
 * meaningful against its own recent history.
 */

#define HS_BLE_DATA_LEN          16

#define HS_BLE_DATA_GSR_VALID    0x01
#define HS_BLE_DATA_HR_VALID     0x02
#define HS_BLE_DATA_SPO2_VALID   0x04
#define HS_BLE_DATA_MIC_VALID    0x08
#define HS_BLE_DATA_BAT_VALID    0x10
#define HS_BLE_DATA_IMU_VALID    0x20
#define HS_BLE_DATA_GSR_READY    0x40    /* baseline held, electrodes on skin */

/* Byte 15: the fusion verdict.  The confidence occupies the low bits so that
 * a client reading only bit 7 gets the answer and a client reading the whole
 * byte gets the answer and how sure the watch was.
 */

#define HS_BLE_DATA_AGITATED     0x80
#define HS_BLE_DATA_CONF_MASK    0x7f

/****************************************************************************
 * Device status (characteristic ...0005)
 *
 * Same fields as the data packet's first six bytes, minus the ones the data
 * packet grew afterwards.  The receiver reads it as
 *
 *     version = value[0]; flags = value[1]; gsr = le16(value, 2);
 *     hr = value[4]; spo2 = value[5]; battery = value[12];
 *
 * with the battery conditioned on flags & 0x10, so those offsets are fixed
 * too.  Bytes 6..11 and 13..15 are reserved.
 *
 * It is only sent when the battery actually changes - the data packet already
 * carries the live values, so a second identical stream every second would be
 * pure overhead.
 *
 *   byte 0    : version (0x01)
 *   byte 1    : flags (see HS_BLE_STATUS_* below)
 *   byte 2-3  : skin conductance in mV   (uint16, little endian)
 *   byte 4    : heart rate in bpm
 *   byte 5    : SpO2 in %
 *   byte 6-11 : reserved (0)
 *   byte 12   : battery percent, 0xff = unknown
 *   byte 13-15: reserved (0)
 */

#define HS_BLE_STATUS_LEN        16
#define HS_BLE_STATUS_VERSION    0x01

#define HS_BLE_STATUS_GSR_VALID  0x01
#define HS_BLE_STATUS_HR_VALID   0x02
#define HS_BLE_STATUS_SPO2_VALID 0x04
#define HS_BLE_STATUS_BAT_VALID  0x10

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

  /* The fusion verdict, sent in the same packet. */

  bool     gsr_ready;
  bool     agitated;
  uint8_t  confidence;          /* 0..100 */
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
 * Name: hs_ble_stage
 *
 * Description:
 *   Record how far the bring-up has got, in words, for the LINK page.
 *
 *   The numeric trace is not enough on its own: the whole point is to find
 *   out where a failure *inside* the host stack bring-up happens, and that
 *   path reports only an errno.  Text survives a freeze - the panel keeps
 *   showing the last frame - so whatever the label says when the picture
 *   stops is the answer.
 *
 *   Takes a printf format so callers can put the errno in the message.
 *
 ****************************************************************************/

void hs_ble_stage(const char *fmt, ...);

/****************************************************************************
 * Name: hs_ble_stage_last
 *
 * Description:
 *   Return the text set by the most recent hs_ble_stage() call.
 *
 ****************************************************************************/

const char *hs_ble_stage_last(void);

/****************************************************************************
 * Name: hs_ble_status_notify
 *
 * Description:
 *   Publish the device status packet (HS_BLE_STATUS_LEN bytes) on the status
 *   characteristic.  It carries the battery level, which the data packet has
 *   no room for in the layout the receiver expects.
 *
 *   Only sends when something in the packet actually differs from the last
 *   one published, and always once after a new peer subscribes.  The live
 *   values already travel on the data characteristic, so repeating them here
 *   every second would be pure overhead.
 *
 * Input Parameters:
 *   sample - same structure the data packet is built from
 *
 ****************************************************************************/

int hs_ble_status_notify(const struct hs_ble_sample_s *sample);

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

/****************************************************************************
 * Name: hs_ble_trace / hs_ble_trace_last
 *
 * Description:
 *   TEMPORARY BRING-UP DIAGNOSTIC.
 *
 *   The Bluetooth link freezes the whole system the moment a central
 *   connects, and the serial console in this environment drops out far too
 *   often to be trusted with catching the crash dump.  The screen, however,
 *   keeps showing the last frame it managed to draw - so the trace code is
 *   rendered there and the frozen frame becomes the evidence.
 *
 *   Each step writes its code *before* the risky call, so the value on
 *   screen during a hang is the step that was running, not the last one that
 *   completed.
 *
 *   Remove both functions once the freeze is fixed.
 *
 ****************************************************************************/

#define HS_BLE_TRACE_OFF        0
#define HS_BLE_TRACE_SWITCH     1    /* switch tapped, bringing the host up */
#define HS_BLE_TRACE_HOST_UP    2    /* host stack running */
#define HS_BLE_TRACE_GATT       3    /* GATT database installed */
#define HS_BLE_TRACE_ADV        4    /* advertising enabled */
#define HS_BLE_TRACE_CCC_SUB    5    /* peer wrote the CCC descriptor */
#define HS_BLE_TRACE_CCC_UNSUB  6
#define HS_BLE_TRACE_SEND_DATA  7    /* about to notify the data characteristic */
#define HS_BLE_TRACE_SEND_STAT  8    /* about to notify the status one */
#define HS_BLE_TRACE_SENT       9    /* the burst went out, loop complete */

void hs_ble_trace(int code);
int  hs_ble_trace_last(void);

#endif /* __APP_HUANGSHAN_HAL_HS_BLE_H */
