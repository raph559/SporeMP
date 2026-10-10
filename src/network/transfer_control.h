#pragma once
#include "coordinator.h"

namespace sporemp::network {
// Pump on the coordinator thread, using its private administrator output
// directory. Claims transfer.request and publishes transfer-receipt.json.
// Schema 1 supports reservation/cancellation; schema 2 exposes correlated
// native phases and read-only status. Returns true when work was present,
// including a rejected or unacknowledged request. Paths never enter the wire.
bool process_transfer_request(Coordinator&, const std::filesystem::path& private_directory, std::string& error);
bool process_global_checkpoint_request(Coordinator&, const std::filesystem::path& private_directory,
                                       std::string& error);
} // namespace sporemp::network
