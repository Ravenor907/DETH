/**
 * @file game_import_test.cpp
 *
 * DETH: importing game files. The archive tests need 7-Zip (7zz) on the PATH or in
 * DETH_TOOLS_DIR and are skipped without it; CI installs it.
 */
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <unistd.h>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "game_import.hpp"
#include "utils/file_util.h"

namespace devilution {
namespace {

std::string TempRoot()
{
	static int counter = 0;
	const char *base = std::getenv("TMPDIR");
	std::string dir = std::string(base != nullptr ? base : "/tmp") + "/deth-import-test-" + std::to_string(::getpid()) + "-" + std::to_string(++counter) + "/";
	RecursivelyCreateDir(dir.c_str());
	return dir;
}

void WriteFile(const std::string &path, size_t size, char fill)
{
	std::ofstream out(path, std::ios::binary);
	std::string data(size, fill);
	out.write(data.data(), static_cast<std::streamsize>(data.size()));
}

std::uintmax_t Size(const std::string &path)
{
	std::uintmax_t size = 0;
	GetFileSize(path.c_str(), &size);
	return size;
}

std::string SevenZipOnPath()
{
	for (const char *dir : { "/usr/bin/", "/usr/local/bin/", "/bin/" })
		for (const char *name : { "7zz", "7z" }) {
			const std::string path = std::string(dir) + name;
			if (FileExists(path.c_str()))
				return path;
		}
	return {};
}

int Sh(const std::string &command)
{
	return std::system(command.c_str());
}

class GameImportTest : public ::testing::Test {
protected:
	void SetUp() override
	{
		src = TempRoot();
		dest = TempRoot();
		setenv("DETH_IMPORT_DEST", dest.c_str(), 1);
	}
	void TearDown() override
	{
		unsetenv("DETH_IMPORT_DEST");
		Sh("rm -rf '" + src + "' '" + dest + "'");
	}

	std::string src;
	std::string dest;
};

TEST(GameImportNames, RecognizesGameFilesAndArchives)
{
	EXPECT_TRUE(IsGameMpqName("DIABDAT.MPQ"));
	EXPECT_TRUE(IsGameMpqName("diabdat.mpq"));
	EXPECT_TRUE(IsGameMpqName("HfMonk.mpq"));
	EXPECT_FALSE(IsGameMpqName("patch_rt.mpq"));
	EXPECT_FALSE(IsGameMpqName("diabdat.mpq.bak"));
	for (const char *name : { "Diablo.7z", "a.zip", "b.RAR", "c.tar.gz", "d.tgz", "e.tar.xz", "setup_diablo_1.09.exe" })
		EXPECT_TRUE(IsImportableArchiveName(name)) << name;
	EXPECT_FALSE(IsImportableArchiveName("readme.txt"));
	EXPECT_FALSE(IsImportableArchiveName("diabdat.mpq"));
}

TEST_F(GameImportTest, CopiesLooseFilesFromAFolder)
{
	RecursivelyCreateDir((src + "Diablo/Hellfire").c_str());
	WriteFile(src + "Diablo/diabdat.mpq", 4096, 'd');
	WriteFile(src + "Diablo/Hellfire/HELLFIRE.MPQ", 2048, 'h');
	WriteFile(src + "Diablo/notes.txt", 10, 'x');
	const ImportResult result = ImportGameFiles(src, nullptr, nullptr);
	EXPECT_EQ(result.imported.size(), 2u);
	EXPECT_EQ(Size(dest + "DIABDAT.MPQ"), 4096u) << "diabdat is stored as DIABDAT.MPQ";
	EXPECT_EQ(Size(dest + "hellfire.mpq"), 2048u);
	EXPECT_FALSE(FileExists((dest + "notes.txt").c_str()));
	EXPECT_EQ(Size(src + "Diablo/diabdat.mpq"), 4096u) << "the player's own file stays where it was";

	const ImportResult again = ImportGameFiles(src, nullptr, nullptr);
	EXPECT_TRUE(again.imported.empty());
	EXPECT_EQ(again.alreadyThere.size(), 2u) << "importing twice is harmless";
}

class GameImportArchiveTest : public GameImportTest {
protected:
	void SetUp() override
	{
		GameImportTest::SetUp();
		sevenZip = SevenZipOnPath();
		if (sevenZip.empty())
			GTEST_SKIP() << "7-Zip not installed";
		WriteFile(src + "DIABDAT.MPQ", 300000, 'd');
		WriteFile(src + "hfmonk.mpq", 1000, 'm');
	}

