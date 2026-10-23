#include <gtest/gtest.h>

#include <map>
#include <string>
#include <vector>

#include "control/control.hpp"
#include "cursor.h"
#include "diablo.h"
#include "engine/render/scrollrt.h"
#include "engine/render/world_view.hpp"
#include "levels/dun_tile_data.hpp"
#include "options.h"
#include "utils/paths.h"
#include "player.h"
#include "utils/ui_fwd.h"

using namespace devilution;

namespace {

void SetupScreen(int width, int height, float worldScale)
{
	gnScreenWidth = width;
	gnScreenHeight = height;
	gnViewportHeight = height;
	CalculatePanelAreas();
	SetWorldScaleForTesting(worldScale);
	CalcViewportGeometry();
}

void SetupPlayer()
{
	Players.resize(1);
	MyPlayerId = 0;
	MyPlayer = &Players[0];
	*MyPlayer = {};
	MyPlayer->_pmode = PM_STAND;
	ViewPosition = { 50, 50 };
}

/** Center of the tile diamond in frame buffer coordinates. */
Point TileCenterInBuffer(Point tile)
{
	const Point bottomLeft = GetScreenPosition(tile);
	return WorldToBuffer(bottomLeft + Displacement { TILE_WIDTH / 2, -TILE_HEIGHT / 2 + 1 });
}

} // namespace

TEST(WorldView, ViewSizeFollowsScale)
{
	SetupScreen(960, 540, 1.5F);
	EXPECT_EQ(GetWorldViewSize(), Size(640, 360));
	SetupScreen(960, 540, 0.75F);
	EXPECT_EQ(GetWorldViewSize(), Size(1280, 720));
	SetupScreen(1097, 617, 1.25F / 1.75F);
	EXPECT_GE(GetWorldViewSize().width * GetWorldScale(), 1097.0F);
	EXPECT_GE(GetWorldViewSize().height * GetWorldScale(), 617.0F);
	SetupScreen(960, 540, 1.0F);
	EXPECT_EQ(GetWorldViewSize(), Size(960, 540));
}

TEST(WorldView, CoordinateConversionRoundTrips)
{
	// Below 1 several world pixels share a buffer pixel, so only zoomed-in scales round trip.
	for (const float scale : { 1.25F, 1.5F, 1.75F, 2.0F, 2.5F, 3.0F }) {
		SetupScreen(960, 540, scale);
		for (int x = 0; x < 300; x += 7) {
			const Point world { x, x / 2 };
			EXPECT_EQ(BufferToWorld(WorldToBuffer(world)), world) << "scale " << scale;
		}
	}
}

TEST(WorldView, PickingFindsEveryVisibleTile)
{
	SetupPlayer();
	for (const float scale : { 0.5F, 0.625F, 0.75F, 0.875F, 1.125F, 1.25F, 1.5F, 1.75F, 2.0F, 2.5F, 3.0F }) {
		SetupScreen(960, 540, scale);
		int checked = 0;
		for (int dx = -6; dx <= 6; ++dx) {
			for (int dy = -6; dy <= 6; ++dy) {
				const Point tile = ViewPosition + Displacement { dx, dy };
				const Point center = TileCenterInBuffer(tile);
				if (center.x < 0 || center.y < 0 || center.x >= gnScreenWidth || center.y >= gnScreenHeight)
					continue;
				bool flipflag = false;
				EXPECT_EQ(ScreenToTileScaled(center, *MyPlayer, flipflag), tile) << "scale " << scale << " delta " << dx << "," << dy;
				++checked;
			}
		}
		EXPECT_GT(checked, 20) << "scale " << scale;
	}
}

TEST(WorldView, PickingReportsSideOfTile)
{
	SetupPlayer();
	SetupScreen(960, 540, 1.5F);
	const Point center = TileCenterInBuffer(ViewPosition);
	const int quarter = static_cast<int>(TILE_WIDTH / 4 * GetWorldScale());

	bool flipflag = false;
	EXPECT_EQ(ScreenToTileScaled(center - Displacement { quarter, 0 }, *MyPlayer, flipflag), ViewPosition);
	EXPECT_TRUE(flipflag);
	EXPECT_EQ(ScreenToTileScaled(center + Displacement { quarter, 0 }, *MyPlayer, flipflag), ViewPosition);
	EXPECT_FALSE(flipflag);
}

