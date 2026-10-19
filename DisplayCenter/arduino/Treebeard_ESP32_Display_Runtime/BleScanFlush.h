// Delivers the reports a NimBLE scan session still holds when it ends.
//
// Why (2026-10-15): the receiver scans ACTIVELY so it can read MSV4 probes' raw sweeps from their
// scan responses. With NimBLE (arduino-esp32 3.3.11, libraries/BLE/src/BLEScan.cpp) an active scan
// holds a scannable advertiser's report until its scan response arrives, and hands over the ones
// still waiting only at BLE_GAP_EVENT_DISC_COMPLETE. BLEScan::stop() cancels discovery without that
// event, and beginCloudRadioSlot() stops the scan for every cloud upload, after which the scan task
// clears the results. A probe too far away for the request/response exchange (sensor 1, outdoors at
// about -91 dBm) lost more than half its packets the first quarter hour after the switch.
//
// So the scan task hands every stored device to the callbacks before clearing. A device already
// reported is reported again, which the receiver tolerates by design: passive scanning reported
// every advertising event, and onResult() drops a wake it already holds.
//
// BLEScanResults keeps its device map private and its only public accessor, getDevice(i), copies
// i+1 devices to return one. The explicit instantiation below names the private member, which C++
// allows in an explicit instantiation (access is not checked there); it reads the map in place and
// copies each device once. It does not edit the library, and a library that renames the member
// fails to compile here rather than misbehaving.
#pragma once

#include <map>
#include <string>

#include <BLEAdvertisedDevice.h>
#include <BLEScan.h>

namespace ble_scan_flush
{
template <typename Tag, typename Tag::type Member>
struct Access
{
  friend typename Tag::type member(Tag) { return Member; }
};

struct Devices
{
  typedef std::map<std::string, BLEAdvertisedDevice*> BLEScanResults::*type;
  friend type member(Devices);
};

template struct Access<Devices, &BLEScanResults::m_vectorAdvertisedDevices>;

// Call only while the scan is stopped (after BLEScan::start() returns, before clearResults()).
inline size_t deliverStoredResults(BLEScan* scan, BLEAdvertisedDeviceCallbacks* callbacks)
{
  if (scan == nullptr || callbacks == nullptr) return 0;
  BLEScanResults* results = scan->getResults();
  if (results == nullptr) return 0;
  size_t delivered = 0;
  for (auto& entry : results->*member(Devices()))
  {
    if (entry.second == nullptr) continue;
    callbacks->onResult(*entry.second);
    delivered++;
  }
  return delivered;
}
}  // namespace ble_scan_flush
