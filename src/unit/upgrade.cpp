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
/**@name upgrade.cpp - The upgrade/allow functions. */
//
//      (c) Copyright 1999-2007 by Vladi Belperchinov-Shabanski and Jimmy Salmon
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

//@{

/*----------------------------------------------------------------------------
--  Includes
----------------------------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "stratagus.h"

#include <string>
#include <vector>
#include <map>

#include "upgrade.h"
#include "player.h"
#include "depend.h"
#include "interface.h"
#include "map.h"
#include "script.h"
#include "spells.h"
#include "unit.h"
#include "unittype.h"
#include "actions.h"
#include "iolib.h"

#include "myendian.h"

#include "util.h"

/*----------------------------------------------------------------------------
--  Declarations
----------------------------------------------------------------------------*/

static void AllowUnitId(CPlayer &player, int id, int units);
static void AllowUpgradeId(CPlayer &player, int id, char af);

/*----------------------------------------------------------------------------
--  Variables
----------------------------------------------------------------------------*/

std::vector<CUpgrade *> AllUpgrades;           /// The main user useable upgrades

	/// How many upgrades modifiers supported
#define UPGRADE_MODIFIERS_MAX (UpgradeMax * 4)
	/// Upgrades modifiers
static CUpgradeModifier *UpgradeModifiers[UPGRADE_MODIFIERS_MAX];
	/// Number of upgrades modifiers used
static int NumUpgradeModifiers;

std::map<std::string, CUpgrade *> Upgrades;

/*----------------------------------------------------------------------------
--  Functions
----------------------------------------------------------------------------*/

CUpgrade::CUpgrade(const std::string &ident) :
	Ident(ident), ID(0)
{
	memset(this->Costs, 0, sizeof(this->Costs));
}

CUpgrade::~CUpgrade()
{
}

/**
**  Create a new upgrade
**
**  @param ident  Upgrade identifier
*/
CUpgrade *CUpgrade::New(const std::string &ident)
{
	CUpgrade *upgrade = Upgrades[ident];
	if (upgrade) {
		return upgrade;
	} else {
		upgrade = new CUpgrade(ident);
		Upgrades[ident] = upgrade;
		upgrade->ID = AllUpgrades.size();
		AllUpgrades.push_back(upgrade);
		return upgrade;
	}
}

/**
**  Get an upgrade
**
**  @param ident  Upgrade identifier
**
**  @return       Upgrade pointer or NULL if not found.
*/
CUpgrade *CUpgrade::Get(const std::string &ident)
{
	CUpgrade *upgrade = Upgrades[ident];
	if (!upgrade) {
		DebugPrint("upgrade not found: %s\n" _C_ ident.c_str());
	}
	return upgrade;
}

/**
**  Init upgrade/allow structures
*/
void InitUpgrades()
{
}

/**
**  Cleanup the upgrade module.
*/
void CleanUpgrades()
{
	//
	//  Free the upgrades.
	//
	while (AllUpgrades.size()) {
		CUpgrade *upgrade = AllUpgrades.back();
		AllUpgrades.pop_back();
		delete upgrade;
	}
	Upgrades.clear();

	//
	//  Free the upgrade modifiers.
	//
	for (int i = 0; i < NumUpgradeModifiers; ++i) {
		delete[] UpgradeModifiers[i]->Modifier.Variables;
		delete UpgradeModifiers[i];
	}
	NumUpgradeModifiers = 0;
}

