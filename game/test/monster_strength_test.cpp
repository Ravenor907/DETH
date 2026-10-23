/**
 * @file monster_strength_test.cpp
 *
 * DETH: the host's Monster Strength in multiplayer.
 */
#include <gtest/gtest.h>

#include "ui_test.hpp"

#include "monster.h"
#include "msg.h"
#include "multi.h"
#include "player.h"
#include "utils/endian_swap.hpp"

namespace devilution {
namespace {

class MonsterStrengthTest : public UITest {
protected:
	void TearDown() override
	{
		MonsterStrengthPercent = 100;
		gbBufferMsgs = 0;
		UITest::TearDown();
	}

	/** Player `pnum` (0 is the host) sends Monster Strength `percent`. */
	static void Receive(uint8_t pnum, uint16_t percent)
	{
		TCmdParam1 cmd;
		cmd.bCmd = CMD_MONSTERSTRENGTH;
		cmd.wParam1 = Swap16LE(percent);
		ParseCmd(pnum, reinterpret_cast<const TCmd *>(&cmd), sizeof(cmd));
	}
};

TEST_F(MonsterStrengthTest, ClampsToTenPercentSteps)
{
	EXPECT_EQ(ClampMonsterStrength(10), MonsterStrengthMin);
	EXPECT_EQ(ClampMonsterStrength(1000), MonsterStrengthMax);
	EXPECT_EQ(ClampMonsterStrength(154), 150);
	EXPECT_EQ(ClampMonsterStrength(155), 160);
	EXPECT_EQ(ClampMonsterStrength(100), 100);
}

TEST_F(MonsterStrengthTest, NormalChangesNothing)
{
	MonsterStrengthPercent = 100;
	EXPECT_EQ(ScaleDamageFromMonster(Fixed26_6::fromInt(7)), Fixed26_6::fromInt(7));
	EXPECT_EQ(ScaleDamageToMonster(Fixed26_6::fromInt(7)), Fixed26_6::fromInt(7));
	EXPECT_EQ(ScaleMonsterExperience(123), 123u);
}

TEST_F(MonsterStrengthTest, StrongerMonstersHitHarderAndLastLonger)
{
	MonsterStrengthPercent = 200;
	EXPECT_EQ(ScaleDamageFromMonster(Fixed26_6::fromInt(10)), Fixed26_6::fromInt(20));
	EXPECT_EQ(ScaleDamageToMonster(Fixed26_6::fromInt(10)), Fixed26_6::fromInt(5));
	EXPECT_EQ(ScaleMonsterExperience(100), 200u);
	EXPECT_EQ(ScaleDamageToMonster(Fixed26_6::fromRaw(1)), Fixed26_6::fromRaw(1)) << "a hit always does something";

	MonsterStrengthPercent = 50;
	EXPECT_EQ(ScaleDamageFromMonster(Fixed26_6::fromInt(10)), Fixed26_6::fromInt(5));
	EXPECT_EQ(ScaleDamageToMonster(Fixed26_6::fromInt(10)), Fixed26_6::fromInt(20));
	EXPECT_EQ(ScaleMonsterExperience(100), 50u);
}

TEST_F(MonsterStrengthTest, OnlyTheHostSetsIt)
{
	Players.resize(2);
	MyPlayer = &Players[1];
	MyPlayerId = 1;
	gbBufferMsgs = 0;

	Receive(0, 250);
	EXPECT_EQ(MonsterStrengthPercent, 250) << "from the host";

	Receive(0, 999);
	EXPECT_EQ(MonsterStrengthPercent, MonsterStrengthMax) << "clamped";

	MonsterStrengthPercent = 100;
	Receive(1, 300);
	EXPECT_EQ(MonsterStrengthPercent, 100) << "a non-host cannot set it";

	MyPlayerId = 0;
	MyPlayer = &Players[0];
	Players.resize(1);
}

} // namespace
} // namespace devilution
