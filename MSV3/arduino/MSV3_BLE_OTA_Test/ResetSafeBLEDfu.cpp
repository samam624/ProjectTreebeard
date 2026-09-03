// Service layout and peer-data handoff are derived from Adafruit's BLEDfu,
// Copyright (c) 2018 Adafruit Industries, BSD-3-Clause licensed. The handoff
// intentionally uses the bootloader's reset-mode magic instead of a soft jump.

#include "ResetSafeBLEDfu.h"
#include <nrf_nvic.h>

#define DFU_REV_APPMODE 0x0001

namespace
{
volatile bool gDfuAllowed = false;

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

void resetSafeDfuWrite(uint16_t connHandle,
                       BLECharacteristic* characteristic,
                       ble_gatts_evt_write_t* request)
{
  if (request->handle != characteristic->handles().value_handle ||
      request->op == BLE_GATTS_OP_PREP_WRITE_REQ ||
      request->op == BLE_GATTS_OP_EXEC_WRITE_REQ_NOW ||
      request->op == BLE_GATTS_OP_EXEC_WRITE_REQ_CANCEL)
  {
    return;
  }

  ble_gatts_rw_authorize_reply_params_t reply = {
    .type = BLE_GATTS_AUTHORIZE_TYPE_WRITE
  };

  if (!characteristic->notifyEnabled(connHandle))
  {
    reply.params.write.gatt_status = BLE_GATT_STATUS_ATTERR_CPS_CCCD_CONFIG_ERROR;
    sd_ble_gatts_rw_authorize_reply(connHandle, &reply);
    return;
  }

  reply.params.write.gatt_status = BLE_GATT_STATUS_SUCCESS;
  if (sd_ble_gatts_rw_authorize_reply(connHandle, &reply) != NRF_SUCCESS)
  {
    return;
  }

  // The application refreshes this immediately before each advertising window from a fresh
  // VBAT/charger-PG measurement. Do not enter a multi-minute flash operation from a marginal
  // battery. Recovery mode itself deliberately has no such gate: after an interrupted transfer,
  // the board must become reachable as soon as harvested/USB power is restored.
  if (request->len > 0 && request->data[0] == 1 && !gDfuAllowed)
  {
    reply.params.write.gatt_status = BLE_GATT_STATUS_ATTERR_WRITE_NOT_PERMITTED;
    sd_ble_gatts_rw_authorize_reply(connHandle, &reply);
    return;
  }

  if (request->len == 0 || request->data[0] != 1)
  {
    return;
  }

  BLEConnection* connection = Bluefruit.Connection(connHandle);
  if (connection == nullptr)
  {
    return;
  }

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
  peer->crc = crc16(reinterpret_cast<uint8_t*>(peer), offsetof(PeerData, crc));

  // Never reset unless the bootloader request was actually stored and read back. The former
  // implementation ignored both SVC return values, so a rare SoftDevice failure could perform a
  // normal application reset after the client had been told that DFU entry succeeded.
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
  connection->disconnect();

  // Success does not return. Clear the retained request if an unexpected NVIC error does return,
  // otherwise a later unrelated watchdog/reset-pin event could enter OTA without a client.
  if (sd_nvic_SystemReset() != NRF_SUCCESS) sd_power_gpregret_clr(0, 0xFF);
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

err_t ResetSafeBLEDfu::begin()
{
  VERIFY_STATUS(BLEService::begin());

  BLECharacteristic packet(kDfuPacketUuid);
  packet.setTempMemory();
  packet.setProperties(CHR_PROPS_WRITE_WO_RESP);
  packet.setMaxLen(20);
  VERIFY_STATUS(packet.begin());

  control_.setProperties(CHR_PROPS_WRITE | CHR_PROPS_NOTIFY);
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
