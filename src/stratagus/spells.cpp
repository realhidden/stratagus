//       _________ __                 __
//      /   _____//  |_____________ _/  |______     ____  __ __  ______
//      \_____  \\   __\_  __ \__  \\   __\__  \   / ___\|  |  \/  ___/
//      /        \|  |  |  | \// __ \|  |  / __ \_/ /_/  >  |  /\___ |
//     /_______  /|__|  |__|  (____  /__| (____  /\___  /|____//____  >
//             \/                  \/          \//_____/            \/
//  ______________________                           ______________________
//                        T H E   W A R   B E G I N S
//         Stratagus - A free fantasy real time strategy game engine
//
/**@name spells.cpp - The spell cast action. */
//
//      (c) Copyright 1998-2006 by Vladi Belperchinov-Shabanski, Lutz Sammer,
//                                 Jimmy Salmon, and Joris DAUPHIN
//
//      This program is free software; you can redistribute it and/or modify
//      it under the terms of the GNU General Public License as published by
//      the Free Software Foundation; only version 2 of the License.
//
//      This program is distributed in the hope that it will be useful,
//      but WITHOUT ANY WARRANTY; without even the implied warranty of
//      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//      GNU General Public License for more details.
//
//      You should have received a copy of the GNU General Public License
//      along with this program; if not, write to the Free Software
//      Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
//      02111-1307, USA.
//

/*
** And when we cast our final spell
** And we meet in our dreams
** A place that no one else can go
** Don't ever let your love die
** Don't ever go breaking this spell
*/

//@{

/*----------------------------------------------------------------------------
-- Notes
----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------
-- Includes
----------------------------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "stratagus.h"

#include "unittype.h"
#include "upgrade.h"
#include "spells.h"
#include "sound.h"
#include "missile.h"
#include "map.h"
#include "tileset.h"
#include "ui.h"
#include "actions.h"

/*----------------------------------------------------------------------------
-- Variables
----------------------------------------------------------------------------*/

/**
** Define the names and effects of all im play available spells.
*/
std::vector<SpellType *> SpellTypeTable;


/*----------------------------------------------------------------------------
-- Functions
----------------------------------------------------------------------------*/

// ****************************************************************************
// Cast the Spell
// ****************************************************************************

/**
**  Cast demolish
**  @param caster       Unit that casts the spell
**  @param spell        Spell-type pointer
**  @param target       Target unit that spell is addressed to
**  @param goalPos      tilePos of target spot when/if target does not exist
**
**  @return             =!0 if spell should be repeated, 0 if not
*/
int Demolish::Cast(CUnit &caster, const SpellType *, CUnit *, const Vec2i &goalPos)
{
	// Allow error margins. (Lame, I know)
	const Vec2i offset = {this->Range + 2, this->Range + 2};
	Vec2i minpos = goalPos - offset;
	Vec2i maxpos = goalPos + offset;

	Map.FixSelectionArea(minpos, maxpos);

	//
	// Terrain effect of the explosion
	//
	Vec2i ipos;
	for (ipos.x = minpos.x; ipos.x <= maxpos.x; ++ipos.x) {
		for (ipos.y = minpos.y; ipos.y <= maxpos.y; ++ipos.y) {
			const int flag = Map.Field(ipos)->Flags;
			if (MapDistance(ipos, goalPos) > this->Range) {
				// Not in circle range
				continue;
			} else if (flag & MapFieldWall) {
				Map.RemoveWall(ipos);
			} else if (flag & MapFieldRocks) {
				Map.ClearTile(MapFieldRocks, ipos);
			} else if (flag & MapFieldForest) {
				Map.ClearTile(MapFieldForest, ipos);
			}
		}
	}

	//
	//  Effect of the explosion on units. Don't bother if damage is 0
	//
	if (this->Damage) {
		std::vector<CUnit *> table;
		Map.SelectFixed(minpos, maxpos, table);
		for (size_t i = 0; i != table.size(); ++i) {
			CUnit &unit = *table[i];
			if (unit.Type->UnitType != UnitTypeFly && unit.IsAlive()
				&& unit.MapDistanceTo(goalPos) <= this->Range) {
				// Don't hit flying units!
				HitUnit(&caster, unit, this->Damage);
			}
		}
	}

	return 1;
}

