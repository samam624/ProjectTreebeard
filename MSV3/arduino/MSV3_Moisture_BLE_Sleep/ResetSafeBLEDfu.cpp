// Service layout and peer-data handoff are derived from Adafruit's BLEDfu,
// Copyright (c) 2018 Adafruit Industries, BSD-3-Clause licensed. The handoff
// intentionally uses the bootloader's reset-mode magic instead of a soft jump.

#include "ResetSafeBLEDfu.h"
#include <nrf_nvic.h>

#define DFU_REV_APPMODE 0x0001

namespace
{
volatile bool gDfuAllowed = false;

// Set by the authorize callback, consumed by the application loop. See completeDfuReset().
volatile bool gDfuPending = false;
volatile uint16_t gDfuPendingConn = BLE_CONN_HANDLE_INVALID;

// Checkpoints, highest reached wins. Recorded rather than printed: Serial work inside a BLE event
// callback delays the ATT response and can change the very timing being measured.
enum : uint8_t
{
  kDfuCpEntered = 1,
  kDfuCpHandleOk = 2,
  kDfuCpOpOk = 3,
  kDfuCpNotifyOk = 4,
  kDfuCpOpcodeOk = 5,
  kDfuCpGateOk = 6,
  kDfuCpConnOk = 7,
  kDfuCpReplied = 8,
  kDfuCpPendingArmed = 9,
};

ResetSafeDfuDiag gDfuDiag = {};

const uint8_t kDfuServiceUuid[16] = {
  0x23, 0xD1, 0xBC, 0xEA, 0x5F, 0x78, 0x23, 0x15,
  0xDE, 0xEF, 0x12, 0x12, 0x30, 0x15, 0x00, 0x00
};

const uint8_t kDfuControlUuid[16] = {
  0x23, 0xD1, 0xBC, 0xEA, 0x5F, 0x78, 0x23, 0x15,
  0xDE, 0xEF, 0x12, 0x12, 0x31, 0x15, 0x00, 0x00
};

const uint8_t kDfuPacketUuid[16] = {
  0x23, 0xD1, 0xBC, 0xEA, 0x5F, 0x78, 0x23, 0x15,
  0xDE, 0xEF, 0x12, 0x12, 0x32, 0x15, 0x00, 0x00
};

const uint8_t kDfuRevisionUuid[16] = {
  0x23, 0xD1, 0xBC, 0xEA, 0x5F, 0x78, 0x23, 0x15,
  0xDE, 0xEF, 0x12, 0x12, 0x34, 0x15, 0x00, 0x00
};

uint16_t crc16(const uint8_t* data, uint8_t length)
{
  uint16_t crc = 0xFFFF;
  while (length--)
  {
    uint8_t x = (crc >> 8) ^ *data++;
    x ^= x >> 4;
    crc = (crc << 8) ^ (uint16_t)(x << 12) ^ (uint16_t)(x << 5) ^ x;
  }
  return crc;
}

// Single funnel for answering an authorize request, so "exactly one reply per path" is a property
// of the code shape rather than something that has to be re-checked by eye after every edit. An
// unanswered BLE_GATTS_EVT_RW_AUTHORIZE_REQUEST leaves the peer's write pending until something
// else tears the link down -- see MSV3_OTA_BOOTLOADER_HANDOFF_2026-09-03.md.
void dfuReplyStatus(uint16_t connHandle,
                    ble_gatts_rw_authorize_reply_params_t& reply,
                    uint16_t status,
                    const ble_gatts_evt_write_t* request)
{
  reply.params.write.gatt_status = status;

  // 2026-09-03, root cause of the buttonless jump never working: a WRITE authorize reply carrying
  // BLE_GATT_STATUS_SUCCESS must also set update=1 and hand the value back to the SoftDevice.
  // Replying SUCCESS with update=0 and p_data=NULL is rejected with NRF_ERROR_INVALID_PARAM (7),
  // and a rejected reply is never transmitted -- the peer's write simply hangs until something
  // else drops the link. Measured on the bench: checkpoint=8, gattStatus=0x0, replyRc=7.
  // Error statuses need none of this, which is why every rejection path worked and only the one
  // path that mattered was silently broken.
  if (status == BLE_GATT_STATUS_SUCCESS && request != nullptr)
  {
    reply.params.write.update = 1;
    reply.params.write.offset = request->offset;
    reply.params.write.len = request->len;
    reply.params.write.p_data = request->data;
  }

  gDfuDiag.gattStatus = status;
  gDfuDiag.replyRc = sd_ble_gatts_rw_authorize_reply(connHandle, &reply);
}

void resetSafeDfuWrite(uint16_t connHandle,
                       BLECharacteristic* characteristic,
                       ble_gatts_evt_write_t* request)
{
  // Fully zero-initialised, then fields assigned. params.write carries update/offset/len/p_data
  // that must all read zero when the value is not being rewritten.
  ble_gatts_rw_authorize_reply_params_t reply = {};
  reply.type = BLE_GATTS_AUTHORIZE_TYPE_WRITE;

  gDfuDiag.valid = true;
  gDfuDiag.checkpoint = kDfuCpEntered;
  gDfuDiag.connHandle = connHandle;
  gDfuDiag.op = request->op;
  gDfuDiag.gattStatus = 0;
  gDfuDiag.replyRc = 0xFFFFFFFFUL;

  // 2026-09-03: every path out of this handler now answers the authorize request exactly once.
  // The previous version returned without replying whenever the request was not a plain write to
  // our value handle, and an unanswered BLE_GATTS_EVT_RW_AUTHORIZE_REQUEST leaves the peer's write
  // pending until something else tears the link down. On Windows that presented as a 28-second
  // stall ended by this application's own OTA_CONNECTED_HOLD_MS idle-peer disconnect -- from the
  // client it was indistinguishable from a dropped connection, and no OTA could ever start.
  // See MSV3_OTA_BOOTLOADER_HANDOFF_2026-09-03.md.
  if (request->handle != characteristic->handles().value_handle)
  {
    dfuReplyStatus(connHandle, reply, BLE_GATT_STATUS_ATTERR_INVALID_HANDLE, request);
    return;
  }
  gDfuDiag.checkpoint = kDfuCpHandleOk;

  // Defensive only. Bluefruit's BLECharacteristic handles queued/long writes internally and
  // invokes this callback for a normal write request, so these ops are not expected here -- bench
  // instrumentation on 2026-09-03 recorded op=1 (BLE_GATTS_OP_WRITE_REQ) for the Windows client
  // that was failing. An earlier revision of this comment claimed Windows was forced onto the
  // long-write path by setMaxLen(23) exceeding the single-PDU ceiling; that was wrong, and
  // setMaxLen is back at the stock Adafruit DFU value. Keep the branch anyway: replying beats
  // returning silently if one ever does arrive.
  if (request->op == BLE_GATTS_OP_PREP_WRITE_REQ ||
      request->op == BLE_GATTS_OP_EXEC_WRITE_REQ_NOW ||
      request->op == BLE_GATTS_OP_EXEC_WRITE_REQ_CANCEL)
  {
    dfuReplyStatus(connHandle, reply, BLE_GATT_STATUS_ATTERR_REQUEST_NOT_SUPPORTED, request);
    return;
  }
  gDfuDiag.checkpoint = kDfuCpOpOk;

  if (!characteristic->notifyEnabled(connHandle))
  {
    dfuReplyStatus(connHandle, reply, BLE_GATT_STATUS_ATTERR_CPS_CCCD_CONFIG_ERROR, request);
    return;
  }
  gDfuDiag.checkpoint = kDfuCpNotifyOk;

  // Anything that is not the enter-bootloader opcode is a legitimate write with nothing to do.
  if (request->len == 0 || request->data[0] != 1)
  {
    dfuReplyStatus(connHandle, reply, BLE_GATT_STATUS_SUCCESS, request);
    return;
  }
  gDfuDiag.checkpoint = kDfuCpOpcodeOk;

  // The application refreshes this immediately before each advertising window from a fresh
  // VBAT/charger-PG measurement. Do not enter a multi-minute flash operation from a marginal
  // battery. Recovery mode itself deliberately has no such gate: after an interrupted transfer,
  // the board must become reachable as soon as harvested/USB power is restored.
  //
  // 2026-09-03: this test used to run AFTER a BLE_GATT_STATUS_SUCCESS reply had already been sent,
  // so its rejection was a second reply to an already-answered request -- refused by the
  // SoftDevice with NRF_ERROR_INVALID_STATE and never delivered. A gated-out request looked to the
  // client like a successful write followed by a bootloader that never appeared. Decide, then reply.
  if (!gDfuAllowed)
  {
    dfuReplyStatus(connHandle, reply, BLE_GATT_STATUS_ATTERR_WRITE_NOT_PERMITTED, request);
    return;
  }
  gDfuDiag.checkpoint = kDfuCpGateOk;

  if (Bluefruit.Connection(connHandle) == nullptr)
  {
    dfuReplyStatus(connHandle, reply, BLE_GATT_STATUS_ATTERR_UNLIKELY_ERROR, request);
    return;
  }
  gDfuDiag.checkpoint = kDfuCpConnOk;

  // Authorize, record the request, and RETURN. The reset used to happen here, inline. A
  // successful sd_ble_gatts_rw_authorize_reply() only means the SoftDevice accepted the ATT
  // response for transmission -- resetting immediately afterwards can tear the radio down before
  // that response is actually on air, which leaves the client waiting on a write that is never
  // answered and never seeing the bootloader appear. Performing a system reset from inside a BLE
  // event handler is unsound regardless. completeDfuReset(), below, is driven from the
  // application loop once the response has had time to go out.
  dfuReplyStatus(connHandle, reply, BLE_GATT_STATUS_SUCCESS, request);
  gDfuDiag.checkpoint = kDfuCpReplied;
  if (gDfuDiag.replyRc != NRF_SUCCESS)
  {
    return;
  }

  gDfuPendingConn = connHandle;
  gDfuPending = true;
  gDfuDiag.checkpoint = kDfuCpPendingArmed;
}
}  // namespace

