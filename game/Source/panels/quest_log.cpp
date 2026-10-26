#include "panels/quest_log.hpp"

#include <algorithm>
#include <string_view>

#include "DiabloUI/ui_flags.hpp"
#include "control/control.hpp"
#include "effects.h"
#include "engine/render/clx_render.hpp"
#include "engine/render/text_render.hpp"
#include "minitext.h"
#include "player.h"
#include "plrmsg.h"
#include "qol/errands.hpp"
#include "utils/format.hpp"
#include "panels/ui_panels.hpp"
#include "quests.h"
#include "utils/language.h"

namespace devilution {

bool QuestLogIsOpen;
OptionalOwnedClxSpriteList pQLogCel;

namespace {

/**
 * Entries of the quest log window: quest ids, or DETH errands as ErrandEntry + errand.
 * `FirstFinishedQuest` are active entries the rest are completed quests.
 */
int EncounteredQuests[MAXQUESTS + ErrandCount];
constexpr int ErrandEntry = 1000;

bool IsErrandEntry(int entry)
{
	return entry >= ErrandEntry;
}

/** DETH: a running errand: the artifact and who wants it. */
std::string ErrandEntryText(int entry)
{
	const auto errand = static_cast<Errand>(entry - ErrandEntry);
	const ErrandState &state = MyPlayer->errands[errand];
	return FormatRuntime(_("{:s} for {:s}"), ErrandArtifactName(errand, state.artifact), ErrandGiverName(errand));
}

std::string EntryText(int entry)
{
	if (IsErrandEntry(entry))
		return ErrandEntryText(entry);
	return std::string(_(QuestsData[entry]._qlstr));
}
/** Overall number of EncounteredQuests entries */
int EncounteredQuestCount;
/** First (nonselectable) finished quest in list */
int FirstFinishedQuest;
/** Currently selected quest list item */
int SelectedQuest;

constexpr Rectangle InnerPanel { { 32, 26 }, { 280, 300 } };
constexpr int LineHeight = 12;
constexpr int MaxSpacing = LineHeight * 2;
int ListYOffset;
int LineSpacing;
/** The number of pixels to move finished quest, to separate them from the active ones */
int FinishedQuestOffset;

int QuestLogMouseToEntry()
{
	Rectangle innerArea = InnerPanel;
	innerArea.position += Displacement(GetLeftPanel().position.x, GetLeftPanel().position.y);
	if (!innerArea.contains(MousePosition) || (EncounteredQuestCount == 0))
		return -1;
	const int y = MousePosition.y - innerArea.position.y;
	for (int i = 0; i < FirstFinishedQuest; i++) {
		if ((y >= ListYOffset + i * LineSpacing)
		    && (y < ListYOffset + i * LineSpacing + LineHeight)) {
			return i;
		}
	}
	return -1;
}

void PrintQLString(const Surface &out, int x, int y, std::string_view str, bool marked, bool disabled = false)
{
	const int width = GetLineWidth(str);
	x += std::max((257 - width) / 2, 0);
	if (marked) {
		ClxDraw(out, GetPanelPosition(UiPanels::Quest, { x - 20, y + 13 }), (*pSPentSpn2Cels)[PentSpn2Spin()]);
	}
	DrawString(out, str, { GetPanelPosition(UiPanels::Quest, { x, y }), { 257, 0 } },
	    { .flags = disabled ? UiFlags::ColorWhitegold : UiFlags::ColorWhite });
	if (marked) {
		ClxDraw(out, GetPanelPosition(UiPanels::Quest, { x + width + 7, y + 13 }), (*pSPentSpn2Cels)[PentSpn2Spin()]);
	}
}

} // namespace

void DrawQuestLog(const Surface &out)
{
	const int l = QuestLogMouseToEntry();
	if (l >= 0) {
		SelectedQuest = l;
	}
	const auto x = InnerPanel.position.x;
	ClxDraw(out, GetPanelPosition(UiPanels::Quest, { 0, 351 }), (*pQLogCel)[0]);
	int y = InnerPanel.position.y + ListYOffset;
	for (int i = 0; i < EncounteredQuestCount; i++) {
		if (i == FirstFinishedQuest) {
			y += FinishedQuestOffset;
		}
		PrintQLString(out, x, y, EntryText(EncounteredQuests[i]), i == SelectedQuest, i >= FirstFinishedQuest);
		y += LineSpacing;
	}
}

void StartQuestlog()
{
	auto sortQuestIdx = [](int a, int b) {
		return QuestsData[a].questBookOrder < QuestsData[b].questBookOrder;
	};

	EncounteredQuestCount = 0;
	for (auto &quest : Quests) {
		if (quest._qactive == QUEST_ACTIVE && quest._qlog) {
			EncounteredQuests[EncounteredQuestCount] = quest._qidx;
			EncounteredQuestCount++;
		}
	}
	std::sort(&EncounteredQuests[0], &EncounteredQuests[EncounteredQuestCount], sortQuestIdx);
	// DETH: running errands (artifact hunts, Ogden's favor) after the quests.
	for (int i = 0; i < ErrandCount; i++) {
		if (MyPlayer->errands[i].level != 0)
			EncounteredQuests[EncounteredQuestCount++] = ErrandEntry + i;
	}
	FirstFinishedQuest = EncounteredQuestCount;
	for (auto &quest : Quests) {
		if (quest._qactive == QUEST_DONE || quest._qactive == QUEST_HIVE_DONE) {
			EncounteredQuests[EncounteredQuestCount] = quest._qidx;
			EncounteredQuestCount++;
		}
	}

	std::sort(&EncounteredQuests[FirstFinishedQuest], &EncounteredQuests[EncounteredQuestCount], sortQuestIdx);

	const bool twoBlocks = FirstFinishedQuest != 0 && FirstFinishedQuest < EncounteredQuestCount;

	ListYOffset = 0;
	FinishedQuestOffset = !twoBlocks ? 0 : LineHeight / 2;

	const int overallMinHeight = (EncounteredQuestCount * LineHeight) + FinishedQuestOffset;
	const int space = InnerPanel.size.height;

	if (EncounteredQuestCount > 0) {
		const int additionalSpace = space - overallMinHeight;
		int addLineSpacing = additionalSpace / EncounteredQuestCount;
		addLineSpacing = std::min(MaxSpacing - LineHeight, addLineSpacing);
		LineSpacing = LineHeight + addLineSpacing;
		if (twoBlocks) {
			int additionalSepSpace = additionalSpace - (addLineSpacing * EncounteredQuestCount);
			additionalSepSpace = std::min(LineHeight, additionalSepSpace);
			FinishedQuestOffset = std::max(4, additionalSepSpace);
		}

		const int overallHeight = (EncounteredQuestCount * LineSpacing) + FinishedQuestOffset;
		ListYOffset += (space - overallHeight) / 2;
	}

	SelectedQuest = FirstFinishedQuest == 0 ? -1 : 0;
	QuestLogIsOpen = true;
}

void QuestlogUp()
{
	if (FirstFinishedQuest == 0) {
		SelectedQuest = -1;
	} else {
		SelectedQuest--;
		if (SelectedQuest < 0) {
			SelectedQuest = FirstFinishedQuest - 1;
		}
		PlaySFX(SfxID::MenuMove);
	}
}

void QuestlogDown()
{
	if (FirstFinishedQuest == 0) {
		SelectedQuest = -1;
	} else {
		SelectedQuest++;
		if (SelectedQuest == FirstFinishedQuest) {
			SelectedQuest = 0;
		}
		PlaySFX(SfxID::MenuMove);
	}
}

void QuestlogEnter()
{
	PlaySFX(SfxID::MenuSelect);
	if (EncounteredQuestCount != 0 && SelectedQuest >= 0 && SelectedQuest < FirstFinishedQuest) {
		const int entry = EncounteredQuests[SelectedQuest];
		if (IsErrandEntry(entry)) {
			// DETH: where the artifact is, or that it is ready to hand over.
			const auto errand = static_cast<Errand>(entry - ErrandEntry);
			const ErrandState &state = MyPlayer->errands[errand];
			if (state.found)
				EventPlrMsg(FormatRuntime(_("You have the {:s}. Bring it to {:s}."), ErrandArtifactName(errand, state.artifact), ErrandGiverName(errand)));
			else
				EventPlrMsg(FormatRuntime(_("A unique monster on level {:d} or deeper carries the {:s}."), state.level, ErrandArtifactName(errand, state.artifact)));
		} else {
			InitQTextMsg(Quests[entry]._qmsg);
		}
	}
	QuestLogIsOpen = false;
}

void QuestlogESC()
{
	const int l = QuestLogMouseToEntry();
	if (l != -1) {
		QuestlogEnter();
	}
}

} // namespace devilution