/**
** Cast circle of power.
**
**  @param caster       Unit that casts the spell
**  @param spell        Spell-type pointer
**  @param target       Target unit that spell is addressed to
**  @param goalPos      tilePos of target spot when/if target does not exist
**
**  @return             =!0 if spell should be repeated, 0 if not
*/
int SpawnPortal::Cast(CUnit &caster, const SpellType *, CUnit *, const Vec2i &goalPos)
{
	// FIXME: vladi: cop should be placed only on explored land
	CUnit *portal = caster.Goal;

	DebugPrint("Spawning a portal exit.\n");
	if (portal) {
		portal->MoveToXY(goalPos);
	} else {
		portal = MakeUnitAndPlace(goalPos, *this->PortalType, &Players[PlayerNumNeutral]);
	}
	//  Goal is used to link to destination circle of power
	caster.Goal = portal;
	//FIXME: setting destination circle of power should use mana
	return 0;
}

/**
** Cast Area Adjust Vitals on all valid units in range.
**
**  @param caster       Unit that casts the spell
**  @param spell        Spell-type pointer
**  @param target       Target unit that spell is addressed to
**  @param goalPos      TilePos of target spot when/if target does not exist
**
**  @return             =!0 if spell should be repeated, 0 if not
*/
int AreaAdjustVitals::Cast(CUnit &caster, const SpellType *spell, CUnit *target, const Vec2i &goalPos)
{
	const Vec2i range = {spell->Range, spell->Range};
	const Vec2i typeSize = {caster.Type->Width, caster.Type->Height};
	std::vector<CUnit *> units;

	// Get all the units around the unit
	Map.Select(goalPos - range, goalPos + typeSize + range, units);
	int hp = this->HP;
	int mana = this->Mana;
	caster.Variable[MANA_INDEX].Value -= spell->ManaCost;
	for (size_t j = 0; j != units.size(); ++j) {
		target = units[j];
		// if (!PassCondition(caster, spell, target, goalPos) {
		if (!CanCastSpell(caster, spell, target, goalPos)) {
			continue;
		}
		if (hp < 0) {
			HitUnit(&caster, *target, -hp);
		} else {
			target->Variable[HP_INDEX].Value += hp;
			if (target->Variable[HP_INDEX].Value > target->Variable[HP_INDEX].Max) {
				target->Variable[HP_INDEX].Value = target->Variable[HP_INDEX].Max;
			}
		}
		target->Variable[MANA_INDEX].Value += mana;
		if (target->Variable[MANA_INDEX].Value < 0) {
			target->Variable[MANA_INDEX].Value = 0;
		}
		if (target->Variable[MANA_INDEX].Value > target->Variable[MANA_INDEX].Max) {
			target->Variable[MANA_INDEX].Value = target->Variable[MANA_INDEX].Max;
		}
	}
	return 0;
}

/**
** Cast area bombardment.
**
**  @param caster       Unit that casts the spell
**  @param spell        Spell-type pointer
**  @param target       Target unit that spell is addressed to
**  @param goalPos      TilePos of target spot when/if target does not exist
**
**  @return             =!0 if spell should be repeated, 0 if not
**  @internal: vladi: blizzard differs than original in this way:
**   original: launches 50 shards at 5 random spots x 10 for 25 mana.
*/
int AreaBombardment::Cast(CUnit &caster, const SpellType *, CUnit *, const Vec2i &goalPos)
{
	int fields = this->Fields;
	const int shards = this->Shards;
	const int damage = this->Damage;
	const PixelDiff offset = { this->StartOffsetX, this->StartOffsetY};
	const MissileType *missile = this->Missile;

	while (fields--) {
		Vec2i dpos;

		// FIXME: radius configurable...
		do {
			// find new destination in the map
			dpos.x = goalPos.x + SyncRand() % 5 - 2;
			dpos.y = goalPos.y + SyncRand() % 5 - 2;
		} while (!Map.Info.IsPointOnMap(dpos));

		const PixelPos dest = Map.TilePosToMapPixelPos_Center(dpos);
		const PixelPos start = dest + offset;
		for (int i = 0; i < shards; ++i) {
			::Missile *mis = MakeMissile(*missile, start, dest);
			//  FIXME: This is just patched up, it works, but I have no idea why.
			//  FIXME: What is the reasoning behind all this?
			if (mis->Type->Speed) {
				mis->Delay = i * mis->Type->Sleep * 2 * PixelTileSize.x / mis->Type->Speed;
			} else {
				mis->Delay = i * mis->Type->Sleep * mis->Type->G->NumFrames;
			}
			mis->Damage = damage;
			// FIXME: not correct -- blizzard should continue even if mage is
			//    destroyed (though it will be quite short time...)
			mis->SourceUnit = &caster;
		}
	}
	return 1;
}