/**
**  Save state of the dependencies to file.
**
**  @param file  Output file.
*/
void SaveUpgrades(CFile *file)
{
	file->printf("\n-- -----------------------------------------\n");
	file->printf("-- MODULE: upgrades\n\n");

	//
	//  Save the allow
	//
	for (std::vector<CUnitType *>::size_type i = 0; i < UnitTypes.size(); ++i) {
		file->printf("DefineUnitAllow(\"%s\", ", UnitTypes[i]->Ident.c_str());
		for (int p = 0; p < PlayerMax; ++p) {
			if (p) {
				file->printf(", ");
			}
			file->printf("%d", Players[p].Allow.Units[i]);
		}
		file->printf(")\n");
	}
	file->printf("\n");

	//
	//  Save the upgrades
	//
	for (std::vector<CUpgrade *>::size_type j = 0; j < AllUpgrades.size(); ++j) {
		file->printf("DefineAllow(\"%s\", \"", AllUpgrades[j]->Ident.c_str());
		for (int p = 0; p < PlayerMax; ++p) {
			file->printf("%c", Players[p].Allow.Upgrades[j]);
		}
		file->printf("\")\n");
	}
}

/*----------------------------------------------------------------------------
--  Ccl part of upgrades
----------------------------------------------------------------------------*/

/**
**  Define a new upgrade modifier.
**
**  @param l  List of modifiers.
*/
static int CclDefineModifier(lua_State *l)
{
	const char *key;
	const char *value;
	CUpgradeModifier *um;
	int args;
	int j;

	args = lua_gettop(l);

	um = new CUpgradeModifier;

	memset(um->ChangeUpgrades, '?', sizeof(um->ChangeUpgrades));
	memset(um->ApplyTo, '?', sizeof(um->ApplyTo));
	um->Modifier.Variables = new CVariable[UnitTypeVar.GetNumberVariable()];

	um->UpgradeId = UpgradeIdByIdent(LuaToString(l, 1));

	for (j = 1; j < args; ++j) {
		if (!lua_istable(l, j + 1)) {
			LuaError(l, "incorrect argument");
		}
		lua_rawgeti(l, j + 1, 1);
		key = LuaToString(l, -1);
		lua_pop(l, 1);
#if 0 // To be removed. must modify lua file.
		if (!strcmp(key, "attack-range")) {
			key = "AttackRange";
		} else if (!strcmp(key, "sight-range")) {
			key = "SightRange";
		} else if (!strcmp(key, "basic-damage")) {
			key = "BasicDamage";
		} else if (!strcmp(key, "piercing-damage")) {
			key = "PiercingDamage";
		} else if (!strcmp(key, "armor")) {
			key = "Armor";
		} else if (!strcmp(key, "hit-points")) {
			key = "HitPoints";
		}
#endif
		if (!strcmp(key, "regeneration-rate")) {
			lua_rawgeti(l, j + 1, 2);
			um->Modifier.Variables[HP_INDEX].Increase = LuaToNumber(l, -1);
			lua_pop(l, 1);
		} else if (!strcmp(key, "cost")) {
			int i;

			if (!lua_istable(l, j + 1) || lua_objlen(l, j + 1) != 2) {
				LuaError(l, "incorrect argument");
			}
			lua_rawgeti(l, j + 1, 1);
			value = LuaToString(l, -1);
			lua_pop(l, 1);
			for (i = 0; i < MaxCosts; ++i) {
				if (!strcmp(value, DefaultResourceNames[i].c_str())) {
					break;
				}
			}
			if (i == MaxCosts) {
				LuaError(l, "Resource not found: %s" _C_ value);
			}
			lua_rawgeti(l, j + 1, 2);
			um->Modifier.Costs[i] = LuaToNumber(l, -1);
			lua_pop(l, 1);
		} else if (!strcmp(key, "allow-unit")) {
			lua_rawgeti(l, j + 1, 2);
			value = LuaToString(l, -1);
			lua_pop(l, 1);
			if (!strncmp(value, "unit-", 5)) {
				lua_rawgeti(l, j + 1, 3);
				um->ChangeUnits[UnitTypeIdByIdent(value)] = LuaToNumber(l, -1);
				lua_pop(l, 1);
			} else {
				LuaError(l, "unit expected");
			}
		} else if (!strcmp(key, "allow")) {
			lua_rawgeti(l, j + 1, 2);
			value = LuaToString(l, -1);
			lua_pop(l, 1);
			if (!strncmp(value, "upgrade-", 8)) {
				lua_rawgeti(l, j + 1, 3);
				um->ChangeUpgrades[UpgradeIdByIdent(value)] = LuaToNumber(l, -1);
				lua_pop(l, 1);
			} else {
				LuaError(l, "upgrade expected");
			}
		} else if (!strcmp(key, "apply-to")) {
			lua_rawgeti(l, j + 1, 2);
			value = LuaToString(l, -1);
			lua_pop(l, 1);
			um->ApplyTo[UnitTypeIdByIdent(value)] = 'X';
		} else if (!strcmp(key, "convert-to")) {
			lua_rawgeti(l, j + 1, 2);
			value = LuaToString(l, -1);
			lua_pop(l, 1);
			um->ConvertTo = UnitTypeByIdent(value);
		} else {
			int index = UnitTypeVar.VariableNameLookup[key]; // variable index;
			if (index != -1) {
				lua_rawgeti(l, j + 1, 2);
				if (lua_istable(l, -1)) {
					DefineVariableField(l, um->Modifier.Variables + index, -1);
				} else if (lua_isnumber(l, -1)) {
					um->Modifier.Variables[index].Enable = 1;
					um->Modifier.Variables[index].Value = LuaToNumber(l, -1);
					um->Modifier.Variables[index].Max = LuaToNumber(l, -1);
				} else {
					LuaError(l, "bad argument type for '%s'\n" _C_ key);
				}
				lua_pop(l, 1);
			} else {
				LuaError(l, "wrong tag: %s" _C_ key);
			}
		}
	}

	UpgradeModifiers[NumUpgradeModifiers++] = um;

	return 0;
}

