# Guild Phase 1 Delphi Trace

This document records the Delphi guild and Sabuk castle semantics that must be
preserved before the C++ guild implementation is changed further. The target is
legacy Mir2 compatibility, not a redesigned MMO guild system.

## Scope

- Guild creation through the guild official NPC.
- Guild member add, remove, leave, rank, notice, chat, ally, and war flows.
- Sabuk castle registration, war start, occupation, and finish timing.
- Legacy `CM_*`, `SM_*`, `RM_*`, and inter-server message ordering.
- Current C++ migration gaps in `WorldService`, `MapActor`, persistence, and
  the client_v1 typed gateway.

Guild warehouse, donation, taxes beyond the castle gold fields, and multi-castle
variants are out of scope until the matching Delphi source paths are audited.

## Delphi Source Evidence

Primary files:

- `Source/Common/Grobal2.pas`
  - `SM_GUILDMESSAGE = 104`
  - `SM_CHANGEGUILDNAME = 750`
  - `SM_OPENGUILDDLG = 753`
  - `SM_OPENGUILDDLG_FAIL = 754`
  - `SM_SENDGUILDMEMBERLIST = 756`
  - `SM_GUILDADDMEMBER_OK = 757`
  - `SM_GUILDADDMEMBER_FAIL = 758`
  - `SM_GUILDDELMEMBER_OK = 759`
  - `SM_GUILDDELMEMBER_FAIL = 760`
  - `SM_GUILDRANKUPDATE_FAIL = 761`
  - `SM_BUILDGUILD_OK = 762`
  - `SM_BUILDGUILD_FAIL = 763`
  - `SM_GUILDMAKEALLY_OK = 768`
  - `SM_GUILDMAKEALLY_FAIL = 769`
  - `SM_GUILDBREAKALLY_OK = 770`
  - `SM_GUILDBREAKALLY_FAIL = 771`
  - `CM_OPENGUILDDLG = 1035`
  - `CM_GUILDHOME = 1036`
  - `CM_GUILDMEMBERLIST = 1037`
  - `CM_GUILDADDMEMBER = 1038`
  - `CM_GUILDDELMEMBER = 1039`
  - `CM_GUILDUPDATENOTICE = 1040`
  - `CM_GUILDUPDATERANKINFO = 1041`
  - `CM_GUILDMAKEALLY = 1044`
  - `CM_GUILDBREAKALLY = 1045`
- `Source/M2Server/Guild.pas`
  - `TGuildRank`, `TGuildWarInfo`, `TGuild`, and `TGuildManager`
  - `DEFRANK = 99`
  - `AddGuildMaster`, `AddMember`, `DelMember`, `MemberLogin`,
    `MemberLogout`, `MemberNameChanged`, `GuildMsg`
  - `UpdateGuildRankStr`
  - `DeclareGuildWar`, `CheckGuildWarTimeOut`
  - `MakeAllyGuild`, `CanAlly`, `BreakAlly`
- `Source/M2Server/ObjBase.pas`
  - `SendChangeGuildName`
  - `ServerGetOpenGuildDlg`
  - `ServerGetGuildMemberList`
  - `ServerGetGuildAddMember`
  - `ServerGetGuildDelMember`
  - `ServerGetGuildUpdateNotice`
  - `ServerGetGuildUpdateRanks`
  - `ServerGetGuildMakeAlly`
  - `ServerGetGuildBreakAlly`
  - `GuildDeclareWar`
  - guild chat handling for `!~`
  - castle occupation check in the player run path
- `Source/M2Server/ObjNpc.pas`
  - `TGuildOfficial.UserBuildGuildNow`
  - `TGuildOfficial.UserDeclareGuildWarNow`
  - `TGuildOfficial.UserRequestCastleWar`
- `Source/M2Server/Castle.pas`
  - `TUserCastle.Run`
  - `ProposeCastleWar`
  - `StartCastleWar`
  - `CheckCastleWarWinCondition`
  - `ChangeCastleOwner`
  - `FinishCastleWar`
- `Source/Client/ClMain.pas`
  - `SendGuildDlg`, `SendGuildHome`, `SendGuildMemberList`
  - `SendGuildAddMem`, `SendGuildDelMem`
  - `SendGuildUpdateNotice`, `SendGuildUpdateGrade`
  - client handlers for guild `SM_*`
- `Source/Client/FState.pas`
  - guild window rendering, guild chat list, member edit dialog, notice edit
    dialog, and client-side automatic member-list refresh after add/remove OK.