/**
** Evaluate missile location description.
**
** @param location     Parameters for location.
** @param caster       Unit that casts the spell
** @param target       Target unit that spell is addressed to
** @param goalPos      TilePos of target spot when/if target does not exist
** @param res          pointer to PixelPos of the result
*/
static void EvaluateMissileLocation(const SpellActionMissileLocation &location,
									CUnit &caster, CUnit *target, const Vec2i &goalPos, PixelPos *res)
{
	if (location.Base == LocBaseCaster) {
		*res = caster.GetMapPixelPosCenter();
	} else {
		if (target) {
			*res = target->GetMapPixelPosCenter();
		} else {
			*res = Map.TilePosToMapPixelPos_Center(goalPos);
		}
	}
	res->x += location.AddX;
	if (location.AddRandX) {
		res->x += SyncRand() % location.AddRandX;
	}
	res->y += location.AddY;
	if (location.AddRandY) {
		res->y += SyncRand() % location.AddRandY;
	}
}

/**
** Cast spawn missile.
**
**  @param caster       Unit that casts the spell
**  @param spell        Spell-type pointer
**  @param target       Target unit that spell is addressed to
**  @param goalPos      TilePos of target spot when/if target does not exist
**
**  @return             =!0 if spell should be repeated, 0 if not
*/
int SpawnMissile::Cast(CUnit &caster, const SpellType *, CUnit *target, const Vec2i &goalPos)
{
	PixelPos startPos;
	PixelPos endPos;

	EvaluateMissileLocation(this->StartPoint, caster, target, goalPos, &startPos);
	EvaluateMissileLocation(this->EndPoint, caster, target, goalPos, &endPos);

	::Missile *missile = MakeMissile(*this->Missile, startPos, endPos);
	missile->TTL = this->TTL;
	missile->Delay = this->Delay;
	missile->Damage = this->Damage;
	if (this->UseUnitVar) {
		missile->Damage = 0;
		missile->SourceUnit = &caster;
	} else if (missile->Damage != 0) {
		missile->SourceUnit = &caster;
	}
	missile->TargetUnit = target;
	return 1;
}

/**
**  Adjust User Variables.
**
**  @param caster   Unit that casts the spell
**  @param spell    Spell-type pointer
**  @param target   Target
**  @param goalPos  coord of target spot when/if target does not exist
**
**  @return        =!0 if spell should be repeated, 0 if not
*/
int AdjustVariable::Cast(CUnit &caster, const SpellType *, CUnit *target, const Vec2i &/*goalPos*/)
{
	for (unsigned int i = 0; i < UnitTypeVar.GetNumberVariable(); ++i) {
		CUnit *unit = (this->Var[i].TargetIsCaster) ? &caster : target;

		if (!unit) {
			continue;
		}
		// Enable flag.
		if (this->Var[i].ModifEnable) {
			unit->Variable[i].Enable = this->Var[i].Enable;
		}
		unit->Variable[i].Enable ^= this->Var[i].InvertEnable;

		// Max field
		if (this->Var[i].ModifMax) {
			unit->Variable[i].Max = this->Var[i].Max;
		}
		unit->Variable[i].Max += this->Var[i].AddMax;

		// Increase field
		if (this->Var[i].ModifIncrease) {
			unit->Variable[i].Increase = this->Var[i].Increase;
		}
		unit->Variable[i].Increase += this->Var[i].AddIncrease;

		// Value field
		if (this->Var[i].ModifValue) {
			unit->Variable[i].Value = this->Var[i].Value;
		}
		unit->Variable[i].Value += this->Var[i].AddValue;
		unit->Variable[i].Value += this->Var[i].IncreaseTime * unit->Variable[i].Increase;

		clamp(&unit->Variable[i].Value, 0, unit->Variable[i].Max);
	}
	return 1;
}


