/**
 * @file multi.h
 *
 * Interface of functions for keeping multiplayer games in sync.
 */
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "dvlnet/leaveinfo.hpp"
#include "deth_version.hpp"
#include "game_mode.hpp"
#include "msg.h"
#include "utils/attributes.h"

namespace devilution {

using net::leaveinfo_t;

// Defined in player.h, forward declared here to allow for functions which operate in the context of a player.
struct Player;

// must be unsigned to generate unsigned comparisons with pnum
/** DETH: room for 8 players; MaxPlayersInGame() is how many a game takes (8 in betas, 4 otherwise). */
#define MAX_PLRS 8

/** DETH: players a game takes: 8 in beta builds, 4 in releases (until 8 is ready for everyone). */
inline int MaxPlayersInGame()
{
	return IsDethBeta ? 8 : 4;
}

/* @brief Contains info of running public game (for game list browsing) */
struct GameInfo {
	std::string name;
	GameData gameData;
	std::vector<std::string> players;
	std::optional<int> latency;
	std::optional<bool> peerIsRelayed;
};

extern bool gbSomebodyWonGameKludge;
extern uint16_t sgwPackPlrOffsetTbl[MAX_PLRS];
extern uint8_t gbActivePlayers;
extern bool gbGameDestroyed;
extern bool gbSelectProvider;
extern std::string GameName;
/** @brief DETH: why the last multiplayer game ended (e.g. the host left); shown once back in the menu. */
extern std::string MultiplayerEndMessage;
/** DETH: the host re-sends its Monster Strength now and then. Call once per game loop. */
void MonsterStrengthTick();
/** DETH: the host changes Monster Strength (remembered for the next game too). */
void SetHostMonsterStrength(int percent);
extern std::string GamePassword;
extern bool PublicGame;
/** DETH: join address (or game id) and password of the current multiplayer game, one per line. */
extern std::string MultiplayerJoinInfo;
/** DETH: the join info plus the internet address once the router has opened the port. */
std::string MultiplayerJoinInfoText();
extern uint8_t gbDeltaSender;
extern uint32_t player_state[MAX_PLRS];
extern bool IsLoopback;

void SwapGameDataLE(GameData &gameData);
DVL_API_FOR_TEST std::string DescribeLeaveReason(leaveinfo_t leaveReason);
std::string FormatGameSeed(const uint32_t gameSeed[4]);

void InitGameInfo();
void NetSendLoPri(uint8_t playerId, const std::byte *data, size_t size);
void NetSendHiPri(uint8_t playerId, const std::byte *data, size_t size);
void multi_send_msg_packet(uint32_t pmask, const std::byte *data, size_t size);
void multi_msg_countdown();
void multi_player_left(uint8_t pnum, leaveinfo_t reason);
void multi_net_ping();

/**
 * @return Always true for singleplayer
 */
bool multi_handle_delta();
void ProcessGameMessagePackets();
void multi_send_zero_packet(uint8_t pnum, _cmd_id bCmd, const std::byte *data, size_t size);
void NetClose();
bool NetInit(bool bSinglePlayer);
void recv_plrinfo(Player &player, const TCmdPlrInfoHdr &header, bool recv);

} // namespace devilution