The Delphi source in this repository is encoded such that several Korean or
Chinese literals render as mojibake in UTF-8 terminals. Compatibility work must
compare bytes or fixture files, not rewritten terminal text.

## Delphi Guild State

`TGuild` stores guild state directly:

- `GuildName`
- `NoticeList`
- `KillGuilds`: string list with `PTGuildWarInfo` objects
- `AllyGuilds`: string list with `TGuild` objects
- `MemberList`: list of `PTGuildRank`
- `FightMemberList`, `MatchPoint`, `BoStartGuildFight`
- `AllowAllyGuild`

`TGuildRank` stores:

- `Rank`
- `RankName`
- `MemList`

The guild lord is not a separate field. The effective lord is the first member
in the first rank group, where the first rank must be `1`. Default members use
rank `99` and the default member title string from the Delphi source.

Online state is embedded in `MemList.Objects[k]`. `MemberLogin` replaces the
stored object with the live `TUserHuman`; `MemberLogout` sets the object back to
nil. C++ must model this with actor ids or handles, not raw pointers.

## Guild File Format

Guilds are listed in the global `GuildFile`; each guild then loads
`GuildDir + GuildName + ".txt"`.

The per-guild file is section-based:

```text
notice line
enemy_guild remaining_ms
ally_guild
#1 lord_title
lord_name
#99 member_title
member_a member_b
```

The exact section header bytes are source/data dependent and must be verified
against real guild files before importing production data.

## Guild Creation

Creation is handled by the guild official NPC with selection
`@@buildguildnow`.

Conditions:

- The requested name, after `Trim`, is non-empty.
- The player is not already in a guild.
- The player has at least `BUILDGUILDFEE` gold. The default in `svMain.pas` is
  `1000000`.
- The player has the configured `__WomaHorn` item.
- `GuildMan.AddGuild(gname, hum.UserName)` succeeds.

Success order:

1. `GuildMan.AddGuild(gname, hum.UserName)`.
2. `UserEngine.SendInterMsg(ISM_ADDGUILD, ServerIndex, gname + "/" + hum.UserName)`.
3. `hum.SendDelItem(pu^)`.
4. `hum.DelItem(pu.MakeIndex, __WomaHorn)`.
5. `hum.DecGold(BUILDGUILDFEE)`.
6. `hum.GoldChanged`.
7. `hum.MyGuild := GuildMan.GetGuildFromMemberName(hum.UserName)`.
8. `MemberLogin` sets `GuildRank` and `GuildRankName`.
9. `hum.SendMsg(self, RM_BUILDGUILD_OK, ...)`.
10. The player message path emits `SM_BUILDGUILD_OK`.

Failure order:

- No resource is removed.
- `RM_BUILDGUILD_FAIL` is sent with `lparam2 = Result`.
- The player message path emits `SM_BUILDGUILD_FAIL` with the legacy error code.

Observed error codes:

- `-1`: already in a guild.
- `-2`: not enough gold.
- `-3`: missing Woma Horn.
- `-4`: empty name or `AddGuild` failed.

## Opening and Refreshing the Guild UI

`CM_OPENGUILDDLG` calls `ServerGetOpenGuildDlg`.

Success payload:

```text
GuildName#13
GuildFlag#13
"1" or "0" commander mode#13
<Notice>#13
notice lines...
<KillGuilds>#13
enemy guild names...
<AllyGuilds>#13
ally guild names...
```

The message is sent as `SM_OPENGUILDDLG` with `series = 1` and an encoded
string body. If the player is not in a guild, the server sends
`SM_OPENGUILDDLG_FAIL`.

`CM_GUILDHOME` calls the same server function.

`CM_GUILDMEMBERLIST` sends `SM_SENDGUILDMEMBERLIST` with this body shape:

```text
#<rank>/*<rank_name>/<member>/<member>/#<rank>/*<rank_name>/<member>/
```

The Delphi client rebuilds the displayed guild lines from this text. It also
stores rank header lines for later `CM_GUILDUPDATERANKINFO`.

## Member Management

`CM_GUILDADDMEMBER` is not an offline invitation and not an application flow.
It is an immediate add performed by the guild lord on the online player in
front of them.

Add-member conditions:

- The requester is the guild lord.
- The target player is online.
- `target.GetFrontCret = requester`.
- The target has `AllowEnterGuild = true`.
- The target is not already in any guild.
- The target is not already a member of this guild.
- The guild is below the Delphi member limit.