/**
** Cast healing. (or exorcism)
**
**  @param caster       Unit that casts the spell
**  @param spell        Spell-type pointer
**  @param target       Target unit that spell is addressed to
**  @param goalPos      coord of target spot when/if target does not exist
**
**  @return             =!0 if spell should be repeated, 0 if not
*/
int AdjustVitals::Cast(CUnit &caster, const SpellType *spell, CUnit *target, const Vec2i &/*goalPos*/)
{
	Assert(spell);
	if (!target) {
		return 0;
	}

	const int hp = this->HP;
	const int mana = this->Mana;
	const int manacost = spell->ManaCost;
	int diffHP;
	int diffMana;

	//  Healing and harming
	if (hp > 0) {
		diffHP = target->Variable[HP_INDEX].Max - target->Variable[HP_INDEX].Value;
	} else {
		diffHP = target->Variable[HP_INDEX].Value;
	}
	if (mana > 0) {
		diffMana = target->Stats->Variables[MANA_INDEX].Max - target->Variable[MANA_INDEX].Value;
	} else {
		diffMana = target->Variable[MANA_INDEX].Value;
	}

	//  When harming cast again to send the hp to negative values.
	//  Carefull, a perfect 0 target hp kills too.
	//  Avoid div by 0 errors too!
	int castcount = 1;
	if (hp) {
		castcount = std::max<int>(castcount,
								  diffHP / abs(hp) + (((hp < 0) && (diffHP % (-hp) > 0)) ? 1 : 0));
	}
	if (mana) {
		castcount = std::max<int>(castcount,
								  diffMana / abs(mana) + (((mana < 0) && (diffMana % (-mana) > 0)) ? 1 : 0));
	}
	if (manacost) {
		castcount = std::min<int>(castcount, caster.Variable[MANA_INDEX].Value / manacost);
	}
	if (this->MaxMultiCast) {
		castcount = std::min<int>(castcount, this->MaxMultiCast);
	}

	caster.Variable[MANA_INDEX].Value -= castcount * manacost;
	if (hp < 0) {
		if (&caster != target) {
			HitUnit(&caster, *target, -(castcount * hp));
		} else {
			target->Variable[HP_INDEX].Value += castcount * hp;
			if (target->Variable[HP_INDEX].Value < 0) {
				target->Variable[HP_INDEX].Value = 0;
			}
		}
	} else {
		target->Variable[HP_INDEX].Value += castcount * hp;
		if (target->Variable[HP_INDEX].Value > target->Variable[HP_INDEX].Max) {
			target->Variable[HP_INDEX].Value = target->Variable[HP_INDEX].Max;
		}
	}
	target->Variable[MANA_INDEX].Value += castcount * mana;
	if (target->Variable[MANA_INDEX].Value < 0) {
		target->Variable[MANA_INDEX].Value = 0;
	}
	if (target->Variable[MANA_INDEX].Value > target->Variable[MANA_INDEX].Max) {
		target->Variable[MANA_INDEX].Value = target->Variable[MANA_INDEX].Max;
	}
	if (spell->RepeatCast) {
		return 1;
	}
	return 0;
}

/**
** Cast polymorph.
**
**  @param caster       Unit that casts the spell
**  @param spell        Spell-type pointer
**  @param target       Target unit that spell is addressed to
**  @param goalPos      coord of target spot when/if target does not exist
**
**  @return             =!0 if spell should be repeated, 0 if not
*/
int Polymorph::Cast(CUnit &caster, const SpellType *spell, CUnit *target, const Vec2i &goalPos)
{
	if (!target) {
		return 0;
	}
	CUnitType &type = *this->NewForm;
	const Vec2i pos = {goalPos.x - type.TileWidth / 2, goalPos.y - type.TileHeight / 2};

	caster.Player->Score += target->Variable[POINTS_INDEX].Value;
	if (caster.IsEnemy(*target)) {
		if (target->Type->Building) {
			caster.Player->TotalRazings++;
		} else {
			caster.Player->TotalKills++;
		}
		if (UseHPForXp) {
			caster.Variable[XP_INDEX].Max += target->Variable[HP_INDEX].Value;
		} else {
			caster.Variable[XP_INDEX].Max += target->Variable[POINTS_INDEX].Value;
		}
		caster.Variable[XP_INDEX].Value = caster.Variable[XP_INDEX].Max;
		caster.Variable[KILL_INDEX].Value++;
		caster.Variable[KILL_INDEX].Max++;
		caster.Variable[KILL_INDEX].Enable = 1;
	}

	// as said somewhere else -- no corpses :)
	target->Remove(NULL);
	Vec2i offset;
	for (offset.x = 0; offset.x < type.TileWidth; ++offset.x) {
		for (offset.y = 0; offset.y < type.TileHeight; ++offset.y) {
			if (!UnitTypeCanBeAt(type, pos + offset)) {
				target->Place(target->tilePos);
				return 0;
			}
		}
	}
	caster.Variable[MANA_INDEX].Value -= spell->ManaCost;
	if (this->PlayerNeutral == 1) {
		MakeUnitAndPlace(pos, type, Players + PlayerNumNeutral);
	} else if (this->PlayerNeutral == 2) {
		MakeUnitAndPlace(pos, type, caster.Player);
	} else {
		MakeUnitAndPlace(pos, type, target->Player);
	}
	UnitLost(*target);
	UnitClearOrders(*target);
	target->Release();
	return 1;
}