/**
**  Define which units are allowed and how much.
*/
static int CclDefineUnitAllow(lua_State *l)
{
	const char *ident;
	int i;
	int args;
	int j;
	int id;

	args = lua_gettop(l);
	j = 0;
	ident = LuaToString(l, j + 1);
	++j;

	if (strncmp(ident, "unit-", 5)) {
		DebugPrint(" wrong ident %s\n" _C_ ident);
		return 0;
	}
	id = UnitTypeIdByIdent(ident);

	i = 0;
	for (; j < args && i < PlayerMax; ++j) {
		AllowUnitId(Players[i], id, LuaToNumber(l, j + 1));
		++i;
	}

	return 0;
}

/**
**  Define which units/upgrades are allowed.
*/
static int CclDefineAllow(lua_State *l)
{
	const char *ident;
	const char *ids;
	int i;
	int n;
	int args;
	int j;
	int id;

	args = lua_gettop(l);
	for (j = 0; j < args; ++j) {
		ident = LuaToString(l, j + 1);
		++j;
		ids = LuaToString(l, j + 1);

		n = strlen(ids);
		if (n > PlayerMax) {
			fprintf(stderr, "%s: Allow string too long %d\n", ident, n);
			n = PlayerMax;
		}

		if (!strncmp(ident, "unit-", 5)) {
			id = UnitTypeIdByIdent(ident);
			for (i = 0; i < n; ++i) {
				if (ids[i] == 'A') {
					AllowUnitId(Players[i], id, UnitMax);
				} else if (ids[i] == 'F') {
					AllowUnitId(Players[i], id, 0);
				}
			}
		} else if (!strncmp(ident, "upgrade-", 8)) {
			id = UpgradeIdByIdent(ident);
			for (i = 0; i < n; ++i) {
				AllowUpgradeId(Players[i], id, ids[i]);
			}
		} else {
			DebugPrint(" wrong ident %s\n" _C_ ident);
		}
	}

	return 0;
}

/**
**  Register CCL features for upgrades.
*/
void UpgradesCclRegister()
{
	lua_register(Lua, "DefineModifier", CclDefineModifier);
	lua_register(Lua, "DefineAllow", CclDefineAllow);
	lua_register(Lua, "DefineUnitAllow", CclDefineUnitAllow);
}