TEST(WorldView, PickingRespectsDiamondEdges)
{
	SetupPlayer();
	SetupScreen(960, 540, 2.0F);
	const Point center = TileCenterInBuffer(ViewPosition);
	const int half = static_cast<int>(TILE_HEIGHT / 2 * GetWorldScale());
	const int halfWidth = static_cast<int>(TILE_WIDTH / 2 * GetWorldScale());
	bool flipflag = false;
	// Just past the top corner belongs to the north neighbour.
	EXPECT_EQ(ScreenToTileScaled(center - Displacement { 0, half + 2 }, *MyPlayer, flipflag), ViewPosition + Direction::North);
	// Just past the right corner belongs to the east neighbour.
	EXPECT_EQ(ScreenToTileScaled(center + Displacement { halfWidth + 2, 0 }, *MyPlayer, flipflag), ViewPosition + Direction::East);
}

TEST(ControllerLayout, XboxStyleDefaults)
{
	paths::SetPrefPath(paths::BasePath() + "test_deth_layout/");
	paths::SetConfigPath(paths::BasePath() + "test_deth_layout/");
	InitPadmapActions();
	LoadOptions(); // applies default bindings (no ini file exists)
	const auto &pad = GetOptions().Padmapper;
	auto button = [&](std::string_view action) { return pad.ButtonComboForAction(action).button; };
	auto modifier = [&](std::string_view action) { return pad.ButtonComboForAction(action).modifier; };

	EXPECT_EQ(button("PrimaryAction"), ControllerButton_BUTTON_A);
	EXPECT_EQ(button("SecondaryAction"), ControllerButton_BUTTON_X);
	EXPECT_EQ(button("CancelAction"), ControllerButton_BUTTON_B);
	EXPECT_EQ(button("DisplaySpells"), ControllerButton_BUTTON_Y);
	EXPECT_EQ(button("SpellAction"), ControllerButton_AXIS_TRIGGERRIGHT);
	EXPECT_EQ(button("StandGround"), ControllerButton_AXIS_TRIGGERLEFT);
	EXPECT_EQ(button("UseHealthPotion"), ControllerButton_BUTTON_LEFTSHOULDER);
	EXPECT_EQ(button("UseManaPotion"), ControllerButton_BUTTON_RIGHTSHOULDER);
	EXPECT_EQ(button("ZoomIn"), ControllerButton_NONE);
	EXPECT_EQ(button("ZoomOut"), ControllerButton_NONE);
	EXPECT_EQ(button("SpellBook"), ControllerButton_BUTTON_DPAD_UP);
	EXPECT_EQ(button("QuestLog"), ControllerButton_BUTTON_DPAD_DOWN);
	EXPECT_EQ(button("Character"), ControllerButton_BUTTON_DPAD_LEFT);
	EXPECT_EQ(button("Inventory"), ControllerButton_BUTTON_DPAD_RIGHT);
	EXPECT_EQ(modifier("QuestLog"), ControllerButton_NONE);
	EXPECT_EQ(modifier("SpellBook"), ControllerButton_NONE);
	EXPECT_EQ(button("ToggleGameMenu1"), ControllerButton_BUTTON_START);
	EXPECT_EQ(modifier("ToggleGameMenu1"), ControllerButton_NONE);
	EXPECT_EQ(button("ToggleAutomap"), ControllerButton_BUTTON_LEFTSTICK);
	// The D-pad no longer walks; the left stick does.
	EXPECT_EQ(button("MoveUp"), ControllerButton_NONE);
	EXPECT_EQ(button("PadMenuNavigator"), ControllerButton_NONE);

	// No two unmodified actions share a button, except deliberate context pairs.
	std::map<int, std::vector<std::string>> byButton;
	for (const auto &action : pad.actions) {
		if (action.boundInput.button == ControllerButton_NONE || action.boundInput.modifier != ControllerButton_NONE)
			continue;
		byButton[action.boundInput.button].push_back(std::string(action.key));
	}
	for (const auto &[btn, names] : byButton) {
		if (names.size() > 1) {
			std::string joined;
			for (const auto &n : names) joined += n + " ";
			ADD_FAILURE() << "button " << btn << " shared by: " << joined;
		}
	}
}