/**
**  Cast capture.
**
**  @param caster       Unit that casts the spell
**  @param spell        Spell-type pointer
**  @param target       Target unit that spell is addressed to
**  @param goalPos      coord of target spot when/if target does not exist
**
**  @return             =!0 if spell should be repeated, 0 if not
*/
int Capture::Cast(CUnit &caster, const SpellType *spell, CUnit *target, const Vec2i &/*goalPos*/)
{
	if (!target || caster.Player == target->Player) {
		return 0;
	}

	if (this->DamagePercent) {
		if ((100 * target->Variable[HP_INDEX].Value) /
			target->Variable[HP_INDEX].Max > this->DamagePercent &&
			target->Variable[HP_INDEX].Value > this->Damage) {
			HitUnit(&caster, *target, this->Damage);
			if (this->SacrificeEnable) {
				// No corpse.
				caster.Remove(NULL);
				UnitLost(caster);
				UnitClearOrders(caster);
			}
			return 1;
		}
	}
	caster.Player->Score += target->Variable[POINTS_INDEX].Value;
	if (caster.IsEnemy(*target)) {
		if (target->Type->Building) {
			caster.Player->TotalRazings++;
		} else {
			caster.Player->TotalKills++;
		}
		if (UseHPForXp) {
			caster.Variable[XP_INDEX].Max += target->Variable[HP_INDEX].Value;
		} else {
			caster.Variable[XP_INDEX].Max += target->Variable[POINTS_INDEX].Value;
		}
		caster.Variable[XP_INDEX].Value = caster.Variable[XP_INDEX].Max;
		caster.Variable[KILL_INDEX].Value++;
		caster.Variable[KILL_INDEX].Max++;
		caster.Variable[KILL_INDEX].Enable = 1;
	}
	target->ChangeOwner(*caster.Player);
	if (this->SacrificeEnable) {
		// No corpse.
		caster.Remove(NULL);
		UnitLost(caster);
		UnitClearOrders(caster);
	} else {
		caster.Variable[MANA_INDEX].Value -= spell->ManaCost;
	}
	UnitClearOrders(*target);
	return 0;
}

class IsDyingAndNotABuilding
{
public:
	bool operator()(const CUnit *unit) const {
		return unit->CurrentAction() == UnitActionDie && !unit->Type->Building;
	}
};


/**
**  Cast summon spell.
**
**  @param caster       Unit that casts the spell
**  @param spell        Spell-type pointer
**  @param target       Target unit that spell is addressed to
**  @param goalPos      coord of target spot when/if target does not exist
**
**  @return             =!0 if spell should be repeated, 0 if not
*/
int Summon::Cast(CUnit &caster, const SpellType *spell, CUnit *target, const Vec2i &goalPos)
{
	Vec2i pos = goalPos;
	int cansummon;
	CUnitType &unittype = *this->UnitType;
	int ttl = this->TTL;

	if (this->RequireCorpse) {
		const Vec2i offset = {1, 1};
		const Vec2i minPos = pos - offset;
		const Vec2i maxPos = pos + offset;

		CUnit *unit = Map.Find_If(minPos, maxPos, IsDyingAndNotABuilding());
		cansummon = 0;

		if (unit != NULL) { //  Found a corpse. eliminate it and proceed to summoning.
			pos = unit->tilePos;
			unit->Remove(NULL);
			unit->Release();
			cansummon = 1;
		}
	} else {
		cansummon = 1;
	}

	if (cansummon) {
		DebugPrint("Summoning a %s\n" _C_ unittype.Name.c_str());

		//
		// Create units.
		// FIXME: do summoned units count on food?
		//
		target = MakeUnit(unittype, caster.Player);
		if (target != NoUnitP) {
			target->tilePos = pos;
			DropOutOnSide(*target, LookingW, NULL);
			//
			//  set life span. ttl=0 results in a permanent unit.
			//
			if (ttl) {
				target->TTL = GameCycle + ttl;
			}

			caster.Variable[MANA_INDEX].Value -= spell->ManaCost;
		} else {
			DebugPrint("Unable to allocate Unit");
		}
		return 1;
	}
	return 0;
}