ResetSafeBLEDfu::ResetSafeBLEDfu()
  : BLEService(kDfuServiceUuid), control_(kDfuControlUuid)
{
}

void ResetSafeBLEDfu::setDfuAllowed(bool allowed)
{
  gDfuAllowed = allowed;
}

bool ResetSafeBLEDfu::dfuResetPending() const
{
  return gDfuPending;
}

bool ResetSafeBLEDfu::takeDiag(ResetSafeDfuDiag& out)
{
  if (!gDfuDiag.valid) return false;
  out.checkpoint = gDfuDiag.checkpoint;
  out.connHandle = gDfuDiag.connHandle;
  out.gattStatus = gDfuDiag.gattStatus;
  out.replyRc = gDfuDiag.replyRc;
  out.op = gDfuDiag.op;
  out.valid = true;
  gDfuDiag.valid = false;
  return true;
}

// Runs from the application loop, not the BLE event callback. By the time this is reached the
// authorize response has had time to reach the peer; performing the GPREGRET arming and system
// reset here means the client sees its write complete before the link drops, so a successful jump
// is distinguishable from a lost connection.
void ResetSafeBLEDfu::completeDfuReset()
{
  if (!gDfuPending) return;
  gDfuPending = false;

  const uint16_t connHandle = gDfuPendingConn;
  BLEConnection* connection = Bluefruit.Connection(connHandle);

  typedef struct
  {
    ble_gap_addr_t addr;
    ble_gap_irk_t irk;
    ble_gap_enc_key_t encKey;
    uint8_t sysAttr[8];
    uint16_t crc;
  } PeerData;

  VERIFY_STATIC(offsetof(PeerData, crc) == 60);
  PeerData* peer = reinterpret_cast<PeerData*>(0x20007F80UL);
  varclr(peer);

  uint16_t sysAttrLength = sizeof(peer->sysAttr);
  sd_ble_gatts_sys_attr_get(connHandle, peer->sysAttr, &sysAttrLength,
                            BLE_GATTS_SYS_ATTR_FLAG_SYS_SRVCS);

  if (connection != nullptr)
  {
    peer->addr = connection->getPeerAddr();
    if (connection->secured())
    {
      bond_keys_t keys;
      if (connection->loadBondKey(&keys))
      {
        peer->addr = keys.peer_id.id_addr_info;
        peer->irk = keys.peer_id.id_info;
        peer->encKey = keys.own_enc;
      }
    }
  }
  peer->crc = crc16(reinterpret_cast<uint8_t*>(peer), offsetof(PeerData, crc));

  // Never reset unless the bootloader request was actually stored and read back. Ignoring these
  // SVC return values would let a rare SoftDevice failure perform a normal application reset after
  // the client had already been told that DFU entry succeeded.
  constexpr uint8_t kDfuOtaResetMagic = 0xA8;
  if (sd_power_gpregret_clr(0, 0xFF) != NRF_SUCCESS ||
      sd_power_gpregret_set(0, kDfuOtaResetMagic) != NRF_SUCCESS)
  {
    sd_power_gpregret_clr(0, 0xFF);
    return;
  }

  uint32_t gpregret = 0;
  if (sd_power_gpregret_get(0, &gpregret) != NRF_SUCCESS ||
      (gpregret & 0xFF) != kDfuOtaResetMagic)
  {
    sd_power_gpregret_clr(0, 0xFF);
    return;
  }

  Bluefruit.Advertising.restartOnDisconnect(false);
  if (connection != nullptr) connection->disconnect();

  // Success does not return. Clear the retained request if an unexpected NVIC error does return,
  // otherwise a later unrelated watchdog/reset-pin event could enter OTA without a client.
  if (sd_nvic_SystemReset() != NRF_SUCCESS) sd_power_gpregret_clr(0, 0xFF);
}