	/** Packs the two game files into `name` with 7-Zip. */
	std::string Pack(const std::string &name, const std::string &extra = "")
	{
		const std::string archive = src + name;
		Sh("cd '" + src + "' && '" + sevenZip + "' a -bd -y " + extra + " '" + archive + "' DIABDAT.MPQ hfmonk.mpq >/dev/null");
		return archive;
	}

	void ExpectBoth(const ImportResult &result)
	{
		EXPECT_EQ(Size(dest + "DIABDAT.MPQ"), 300000u);
		EXPECT_EQ(Size(dest + "hfmonk.mpq"), 1000u);
		EXPECT_EQ(result.imported.size(), 2u);
		EXPECT_TRUE(result.problems.empty()) << (result.problems.empty() ? "" : result.problems[0]);
	}

	std::string sevenZip;
};

TEST_F(GameImportArchiveTest, Zip)
{
	ExpectBoth(ImportGameFiles(Pack("Diablo.zip"), nullptr, nullptr));
}

TEST_F(GameImportArchiveTest, SevenZip)
{
	ExpectBoth(ImportGameFiles(Pack("Diablo.7z"), nullptr, nullptr));
}

TEST_F(GameImportArchiveTest, TarGz)
{
	Sh("cd '" + src + "' && '" + sevenZip + "' a -bd -y -ttar Diablo.tar DIABDAT.MPQ hfmonk.mpq >/dev/null && '" + sevenZip + "' a -bd -y -tgzip Diablo.tar.gz Diablo.tar >/dev/null && rm Diablo.tar");
	ExpectBoth(ImportGameFiles(src + "Diablo.tar.gz", nullptr, nullptr));
}

TEST_F(GameImportArchiveTest, PasswordSevenZipWithHiddenNames)
{
	const std::string archive = Pack("Locked.7z", "-p'deth pass' -mhe=on");
	int asked = 0;
	const ImportResult result = ImportGameFiles(archive, [&](std::string_view name, bool retry) -> std::optional<std::string> {
		EXPECT_EQ(name, "Locked.7z");
		EXPECT_EQ(retry, asked > 0);
		return ++asked == 1 ? std::string("wrong") : std::string("deth pass");
	},
	    nullptr);
	EXPECT_EQ(asked, 2) << "asked, wrong, asked again";
	ExpectBoth(result);
}

TEST_F(GameImportArchiveTest, PasswordZip)
{
	const std::string archive = Pack("Locked.zip", "-pzippass");
	ExpectBoth(ImportGameFiles(archive, [](std::string_view, bool) -> std::optional<std::string> { return std::string("zippass"); }, nullptr));
}

TEST_F(GameImportArchiveTest, CancelledPasswordSkipsTheArchive)
{
	const std::string archive = Pack("Locked.7z", "-psecret -mhe=on");
	const ImportResult result = ImportGameFiles(archive, [](std::string_view, bool) -> std::optional<std::string> { return std::nullopt; }, nullptr);
	EXPECT_TRUE(result.imported.empty());
	EXPECT_EQ(result.problems.size(), 1u);
	EXPECT_FALSE(FileExists((dest + "DIABDAT.MPQ").c_str()));
}

TEST_F(GameImportArchiveTest, NestedArchives)
{
	// zip inside 7z inside tar.gz, with a folder in between.
	Sh("cd '" + src + "' && mkdir -p inner/Game && cp DIABDAT.MPQ hfmonk.mpq inner/Game/ && cd inner && '" + sevenZip + "' a -bd -y ../a.zip Game >/dev/null && cd .. && '" + sevenZip + "' a -bd -y b.7z a.zip >/dev/null && '" + sevenZip + "' a -bd -y -ttar c.tar b.7z >/dev/null && '" + sevenZip + "' a -bd -y -tgzip c.tar.gz c.tar >/dev/null");
	ExpectBoth(ImportGameFiles(src + "c.tar.gz", nullptr, nullptr));
	EXPECT_EQ(ListDirectories(dest.c_str()).size(), 0u) << "no temporary folders left behind";
}

TEST_F(GameImportArchiveTest, ArchiveWithoutGameFiles)
{
	WriteFile(src + "readme.txt", 10, 'r');
	Sh("cd '" + src + "' && '" + sevenZip + "' a -bd -y other.zip readme.txt >/dev/null");
	const ImportResult result = ImportGameFiles(src + "other.zip", nullptr, nullptr);
	EXPECT_TRUE(result.imported.empty());
	EXPECT_TRUE(result.problems.empty());
}

} // namespace
} // namespace devilution