/*----------------------------------------------------------------------------
-- General/Map functions
----------------------------------------------------------------------------*/

// AllowStruct and UpgradeTimers will be static in the player so will be
// load/saved with the player struct

/**
**  UnitType ID by identifier.
**
**  @param ident  The unit-type identifier.
**  @return       Unit-type ID (int) or -1 if not found.
*/
int UnitTypeIdByIdent(const std::string &ident)
{
	const CUnitType *type;

	if ((type = UnitTypeByIdent(ident))) {
		return type->Slot;
	}
	DebugPrint(" fix this %s\n" _C_ ident.c_str());
	Assert(0);
	return -1;
}

/**
**  Upgrade ID by identifier.
**
**  @param ident  The upgrade identifier.
**  @return       Upgrade ID (int) or -1 if not found.
*/
int UpgradeIdByIdent(const std::string &ident)
{
	const CUpgrade *upgrade;

	if ((upgrade = CUpgrade::Get(ident))) {
		return upgrade->ID;
	}
	DebugPrint(" fix this %s\n" _C_ ident.c_str());
	return -1;
}

/*----------------------------------------------------------------------------
-- Upgrades
----------------------------------------------------------------------------*/

/**
**  Convert unit-type to.
**
**  @param player  For this player.
**  @param src     From this unit-type.
**  @param dst     To this unit-type.
*/
static void ConvertUnitTypeTo(CPlayer &player, const CUnitType &src, CUnitType &dst)
{
	for (int i = 0; i < player.TotalNumUnits; ++i) {
		CUnit &unit = *player.Units[i];
		//
		//  Convert already existing units to this type.
		//
		if (unit.Type == &src) {
			CommandTransformIntoType(unit, dst);
		//
		//  Convert trained units to this type.
		//  FIXME: what about buildings?
		//
		} else {
			for (size_t j = 0; j < unit.Orders.size(); ++j) {
				if (unit.Orders[j]->Action == UnitActionTrain
					&& unit.Orders[j]->Arg1.Type == &src) {
						if (j == 0) {
							// Must Adjust Ticks to the fraction that was trained
							unit.CurrentOrder()->Data.Train.Ticks =
								unit.CurrentOrder()->Data.Train.Ticks *
								dst.Stats[player.Index].Costs[TimeCost] /
								src.Stats[player.Index].Costs[TimeCost];
						}
					unit.Orders[j]->Arg1.Type = &dst;
				}
			}
		}
	}
}

