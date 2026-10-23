#include <string_view>

#include <gtest/gtest.h>

#include "deth_update.h"

namespace devilution {
namespace {

TEST(DethUpdate, CompareVersions)
{
	// Stable releases order numerically.
	EXPECT_LT(CompareDethVersions("1.0.0", "1.0.1"), 0);
	EXPECT_LT(CompareDethVersions("1.0.9", "1.1.0"), 0);
	EXPECT_GT(CompareDethVersions("v1.2.0", "1.1.9"), 0);
	EXPECT_EQ(CompareDethVersions("v1.1.0", "1.1.0"), 0);

	// Betas (M.m.b.N) come after their stable release and before the next one.
	EXPECT_LT(CompareDethVersions("1.0.0", "1.0.b.1"), 0);
	EXPECT_LT(CompareDethVersions("1.0.b.1", "1.0.b.2"), 0);
	EXPECT_LT(CompareDethVersions("1.0.b.9", "1.0.b.10"), 0);
	EXPECT_LT(CompareDethVersions("1.0.b.3", "1.0.1"), 0);
	EXPECT_LT(CompareDethVersions("1.0.b.3", "1.1.0"), 0);
	EXPECT_EQ(CompareDethVersions("v1.0.b.1", "1.0.b.1"), 0);
}

/**
 * Updates must never break: from every version installed in the wild, the next releases must
 * look newer to that version's updater. Add each published version to `Published` and each
 * planned naming to `Planned`.
 */
TEST(DethUpdate, InstalledUpdatersSeeEveryNewRelease)
{
	constexpr std::string_view Published[] = { "1.0.0" };
	constexpr std::string_view Planned[] = { "1.0.b.1", "1.0.b.10", "1.0.1", "1.1.0", "1.1.b.1", "2.0.0" };

	for (std::string_view next : Planned) {
		for (std::string_view installed : Published)
			EXPECT_GT(CompareDethVersions(next, installed), 0) << installed << " would not see " << next;
	}
}

TEST(DethUpdate, OnlyProperReleaseTagsAreUpdates)
{
	for (std::string_view good : { "1.0.0", "1.0.b.2", "10.20.30", "1.1.b.12" })
		EXPECT_TRUE(IsDethReleaseVersion(good)) << good;
	// Helper, test or malformed tags must never be offered.
	for (std::string_view bad : { "1.0.1-rc1", "v1.0.0", "1.0", "1.0.0-beta1", "1.0.0.1", "1.0.b", "1.0.b.", "1.0.x.1", "", "latest", "1.0.0 " })
		EXPECT_FALSE(IsDethReleaseVersion(bad)) << bad;
	// A stable release is newer than every beta of the previous one and older than its own betas.
	EXPECT_GT(CompareDethVersions("1.1.0", "1.0.b.99"), 0);
	EXPECT_LT(CompareDethVersions("1.1.0", "1.1.b.1"), 0);
}

} // namespace
} // namespace devilution