// ****************************************************************************
// Target constructor
// ****************************************************************************

/**
**  Target constructor for unit.
**
**  @param unit  Target unit.
**
**  @return the new target.
*/
static Target *NewTargetUnit(CUnit &unit)
{
	return new Target(TargetUnit, &unit, unit.tilePos);
}

// ****************************************************************************
// Main local functions
// ****************************************************************************

/**
**  Check the condition.
**
**  @param caster      Pointer to caster unit.
**  @param spell       Pointer to the spell to cast.
**  @param target      Pointer to target unit, or 0 if it is a position spell.
**  @param goalPos     position, or {-1, -1} if it is a unit spell.
**  @param condition   Pointer to condition info.
**
**  @return            true if passed, false otherwise.
*/
static bool PassCondition(const CUnit &caster, const SpellType *spell, const CUnit *target,
						  const Vec2i &/*goalPos*/, const ConditionInfo *condition)
{
	if (caster.Variable[MANA_INDEX].Value < spell->ManaCost) { // Check caster mana.
		return false;
	}
	if (spell->Target == TargetUnit) { // Casting a unit spell without a target.
		if ((!target) || target->Destroyed || target->CurrentAction() == UnitActionDie) {
			return false;
		}
	}
	if (!condition) { // no condition, pass.
		return true;
	}
	for (unsigned int i = 0; i < UnitTypeVar.GetNumberVariable(); i++) { // for custom variables
		const CUnit *unit;

		if (!condition->Variable[i].Check) {
			continue;
		}

		unit = (condition->Variable[i].ConditionApplyOnCaster) ? &caster : target;
		//  Spell should target location and have unit condition.
		if (unit == NULL) {
			continue;
		}
		if (condition->Variable[i].Enable != CONDITION_TRUE) {
			if ((condition->Variable[i].Enable == CONDITION_ONLY) ^ (unit->Variable[i].Enable)) {
				return false;
			}
		}
		// Value and Max
		if (condition->Variable[i].MinValue >= unit->Variable[i].Value) {
			return false;
		}
		if (condition->Variable[i].MaxValue != -1 &&
			condition->Variable[i].MaxValue <= unit->Variable[i].Value) {
			return false;
		}

		if (condition->Variable[i].MinMax >= unit->Variable[i].Max) {
			return false;
		}

		if (!unit->Variable[i].Max) {
			continue;
		}
		// Percent
		if (condition->Variable[i].MinValuePercent * unit->Variable[i].Max
			>= 100 * unit->Variable[i].Value) {
			return false;
		}
		if (condition->Variable[i].MaxValuePercent * unit->Variable[i].Max
			<= 100 * unit->Variable[i].Value) {
			return false;
		}
	}

	if (!target) {
		return true;
	}
	if (!target->Type->CheckUserBoolFlags(condition->BoolFlag)) {
		return false;
	}
	if (condition->Alliance != CONDITION_TRUE) {
		if ((condition->Alliance == CONDITION_ONLY) ^
			// own units could be not allied ?
			(caster.IsAllied(*target) || target->Player == caster.Player)) {
			return false;
		}
	}
	if (condition->Opponent != CONDITION_TRUE) {
		if ((condition->Opponent == CONDITION_ONLY) ^
			(caster.IsEnemy(*target) && 1)) {
			return false;
		}
	}
	if (condition->TargetSelf != CONDITION_TRUE) {
		if ((condition->TargetSelf == CONDITION_ONLY) ^ (&caster == target)) {
			return false;
		}
	}
	return true;
}