/**
**  Apply the modifiers of an upgrade.
**
**  This function will mark upgrade done and do all required modifications
**  to unit types and will modify allow/forbid maps
**
**  @param player  Player that get all the upgrades.
**  @param um      Upgrade modifier that do the effects
*/
static void ApplyUpgradeModifier(CPlayer &player, const CUpgradeModifier *um)
{
	int z;                      // iterator on upgrade or unittype.
	int pn;                     // player number.
	int varModified;            // 0 if variable is not modified.
	int numunits;               // number of unit of the current type.
	CUnit *unitupgrade[UnitMax]; // array of unit of the current type

	Assert(um);
	pn = player.Index;
	for (z = 0; z < UpgradeMax; ++z) {
		// allow/forbid upgrades for player.  only if upgrade is not acquired

		// FIXME: check if modify is allowed

		if (player.Allow.Upgrades[z] != 'R') {
			if (um->ChangeUpgrades[z] == 'A') {
				player.Allow.Upgrades[z] = 'A';
			}
			if (um->ChangeUpgrades[z] == 'F') {
				player.Allow.Upgrades[z] = 'F';
			}
			// we can even have upgrade acquired w/o costs
			if (um->ChangeUpgrades[z] == 'R') {
				player.Allow.Upgrades[z] = 'R';
			}
		}
	}

	for (z = 0; z < UnitTypeMax; ++z) {
		// add/remove allowed units

		// FIXME: check if modify is allowed

		player.Allow.Units[z] += um->ChangeUnits[z];

		Assert(um->ApplyTo[z] == '?' || um->ApplyTo[z] == 'X');

		// this modifier should be applied to unittype id == z
		if (um->ApplyTo[z] == 'X') {

			// If Sight range is upgraded, we need to change EVERY unit
			// to the new range, otherwise the counters get confused.
			if (um->Modifier.Variables[SIGHTRANGE_INDEX].Value) {
				numunits = FindUnitsByType(*UnitTypes[z], unitupgrade);
				for (numunits--; numunits >= 0; --numunits) {
					CUnit &unit = *unitupgrade[numunits];
					if (unit.Player->Index == pn && !unit.Removed) {
						MapUnmarkUnitSight(unit);
						unit.CurrentSightRange = UnitTypes[z]->Stats[pn].Variables[SIGHTRANGE_INDEX].Max +
							um->Modifier.Variables[SIGHTRANGE_INDEX].Value;
						MapMarkUnitSight(unit);
					}
				}
			}
			// upgrade costs :)
			for (unsigned int j = 0; j < MaxCosts; ++j) {
				UnitTypes[z]->Stats[pn].Costs[j] += um->Modifier.Costs[j];
			}

			varModified = 0;
			for (unsigned int j = 0; j < UnitTypeVar.GetNumberVariable(); j++) {
				varModified |= um->Modifier.Variables[j].Value
					| um->Modifier.Variables[j].Max
					| um->Modifier.Variables[j].Increase;
				UnitTypes[z]->Stats[pn].Variables[j].Value += um->Modifier.Variables[j].Value;
				if (UnitTypes[z]->Stats[pn].Variables[j].Value < 0) {
					UnitTypes[z]->Stats[pn].Variables[j].Value = 0;
				}
				UnitTypes[z]->Stats[pn].Variables[j].Max += um->Modifier.Variables[j].Max;
				if (UnitTypes[z]->Stats[pn].Variables[j].Max < 0) {
					UnitTypes[z]->Stats[pn].Variables[j].Max = 0;
				}
				if (UnitTypes[z]->Stats[pn].Variables[j].Value > UnitTypes[z]->Stats[pn].Variables[j].Max) {
					UnitTypes[z]->Stats[pn].Variables[j].Value = UnitTypes[z]->Stats[pn].Variables[j].Max;
				}
				UnitTypes[z]->Stats[pn].Variables[j].Increase += um->Modifier.Variables[j].Increase;
			}

			// And now modify ingame units
			if (varModified) {
				numunits = FindUnitsByType(*UnitTypes[z], unitupgrade);
				numunits--; // Change to 0 Start not 1 start
				for (; numunits >= 0; --numunits) {
					CUnit &unit = *unitupgrade[numunits];
					if (unit.Player->Index != player.Index) {
						continue;
					}
					for (unsigned int j = 0; j < UnitTypeVar.GetNumberVariable(); j++) {
						unit.Variable[j].Value += um->Modifier.Variables[j].Value;
						if (unit.Variable[j].Value < 0) {
							unit.Variable[j].Value = 0;
						}
						unit.Variable[j].Max += um->Modifier.Variables[j].Max;
						if (unit.Variable[j].Max < 0) {
							unit.Variable[j].Max = 0;
						}
						if (unit.Variable[j].Value > unit.Variable[j].Max) {
							unit.Variable[j].Value = unit.Variable[j].Max;
						}
						unit.Variable[j].Increase += um->Modifier.Variables[j].Increase;
					}
				}
			}
			if (um->ConvertTo) {
				ConvertUnitTypeTo(player, *UnitTypes[z], *um->ConvertTo);
			}
		}
	}
}

