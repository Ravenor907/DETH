#include <gtest/gtest.h>

#include "dvlnet/join_code.hpp"

namespace devilution {
namespace {

TEST(JoinCode, WifiCodesRoundTripHomeAddresses)
{
	for (const char *address : { "192.168.0.1", "192.168.1.137", "192.168.255.255", "10.0.0.5", "10.31.200.9", "10.255.255.254", "172.16.0.1", "172.31.44.200" }) {
		const std::string code = MakeWifiCode(address);
		EXPECT_EQ(code.size(), WifiCodeLength) << address << " -> " << code;
		for (char c : code)
			EXPECT_NE(JoinCodeAlphabet.find(c), std::string_view::npos) << code;
		EXPECT_EQ(ParseWifiCode(code).value_or("none"), address) << code;
	}
}

TEST(JoinCode, OtherAddressesTakeTheLongForm)
{
	for (const char *address : { "100.64.3.9", "203.0.113.7", "172.32.0.1", "8.8.8.8" }) {
		const std::string code = MakeWifiCode(address);
		EXPECT_EQ(code.size(), WifiCodeFullLength) << address;
		EXPECT_EQ(ParseWifiCode(code).value_or("none"), address) << code;
	}
	EXPECT_TRUE(MakeWifiCode("").empty());
	EXPECT_TRUE(MakeWifiCode("not an address").empty());
	EXPECT_TRUE(MakeWifiCode("192.168.1").empty());
	EXPECT_TRUE(MakeWifiCode("192.168.1.300").empty());
}

TEST(JoinCode, TyposAreRefused)
{
	const std::string code = MakeWifiCode("192.168.1.137");
	int accepted = 0;
	for (size_t i = 0; i < code.size(); ++i) {
		for (char c : JoinCodeAlphabet) {
			if (c == code[i])
				continue;
			std::string wrong = code;
			wrong[i] = c;
			if (ParseWifiCode(wrong))
				++accepted;
		}
	}
	// 6 characters x 31 alternatives = 186 single-character typos; the check catches nearly all of them.
	EXPECT_LT(accepted, 30);
	EXPECT_FALSE(ParseWifiCode("ABCDE"));
	EXPECT_FALSE(ParseWifiCode("ABCDEFGH"));
	EXPECT_FALSE(ParseWifiCode("ABCDU1"));
}

TEST(JoinCode, NormalizeMapsLookalikes)
{
	EXPECT_EQ(NormalizeJoinCode("7q2m xp"), "7Q2MXP");
	EXPECT_EQ(NormalizeJoinCode("oIl-0"), "0110");
	const std::string code = MakeWifiCode("10.0.0.2");
	std::string lower = code;
	for (char &c : lower)
		if (c >= 'A' && c <= 'Z')
			c = static_cast<char>(c - 'A' + 'a');
	EXPECT_EQ(ParseWifiCode(NormalizeJoinCode(lower)).value_or("none"), "10.0.0.2");
}

TEST(JoinCode, OnlineCodesCarryTheNode)
{
	EXPECT_EQ(HostNodeFromCode("0000000000"), 0u);
	EXPECT_EQ(HostNodeFromCode("00000001ZZ"), 1u);
	EXPECT_EQ(HostNodeFromCode("ABC"), 0u);
	EXPECT_EQ(RandomCodeChars(4).size(), 4u);
}

} // namespace
} // namespace devilution