/**
**  Select the target for the autocast.
**
**  @param caster    Unit who would cast the spell.
**  @param spell     Spell-type pointer.
**
**  @return          Target* chosen target or Null if spell can't be cast.
**  @todo FIXME: should be global (for AI) ???
**  @todo FIXME: write for position target.
*/
static Target *SelectTargetUnitsOfAutoCast(CUnit &caster, const SpellType *spell)
{
	AutoCastInfo *autocast;

	// Ai cast should be a lot better. Use autocast if not found.
	if (caster.Player->AiEnabled && spell->AICast) {
		autocast = spell->AICast;
	} else {
		autocast = spell->AutoCast;
	}
	Assert(autocast);
	const Vec2i &pos = caster.tilePos;
	int range = autocast->Range;


	// Select all units aroung the caster

	std::vector<CUnit *> table;
	Map.SelectAroundUnit(caster, range, table);

	//  Check every unit if it is hostile
	int combat = 0;
	for (size_t i = 0; i < table.size(); ++i) {
		if (caster.IsEnemy(*table[i]) && !table[i]->Type->Coward) {
			combat = 1;
			break;
		}
	}

	// Check generic conditions. FIXME: a better way to do this?
	if (autocast->Combat != CONDITION_TRUE) {
		if ((autocast->Combat == CONDITION_ONLY) ^ (combat)) {
			return NULL;
		}
	}

	switch (spell->Target) {
		case TargetSelf :
			if (PassCondition(caster, spell, &caster, pos, spell->Condition)
				&& PassCondition(caster, spell, &caster, pos, autocast->Condition)) {
				return NewTargetUnit(caster);
			}
			return NULL;
		case TargetPosition:
			return 0;
			//  Autocast with a position? That's hard
			//  Possibilities: cast reveal-map on a dark region
			//  Cast raise dead on a bunch of corpses. That would rule.
			//  Cast summon until out of mana in the heat of battle. Trivial?
			//  Find a tight group of units and cast area-damage spells. HARD,
			//  but it is a must-have for AI. What about area-heal?
		case TargetUnit: {
			// The units are already selected.
			//  Check every unit if it is a possible target

			int n = 0;
			for (size_t i = 0; i != table.size(); ++i) {
				//  FIXME: autocast conditions should include normal conditions.
				//  FIXME: no, really, they should.
				if (PassCondition(caster, spell, table[i], pos, spell->Condition)
					&& PassCondition(caster, spell, table[i], pos, autocast->Condition)) {
					table[n++] = table[i];
				}
			}
			// Now select the best unit to target.
			// FIXME: Some really smart way to do this.
			// FIXME: Heal the unit with the lowest hit-points
			// FIXME: Bloodlust the unit with the highest hit-point
			// FIMXE: it will survive more
			if (n != 0) {
#if 0
				// For the best target???
				sort(table, n, spell->autocast->f_order);
				return NewTargetUnit(*table[0]);
#else
				// Best unit, random unit, oh well, same stuff.
				int index = SyncRand() % n;
				return NewTargetUnit(*table[index]);
#endif
			}
			break;
		}
		default:
			// Something is wrong
			DebugPrint("Spell is screwed up, unknown target type\n");
			Assert(0);
			return NULL;
			break;
	}
	return NULL; // Can't spell the auto-cast.
}

// ****************************************************************************
// Public spell functions
// ****************************************************************************

// ****************************************************************************
// Constructor and destructor
// ****************************************************************************

/**
** Spells constructor, inits spell id's and sounds
*/
void InitSpells()
{
}

/**
**  Get spell-type struct pointer by string identifier.
**
**  @param ident  Spell identifier.
**
**  @return       spell-type struct pointer.
*/
SpellType *SpellTypeByIdent(const std::string &ident)
{
	for (std::vector<SpellType *>::iterator i = SpellTypeTable.begin(); i < SpellTypeTable.end(); ++i) {
		if ((*i)->Ident == ident) {
			return *i;
		}
	}
	return NULL;
}

// ****************************************************************************
// CanAutoCastSpell, CanCastSpell, AutoCastSpell, CastSpell.
// ****************************************************************************

/**
**  Check if spell is research for player \p player.
**  @param player    player for who we want to know if he knows the spell.
**  @param spellid   id of the spell to check.
**
**  @return          0 if spell is not available, else no null.
*/
bool SpellIsAvailable(const CPlayer &player, int spellid)
{
	int dependencyId;
	dependencyId = SpellTypeTable[spellid]->DependencyId;

	return dependencyId == -1 || UpgradeIdAllowed(player, dependencyId) == 'R';
}

/**
**  Check if unit can cast the spell.
**
**  @param caster    Unit that casts the spell
**  @param spell     Spell-type pointer
**  @param target    Target unit that spell is addressed to
**  @param goalPos   coord of target spot when/if target does not exist
**
**  @return          =!0 if spell should/can casted, 0 if not
**  @note caster must know the spell, and spell must be researched.
*/
bool CanCastSpell(const CUnit &caster, const SpellType *spell,
				  const CUnit *target, const Vec2i &goalPos)
{
	if (spell->Target == TargetUnit && target == NULL) {
		return false;
	}
	return PassCondition(caster, spell, target, goalPos, spell->Condition);
}

