// DETH: a minimal smpq replacement for builds where the smpq package is not available
// (the Flatpak SDK). It supports only what CMake/Assets.cmake and CMake/Mods.cmake use:
//   smpq -A -M 1 -C BZIP2 -c ARCHIVE FILE...
// Files are added with their relative path as the archived name ('/' becomes '\').
#include <StormLib.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

int main(int argc, char **argv)
{
	std::string archive;
	std::vector<std::string> files;
	bool create = false;
	for (int i = 1; i < argc; ++i) {
		const std::string arg = argv[i];
		if (arg == "-M" || arg == "-C") {
			++i; // format version / compression: always MPQ v1 with BZIP2
		} else if (arg == "-c") {
			create = true;
		} else if (arg == "-A") {
			// "add" mode marker; nothing to do
		} else if (archive.empty()) {
			archive = arg;
		} else {
			files.push_back(arg);
		}
	}
	if (!create || archive.empty()) {
		std::fprintf(stderr, "usage: smpq -A -M 1 -C BZIP2 -c ARCHIVE FILE...\n");
		return 2;
	}
	std::remove(archive.c_str());

	DWORD maxFiles = 16;
	while (maxFiles < files.size() * 2 + 8)
		maxFiles *= 2;
	HANDLE mpq = nullptr;
	if (!SFileCreateArchive(archive.c_str(), MPQ_CREATE_ARCHIVE_V1 | MPQ_CREATE_LISTFILE, maxFiles, &mpq)) {
		std::fprintf(stderr, "smpq: cannot create %s (error %u)\n", archive.c_str(), static_cast<unsigned>(GetLastError()));
		return 1;
	}
	int failures = 0;
	for (const std::string &file : files) {
		std::string name = file;
		for (char &c : name) {
			if (c == '/') c = '\\';
		}
		if (!SFileAddFileEx(mpq, file.c_str(), name.c_str(), MPQ_FILE_COMPRESS | MPQ_FILE_REPLACEEXISTING, MPQ_COMPRESSION_BZIP2, MPQ_COMPRESSION_NEXT_SAME)) {
			std::fprintf(stderr, "smpq: cannot add %s (error %u)\n", file.c_str(), static_cast<unsigned>(GetLastError()));
			++failures;
		}
	}
	if (!SFileCloseArchive(mpq)) {
		std::fprintf(stderr, "smpq: cannot write %s\n", archive.c_str());
		return 1;
	}
	return failures == 0 ? 0 : 1;
}
