#include <cstddef>

#include "DiabloUI/support_lines.h"
#include "utils/language.h"

namespace devilution {

const char *const SupportLines[] = {
	"",
	N_("DETH is maintained by Ravenor907. Issues and bugs can be reported at https://github.com/Ravenor907/DETH Please include the version number, your device and what happened."),
	"",
	"",
	N_("Disclaimer:"),
	N_("	DETH is not supported or maintained by Blizzard Entertainment, nor GOG.com. Neither Blizzard Entertainment nor GOG.com has tested or certified the quality or compatibility of DETH. All inquiries regarding DETH should be directed to its GitHub page, not to Blizzard Entertainment or GOG.com."),
	"",
	"",
	N_("	DETH is a modified build of the DevilutionX engine, used under its license. It makes use of Charis SIL, New Athena Unicode, Unifont, and Noto which are licensed under the SIL Open Font License, as well as Twitmoji which is licensed under CC-BY 4.0, and SDL which is licensed under the zlib-license."),
	"",
	"",
};

const std::size_t SupportLinesSize = sizeof(SupportLines) / sizeof(SupportLines[0]);

} // namespace devilution