/**
**  Check if the spell can be auto cast and cast it.
**
**  @param caster    Unit who can cast the spell.
**  @param spell     Spell-type pointer.
**
**  @return          1 if spell is casted, 0 if not.
*/
int AutoCastSpell(CUnit &caster, const SpellType *spell)
{
	Target *target;

	//  Check for mana, trivial optimization.
	if (!SpellIsAvailable(*caster.Player, spell->Slot)
		|| caster.Variable[MANA_INDEX].Value < spell->ManaCost) {
		return 0;
	}
	target = SelectTargetUnitsOfAutoCast(caster, spell);
	if (target == NULL) {
		return 0;
	} else {
		// Must move before ?
		// FIXME: SpellType* of CommandSpellCast must be const.
		CommandSpellCast(caster, target->targetPos, target->Unit,
						 const_cast<SpellType *>(spell), FlushCommands);
		delete target;
	}
	return 1;
}

/**
** Spell cast!
**
** @param caster    Unit that casts the spell
** @param spell     Spell-type pointer
** @param target    Target unit that spell is addressed to
** @param x         X coord of target spot when/if target does not exist
** @param y         Y coord of target spot when/if target does not exist
**
** @return          !=0 if spell should/can continue or 0 to stop
*/
int SpellCast(CUnit &caster, const SpellType *spell, CUnit *target, const Vec2i &goalPos)
{
	Vec2i pos = goalPos;
	int cont;             // Should we recast the spell.
	int mustSubtractMana; // false if action which have their own calculation is present.

	caster.Variable[INVISIBLE_INDEX].Value = 0;// unit is invisible until attacks // FIXME: Must be configurable
	if (target) {
		pos = target->tilePos;
	}
	//
	// For TargetSelf, you target.... YOURSELF
	//
	if (spell->Target == TargetSelf) {
		pos = caster.tilePos;
		target = &caster;
	}
	DebugPrint("Spell cast: (%s), %s -> %s (%d,%d)\n" _C_ spell->Ident.c_str() _C_
			   caster.Type->Name.c_str() _C_ target ? target->Type->Name.c_str() : "none" _C_ pos.x _C_ pos.y);
	if (CanCastSpell(caster, spell, target, pos)) {
		cont = 1;
		mustSubtractMana = 1;
		//
		//  Ugly hack, CastAdjustVitals makes it's own mana calculation.
		//
		PlayGameSound(spell->SoundWhenCast.Sound, MaxSampleVolume);
		for (std::vector<SpellActionType *>::const_iterator act = spell->Action.begin();
			 act != spell->Action.end(); ++act) {
			if ((*act)->ModifyManaCaster) {
				mustSubtractMana = 0;
			}
			cont = cont & (*act)->Cast(caster, spell, target, pos);
		}
		if (mustSubtractMana) {
			caster.Variable[MANA_INDEX].Value -= spell->ManaCost;
		}
		//
		// Spells like blizzard are casted again.
		// This is sort of confusing, we do the test again, to
		// check if it will be possible to cast again. Otherwise,
		// when you're out of mana the caster will try again ( do the
		// anim but fail in this proc.
		//
		if (spell->RepeatCast && cont) {
			return CanCastSpell(caster, spell, target, pos);
		}
	}
	//
	// Can't cast, STOP.
	//
	return 0;
}


/**
**  SpellType constructor.
*/
SpellType::SpellType(int slot, const std::string &ident) :
	Ident(ident), Slot(slot), Target(), Action(),
	Range(0), ManaCost(0), RepeatCast(0),
	DependencyId(-1), Condition(NULL),
	AutoCast(NULL), AICast(NULL)
{
}

/**
**  SpellType destructor.
*/
SpellType::~SpellType()
{
	for (std::vector<SpellActionType *>::iterator act = Action.begin(); act != Action.end(); ++act) {
		delete *act;
	}
	Action.clear();

	delete Condition;
	//
	// Free Autocast.
	//
	delete AutoCast;
	delete AICast;
}


/**
** Cleanup the spell subsystem.
*/
void CleanSpells()
{
	DebugPrint("Cleaning spells.\n");
	for (std::vector<SpellType *>::iterator i = SpellTypeTable.begin(); i < SpellTypeTable.end(); ++i) {
		delete *i;
	}
	SpellTypeTable.clear();
}

//@}
