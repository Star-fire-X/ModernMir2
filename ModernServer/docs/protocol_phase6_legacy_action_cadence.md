# Phase 6 Legacy Action Cadence

Phase 6 keeps the protocol bridge unchanged and adds only the legacy action gates that have direct Delphi evidence and were not fully covered by the per-player input budget.

## Scope

- Physical player attacks now pass through a shared world/player cadence gate before they can broadcast, damage, or consume a prepared sword skill.
- Trade confirmation now requires a 1000 ms stable window after trade start or the last successful offer change.
- Legacy framed and client_v1 both reach these gates through the existing canonical-to-`LogicCommand` gameplay path.

This phase does not add a general rate limiter, change canonical mapping, change login state, change string encoding, or change the per-player inbox budget.

## Delphi Compatibility Notes

- `WalkXY` and `RunXY` already have legacy movement timing in the server movement path.
- `SpellXY` already has legacy spell cadence in `Player::begin_spell_attempt`.
- `HitXY` has an independent physical attack interval. The implemented interval is `900 - hit_speed * 60` ms, clamped to a 200 ms minimum.
- `ApplyItemParameters` attack-speed evidence comes from weapon `std_mode 5/6` MAC high byte and necklace/bracelet `std_mode 21/23` AC/MAC low byte rules. `ItemConfig::atk_spd` is intentionally not used in this phase.
- `ServerGetDealEnd` requires the deal contents to remain stable for 1000 ms before both sides can complete a trade.

## Attack Cadence

`Player::begin_attack_attempt(now_ms)` owns the physical attack cadence state:

- First valid attack attempt is allowed.
- Attempts before the current interval are rejected with the existing ack-false packet.
- Rejected attacks do not broadcast, do not damage, and do not consume a prepared sword skill.
- Repeated over-fast attempts eventually force-disconnect with reason `speed_hack_attack`.

The check is in the world actor attack handler after the attacker and sword-skill basic checks are known, and before attack side effects begin.

## Trade Stability

Each `TradeOffer` records `last_change_time_ms`.

- Trade start initializes both sides to the current time.
- Successful add-item, remove-item, or set-gold updates both sides and clears both accepted flags.
- `trade_accept` cancels the trade if either side is within 1000 ms of the latest change.
- Successful stable acceptance keeps the existing commit, cancel, disconnect, and persistence behavior.

## Remaining Out Of Scope

- Pickup, NPC, inventory, and chat still rely on the Phase 5 per-player input budget.
- client_v1 batch behavior is not changed here.
- Error-code and wire-response unification remains out of scope.
- Encoding and legacy byte-string behavior are unchanged.