Success order:

1. `TGuild.AddMember(target)` adds the member to rank `99`.
2. `GuildInfoChange` schedules and saves guild state.
3. `ISM_RELOADGUILD` is sent.
4. The target's `MyGuild` is assigned.
5. `MemberLogin` sets the target rank and rank name.
6. The requester receives `SM_GUILDADDMEMBER_OK`.
7. The Delphi client immediately sends `CM_GUILDMEMBERLIST`.
8. The requester receives `SM_SENDGUILDMEMBERLIST`.

Failure sends `SM_GUILDADDMEMBER_FAIL` with the legacy error code.

`CM_GUILDDELMEMBER` is lord-only.

Remove-member success order:

1. Delete from `MemberList`.
2. If the target is online, clear `MyGuild` and call `GuildRankChanged(0, "")`.
3. Send `ISM_RELOADGUILD`.
4. Send `SM_GUILDDELMEMBER_OK` to the requester.
5. The Delphi client immediately requests the member list again.

If the lord removes themselves, the code tries `DelGuildMaster`. When that
removes the last lord, `GuildMan.DelGuild` breaks the guild, sends
`ISM_DELGUILD`, clears the requester membership, and sends the OK result.

Ordinary member leave is handled by `GuildSecession`, not by the guild window
delete-member command. It only allows `GuildRank > 1`.

## Rank and Notice Editing

`CM_GUILDUPDATENOTICE`:

- Requires the player to be in a guild.
- Requires `GuildRank = 1`.
- Replaces `NoticeList` with body lines split by `#13`.
- Saves the guild.
- Sends `ISM_RELOADGUILD`.
- Calls `ServerGetOpenGuildDlg`, so the visible response is
  `SM_OPENGUILDDLG`, not a separate OK message.

`CM_GUILDUPDATERANKINFO`:

- Requires `GuildRank = 1`.
- Parses the rank edit text into a temporary rank/member structure.
- Rejects if the resulting text is unchanged.
- Requires first rank group to have rank `1` and a non-empty name.
- Requires the lord group to contain no more than two members.
- Requires at least one lord to be online when the lord group has up to two
  members.
- Requires the new member set to exactly match the old member set.
- Rejects duplicate, zero, or greater-than-99 ranks.
- On success, swaps the new rank list into the guild, updates online member
  ranks, saves, sends `ISM_RELOADGUILD`, and returns `SM_SENDGUILDMEMBERLIST`.
- On failure, sends `SM_GUILDRANKUPDATE_FAIL` for errors `<= -2`.

The rank editor must not be used to add or delete members.

## Guild Chat

The player chat parser treats `!~text` as guild chat. The server calls
`TGuild(MyGuild).GuildMsg(UserName + ":" + text)` and sends an inter-server
guild message.

`GuildMsg` iterates `MemberList` in rank order and member order. Only online
members with `BoHearGuildMsg = true` receive `RM_GUILDMESSAGE`. The player
message path emits `SM_GUILDMESSAGE = 104`.

This order is observable and must not be replaced with session-id ordering,
map ordering, or frame-end batching.

## Ally and War

`CM_GUILDMAKEALLY`:

- Uses the target in front of the requester.
- Requires both actors to be user humans.
- Requires both players to be guild lords.
- Requires the target guild to have `AllowAllyGuild = true`.
- Requires neither guild to be at war with the other.
- Adds both directions, sends guild chat notices to both guilds, refreshes names
  in both guilds, sends `ISM_RELOADGUILD` for both guilds, then sends
  `SM_GUILDMAKEALLY_OK` to the requester.

`CM_GUILDBREAKALLY`:

- Requires requester to be lord.
- Requires the named guild to exist and already be allied.
- Removes both directions, sends guild chat notices, refreshes names, sends
  reloads, then sends `SM_GUILDBREAKALLY_OK`.

Guild war is started by the guild official NPC through `@@guildwar`, not by a
`CM_GUILD*` packet. The NPC checks that the target guild exists, deducts
`GUILDWARFEE`, calls `GuildDeclareWar`, and sends gold changed.

`DeclareGuildWar` is symmetric. It calls `DeclareGuildWar` on both guilds. A
new or repeated declaration sets `WarStartTime = GetTickCount` and
`WarRemain = 3 * 60 * 60 * 1000`. Repeated declarations refresh the timer and
send a different guild message.

`GuildMan.CheckGuildWarTimeOut` removes expired entries and sends a guild
message that the war ended.

## Sabuk Castle

