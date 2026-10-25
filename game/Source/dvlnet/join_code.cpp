#include "dvlnet/join_code.hpp"

#include <array>
#include <cstdio>
#include <random>

namespace devilution {

namespace {

/** 3-bit check over the four address bytes, so a mistyped code is refused instead of dialled. */
uint32_t AddressCheck(uint32_t ip)
{
	uint32_t sum = 0;
	for (int i = 0; i < 4; ++i)
		sum += (ip >> (8 * i)) & 0xFF;
	return (sum + (sum >> 3)) & 7;
}

} // namespace

std::string NormalizeJoinCode(std::string_view text)
{
	std::string out;
	for (char c : text) {
		if (c >= 'a' && c <= 'z')
			c = static_cast<char>(c - 'a' + 'A');
		if (c == 'O' || c == 'Q')
			c = '0';
		else if (c == 'I' || c == 'L')
			c = '1';
		if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
			out += c;
	}
	return out;
}

uint64_t HostNodeFromCode(std::string_view code)
{
	if (code.size() != JoinCodeLength)
		return 0;
	uint64_t node = 0;
	for (size_t i = 0; i < OnlineHostPartLength; ++i) {
		const size_t value = JoinCodeAlphabet.find(code[i]);
		if (value == std::string_view::npos)
			return 0;
		node = (node << 5) | value;
	}
	return node;
}

std::string RandomCodeChars(size_t count)
{
	std::random_device rd;
	std::uniform_int_distribution<size_t> dist(0, JoinCodeAlphabet.size() - 1);
	std::string out;
	while (out.size() < count)
		out += JoinCodeAlphabet[dist(rd)];
	return out;
}

namespace {

std::string EncodeBits(uint64_t bits, size_t chars)
{
	std::string code;
	for (size_t i = 0; i < chars; ++i)
		code += JoinCodeAlphabet[(bits >> (5 * (chars - 1 - i))) & 31];
	return code;
}

std::optional<uint64_t> DecodeBits(std::string_view code)
{
	uint64_t bits = 0;
	for (char c : code) {
		const size_t value = JoinCodeAlphabet.find(c);
		if (value == std::string_view::npos)
			return std::nullopt;
		bits = (bits << 5) | value;
	}
	return bits;
}

std::string Dotted(uint32_t ip)
{
	return std::to_string(ip >> 24) + "." + std::to_string((ip >> 16) & 255) + "." + std::to_string((ip >> 8) & 255) + "." + std::to_string(ip & 255);
}

} // namespace

std::string MakeWifiCode(std::string_view ipv4)
{
	std::array<unsigned, 4> b {};
	char extra = 0;
	if (std::sscanf(std::string(ipv4).c_str(), "%u.%u.%u.%u%c", &b[0], &b[1], &b[2], &b[3], &extra) != 4)
		return {};
	for (unsigned v : b)
		if (v > 255)
			return {};
	const uint32_t ip = (b[0] << 24) | (b[1] << 16) | (b[2] << 8) | b[3];
	const uint32_t check = AddressCheck(ip);
	// 6 characters = 30 bits: 2-bit range tag, the host part of the address, the check in what is left.
	if (b[0] == 192 && b[1] == 168)
		return EncodeBits((0ull << 28) | (static_cast<uint64_t>(ip & 0xFFFF) << 12) | (check << 9) | (check << 6) | (check << 3) | check, WifiCodeLength);
	if (b[0] == 10)
		return EncodeBits((1ull << 28) | (static_cast<uint64_t>(ip & 0xFFFFFF) << 4) | (check << 1) | (check >> 2), WifiCodeLength);
	if (b[0] == 172 && b[1] >= 16 && b[1] <= 31)
		return EncodeBits((2ull << 28) | (static_cast<uint64_t>(ip & 0xFFFFF) << 8) | (check << 5) | (check << 2) | (check >> 1), WifiCodeLength);
	// Anything else: 7 characters, 3-bit check then the whole address.
	return EncodeBits((static_cast<uint64_t>(check) << 32) | ip, WifiCodeFullLength);
}

std::optional<std::string> ParseWifiCode(std::string_view code)
{
	if (code.size() == WifiCodeFullLength) {
		const std::optional<uint64_t> bits = DecodeBits(code);
		if (!bits)
			return std::nullopt;
		const uint32_t ip = static_cast<uint32_t>(*bits & 0xFFFFFFFFu);
		if (((*bits >> 32) & 7) != AddressCheck(ip))
			return std::nullopt;
		return Dotted(ip);
	}
	if (code.size() != WifiCodeLength)
		return std::nullopt;
	const std::optional<uint64_t> bits = DecodeBits(code);
	if (!bits)
		return std::nullopt;
	const uint64_t v = *bits;
	uint32_t ip;
	switch ((v >> 28) & 3) {
	case 0:
		ip = (192u << 24) | (168u << 16) | static_cast<uint32_t>((v >> 12) & 0xFFFF);
		if ((v & 0xFFF) != ((AddressCheck(ip) << 9) | (AddressCheck(ip) << 6) | (AddressCheck(ip) << 3) | AddressCheck(ip)))
			return std::nullopt;
		return Dotted(ip);
	case 1:
		ip = (10u << 24) | static_cast<uint32_t>((v >> 4) & 0xFFFFFF);
		if ((v & 0xF) != ((AddressCheck(ip) << 1) | (AddressCheck(ip) >> 2)))
			return std::nullopt;
		return Dotted(ip);
	case 2:
		ip = (172u << 24) | (16u << 16) | static_cast<uint32_t>((v >> 8) & 0xFFFFF);
		if ((v & 0xFF) != ((AddressCheck(ip) << 5) | (AddressCheck(ip) << 2) | (AddressCheck(ip) >> 1)))
			return std::nullopt;
		return Dotted(ip);
	default:
		return std::nullopt;
	}
}

} // namespace devilution