err_t ResetSafeBLEDfu::begin()
{
  VERIFY_STATUS(BLEService::begin());

  BLECharacteristic packet(kDfuPacketUuid);
  packet.setTempMemory();
  packet.setProperties(CHR_PROPS_WRITE_WO_RESP);
  packet.setMaxLen(20);
  VERIFY_STATUS(packet.begin());

  control_.setProperties(CHR_PROPS_WRITE | CHR_PROPS_NOTIFY);
  // 23 is the stock Adafruit DFU value. Briefly changed to 20 on 2026-09-03 on the theory that a
  // declared max length at the MTU ceiling pushed Windows onto the queued-write path; that theory
  // was disproven -- Bluefruit handles queued writes internally, and bench instrumentation showed
  // the failing client sending an ordinary op=1 write request. Reverted rather than left in place
  // on a rationale that turned out to be false.
  control_.setMaxLen(23);
  control_.setWriteAuthorizeCallback(resetSafeDfuWrite);
  VERIFY_STATUS(control_.begin());

  BLECharacteristic revision(kDfuRevisionUuid);
  revision.setTempMemory();
  revision.setProperties(CHR_PROPS_READ);
  revision.setFixedLen(2);
  VERIFY_STATUS(revision.begin());
  revision.write16(DFU_REV_APPMODE);

  return ERROR_NONE;
}