`TUserCastle` is a global-style castle service, not a map-owned object.

Registration:

- `UserRequestCastleWar` requires a guild lord.
- The guild must not already own the castle.
- The player must have `__ZumaPiece`.
- `ProposeCastleWar` rejects duplicate registrations.
- The attack date is set to `CalcDay(Date, 1 + 3)` in the current source.
- The Zuma piece is removed after successful registration.

War start:

- `TUserCastle.Run` is intended to run every 10 seconds.
- At 20:00, once per day, it scans the attacker list for today's date.
- Matching attackers are moved into `RushGuildList`.
- The current owner guild is also inserted into `RushGuildList`.
- `BoCastleUnderAttack` becomes true.
- `LatestWarDateTime`, `CastleAttackStarted`, and war timeout flags are set.
- `StartCastleWar` refreshes visible player names in the castle area.
- The attacker list is saved.
- `ISM_RELOADCASTLEINFO` is sent.
- A global system message announces the start.
- The main door is activated/closed.

During war:

- The three core walls have `BoStoneMode = false`.
- At 10 minutes before the three-hour timeout, the server sends a global warning.
- At three hours, `FinishCastleWar` ends the war.

Occupation:

- The occupation check runs from the player run path, not from `TUserCastle.Run`.
- The player must be in `CorePEnvir`.
- The castle must be under attack.
- The player must have a guild.
- The player's guild must not already be the owner.
- The player's guild must be in `RushGuildList`.
- At least 10 minutes must have elapsed since `CastleAttackStarted`.
- Every live player in `CorePEnvir` must belong to the same guild.

Success order:

1. `ChangeCastleOwner`.
2. Save `Sabuk.txt`.
3. Refresh old and new owner guild member names.
4. Global occupation message.
5. `ISM_SYSOPMSG`.
6. `ISM_CHANGECASTLEOWNER`.
7. If `RushGuildList.Count <= 1`, immediately `FinishCastleWar`.

Finish order:

1. `BoCastleUnderAttack = false`.
2. Clear `RushGuildList`.
3. Area users leave free PK area.
4. Non-owner guild members in the castle area are randomly moved home.
5. Global finish message.
6. `ISM_SYSOPMSG`.

## Current C++ Migration Gaps

Existing useful C++ pieces:

- `ModernServer/src/services/world_service.*` already owns the legacy frame loop.
- `ModernServer/src/world/map_actor_helpers.hpp` already has guild/castle NPC
  helpers and persistence dispatch helpers.
- `ModernServer/src/config/models.hpp` has `GuildState` and
  `GuildCastleSnapshot`.
- `ModernServer/src/storage/repository.*` persists guild and castle snapshots.
- `shared/protocol/client_v1/protocol.hpp` has typed guild messages
  `guild_open_request = 547` through `guild_state = 554`.
- `ModernClient/src/protocol/delphi_protocol_map.hpp` marks guild UI migration
  as partial.

Required changes before full compatibility:

- Replace the flat C++ `GuildState` member vector with rank-group compatible
  state or add a legacy-facing model that preserves rank order.
- Move client_v1 guild mutations out of the gateway and into world-authoritative
  guild commands.
- Fix client_v1 `can_admin`; the Delphi commander condition is `GuildRank = 1`,
  not "has a guild".
- Implement direct face-to-face add-member semantics for legacy guild actions.
- Preserve the Delphi response sequence after add/remove OK, where the client
  asks for the member list.
- Implement guild notice, rank, ally, hostile, and war state persistence.
- Implement `TUserCastle`-equivalent castle timing and player-run occupation
  checks.

## Frame Placement

The compatibility target is:

```text
RunSocketRun
DecodeIdSocket
UserEngineExecuteRun
EventManagerRun
ServerMessageRun
```

Guild player requests, NPC guild selections, `!~` guild chat, and player-run
castle occupation checks should execute during `UserEngineExecuteRun`.

Guild war timeout and `TUserCastle.Run` should execute during
`EventManagerRun`.

The server must mutate in-memory guild state before enqueueing persistence.
Persistence failure should leave a dirty state for retry; it must not roll back
messages already emitted in the Delphi order.

## Golden Trace Requirements

The JSON fixtures in `ModernServer/tests/golden/guild_phase1` are intentionally
static for this review PR. Later implementation PRs must add executable smoke
tests that load those fixtures and compare real server traces.

Every trace case records the client-visible order, not only the final state.
Passing final-state assertions is insufficient for this migration area.
