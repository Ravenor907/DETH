/**
 * @file join_code.hpp
 *
 * DETH join codes: 10 Crockford base32 characters that friends share instead of addresses.
 *
 * Online (ZeroTier): 8 characters carry the host's node ID, 2 are random.
 * Same Wi-Fi (TCP): 6 characters carry the host's home-network address (192.168.x.x, 10.x.x.x
 * or 172.16-31.x.x) with a check; the code is also the game password. Any other address takes
 * the 7-character full form. Either way, nobody types or sees an address.
 */
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace devilution {

// No I, L, O or Q: in the game font they pass for 1 or 0, so a typed O, Q, I or L is read as 0 or 1.
constexpr std::string_view JoinCodeAlphabet = "0123456789ABCDEFGHJKMNPRSTUVWXYZ";
constexpr size_t JoinCodeLength = 10;
/** Online codes: characters that carry the host's ZeroTier node ID. */
constexpr size_t OnlineHostPartLength = 8;
/** Wi-Fi codes: 6 characters for a home-network address, 7 for any other IPv4 address. */
constexpr size_t WifiCodeLength = 6;
constexpr size_t WifiCodeFullLength = 7;

/** Uppercases, maps O to 0 and I/L to 1, drops anything that is not a letter or digit. */
std::string NormalizeJoinCode(std::string_view text);

/** The host's ZeroTier node ID from an online code, or 0. */
uint64_t HostNodeFromCode(std::string_view code);

/** `count` random code characters. */
std::string RandomCodeChars(size_t count);

/** The Wi-Fi code for a dotted IPv4 address, or empty if it is not one. */
std::string MakeWifiCode(std::string_view ipv4);

/** The address behind a normalized Wi-Fi code; nullopt if it is not one (wrong length, bad character, failed check). */
std::optional<std::string> ParseWifiCode(std::string_view code);

} // namespace devilution