/**
**  Handle that an upgrade was acquired.
**
**  @param player   Player researching the upgrade.
**  @param upgrade  Upgrade ready researched.
*/
void UpgradeAcquire(CPlayer &player, const CUpgrade *upgrade)
{
	int z;
	int id;

	id = upgrade->ID;
	player.UpgradeTimers.Upgrades[id] = upgrade->Costs[TimeCost];
	AllowUpgradeId(player, id, 'R');  // research done

	for (z = 0; z < NumUpgradeModifiers; ++z) {
		if (UpgradeModifiers[z]->UpgradeId == id) {
			ApplyUpgradeModifier(player, UpgradeModifiers[z]);
		}
	}

	//
	//  Upgrades could change the buttons displayed.
	//
	if (&player == ThisPlayer) {
		SelectedUnitChanged();
	}
}

#if 0 // UpgradeLost not implemented.
/**
**  for now it will be empty?
**  perhaps acquired upgrade can be lost if (for example) a building is lost
**  (lumber mill? stronghold?)
**  this function will apply all modifiers in reverse way
*/
void UpgradeLost(Player &player, int id)
{
	player.UpgradeTimers.Upgrades[id] = 0;
	AllowUpgradeId(player, id, 'A'); // research is lost i.e. available
	// FIXME: here we should reverse apply upgrade...
}
#endif

/*----------------------------------------------------------------------------
--  Allow(s)
----------------------------------------------------------------------------*/

// all the following functions are just map handlers, no specific notes

/**
**  Change allow for an unit-type.
**
**  @param player  Player to change
**  @param id      unit type id
**  @param units   maximum amount of units allowed
*/
static void AllowUnitId(CPlayer &player, int id, int units)
{
	player.Allow.Units[id] = units;
}

/**
**  Change allow for an upgrade.
**
**  @param player  Player to change
**  @param id      upgrade id
**  @param af      `A'llow/`F'orbid/`R'eseached
*/
static void AllowUpgradeId(CPlayer &player, int id, char af)
{
	Assert(af == 'A' || af == 'F' || af == 'R');
	player.Allow.Upgrades[id] = af;
}

/**
**  Return the allow state of the unit.
**
**  @param player   Check state of this player.
**  @param id       Unit identifier.
**
**  @return the allow state of the unit.
*/
int UnitIdAllowed(const CPlayer &player, int id)
{
	Assert(id >= 0 && id < UnitTypeMax);
	return player.Allow.Units[id];
}

/**
**  Return the allow state of an upgrade.
**
**  @param player  Check state for this player.
**  @param id      Upgrade identifier.
**
**  @return the allow state of the upgrade.
*/
char UpgradeIdAllowed(const CPlayer &player, int id)
{
	Assert(id >= 0 && id < UpgradeMax);
	return player.Allow.Upgrades[id];
}

// ***************by string identifiers's

/**
**  Return the allow state of an upgrade.
**
**  @param player  Check state for this player.
**  @param ident   Upgrade identifier.
**
**  @note This function shouldn't be used during runtime, it is only for setup.
*/
char UpgradeIdentAllowed(const CPlayer &player, const std::string &ident)
{
	int id;

	if ((id = UpgradeIdByIdent(ident)) != -1) {
		return UpgradeIdAllowed(player, id);
	}
	DebugPrint("Fix your code, wrong idenifier `%s'\n" _C_ ident.c_str());
	return '-';
}

/*----------------------------------------------------------------------------
--  Check availablity
----------------------------------------------------------------------------*/

/**
**  Check if upgrade (also spells) available for the player.
**
**  @param player  Player pointer.
**  @param ident   Upgrade ident.
*/
int UpgradeIdentAvailable(const CPlayer &player, const std::string &ident)
{
	int allow;

#if 0
	//
	//  Check dependencies
	//
	if (!CheckDependByIdent(player, ident)) {
		return 0;
	}
#endif
	//
	//  Allowed by level
	//
	allow = UpgradeIdentAllowed(player, ident);
	return allow == 'R' || allow == 'X';
}

//@}
