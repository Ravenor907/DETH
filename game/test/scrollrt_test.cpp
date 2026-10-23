#include <gtest/gtest.h>

#include "control/control.hpp"
#include "diablo.h"
#include "engine/render/scrollrt.h"
#include "options.h"
#include "utils/ui_fwd.h"

using namespace devilution;

// TilesInView

TEST(Scroll_rt, calc_tiles_in_view_original)
{
	gnScreenWidth = 640;
	gnScreenHeight = 480;
	gnViewportHeight = gnScreenHeight - 128;
	int columns = 0;
	int rows = 0;
	TilesInView(&columns, &rows);
	EXPECT_EQ(columns, 10);
	EXPECT_EQ(rows, 11);
}

TEST(Scroll_rt, calc_tiles_in_view_960_540)
{
	gnScreenWidth = 960;
	gnScreenHeight = 540;
	gnViewportHeight = gnScreenHeight;
	int columns = 0;
	int rows = 0;
	TilesInView(&columns, &rows);
	EXPECT_EQ(columns, 15);
	EXPECT_EQ(rows, 17);
}

TEST(Scroll_rt, calc_tiles_in_view_640_512)
{
	gnScreenWidth = 640;
	gnScreenHeight = 512;
	gnViewportHeight = gnScreenHeight - 128;
	int columns = 0;
	int rows = 0;
	TilesInView(&columns, &rows);
	EXPECT_EQ(columns, 10);
	EXPECT_EQ(rows, 12);
}

// CalcTileOffset

TEST(Scroll_rt, calc_tile_offset_original)
{
	gnScreenWidth = 640;
	gnScreenHeight = 480;
	gnViewportHeight = gnScreenHeight - 128;
	int x = 0;
	int y = 0;
	CalcTileOffset(&x, &y);
	EXPECT_EQ(x, 0);
	EXPECT_EQ(y, 0);
}

TEST(Scroll_rt, calc_tile_offset_960_540)
{
	gnScreenWidth = 960;
	gnScreenHeight = 540;
	gnViewportHeight = gnScreenHeight;
	int x = 0;
	int y = 0;
	CalcTileOffset(&x, &y);
	EXPECT_EQ(x, 0);
	EXPECT_EQ(y, 2);
}

TEST(Scroll_rt, calc_tile_offset_853_480)
{
	gnScreenWidth = 853;
	gnScreenHeight = 480;
	gnViewportHeight = gnScreenHeight;
	int x = 0;
	int y = 0;
	CalcTileOffset(&x, &y);
	EXPECT_EQ(x, 21);
	EXPECT_EQ(y, 0);
}

// RowsCoveredByPanel

TEST(Scroll_rt, calc_tiles_covered_by_panel_original)
{
	gnScreenWidth = 640;
	CalculatePanelAreas();
	EXPECT_EQ(RowsCoveredByPanel(), 0);
}

TEST(Scroll_rt, calc_tiles_covered_by_panel_960)
{
	gnScreenWidth = 960;
	CalculatePanelAreas();
	EXPECT_EQ(RowsCoveredByPanel(), 4);
}

