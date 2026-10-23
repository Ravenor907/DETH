#pragma once

#include <string>

namespace devilution::net {

/**
 * @brief DETH: this device's address on the local network (what others type to join a TCP game).
 * Empty if it cannot be determined. No packets are sent.
 */
std::string GetLocalNetworkAddress();

} // namespace devilution::net
