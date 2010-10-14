/**
 * @file cp_aircraft.c
 * @brief Most of the aircraft related stuff.
 * @sa cl_airfight.c
 * @note Aircraft management functions prefix: AIR_
 * @note Aircraft menu(s) functions prefix: AIM_
 * @note Aircraft equipment handling functions prefix: AII_
 */

/*
Copyright (C) 2002-2010 UFO: Alien Invasion.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include "../client.h"
#include "../ui/ui_main.h"
#include "../ui/ui_popup.h"
#include "../../shared/parse.h"
#include "cp_campaign.h"
#include "cp_mapfightequip.h"
#include "cp_map.h"
#include "cp_ufo.h"
#include "cp_alienbase.h"
#include "cp_time.h"
#include "cp_missions.h"
#include "save/save_aircraft.h"

/**
 * @brief Iterates through the aircraft
 * @param[in] lastAircraft Pointer of the aircraft to iterate from. call with NULL to get the first one.
 */
aircraft_t* AIR_GetNext (aircraft_t *lastAircraft)
{
	return (aircraft_t *)LIST_GetNext(ccs.aircraft, (void*)lastAircraft);
}

/**
 * @brief Iterates through the aircraft of a base
 * @param[in] lastAircraft Pointer of the aircraft to iterate from. call with NULL to get the first one.
 */
aircraft_t* AIR_GetNextFromBase (const base_t *b, aircraft_t *lastAircraft)
{
	if (b) {
		aircraft_t *aircraft = lastAircraft;
		while ((aircraft = AIR_GetNext(aircraft)) != NULL) {
			if (aircraft->homebase != b)
				continue;
			if (aircraft->status != AIR_CRASHED)
				return aircraft;
		}
	}

	return NULL;
}

/**
 * @brief Checks whether there is any aircraft assigned to the given base
 * @param[in] base The base to check
 * @return @c true if there is at least one aircraft, @c false otherwise (or when the @c base pointer is @c NULL)
 */
qboolean AIR_BaseHasAircraft (const base_t *base)
{
	return base != NULL && AIR_GetNextFromBase(base, NULL) != NULL;
}

/**
 * @brief Returns the number of aircraft on the given base
 * @param[in] base The base to check
 */
int AIR_BaseCountAircraft (const base_t *base)
{
	aircraft_t *aircraft = NULL;
	int count = 0;

	while ((aircraft = AIR_GetNextFromBase(base, aircraft)) != NULL)
		count++;

	return count;
}

/**
 * @brief Updates hangar capacities for one aircraft in given base.
 * @param[in] aircraftTemplate Aircraft template.
 * @param[in,out] base Pointer to base.
 * @return AIRCRAFT_HANGAR_BIG if aircraft was placed in big hangar
 * @return AIRCRAFT_HANGAR_SMALL if small
 * @return AIRCRAFT_HANGAR_ERROR if error or not possible
 * @sa AIR_NewAircraft
 * @sa AIR_UpdateHangarCapForAll
 */
static int AIR_UpdateHangarCapForOne (const aircraft_t const *aircraftTemplate, base_t *base)
{
	assert(aircraftTemplate);
	assert(aircraftTemplate == aircraftTemplate->tpl);	/* Make sure it's an aircraft template. */

	if (!base)
		return AIRCRAFT_HANGAR_ERROR;

	if (!AIR_AircraftAllowed(base)) {
		Com_Printf("AIR_UpdateHangarCapForOne: base does not have any hangar - error!\n");
		return AIRCRAFT_HANGAR_ERROR;
	}

	if (aircraftTemplate->size >= AIRCRAFT_LARGE) {
		int freeSpace;
		if (!B_GetBuildingStatus(base, B_HANGAR)) {
			Com_Printf("AIR_UpdateHangarCapForOne: base does not have big hangar - error!\n");
			return AIRCRAFT_HANGAR_ERROR;
		}
		freeSpace = base->capacities[CAP_AIRCRAFT_BIG].max - base->capacities[CAP_AIRCRAFT_BIG].cur;
		if (freeSpace > 0) {
			base->capacities[CAP_AIRCRAFT_BIG].cur++;
			return AIRCRAFT_HANGAR_BIG;
		}
	} else {
		int freeSpace;
		if (!B_GetBuildingStatus(base, B_SMALL_HANGAR)) {
			Com_Printf("AIR_UpdateHangarCapForOne: base does not have small hangar - error!\n");
			return AIRCRAFT_HANGAR_ERROR;
		}
		freeSpace = base->capacities[CAP_AIRCRAFT_SMALL].max - base->capacities[CAP_AIRCRAFT_SMALL].cur;
		if (freeSpace > 0) {
			base->capacities[CAP_AIRCRAFT_SMALL].cur++;
			return AIRCRAFT_HANGAR_SMALL;
		}
	}

	/* No free space for this aircraft. This should never happen here. */
	Sys_Error("AIR_UpdateHangarCapForOne: no free space!\n");
}

/**
 * @brief Updates current capacities for hangars in given base.
 * @param[in,out] base The base we want to update.
 * @note Call this function whenever you sell/loss aircraft in given base.
 * @sa BS_SellAircraft_f
 */
void AIR_UpdateHangarCapForAll (base_t *base)
{
	aircraft_t *aircraft;

	if (!base)
		return;

	/* Reset current capacities for hangar. */
	base->capacities[CAP_AIRCRAFT_BIG].cur = 0;
	base->capacities[CAP_AIRCRAFT_SMALL].cur = 0;

	aircraft = NULL;
	while ((aircraft = AIR_GetNextFromBase(base, aircraft)) != NULL)
		AIR_UpdateHangarCapForOne(aircraft->tpl, base);
}

#ifdef DEBUG
/**
 * @brief Debug function which lists all aircraft in all bases.
 * @note Use with baseIdx as a parameter to display all aircraft in given base.
 * @note called with debug_listaircraft
 */
void AIR_ListAircraft_f (void)
{
	int k, baseIdx;

	if (Cmd_Argc() == 2)
		baseIdx = atoi(Cmd_Argv(1));

	for (baseIdx = 0; baseIdx < MAX_BASES; baseIdx++) {
		const base_t *base = B_GetFoundedBaseByIDX(baseIdx);
		aircraft_t *aircraft;
		if (!base)
			continue;

		Com_Printf("Aircraft in %s: %i\n", base->name, AIR_BaseCountAircraft(base));

		aircraft = NULL;
		while ((aircraft = AIR_GetNextFromBase(base, aircraft)) != NULL) {
			linkedList_t *l;
			Com_Printf("Aircraft %s\n", aircraft->name);
			Com_Printf("...idx global %i\n", aircraft->idx);
			Com_Printf("...homebase: %s\n", aircraft->homebase ? aircraft->homebase->name : "NO HOMEBASE");
			for (k = 0; k < aircraft->maxWeapons; k++) {
				if (aircraft->weapons[k].item) {
					Com_Printf("...weapon slot %i contains %s", k, aircraft->weapons[k].item->id);
					if (!aircraft->weapons[k].installationTime)
						Com_Printf(" (functional)\n");
					else if (aircraft->weapons[k].installationTime > 0)
						Com_Printf(" (%i hours before installation is finished)\n",aircraft->weapons[k].installationTime);
					else
						Com_Printf(" (%i hours before removing is finished)\n",aircraft->weapons[k].installationTime);
					if (aircraft->weapons[k].ammo)
						if (aircraft->weapons[k].ammoLeft > 1)
							Com_Printf("......this weapon is loaded with ammo %s\n", aircraft->weapons[k].ammo->id);
						else
							Com_Printf("......no more ammo (%s)\n", aircraft->weapons[k].ammo->id);
					else
						Com_Printf("......this weapon isn't loaded with ammo\n");
				}
				else
					Com_Printf("...weapon slot %i is empty\n", k);
			}
			if (aircraft->shield.item) {
				Com_Printf("...armour slot contains %s", aircraft->shield.item->id);
				if (!aircraft->shield.installationTime)
						Com_Printf(" (functional)\n");
					else if (aircraft->shield.installationTime > 0)
						Com_Printf(" (%i hours before installation is finished)\n",aircraft->shield.installationTime);
					else
						Com_Printf(" (%i hours before removing is finished)\n",aircraft->shield.installationTime);
			} else
				Com_Printf("...armour slot is empty\n");
			for (k = 0; k < aircraft->maxElectronics; k++) {
				if (aircraft->electronics[k].item) {
					Com_Printf("...electronics slot %i contains %s", k, aircraft->electronics[k].item->id);
					if (!aircraft->electronics[k].installationTime)
						Com_Printf(" (functional)\n");
					else if (aircraft->electronics[k].installationTime > 0)
						Com_Printf(" (%i hours before installation is finished)\n",aircraft->electronics[k].installationTime);
					else
						Com_Printf(" (%i hours before removing is finished)\n",aircraft->electronics[k].installationTime);
				}
				else
					Com_Printf("...electronics slot %i is empty\n", k);
			}
			if (aircraft->pilot) {
				Com_Printf("...pilot: idx: %i name: %s\n", aircraft->pilot->idx, aircraft->pilot->chr.name);
			} else {
				Com_Printf("...no pilot assigned\n");
			}
			Com_Printf("...damage: %i\n", aircraft->damage);
			Com_Printf("...stats: ");
			for (k = 0; k < AIR_STATS_MAX; k++) {
				if (k == AIR_STATS_WRANGE)
					Com_Printf("%.2f ", aircraft->stats[k] / 1000.0f);
				else
					Com_Printf("%i ", aircraft->stats[k]);
			}
			Com_Printf("\n");
			Com_Printf("...name %s\n", aircraft->id);
			Com_Printf("...type %i\n", aircraft->type);
			Com_Printf("...size %i\n", aircraft->maxTeamSize);
			Com_Printf("...fuel %i\n", aircraft->fuel);
			Com_Printf("...status %s\n", AIR_AircraftStatusToName(aircraft));
			Com_Printf("...pos %.0f:%.0f\n", aircraft->pos[0], aircraft->pos[1]);
			Com_Printf("...team: (%i/%i)\n", LIST_Count(aircraft->acTeam), aircraft->maxTeamSize);
			for (l = aircraft->acTeam; l != NULL; l = l->next) {
				const employee_t const *employee = (const employee_t *)l->data;
				const character_t *chr = &employee->chr;
				Com_Printf("......idx (in global array): %i\n", employee->idx);
				if (chr)
					Com_Printf(".........name: %s\n", chr->name);
				else
					Com_Printf(".........ERROR: Could not get character for employee %i\n", employee->idx);
			}
		}
	}
}
#endif

static equipDef_t eTempEq;		/**< Used to count ammo in magazines. */

/**
 * @brief Count and collect ammo from gun magazine.
 * @param[in] magazine Pointer to invList_t being magazine.
 * @param[in] aircraft Pointer to aircraft used in this mission.
 * @sa AII_CollectingItems
 */
static void AII_CollectingAmmo (aircraft_t *aircraft, const invList_t *magazine)
{
	/* Let's add remaining ammo to market. */
	eTempEq.numItemsLoose[magazine->item.m->idx] += magazine->item.a;
	if (eTempEq.numItemsLoose[magazine->item.m->idx] >= magazine->item.t->ammo) {
		/* There are more or equal ammo on the market than magazine needs - collect magazine. */
		eTempEq.numItemsLoose[magazine->item.m->idx] -= magazine->item.t->ammo;
		AII_CollectItem(aircraft, magazine->item.m, 1);
	}
}

/**
 * @brief Add an item to aircraft inventory.
 * @param[in,out] aircraft Aircraft to load to
 * @param[in] item Item to add
 * @param amount Number of items to add
 * @sa AL_AddAliens
 * @sa AII_CollectingItems
 */
void AII_CollectItem (aircraft_t *aircraft, const objDef_t *item, int amount)
{
	int i;
	itemsTmp_t *cargo = aircraft->itemcargo;

	for (i = 0; i < aircraft->itemTypes; i++) {
		if (cargo[i].item == item) {
			Com_DPrintf(DEBUG_CLIENT, "AII_CollectItem: collecting %s (%i) amount %i -> %i\n", item->name, item->idx, cargo[i].amount, cargo[i].amount + amount);
			cargo[i].amount += amount;
			return;
		}
	}
	Com_DPrintf(DEBUG_CLIENT, "AII_CollectItem: adding %s (%i) amount %i\n", item->name, item->idx, amount);
	cargo[i].item = item;
	cargo[i].amount = amount;
	aircraft->itemTypes++;
}

/**
 * @brief Process items carried by soldiers.
 * @param[in] soldier Pointer to le_t being a soldier from our team.
 * @sa AII_CollectingItems
 */
static void AII_CarriedItems (const le_t *soldier)
{
	containerIndex_t container;
	invList_t *invList;

	for (container = 0; container < csi.numIDs; container++) {
		/* Items on the ground are collected as ET_ITEM */
		if (INVDEF(container)->temp)
			continue;
		for (invList = CONTAINER(soldier, container); invList; invList = invList->next) {
			const objDef_t *item = invList->item.t;
			technology_t *tech = RS_GetTechForItem(item);
			ccs.eMission.numItems[item->idx]++;
			RS_MarkCollected(tech);

			if (!item->reload || invList->item.a == 0)
				continue;
			ccs.eMission.numItemsLoose[invList->item.m->idx] += invList->item.a;
			if (ccs.eMission.numItemsLoose[invList->item.m->idx] >= item->ammo) {
				ccs.eMission.numItemsLoose[invList->item.m->idx] -= item->ammo;
				ccs.eMission.numItems[invList->item.m->idx]++;
			}
			/* The guys keep their weapons (half-)loaded. Auto-reload
			 * will happen at equip screen or at the start of next mission,
			 * but fully loaded weapons will not be reloaded even then. */
		}
	}
}

/**
 * @brief Collect items from the battlefield.
 * @note The way we are collecting items:
 * @note 1. Grab everything from the floor and add it to the aircraft cargo here.
 * @note 2. When collecting gun, collect it's remaining ammo as well
 * @note 3. Sell everything or add to base storage when dropship lands in base.
 * @note 4. Items carried by soldiers are processed nothing is being sold.
 * @sa CL_ParseResults
 * @sa AII_CollectingAmmo
 * @sa AII_CarriedItems
 */
void AII_CollectingItems (aircraft_t *aircraft, int won)
{
	int i, j;
	le_t *le = NULL;
	itemsTmp_t *cargo;
	itemsTmp_t prevItemCargo[MAX_CARGO];
	int prevItemTypes;

	/* Save previous cargo */
	memcpy(prevItemCargo, aircraft->itemcargo, sizeof(aircraft->itemcargo));
	prevItemTypes = aircraft->itemTypes;
	/* Make sure itemcargo is empty. */
	memset(aircraft->itemcargo, 0, sizeof(aircraft->itemcargo));

	/* Make sure eTempEq is empty as well. */
	memset(&eTempEq, 0, sizeof(eTempEq));

	cargo = aircraft->itemcargo;
	aircraft->itemTypes = 0;

	while ((le = LE_GetNextInUse(le))) {
		/* Winner collects everything on the floor, and everything carried
		 * by surviving actors. Loser only gets what their living team
		 * members carry. */
		if (LE_IsItem(le)) {
			if (won) {
				invList_t *item;
				for (item = FLOOR(le); item; item = item->next) {
					AII_CollectItem(aircraft, item->item.t, 1);
					if (item->item.t->reload && item->item.a > 0)
						AII_CollectingAmmo(aircraft, item);
				}
			}
		} else if (LE_IsActor(le)) {
			/* The items are already dropped to floor and are available
			 * as ET_ITEM if the actor is dead; or the actor is not ours. */
			/* First of all collect armour of dead or stunned actors (if won). */
			if (won && LE_IsDead(le)) {
				invList_t *item = ARMOUR(le);
				if (item)
					AII_CollectItem(aircraft, item->item.t, 1);
			} else if (le->team == cls.team && !LE_IsDead(le)) {
				/* Finally, the living actor from our team. */
				AII_CarriedItems(le);
			}
		}
	}
	/* Fill the missionResults array. */
	ccs.missionResults.itemTypes = aircraft->itemTypes;
	for (i = 0; i < aircraft->itemTypes; i++)
		ccs.missionResults.itemAmount += cargo[i].amount;

#ifdef DEBUG
	/* Print all of collected items. */
	for (i = 0; i < aircraft->itemTypes; i++) {
		if (cargo[i].amount > 0)
			Com_DPrintf(DEBUG_CLIENT, "Collected items: idx: %i name: %s amount: %i\n", cargo[i].item->idx, cargo[i].item->name, cargo[i].amount);
	}
#endif

	/* Put previous cargo back */
	for (i = 0; i < prevItemTypes; i++) {
		for (j = 0; j < aircraft->itemTypes; j++) {
			if (cargo[j].item == prevItemCargo[i].item) {
				cargo[j].amount += prevItemCargo[i].amount;
				break;
			}
		}
		if (j == aircraft->itemTypes) {
			cargo[j] = prevItemCargo[i];
			aircraft->itemTypes++;
		}
	}
}

/**
 * @brief Translates the aircraft status id to a translatable string
 * @param[in] aircraft Aircraft to translate the status of
 * @return Translation string of given status.
 */
const char *AIR_AircraftStatusToName (const aircraft_t * aircraft)
{
	assert(aircraft);
	assert(aircraft->homebase);

	/* display special status if base-attack affects aircraft */
	if (B_IsUnderAttack(aircraft->homebase) && AIR_IsAircraftInBase(aircraft))
		return _("ON RED ALERT");

	switch (aircraft->status) {
	case AIR_NONE:
		return _("Nothing - should not be displayed");
	case AIR_HOME:
		return _("at home base");
	case AIR_REFUEL:
		return _("refuelling");
	case AIR_IDLE:
		return _("idle");
	case AIR_TRANSIT:
		return _("in transit");
	case AIR_MISSION:
		return _("enroute to mission");
	case AIR_UFO:
		return _("pursuing a UFO");
	case AIR_DROP:
		return _("ready to drop soldiers");
	case AIR_INTERCEPT:
		return _("intercepting a UFO");
	case AIR_TRANSFER:
		return _("enroute to new home base");
	case AIR_RETURNING:
		return _("returning to base");
	case AIR_CRASHED:
		Com_Error(ERR_DROP, "AIR_CRASHED should not be visible anywhere");
	}
	return NULL;
}

/**
 * @brief Checks whether given aircraft is in its homebase.
 * @param[in] aircraft Pointer to an aircraft.
 * @return qtrue if given aircraft is in its homebase.
 * @return qfalse if given aircraft is not in its homebase.
 */
qboolean AIR_IsAircraftInBase (const aircraft_t * aircraft)
{
	if (aircraft->status == AIR_HOME || aircraft->status == AIR_REFUEL)
		return qtrue;
	return qfalse;
}

/**
 * @brief Checks whether given aircraft is on geoscape.
 * @param[in] aircraft Pointer to an aircraft.
 * @note aircraft may be neither on geoscape, nor in base (like when it's transferred)
 * @return @c false if given aircraft is not on geoscape, @c true if given aircraft is on geoscape.
 * @sa UFO_IsUFOSeenOnGeoscape
 */
qboolean AIR_IsAircraftOnGeoscape (const aircraft_t * aircraft)
{
	switch (aircraft->status) {
	case AIR_IDLE:
	case AIR_TRANSIT:
	case AIR_MISSION:
	case AIR_UFO:
	case AIR_DROP:
	case AIR_INTERCEPT:
	case AIR_RETURNING:
		return qtrue;
	case AIR_NONE:
	case AIR_REFUEL:
	case AIR_HOME:
	case AIR_TRANSFER:
	case AIR_CRASHED:
		return qfalse;
	}

	Com_Error(ERR_FATAL, "Unknown aircraft status %i", aircraft->status);
}

/**
 * @brief Calculates the amount of aircraft (of the given type) in the selected base
 * @param[in] base The base to count the aircraft in
 * @param[in] aircraftType The type of the aircraft you want
 * @note that this type is just transporter/interceptor/ufo category
 */
int AIR_CountTypeInBase (const base_t *base, aircraftType_t aircraftType)
{
	int count = 0;
	aircraft_t *aircraft;

	aircraft = NULL;
	while ((aircraft = AIR_GetNextFromBase(base, aircraft)) != NULL)
		if (aircraft->type == aircraftType)
			count++;

	return count;
}

/**
 * @brief Calculates the amount of aircraft (of the given type) in the selected base
 * @param[in] base The base to count the aircraft in
 * @param[in] aircraftTemplate The type of the aircraft you want
 */
int AIR_CountInBaseByTemplate (const base_t *base, const aircraft_t *aircraftTemplate)
{
	int count = 0;
	aircraft_t *aircraft;

	aircraft = NULL;
	while ((aircraft = AIR_GetNextFromBase(base, aircraft)) != NULL)
		if (aircraft->tpl == aircraftTemplate)
			count++;

	return count;
}

/**
 * @brief Returns the string that matches the given aircraft type
 */
const char *AIR_GetAircraftString (aircraftType_t aircraftType)
{
	switch (aircraftType) {
	case AIRCRAFT_INTERCEPTOR:
		return _("Interceptor");
	case AIRCRAFT_TRANSPORTER:
		return _("Transporter");
	case AIRCRAFT_UFO:
		return _("UFO");
	}
	return "";
}

/**
 * @brief Some of the aircraft values needs special calculations when they
 * are shown in the menus
 * @sa CL_AircraftStatToName
 */
int CL_AircraftMenuStatsValues (const int value, const int stat)
{
	switch (stat) {
	case AIR_STATS_SPEED:
	case AIR_STATS_MAXSPEED:
		/* Convert into km/h, and round this value */
		return 10 * (int) (111.2 * value / 10.0f);
	case AIR_STATS_FUELSIZE:
		return value / 1000;
	default:
		return value;
	}
}

/**
 * @brief Calculates the range an aircraft can fly on the geoscape
 * @param aircraft The aircraft to calculate the range for
 * @return The range
 */
int AIR_GetOperationRange (const aircraft_t *aircraft)
{
	const int range = aircraft->stats[AIR_STATS_SPEED] * aircraft->stats[AIR_STATS_FUELSIZE];
	/* the 2.0f factor is for going to destination and then come back */
	return 100 * (int) (KILOMETER_PER_DEGREE * range / (2.0f * (float)SECONDS_PER_HOUR * 100.0f));
}

/**
 * @brief Calculates the remaining range the aircraft can fly
 * @param aircraft The aircraft to calculate the remaining range for
 * @return The remaining range
 */
int AIR_GetRemainingRange (const aircraft_t *aircraft)
{
	return aircraft->stats[AIR_STATS_SPEED] * aircraft->fuel;
}

/**
 * @brief check if aircraft has enough fuel to go to destination, and then come back home
 * @param[in] aircraft Pointer to the aircraft
 * @param[in] destination Pointer to the position the aircraft should go to
 * @sa MAP_MapCalcLine
 * @return qtrue if the aircraft can go to the position, qfalse else
 */
qboolean AIR_AircraftHasEnoughFuel (const aircraft_t *aircraft, const vec2_t destination)
{
	base_t *base;
	float distance;

	assert(aircraft);
	base = (base_t *) aircraft->homebase;
	assert(base);

	/* Calculate the line that the aircraft should follow to go to destination */
	distance = GetDistanceOnGlobe(aircraft->pos, destination);

	/* Calculate the line that the aircraft should then follow to go back home */
	distance += GetDistanceOnGlobe(destination, base->pos);

	/* Check if the aircraft has enough fuel to go to destination and then go back home */
	return (distance <= AIR_GetRemainingRange(aircraft) / (float)SECONDS_PER_HOUR);
}

/**
 * @brief check if aircraft has enough fuel to go to destination
 * @param[in] aircraft Pointer to the aircraft
 * @param[in] destination Pointer to the position the aircraft should go to
 * @sa MAP_MapCalcLine
 * @return qtrue if the aircraft can go to the position, qfalse else
 */
qboolean AIR_AircraftHasEnoughFuelOneWay (const aircraft_t const *aircraft, const vec2_t destination)
{
	float distance;

	assert(aircraft);

	/* Calculate the line that the aircraft should follow to go to destination */
	distance = GetDistanceOnGlobe(aircraft->pos, destination);

	/* Check if the aircraft has enough fuel to go to destination */
	return (distance <= AIR_GetRemainingRange(aircraft) / (float)SECONDS_PER_HOUR);
}

/**
 * @brief Calculates the way back to homebase for given aircraft and returns it.
 * @param[in] aircraft Pointer to aircraft, which should return to base.
 * @note Command to call this: "aircraft_return".
 */
void AIR_AircraftReturnToBase (aircraft_t *aircraft)
{
	if (aircraft && AIR_IsAircraftOnGeoscape(aircraft)) {
		const base_t *base = aircraft->homebase;
		MAP_MapCalcLine(aircraft->pos, base->pos, &aircraft->route);
		aircraft->status = AIR_RETURNING;
		aircraft->time = 0;
		aircraft->point = 0;
		aircraft->mission = NULL;
	}
}

/**
 * @brief Returns the index of the aircraft in the base->aircraft array.
 * @param[in] aircraft The aircraft to get the index for.
 * @return The array index or AIRCRAFT_INBASE_INVALID on error.
 */
int AIR_GetAircraftIDXInBase (const aircraft_t* aircraft)
{
	int i;
	base_t *base;
	aircraft_t *aircraftInBase;

	if (!aircraft || !aircraft->homebase)
		return AIRCRAFT_INBASE_INVALID;

	base = aircraft->homebase;

	i = 0;
	aircraftInBase = NULL;
	while ((aircraftInBase = AIR_GetNextFromBase(base, aircraftInBase)) != NULL) {
		if (aircraftInBase == aircraft)
			return i;
		i++;
	}

	/* Aircraft not found in base */
	return AIRCRAFT_INBASE_INVALID;
}

/**
 * @param base The base to get the aircraft from
 * @param index The index of the aircraft in the given base
 * @return @c NULL if there is no such aircraft in the given base, or the aircraft pointer that belongs to the given index.
 */
aircraft_t *AIR_GetAircraftFromBaseByIDXSafe (base_t* base, int index)
{
	aircraft_t *aircraft;
	int i;

	i = 0;
	aircraft = NULL;
	while ((aircraft = AIR_GetNextFromBase(base, aircraft)) != NULL) {
		if (index == i)
			return aircraft;
		i++;
	}

	return NULL;
}

/**
 * @brief Searches the global array of aircraft types for a given aircraft.
 * @param[in] name Aircraft id.
 * @return aircraft_t pointer or NULL if not found.
 * @note This function gives no warning on null name or if no aircraft found
 */
const aircraft_t *AIR_GetAircraftSilent (const char *name)
{
	int i;

	if (!name)
		return NULL;
	for (i = 0; i < ccs.numAircraftTemplates; i++) {
		const aircraft_t *aircraftTemplate = &ccs.aircraftTemplates[i];
		if (!strcmp(aircraftTemplate->id, name))
			return aircraftTemplate;
	}
	return NULL;
}

/**
 * @brief Searches the global array of aircraft types for a given aircraft.
 * @param[in] name Aircraft id.
 * @return aircraft_t pointer or errors out (ERR_DROP)
 */
const aircraft_t *AIR_GetAircraft (const char *name)
{
	const aircraft_t *aircraft = AIR_GetAircraftSilent(name);
	if (name == NULL || name[0] == '\0')
		Com_Error(ERR_DROP, "AIR_GetAircraft called with NULL name!");
	else if (aircraft == NULL)
		Com_Error(ERR_DROP, "Aircraft '%s' not found", name);

	return aircraft;
}

/**
 * @brief Initialise aircraft pointer in each slot of an aircraft.
 * @param[in] aircraft Pointer to the aircraft where slots are.
 */
static void AII_SetAircraftInSlots (aircraft_t *aircraft)
{
	int i;

	/* initialise weapon slots */
	for (i = 0; i < MAX_AIRCRAFTSLOT; i++) {
		aircraft->weapons[i].aircraft = aircraft;
		aircraft->electronics[i].aircraft = aircraft;
	}
	aircraft->shield.aircraft = aircraft;
}

/**
 * @brief Adds a new aircraft from a given aircraft template to the base and sets the homebase for the new aircraft
 * to the given base
 * @param[out] base The base to add the aircraft to
 * @param[in] aircraftTemplate The template to create the new aircraft from
 * @return the newly added aircraft
 * @sa AIR_Delete
 */
aircraft_t *AIR_Add (base_t *base, const aircraft_t *aircraftTemplate)
{
	aircraft_t *aircraft = (aircraft_t *)LIST_Add(&ccs.aircraft, (const byte *)aircraftTemplate, sizeof(*aircraftTemplate))->data;
	aircraft->homebase = base;
	return aircraft;
}

/**
 * @brief Will remove the given aircraft from the base
 * @param[out] base The base to remove the aircraft from
 * @param aircraft The aircraft to remove
 * @return @c true if the aircraft was removed, @c false otherwise
 * @sa AIR_Add
 */
qboolean AIR_Delete (base_t *base, const aircraft_t *aircraft)
{
	return LIST_Remove(&ccs.aircraft, (const void *)aircraft);
}

/**
 * @brief Places a new aircraft in the given base.
 * @param[in] base Pointer to base where aircraft should be added.
 * @param[in] name Id of aircraft to add (aircraft->id).
 * @sa B_Load
 */
aircraft_t* AIR_NewAircraft (base_t *base, const aircraft_t *aircraftTemplate)
{
	/* copy generic aircraft description to individual aircraft in base
	 * we do this because every aircraft can have its own parameters
	 * now lets use the aircraft array for the base to set some parameters */
	aircraft_t *aircraft = AIR_Add(base, aircraftTemplate);
	aircraft->idx = ccs.campaignStats.aircraftHad++;	/**< set a unique index to this aircraft. */
	aircraft->homebase = base;
	/* Update the values of its stats */
	AII_UpdateAircraftStats(aircraft);
	/* initialise aircraft pointer in slots */
	AII_SetAircraftInSlots(aircraft);
	/* give him some fuel */
	aircraft->fuel = aircraft->stats[AIR_STATS_FUELSIZE];
	/* Set HP to maximum */
	aircraft->damage = aircraft->stats[AIR_STATS_DAMAGE];
	/* Set Default Name */
	Q_strncpyz(aircraft->name, _(aircraft->defaultName), lengthof(aircraft->name));

	/* set initial direction of the aircraft */
	VectorSet(aircraft->direction, 1, 0, 0);

	AIR_ResetAircraftTeam(aircraft);

	Com_sprintf(cp_messageBuffer, sizeof(cp_messageBuffer), _("A new %s is ready in %s"), _(aircraft->tpl->name), base->name);
	MS_AddNewMessage(_("Notice"), cp_messageBuffer, qfalse, MSG_STANDARD, NULL);
	Com_DPrintf(DEBUG_CLIENT, "Setting aircraft to pos: %.0f:%.0f\n", base->pos[0], base->pos[1]);
	Vector2Copy(base->pos, aircraft->pos);
	RADAR_Initialise(&aircraft->radar, RADAR_AIRCRAFTRANGE, RADAR_AIRCRAFTTRACKINGRANGE, 1.0f, qfalse);

	/* Update base capacities. */
	Com_DPrintf(DEBUG_CLIENT, "idx_sample: %i name: %s weight: %i\n", aircraft->tpl->idx, aircraft->id, aircraft->size);
	Com_DPrintf(DEBUG_CLIENT, "Adding new aircraft %s with IDX %i for %s\n", aircraft->id, aircraft->idx, base->name);
	if (!base->aircraftCurrent)
		base->aircraftCurrent = aircraft;
	aircraft->hangar = AIR_UpdateHangarCapForOne(aircraft->tpl, base);
	if (aircraft->hangar == AIRCRAFT_HANGAR_ERROR)
		Com_Printf("AIR_NewAircraft: ERROR, new aircraft but no free space in hangars!\n");
	/* also update the base menu buttons */
	Cmd_ExecuteString("base_init");
	return aircraft;
}

int AIR_GetCapacityByAircraftWeight (const aircraft_t *aircraft)
{
	switch (aircraft->size) {
	case AIRCRAFT_SMALL:
		return CAP_AIRCRAFT_SMALL;
	case AIRCRAFT_LARGE:
		return CAP_AIRCRAFT_BIG;
	}
	Com_Error(ERR_DROP, "AIR_GetCapacityByAircraftWeight: Unknown weight of aircraft '%i'\n", aircraft->size);
}

/**
 * @brief Calculate used storage room corresponding to items in an aircraft.
 * @param[in] aircraft Pointer to the aircraft.
 */
static int AIR_GetStorageRoom (const aircraft_t *aircraft)
{
	int size = 0;
	linkedList_t* l;

	for (l = aircraft->acTeam; l != NULL; l = l->next) {
		const employee_t const *employee = (const employee_t *)l->data;
		containerIndex_t container;
		for (container = 0; container < csi.numIDs; container++) {
			invList_t *ic;
#if 0
			/* ignore items linked from any temp container */
			if (INVDEF(container)->temp)
				continue;
#endif
			for (ic = CONTAINER(&employee->chr, container); ic; ic = ic->next) {
				const objDef_t *obj = ic->item.t;
				size += obj->size;

				obj = ic->item.m;
				if (obj)
					size += obj->size;
			}
		}
	}

	return size;
}

const char *AIR_CheckMoveIntoNewHomebase (const aircraft_t *aircraft, const base_t* base, const int capacity)
{
	if (!B_GetBuildingStatus(base, B_GetBuildingTypeByCapacity(capacity)))
		return _("No operational hangars at that base.");

	/* not enough capacity */
	if (base->capacities[capacity].cur >= base->capacities[capacity].max)
		return _("No free hangars at that base.");

	if (aircraft->maxTeamSize + (AIR_GetPilot(aircraft) ? 1 : 0) + base->capacities[CAP_EMPLOYEES].cur >  base->capacities[CAP_EMPLOYEES].max)
		return _("Insufficient free crew quarter space at that base.");

	if (aircraft->maxTeamSize && base->capacities[CAP_ITEMS].cur + AIR_GetStorageRoom(aircraft) > base->capacities[CAP_ITEMS].max)
		return _("Insufficient storage space at that base.");

	/* check aircraft fuel, because the aircraft has to travel to the new base */
	if (!AIR_AircraftHasEnoughFuelOneWay(aircraft, base->pos))
		return _("That base is beyond this aircraft's range.");

	return NULL;
}

/**
 * @brief Transfer items carried by a soldier from one base to another.
 * @param[in] chr Pointer to the character.
 * @param[in] sourceBase Base where employee comes from.
 * @param[in] destBase Base where employee is going.
 */
static void AIR_TransferItemsCarriedByCharacterToBase (character_t *chr, base_t *sourceBase, base_t* destBase)
{
	const invList_t *ic;
	containerIndex_t container;

	for (container = 0; container < csi.numIDs; container++) {
#if 0
		/* ignore items linked from any temp container */
		if (INVDEF(container)->temp)
			continue;
#endif
		for (ic = CONTAINER(chr, container); ic; ic = ic->next) {
			const objDef_t *obj = ic->item.t;
			B_UpdateStorageAndCapacity(sourceBase, obj, -1, qfalse, qfalse);
			B_UpdateStorageAndCapacity(destBase, obj, 1, qfalse, qfalse);

			obj = ic->item.m;
			if (obj) {
				B_UpdateStorageAndCapacity(sourceBase, obj, -1, qfalse, qfalse);
				B_UpdateStorageAndCapacity(destBase, obj, 1, qfalse, qfalse);
			}
		}
	}
}

/**
 * @brief Moves a given aircraft to a new base (also the employees and inventory)
 * @note Also checks for a working hangar
 * @param[in] aircraft The aircraft to move into a new base
 * @param[in] base The base to move the aircraft into
 */
qboolean AIR_MoveAircraftIntoNewHomebase (aircraft_t *aircraft, base_t *base)
{
	base_t *oldBase;
	baseCapacities_t capacity;
	linkedList_t* l;

	assert(aircraft);
	assert(base);
	assert(base != aircraft->homebase);

	Com_DPrintf(DEBUG_CLIENT, "AIR_MoveAircraftIntoNewHomebase: Change homebase of '%s' to '%s'\n", aircraft->id, base->name);

	/* Is aircraft being transfered? */
	if (aircraft->status == AIR_TRANSFER) {
		/* Move the aircraft to the new base to avoid fuel problems */
		VectorCopy(base->pos, aircraft->pos);
		aircraft->status = AIR_HOME;
	}

	capacity = AIR_GetCapacityByAircraftWeight(aircraft);
	if (AIR_CheckMoveIntoNewHomebase(aircraft, base, capacity))
		return qfalse;

	oldBase = aircraft->homebase;
	assert(oldBase);

	/* Transfer employees */
	E_MoveIntoNewBase(AIR_GetPilot(aircraft), base);

	for (l = aircraft->acTeam; l != NULL; l = l->next) {
		employee_t *employee = (employee_t *)l->data;
		E_MoveIntoNewBase(employee, base);
		/* Transfer items carried by soldiers from oldBase to base */
		AIR_TransferItemsCarriedByCharacterToBase(&employee->chr, oldBase, base);
	}

	/* Move aircraft to new base */
	oldBase->capacities[capacity].cur--;
	aircraft->homebase = base;
	base->capacities[capacity].cur++;

	if (oldBase->aircraftCurrent == aircraft)
		oldBase->aircraftCurrent = AIR_GetNextFromBase(oldBase, NULL);
	if (!base->aircraftCurrent)
		base->aircraftCurrent = aircraft;

	/* No need to update global IDX of every aircraft: the global IDX of this aircraft did not change */
	/* Redirect selectedAircraft */
	MAP_SelectAircraft(aircraft);

	if (aircraft->status == AIR_RETURNING) {
		/* redirect to the new base */
		AIR_AircraftReturnToBase(aircraft);
	}

	return qtrue;
}

/**
 * @brief Removes an aircraft from its base and the game.
 * @param[in] aircraft Pointer to aircraft that should be removed.
 * @note The assigned soldiers (if any) are removed/unassinged from the aircraft - not fired.
 * @sa AIR_DestroyAircraft
 * @note If you want to do something different (kill, fire, etc...) do it before calling this function.
 * @todo Return status of deletion for better error handling.
 * @note This function has the side effect, that the base aircraft number is
 * reduced by one, also the ccs.employees pointers are
 * moved to fill the gap of the removed employee. Thus pointers like acTeam in
 * the aircraft can point to wrong employees now. This has to be taken into
 * account
 */
void AIR_DeleteAircraft (aircraft_t *aircraft)
{
	int i;
	base_t *base;
	/* Check if aircraft is on geoscape while it's not destroyed yet */
	const qboolean aircraftIsOnGeoscape = AIR_IsAircraftOnGeoscape(aircraft);

	assert(aircraft);
	base = aircraft->homebase;
	assert(base);

	MAP_NotifyAircraftRemoved(aircraft);
	TR_NotifyAircraftRemoved(aircraft);

	/* Remove pilot and all soldiers from the aircraft (the employees are still hired after this). */
	AIR_RemoveEmployees(aircraft);

	/* base is NULL here because we don't want to readd this to the inventory
	 * If you want this in the inventory you really have to call these functions
	 * before you are destroying a craft */
	for (i = 0; i < MAX_AIRCRAFTSLOT; i++) {
		AII_RemoveItemFromSlot(NULL, aircraft->weapons, qfalse);
		AII_RemoveItemFromSlot(NULL, aircraft->electronics, qfalse);
	}
	AII_RemoveItemFromSlot(NULL, &aircraft->shield, qfalse);

	if (base->aircraftCurrent == aircraft)
		base->aircraftCurrent = NULL;

	AIR_Delete(base, aircraft);

	if (!AIR_BaseHasAircraft(base)) {
		Cvar_SetValue("mn_equipsoldierstate", 0);
		Cvar_Set("mn_aircraftstatus", "");
		Cvar_Set("mn_aircraftinbase", "0");
		Cvar_Set("mn_aircraftname", "");
		Cvar_Set("mn_aircraft_model", "");
	} else if (base->aircraftCurrent == NULL) {
		base->aircraftCurrent = AIR_GetNextFromBase(base, NULL);
	}

	/* also update the base menu buttons */
	Cmd_ExecuteString("base_init");

	/* update hangar capacities */
	AIR_UpdateHangarCapForAll(base);

	/* Update Radar overlay */
	if (aircraftIsOnGeoscape)
		RADAR_UpdateWholeRadarOverlay();
}

/**
 * @brief Removes an aircraft from its base and the game.
 * @param[in] aircraft Pointer to aircraft that should be removed.
 * @note aircraft and assigned soldiers (if any) are removed from game.
 * @todo Return status of deletion for better error handling.
 */
void AIR_DestroyAircraft (aircraft_t *aircraft)
{
	linkedList_t* l;
	employee_t *pilot;

	assert(aircraft);

	for (l = aircraft->acTeam; l != NULL;) {
		employee_t *employee = (employee_t *)l->data;
		l = l->next;
		E_RemoveInventoryFromStorage(employee);
		E_DeleteEmployee(employee);
	}
	/* the craft may no longer have any employees assigned */
	/* remove the pilot */
	pilot = AIR_GetPilot(aircraft);
	if (pilot && E_DeleteEmployee(pilot)) {
		AIR_SetPilot(aircraft, NULL);
	} else {
		/* This shouldn't ever happen. */
		Com_Error(ERR_DROP, "AIR_DestroyAircraft: aircraft id %s had no pilot\n", aircraft->id);
	}

	AIR_DeleteAircraft(aircraft);
}

/**
 * @brief Moves given aircraft.
 * @param[in] dt time delta
 * @param[in] aircraft Pointer to aircraft on its way.
 * @return true if the aircraft reached its destination.
 */
qboolean AIR_AircraftMakeMove (int dt, aircraft_t* aircraft)
{
	float dist;

	/* calc distance */
	aircraft->time += dt;
	aircraft->fuel -= dt;

	dist = (float) aircraft->stats[AIR_STATS_SPEED] * aircraft->time / (float)SECONDS_PER_HOUR;

	/* Check if destination reached */
	if (dist >= aircraft->route.distance * (aircraft->route.numPoints - 1)) {
		return qtrue;
	} else {
		/* calc new position */
		float frac = dist / aircraft->route.distance;
		const int p = (int) frac;
		frac -= p;
		aircraft->point = p;
		aircraft->pos[0] = (1 - frac) * aircraft->route.point[p][0] + frac * aircraft->route.point[p + 1][0];
		aircraft->pos[1] = (1 - frac) * aircraft->route.point[p][1] + frac * aircraft->route.point[p + 1][1];

		MAP_CheckPositionBoundaries(aircraft->pos);
	}

	dist = (float) aircraft->stats[AIR_STATS_SPEED] * (aircraft->time + dt) / (float)SECONDS_PER_HOUR;

	/* Now calculate the projected position. This is the position that the aircraft should get on
	 * next frame if its route didn't change in meantime. */
	if (dist >= aircraft->route.distance * (aircraft->route.numPoints - 1)) {
		VectorSet(aircraft->projectedPos, 0.0f, 0.0f, 0.0f);
	} else {
		/* calc new position */
		float frac = dist / aircraft->route.distance;
		const int p = (int) frac;
		frac -= p;
		aircraft->projectedPos[0] = (1 - frac) * aircraft->route.point[p][0] + frac * aircraft->route.point[p + 1][0];
		aircraft->projectedPos[1] = (1 - frac) * aircraft->route.point[p][1] + frac * aircraft->route.point[p + 1][1];

		MAP_CheckPositionBoundaries(aircraft->projectedPos);
	}

	return qfalse;
}

static void AIR_Move (aircraft_t* aircraft, int deltaTime)
{
	/* Aircraft is moving */
	if (AIR_AircraftMakeMove(deltaTime, aircraft)) {
		/* aircraft reach its destination */
		const float *end = aircraft->route.point[aircraft->route.numPoints - 1];
		Vector2Copy(end, aircraft->pos);
		MAP_CheckPositionBoundaries(aircraft->pos);

		switch (aircraft->status) {
		case AIR_MISSION:
			/* Aircraft reached its mission */
			assert(aircraft->mission);
			aircraft->mission->active = qtrue;
			aircraft->status = AIR_DROP;
			ccs.missionAircraft = aircraft;
			MAP_SelectMission(aircraft->mission);
			ccs.interceptAircraft = aircraft;
			CL_GameTimeStop();
			UI_PushWindow("popup_intercept_ready", NULL);
			break;
		case AIR_RETURNING:
			/* aircraft entered in homebase */
			aircraft->status = AIR_REFUEL;
			B_AircraftReturnedToHomeBase(aircraft);
			break;
		case AIR_TRANSFER:
		case AIR_UFO:
			break;
		default:
			aircraft->status = AIR_IDLE;
			break;
		}
	}
}

static void AIR_Refuel (aircraft_t *aircraft, int deltaTime)
{
	/* Aircraft is refuelling at base */
	int fillup;

	if (aircraft->fuel < 0)
		aircraft->fuel = 0;
	/* amount of fuel we would like to load */
	fillup = min(deltaTime * AIRCRAFT_REFUEL_FACTOR, aircraft->stats[AIR_STATS_FUELSIZE] - aircraft->fuel);
	/* This craft uses antimatter as fuel */
	assert(aircraft->homebase);
	if (aircraft->stats[AIR_STATS_ANTIMATTER] > 0 && fillup > 0) {
		/* the antimatter we have */
		const int amAvailable = B_ItemInBase(INVSH_GetItemByID(ANTIMATTER_TECH_ID), aircraft->homebase);
		/* current antimatter level in craft */
		const int amCurrentLevel = aircraft->stats[AIR_STATS_ANTIMATTER] * (aircraft->fuel / (float) aircraft->stats[AIR_STATS_FUELSIZE]);
		/* next antimatter level in craft */
		const int amNextLevel = aircraft->stats[AIR_STATS_ANTIMATTER] * ((aircraft->fuel + fillup) / (float) aircraft->stats[AIR_STATS_FUELSIZE]);
		/* antimatter needed */
		int amLoad = amNextLevel - amCurrentLevel;

		if (amLoad > amAvailable) {
			/* amount of fuel we can load */
			fillup = aircraft->stats[AIR_STATS_FUELSIZE] * ((amCurrentLevel + amAvailable) / (float) aircraft->stats[AIR_STATS_ANTIMATTER]) - aircraft->fuel;
			amLoad = amAvailable;

			if (!aircraft->notifySent[AIR_CANNOT_REFUEL]) {
				Com_sprintf(cp_messageBuffer, lengthof(cp_messageBuffer),
						_("Craft %s couldn't be completely refueled at %s. Not enough antimatter."), aircraft->name, aircraft->homebase->name);
				MSO_CheckAddNewMessage(NT_AIRCRAFT_CANNOTREFUEL, _("Notice"), cp_messageBuffer, qfalse, MSG_STANDARD, NULL);
				aircraft->notifySent[AIR_CANNOT_REFUEL] = qtrue;
			}
		}

		if (amLoad > 0)
			B_ManageAntimatter(aircraft->homebase, amLoad, qfalse);
	}

	aircraft->fuel += fillup;

	if (aircraft->fuel >= aircraft->stats[AIR_STATS_FUELSIZE]) {
		aircraft->fuel = aircraft->stats[AIR_STATS_FUELSIZE];
		aircraft->status = AIR_HOME;
		Com_sprintf(cp_messageBuffer, lengthof(cp_messageBuffer),
				_("Craft %s has refueled at %s."), aircraft->name, aircraft->homebase->name);
		MSO_CheckAddNewMessage(NT_AIRCRAFT_REFUELED, _("Notice"), cp_messageBuffer, qfalse, MSG_STANDARD, NULL);
		aircraft->notifySent[AIR_CANNOT_REFUEL] = qfalse;
	}

}

/**
 * @brief Handles aircraft movement and actions in geoscape mode
 * @sa CL_CampaignRun
 * @param[in] dt time delta (may be 0 if radar overlay should be updated but no aircraft moves)
 * @param[in] updateRadarOverlay True if radar overlay should be updated (not needed if geoscape isn't updated)
 */
void CL_CampaignRunAircraft (campaign_t* campaign, int dt, qboolean updateRadarOverlay)
{
	/* true if at least one aircraft moved: radar overlay must be updated
	 * This is static because aircraft can move without radar being
	 * updated (sa CL_CampaignRun) */
	static qboolean radarOverlayReset = qfalse;

	assert(dt >= 0);

	if (dt > 0) {
		base_t *base = NULL;

		while ((base = B_GetNextFounded(base)) != NULL) {
			aircraft_t *aircraft;

			/* Run each aircraft */
			aircraft = NULL;
			while ((aircraft = AIR_GetNextFromBase(base, aircraft)) != NULL) {
				int k;
				assert(aircraft->homebase);
				if (aircraft->status == AIR_IDLE) {
					/* Aircraft idle out of base */
					aircraft->fuel -= dt;
				} else if (AIR_IsAircraftOnGeoscape(aircraft)) {
					AIR_Move(aircraft, dt);
					/* radar overlay should be updated */
					radarOverlayReset = qtrue;
				} else if (aircraft->status == AIR_REFUEL) {
					AIR_Refuel(aircraft, dt);
				}

				/* Check aircraft low fuel (only if aircraft is not already returning to base or in base) */
				if (aircraft->status != AIR_RETURNING && AIR_IsAircraftOnGeoscape(aircraft) &&
					!AIR_AircraftHasEnoughFuel(aircraft, aircraft->pos)) {
					/** @todo check if aircraft can go to a closer base with free space */
					MS_AddNewMessage(_("Notice"), va(_("Craft %s is low on fuel and must return to base."), aircraft->name), qfalse, MSG_STANDARD, NULL);
					AIR_AircraftReturnToBase(aircraft);
				}

				/* Aircraft purchasing ufo */
				if (aircraft->status == AIR_UFO) {
					/* Solve the fight */
					AIRFIGHT_ExecuteActions(campaign, aircraft, aircraft->aircraftTarget);
				}

				for (k = 0; k < aircraft->maxWeapons; k++) {
					/* Update delay to launch next projectile */
					if (AIR_IsAircraftOnGeoscape(aircraft) && (aircraft->weapons[k].delayNextShot > 0))
						aircraft->weapons[k].delayNextShot -= dt;
					/* Reload if needed */
					if (aircraft->weapons[k].ammoLeft <= 0)
						AII_ReloadWeapon(&aircraft->weapons[k]);
				}
			}
		}
	}

	if (updateRadarOverlay && radarOverlayReset && MAP_IsRadarOverlayActivated()) {
		RADAR_UpdateWholeRadarOverlay();
		radarOverlayReset = qfalse;
	}
}

/**
 * @brief Returns aircraft for a given global index.
 * @param[in] aircraftIdx Global aircraft index.
 * @return An aircraft pointer (to a struct in a base) that has the given index or NULL if no aircraft found.
 */
aircraft_t* AIR_AircraftGetFromIDX (int aircraftIdx)
{
	base_t *base = NULL;
	while ((base = B_GetNextFounded(base)) != NULL) {
		aircraft_t* aircraft = NULL;
		while ((aircraft = AIR_GetNextFromBase(base, aircraft)) != NULL) {
			if (aircraft->idx == aircraftIdx) {
				Com_DPrintf(DEBUG_CLIENT, "AIR_AircraftGetFromIDX: aircraft idx: %i - base idx: %i (%s)\n",
						aircraft->idx, base->idx, base->name);
				return aircraft;
			}
		}
	}

	return NULL;
}

/**
 * @brief Sends the specified aircraft to specified mission.
 * @param[in] aircraft Pointer to aircraft to send to mission.
 * @param[in] mission Pointer to given mission.
 * @return qtrue if sending an aircraft to specified mission is possible.
 */
qboolean AIR_SendAircraftToMission (aircraft_t *aircraft, mission_t *mission)
{
	if (!aircraft || !mission)
		return qfalse;

	if (AIR_GetTeamSize(aircraft) == 0) {
		UI_Popup(_("Notice"), _("Assign one or more soldiers to this aircraft first."));
		return qfalse;
	}

	/* if aircraft was in base */
	if (AIR_IsAircraftInBase(aircraft)) {
		/* reload its ammunition */
		AII_ReloadAircraftWeapons(aircraft);
	}

	/* ensure interceptAircraft is set correctly */
	ccs.interceptAircraft = aircraft;

	/* if mission is a base-attack and aircraft already in base, launch
	 * mission immediately */
	if (B_IsUnderAttack(aircraft->homebase) && AIR_IsAircraftInBase(aircraft)) {
		aircraft->mission = mission;
		mission->active = qtrue;
		UI_PushWindow("popup_baseattack", NULL);
		return qtrue;
	}

	if (!AIR_AircraftHasEnoughFuel(aircraft, mission->pos)) {
		MS_AddNewMessage(_("Notice"), _("Insufficient fuel."), qfalse, MSG_STANDARD, NULL);
		return qfalse;
	}

	MAP_MapCalcLine(aircraft->pos, mission->pos, &aircraft->route);
	aircraft->status = AIR_MISSION;
	aircraft->time = 0;
	aircraft->point = 0;
	aircraft->mission = mission;

	return qtrue;
}

/**
 * @brief Initialise all values of an aircraft slot.
 * @param[in] aircraftTemplate Pointer to the aircraft which needs initalisation of its slots.
 */
static void AII_InitialiseAircraftSlots (aircraft_t *aircraftTemplate)
{
	int i;

	/* initialise weapon slots */
	for (i = 0; i < MAX_AIRCRAFTSLOT; i++) {
		AII_InitialiseSlot(aircraftTemplate->weapons + i, aircraftTemplate, NULL, NULL, AC_ITEM_WEAPON);
		AII_InitialiseSlot(aircraftTemplate->electronics + i, aircraftTemplate, NULL, NULL, AC_ITEM_ELECTRONICS);
	}
	AII_InitialiseSlot(&aircraftTemplate->shield, aircraftTemplate, NULL, NULL, AC_ITEM_SHIELD);
}

/**
 * @brief List of valid strings for itemPos_t
 * @note must be in the same order than @c itemPos_t
 */
static const char *air_position_strings[AIR_POSITIONS_MAX] = {
	"nose_left",
	"nose_center",
	"nose_right",
	"wing_left",
	"wing_right",
	"rear_left",
	"rear_center",
	"rear_right"
};

/** @brief Valid aircraft parameter definitions from script files.
 * @note wrange can't be parsed in aircraft definition: this is a property
 * of weapon */
static const value_t aircraft_param_vals[] = {
	{"speed", V_INT, offsetof(aircraft_t, stats[AIR_STATS_SPEED]), MEMBER_SIZEOF(aircraft_t, stats[0])},
	{"maxspeed", V_INT, offsetof(aircraft_t, stats[AIR_STATS_MAXSPEED]), MEMBER_SIZEOF(aircraft_t, stats[0])},
	{"shield", V_INT, offsetof(aircraft_t, stats[AIR_STATS_SHIELD]), MEMBER_SIZEOF(aircraft_t, stats[0])},
	{"ecm", V_INT, offsetof(aircraft_t, stats[AIR_STATS_ECM]), MEMBER_SIZEOF(aircraft_t, stats[0])},
	{"damage", V_INT, offsetof(aircraft_t, stats[AIR_STATS_DAMAGE]), MEMBER_SIZEOF(aircraft_t, stats[0])},
	{"accuracy", V_INT, offsetof(aircraft_t, stats[AIR_STATS_ACCURACY]), MEMBER_SIZEOF(aircraft_t, stats[0])},
	{"antimatter", V_INT, offsetof(aircraft_t, stats[AIR_STATS_ANTIMATTER]), MEMBER_SIZEOF(aircraft_t, stats[0])},

	{NULL, 0, 0, 0}
};

/** @brief Valid aircraft definition values from script files. */
static const value_t aircraft_vals[] = {
	{"name", V_STRING, offsetof(aircraft_t, name), 0},
	{"defaultname", V_TRANSLATION_STRING, offsetof(aircraft_t, defaultName), 0},
	{"numteam", V_INT, offsetof(aircraft_t, maxTeamSize), MEMBER_SIZEOF(aircraft_t, maxTeamSize)},
	{"size", V_INT, offsetof(aircraft_t, size), MEMBER_SIZEOF(aircraft_t, size)},
	{"nogeoscape", V_BOOL, offsetof(aircraft_t, notOnGeoscape), MEMBER_SIZEOF(aircraft_t, notOnGeoscape)},
	{"interestlevel", V_INT, offsetof(aircraft_t, ufoInterestOnGeoscape), MEMBER_SIZEOF(aircraft_t, ufoInterestOnGeoscape)},

	{"image", V_CLIENT_HUNK_STRING, offsetof(aircraft_t, image), 0},
	{"model", V_CLIENT_HUNK_STRING, offsetof(aircraft_t, model), 0},
	/* price for selling/buying */
	{"price", V_INT, offsetof(aircraft_t, price), MEMBER_SIZEOF(aircraft_t, price)},
	/* this is needed to let the buy and sell screen look for the needed building */
	/* to place the aircraft in */
	{"building", V_CLIENT_HUNK_STRING, offsetof(aircraft_t, building), 0},

	{NULL, 0, 0, 0}
};

/**
 * @brief Parses all aircraft that are defined in our UFO-scripts.
 * @sa CL_ParseClientData
 * @sa CL_ParseScriptSecond
 * @note parses the aircraft into our aircraft_sample array to use as reference
 * @note This parsing function writes into two different memory pools
 * one is the cp_campaignPool which is cleared on every new game, the other is
 * cl_genericPool which is existent until you close the game
 */
void AIR_ParseAircraft (const char *name, const char **text, qboolean assignAircraftItems)
{
	const char *errhead = "AIR_ParseAircraft: unexpected end of file (aircraft ";
	aircraft_t *aircraftTemplate;
	const value_t *vp;
	const char *token;
	int i;
	technology_t *tech;
	aircraftItemType_t itemType = MAX_ACITEMS;

	if (ccs.numAircraftTemplates >= MAX_AIRCRAFT) {
		Com_Printf("AIR_ParseAircraft: too many aircraft definitions; def \"%s\" ignored\n", name);
		return;
	}

	if (!assignAircraftItems) {
		aircraftTemplate = NULL;
		for (i = 0; i < ccs.numAircraftTemplates; i++) {
			aircraft_t *aircraft = &ccs.aircraftTemplates[i];
			if (!strcmp(aircraft->id, name)) {
				aircraftTemplate = aircraft;
				break;
			}
		}

		if (aircraftTemplate) {
			Com_Printf("AIR_ParseAircraft: Second aircraft with same name found (%s) - second ignored\n", name);
			return;
		}

		/* initialize the menu */
		aircraftTemplate = &ccs.aircraftTemplates[ccs.numAircraftTemplates];
		memset(aircraftTemplate, 0, sizeof(*aircraftTemplate));

		Com_DPrintf(DEBUG_CLIENT, "...found aircraft %s\n", name);
		aircraftTemplate->tpl = aircraftTemplate;
		aircraftTemplate->id = Mem_PoolStrDup(name, cl_genericPool, 0);
		aircraftTemplate->status = AIR_HOME;
		/* default is no ufo */
		aircraftTemplate->ufotype = UFO_MAX;
		aircraftTemplate->maxWeapons = 0;
		aircraftTemplate->maxElectronics = 0;
		AII_InitialiseAircraftSlots(aircraftTemplate);
		/* Initialise UFO sensored */
		RADAR_InitialiseUFOs(&aircraftTemplate->radar);

		ccs.numAircraftTemplates++;
	} else {
		aircraftTemplate = NULL;
		for (i = 0; i < ccs.numAircraftTemplates; i++) {
			aircraft_t *aircraft = &ccs.aircraftTemplates[i];
			if (!strcmp(aircraft->id, name)) {
				aircraftTemplate = aircraft;
				break;
			}
		}
		if (!aircraftTemplate)
			Sys_Error("Could not find aircraft '%s'", name);
	}

	/* get it's body */
	token = Com_Parse(text);

	if (!*text || *token != '{') {
		Com_Printf("AIR_ParseAircraft: aircraft def \"%s\" without body ignored\n", name);
		return;
	}

	do {
		token = Com_EParse(text, errhead, name);
		if (!*text)
			break;
		if (*token == '}')
			break;

		if (!strcmp(token, "name")) {
			token = Com_EParse(text, errhead, name);
			if (!*text)
				return;
			if (token[0] == '_')
				token++;
			Q_strncpyz(aircraftTemplate->name, token, sizeof(aircraftTemplate->name));
			continue;
		}
		if (assignAircraftItems) {
			/* write into cp_campaignPool - this data is reparsed on every new game */
			/* blocks like param { [..] } - otherwise we would leave the loop too early */
			if (*token == '{') {
				FS_SkipBlock(text);
			} else if (!strcmp(token, "shield")) {
				token = Com_EParse(text, errhead, name);
				if (!*text)
					return;
				Com_DPrintf(DEBUG_CLIENT, "use shield %s for aircraft %s\n", token, aircraftTemplate->id);
				tech = RS_GetTechByID(token);
				if (tech)
					aircraftTemplate->shield.item = INVSH_GetItemByID(tech->provides);
			} else if (!strcmp(token, "slot")) {
				token = Com_EParse(text, errhead, name);
				if (!*text || *token != '{') {
					Com_Printf("AIR_ParseAircraft: Invalid slot value for aircraft: %s\n", name);
					return;
				}
				do {
					token = Com_EParse(text, errhead, name);
					if (!*text)
						break;
					if (*token == '}')
						break;

					if (!strcmp(token, "type")) {
						token = Com_EParse(text, errhead, name);
						if (!*text)
							return;
						for (i = 0; i < MAX_ACITEMS; i++) {
							if (!strcmp(token, air_slot_type_strings[i])) {
								itemType = i;
								switch (itemType) {
								case AC_ITEM_WEAPON:
									aircraftTemplate->maxWeapons++;
									break;
								case AC_ITEM_ELECTRONICS:
									aircraftTemplate->maxElectronics++;
									break;
								default:
									itemType = MAX_ACITEMS;
								}
								break;
							}
						}
						if (i == MAX_ACITEMS)
							Com_Error(ERR_DROP, "Unknown value '%s' for slot type\n", token);
					} else if (!strcmp(token, "position")) {
						token = Com_EParse(text, errhead, name);
						if (!*text)
							return;
						for (i = 0; i < AIR_POSITIONS_MAX; i++) {
							if (!strcmp(token, air_position_strings[i])) {
								switch (itemType) {
								case AC_ITEM_WEAPON:
									aircraftTemplate->weapons[aircraftTemplate->maxWeapons - 1].pos = i;
									break;
								case AC_ITEM_ELECTRONICS:
									aircraftTemplate->electronics[aircraftTemplate->maxElectronics - 1].pos = i;
									break;
								default:
									i = AIR_POSITIONS_MAX;
								}
								break;
							}
						}
						if (i == AIR_POSITIONS_MAX)
							Com_Error(ERR_DROP, "Unknown value '%s' for slot position\n", token);
					} else if (!strcmp(token, "contains")) {
						token = Com_EParse(text, errhead, name);
						if (!*text)
							return;
						tech = RS_GetTechByID(token);
						if (tech) {
							switch (itemType) {
							case AC_ITEM_WEAPON:
								aircraftTemplate->weapons[aircraftTemplate->maxWeapons - 1].item = INVSH_GetItemByID(tech->provides);
								Com_DPrintf(DEBUG_CLIENT, "use weapon %s for aircraft %s\n", token, aircraftTemplate->id);
								break;
							case AC_ITEM_ELECTRONICS:
								aircraftTemplate->electronics[aircraftTemplate->maxElectronics - 1].item = INVSH_GetItemByID(tech->provides);
								Com_DPrintf(DEBUG_CLIENT, "use electronics %s for aircraft %s\n", token, aircraftTemplate->id);
								break;
							default:
								Com_Printf("Ignoring item value '%s' due to unknown slot type\n", token);
							}
						}
					} else if (!strcmp(token, "ammo")) {
						token = Com_EParse(text, errhead, name);
						if (!*text)
							return;
						tech = RS_GetTechByID(token);
						if (tech) {
							if (itemType == AC_ITEM_WEAPON) {
								aircraftTemplate->weapons[aircraftTemplate->maxWeapons - 1].ammo = INVSH_GetItemByID(tech->provides);
								Com_DPrintf(DEBUG_CLIENT, "use ammo %s for aircraft %s\n", token, aircraftTemplate->id);
							} else
								Com_Printf("Ignoring ammo value '%s' due to unknown slot type\n", token);
						}
					} else if (!strcmp(token, "size")) {
						token = Com_EParse(text, errhead, name);
						if (!*text)
							return;
						if (itemType == AC_ITEM_WEAPON) {
							if (!strcmp(token, "light"))
								aircraftTemplate->weapons[aircraftTemplate->maxWeapons - 1].size = ITEM_LIGHT;
							else if (!strcmp(token, "medium"))
								aircraftTemplate->weapons[aircraftTemplate->maxWeapons - 1].size = ITEM_MEDIUM;
							else if (!strcmp(token, "heavy"))
								aircraftTemplate->weapons[aircraftTemplate->maxWeapons - 1].size = ITEM_HEAVY;
							else
								Com_Printf("Unknown size value for aircraft slot: '%s'\n", token);
						} else
							Com_Printf("Ignoring size parameter '%s' for non-weapon aircraft slots\n", token);
					} else
						Com_Printf("AIR_ParseAircraft: Ignoring unknown slot value '%s'\n", token);
				} while (*text); /* dummy condition */
			}
		} else {
			if (!strcmp(token, "shield")) {
				Com_EParse(text, errhead, name);
				continue;
			}
			/* check for some standard values */
			for (vp = aircraft_vals; vp->string; vp++)
				if (!strcmp(token, vp->string)) {
					/* found a definition */
					token = Com_EParse(text, errhead, name);
					if (!*text)
						return;
					switch (vp->type) {
					case V_TRANSLATION_STRING:
						token++;
					case V_CLIENT_HUNK_STRING:
						Mem_PoolStrDupTo(token, (char**) ((char*)aircraftTemplate + (int)vp->ofs), cl_genericPool, 0);
						break;
					default:
						Com_EParseValue(aircraftTemplate, token, vp->type, vp->ofs, vp->size);
					}

					break;
				}

			if (!strcmp(token, "type")) {
				token = Com_EParse(text, errhead, name);
				if (!*text)
					return;
				if (!strcmp(token, "transporter"))
					aircraftTemplate->type = AIRCRAFT_TRANSPORTER;
				else if (!strcmp(token, "interceptor"))
					aircraftTemplate->type = AIRCRAFT_INTERCEPTOR;
				else if (!strcmp(token, "ufo")) {
					aircraftTemplate->type = AIRCRAFT_UFO;
					aircraftTemplate->ufotype = Com_UFOShortNameToID(aircraftTemplate->id);
				}
			} else if (!strcmp(token, "slot")) {
				token = Com_EParse(text, errhead, name);
				if (!*text || *token != '{') {
					Com_Printf("AIR_ParseAircraft: Invalid slot value for aircraft: %s\n", name);
					return;
				}
				FS_SkipBlock(text);
			} else if (!strcmp(token, "param")) {
				token = Com_EParse(text, errhead, name);
				if (!*text || *token != '{') {
					Com_Printf("AIR_ParseAircraft: Invalid param value for aircraft: %s\n", name);
					return;
				}
				do {
					token = Com_EParse(text, errhead, name);
					if (!*text)
						break;
					if (*token == '}')
						break;

					if (!strcmp(token, "range")) {
						/* this is the range of aircraft, must be translated into fuel */
						token = Com_EParse(text, errhead, name);
						if (!*text)
							return;
						Com_EParseValue(aircraftTemplate, token, V_INT, offsetof(aircraft_t, stats[AIR_STATS_FUELSIZE]), MEMBER_SIZEOF(aircraft_t, stats[0]));
						if (aircraftTemplate->stats[AIR_STATS_SPEED] == 0)
							Com_Error(ERR_DROP, "AIR_ParseAircraft: speed value must be entered before range value");
						aircraftTemplate->stats[AIR_STATS_FUELSIZE] = (int) (2.0f * (float)SECONDS_PER_HOUR * aircraftTemplate->stats[AIR_STATS_FUELSIZE]) /
							((float) aircraftTemplate->stats[AIR_STATS_SPEED]);
					} else {
						for (vp = aircraft_param_vals; vp->string; vp++)
							if (!strcmp(token, vp->string)) {
								/* found a definition */
								token = Com_EParse(text, errhead, name);
								if (!*text)
									return;
								switch (vp->type) {
								case V_TRANSLATION_STRING:
									token++;
								case V_CLIENT_HUNK_STRING:
									Mem_PoolStrDupTo(token, (char**) ((char*)aircraftTemplate + (int)vp->ofs), cl_genericPool, 0);
									break;
								default:
									Com_EParseValue(aircraftTemplate, token, vp->type, vp->ofs, vp->size);
								}
								break;
							}
					}
					if (!vp->string)
						Com_Printf("AIR_ParseAircraft: Ignoring unknown param value '%s'\n", token);
				} while (*text); /* dummy condition */
			} else if (!vp->string) {
				Com_Printf("AIR_ParseAircraft: unknown token \"%s\" ignored (aircraft %s)\n", token, name);
				Com_EParse(text, errhead, name);
			}
		} /* assignAircraftItems */
	} while (*text);

	if (aircraftTemplate->size < AIRCRAFT_SMALL || aircraftTemplate->size > AIRCRAFT_LARGE)
		Sys_Error("Invalid aircraft size given for '%s'", aircraftTemplate->id);
}

#ifdef DEBUG
void AIR_ListCraftIndexes_f (void)
{
	int i;

	Com_Printf("Base\tlocalIDX\t(Craftname)\n");
	for (i = 0; i < ccs.numBases; i++) {
		const base_t *base = B_GetBaseByIDX(i);
		aircraft_t *aircraft;

		aircraft = NULL;
		while ((aircraft = AIR_GetNextFromBase(base, aircraft)) != NULL) {
			Com_Printf("%i (%s)\t%i\t(%s)\n", i, base->name, aircraft->idx, aircraft->name);
		}
	}
}

/**
 * @brief Debug function that prints aircraft to game console
 */
void AIR_ListAircraftSamples_f (void)
{
	int i = 0, max = ccs.numAircraftTemplates;
	const value_t *vp;

	Com_Printf("%i aircraft\n", max);
	if (Cmd_Argc() == 2) {
		max = atoi(Cmd_Argv(1));
		if (max >= ccs.numAircraftTemplates || max < 0)
			return;
		i = max - 1;
	}
	for (; i < max; i++) {
		aircraft_t *aircraftTemplate = &ccs.aircraftTemplates[i];
		Com_Printf("aircraft: '%s'\n", aircraftTemplate->id);
		for (vp = aircraft_vals; vp->string; vp++) {
			Com_Printf("..%s: %s\n", vp->string, Com_ValueToStr(aircraftTemplate, vp->type, vp->ofs));
		}
		for (vp = aircraft_param_vals; vp->string; vp++) {
			Com_Printf("..%s: %s\n", vp->string, Com_ValueToStr(aircraftTemplate, vp->type, vp->ofs));
		}
	}
}
#endif

/*===============================================
Aircraft functions related to UFOs or missions.
===============================================*/

/**
 * @brief Notify that a mission has been removed.
 * @note Aircraft currently moving to the mission will be redirect to base
 * @param[in] mission Pointer to the mission that has been removed.
 */
void AIR_AircraftsNotifyMissionRemoved (const mission_t *const mission)
{
	base_t *base = NULL;
	while ((base = B_GetNextFounded(base)) != NULL) {
		aircraft_t* aircraft = NULL;
		while ((aircraft = AIR_GetNextFromBase(base, aircraft)) != NULL) {
			if (aircraft->status == AIR_MISSION && aircraft->mission == mission)
				AIR_AircraftReturnToBase(aircraft);
		}
	}
}

/**
 * @brief Notify that a UFO has been removed.
 * @param[in] ufo Pointer to UFO that has been removed.
 * @param[in] destroyed True if the UFO has been destroyed, false if it only landed.
 */
void AIR_AircraftsNotifyUFORemoved (const aircraft_t *const ufo, qboolean destroyed)
{
	base_t *base;

	assert(ufo);

	base = NULL;
	while ((base = B_GetNextFounded(base)) != NULL) {
		aircraft_t* aircraft;
		int i;
		/* Base currently targeting the specified ufo loose their target */
		for (i = 0; i < base->numBatteries; i++) {
			baseWeapon_t *baseWeapon = &base->batteries[i];
			if (baseWeapon->target == ufo)
				baseWeapon->target = NULL;
			else if (destroyed && (baseWeapon->target > ufo))
				baseWeapon->target--;
		}
		for (i = 0; i < base->numLasers; i++) {
			baseWeapon_t *baseWeapon = &base->lasers[i];
			if (baseWeapon->target == ufo)
				baseWeapon->target = NULL;
			else if (destroyed && (baseWeapon->target > ufo))
				baseWeapon->target--;
		}
		/* Aircraft currently purchasing the specified ufo will be redirect to base */
		aircraft = NULL;
		while ((aircraft = AIR_GetNextFromBase(base, aircraft)) != NULL)
			if (aircraft->status == AIR_UFO) {
				if (ufo == aircraft->aircraftTarget)
					AIR_AircraftReturnToBase(aircraft);
				else if (destroyed && (ufo < aircraft->aircraftTarget)) {
					aircraft->aircraftTarget--;
				}
			}
	}
}

/**
 * @brief Notify that a UFO disappear from radars.
 * @note Aircraft currently pursuing the specified UFO will be redirected to base
 * @param[in] ufo Pointer to a UFO that has disappeared.
 */
void AIR_AircraftsUFODisappear (const aircraft_t *const ufo)
{
	base_t *base = NULL;
	while ((base = B_GetNextFounded(base)) != NULL) {
		aircraft_t *aircraft = NULL;
		while ((aircraft = AIR_GetNextFromBase(base, aircraft)) != NULL) {
			if (aircraft->status == AIR_UFO)
				if (ufo == aircraft->aircraftTarget)
					AIR_AircraftReturnToBase(aircraft);
		}
	}
}

/**
 * @brief funtion we need to find roots.
 * @param[in] c angle SOT
 * @param[in] B angle STI
 * @param[in] speedRatio ratio of speed of shooter divided by speed of target.
 * @param[in] a angle TOI
 * @note S is the position of the shooter, T the position of the target,
 * D the destination of the target and I the interception point where shooter should reach target.
 * @return value of the function.
 */
static inline float AIR_GetDestinationFunction (const float c, const float B, const float speedRatio, float a)
{
	return pow(cos(a) - cos(speedRatio * a) * cos(c), 2.)
		- sin(c) * sin(c) * (sin(speedRatio * a) * sin(speedRatio * a) - sin(a) * sin(a) * sin(B) * sin(B));
}

/**
 * @brief derivative of the funtion we need to find roots.
 * @param[in] c angle SOT
 * @param[in] B angle STI
 * @param[in] speedRatio ratio of speed of shooter divided by speed of target.
 * @param[in] a angle TOI
 * @note S is the position of the shooter, T the position of the target,
 * D the destination of the target and I the interception point where shooter should reach target.
 * @return value of the derivative function.
 */
static inline float AIR_GetDestinationDerivativeFunction (const float c, const float B, const float speedRatio, float a)
{
	return 2. * (cos(a) - cos(speedRatio * a) * cos(c)) * (- sin(a) + speedRatio * sin(speedRatio * a) * cos(c))
		- sin(c) * sin(c) * (speedRatio * sin(2. * speedRatio * a) - sin(2. * a) * sin(B) * sin(B));
}

/**
 * @brief Find the roots of a function.
 * @param[in] c angle SOT
 * @param[in] B angle STI
 * @param[in] speedRatio ratio of speed of shooter divided by speed of target.
 * @param[in] start minimum value of the root to find
 * @note S is the position of the shooter, T the position of the target,
 * D the destination of the target and I the interception point where shooter should reach target.
 * @return root of the function.
 */
static float AIR_GetDestinationFindRoot (const float c, const float B, const float speedRatio, float start)
{
	const float BIG_STEP = .05;				/**< step for rough calculation. this value must be short enough so
											 * that we're sure there's only 1 root in this range. */
	const float PRECISION_ROOT = 0.000001;		/**< precision of the calculation */
	const float MAXIMUM_VALUE_ROOT = 2. * M_PI;	/**< maximum value of the root to search */
	float epsilon;							/**< precision of current point */
	float begin, end, middle;				/**< abscissa of the point */
	float fBegin, fEnd, fMiddle;			/**< ordinate of the point */
	float fdBegin, fdEnd, fdMiddle;			/**< derivative of the point */

	/* there may be several solution, first try to find roughly the smallest one */
	end = start + PRECISION_ROOT / 10.;		/* don't start at 0: derivative is 0 */
	fEnd = AIR_GetDestinationFunction(c, B, speedRatio, end);
	fdEnd = AIR_GetDestinationDerivativeFunction(c, B, speedRatio, end);

	do {
		begin = end;
		fBegin = fEnd;
		fdBegin = fdEnd;
		end = begin + BIG_STEP;
		if (end > MAXIMUM_VALUE_ROOT) {
			end = MAXIMUM_VALUE_ROOT;
			fEnd = AIR_GetDestinationFunction(c, B, speedRatio, end);
			break;
		}
		fEnd = AIR_GetDestinationFunction(c, B, speedRatio, end);
		fdEnd = AIR_GetDestinationDerivativeFunction(c, B, speedRatio, end);
	} while  (fBegin * fEnd > 0 && fdBegin * fdEnd > 0);

	if (fBegin * fEnd > 0) {
		if (fdBegin * fdEnd < 0) {
			/* the sign of derivative changed: we could have a root somewhere
			 * between begin and end: try to narrow down the root until fBegin * fEnd < 0 */
			middle = (begin + end) / 2.;
			fMiddle = AIR_GetDestinationFunction(c, B, speedRatio, middle);
			fdMiddle = AIR_GetDestinationDerivativeFunction(c, B, speedRatio, middle);
			do {
				if (fdEnd * fdMiddle < 0) {
					/* root is bigger than middle */
					begin = middle;
					fBegin = fMiddle;
					fdBegin = fdMiddle;
				} else if (fdBegin * fdMiddle < 0) {
					/* root is smaller than middle */
					end = middle;
					fEnd = fMiddle;
					fdEnd = fdMiddle;
				} else {
					Com_Error(ERR_DROP, "AIR_GetDestinationFindRoot: Error in calculation, can't find root");
				}
				middle = (begin + end) / 2.;
				fMiddle = AIR_GetDestinationFunction(c, B, speedRatio, middle);
				fdMiddle = AIR_GetDestinationDerivativeFunction(c, B, speedRatio, middle);

				epsilon = end - middle ;

				if (epsilon < PRECISION_ROOT) {
					/* this is only a root of the derivative: no root of the function itself
					 * proceed with next value */
					return AIR_GetDestinationFindRoot(c, B, speedRatio, end);
				}
			} while  (fBegin * fEnd > 0);
		} else {
			/* there's no solution, return default value */
			Com_DPrintf(DEBUG_CLIENT, "AIR_GetDestinationFindRoot: Did not find solution is range %.2f, %.2f\n", start, MAXIMUM_VALUE_ROOT);
			return -10.;
		}
	}

	/* now use dichotomy to get more precision on the solution */

	middle = (begin + end) / 2.;
	fMiddle = AIR_GetDestinationFunction(c, B, speedRatio, middle);

	do {
		if (fEnd * fMiddle < 0) {
			/* root is bigger than middle */
			begin = middle;
			fBegin = fMiddle;
		} else if (fBegin * fMiddle < 0) {
			/* root is smaller than middle */
			end = middle;
			fEnd = fMiddle;
		} else {
			Com_DPrintf(DEBUG_CLIENT, "AIR_GetDestinationFindRoot: Error in calculation, one of the value is nan\n");
			return -10.;
		}
		middle = (begin + end) / 2.;
		fMiddle = AIR_GetDestinationFunction(c, B, speedRatio, middle);

		epsilon = end - middle ;
	} while (epsilon > PRECISION_ROOT);
	return middle;
}

/**
 * @brief Calculates the point where aircraft should go to intecept a moving target.
 * @param[in] shooter Pointer to shooting aircraft.
 * @param[in] target Pointer to target aircraft.
 * @param[out] dest Destination that shooting aircraft should aim to intercept target aircraft.
 * @todo only compute this calculation every time target changes destination, or one of the aircraft speed changes.
 * @sa AIR_SendAircraftPursuingUFO
 * @sa UFO_SendPursuingAircraft
 */
void AIR_GetDestinationWhilePursuing (const aircraft_t const *shooter, const aircraft_t const *target, vec2_t *dest)
{
	vec3_t shooterPos, targetPos, targetDestPos, shooterDestPos, rotationAxis;
	vec3_t tangentVectTS, tangentVectTD;
	float a, b, c, B;

	const float speedRatio = (float)(shooter->stats[AIR_STATS_SPEED]) / target->stats[AIR_STATS_SPEED];

	c = GetDistanceOnGlobe(shooter->pos, target->pos) * torad;

	/* Convert aircraft position into cartesian frame */
	PolarToVec(shooter->pos, shooterPos);
	PolarToVec(target->pos, targetPos);
	PolarToVec(target->route.point[target->route.numPoints - 1], targetDestPos);

	/** In the following, we note S the position of the shooter, T the position of the target,
	 * D the destination of the target and I the interception point where shooter should reach target
	 * O is the center of earth.
	 * A, B and C are the angles TSI, STI and SIT
	 * a, b, and c are the angles TOI, SOI and SOT
	 *
	 * According to geometry on a sphere, the values defined above must be solutions of both equations:
	 *		sin(A) / sin(a) = sin(B) / sin(b)
	 *		cos(a) = cos(b) * cos(c) + sin(b) * sin(c) * cos(A)
	 * And we have another equation, given by the fact that shooter and target must reach I at the same time:
	 *		shooterSpeed * a = targetSpeed * b
	 * We the want to find and equation linking a, c and B (we know the last 2 values). We therefore
	 * eliminate b, then A, to get the equation we need to solve:
	 *		pow(cos(a) - cos(speedRatio * a) * cos(c), 2.)
	 *		- sin(c) * sin(c) * (sin(speedRatio * a) * sin(speedRatio * a) - sin(a) * sin(a) * sin(B) * sin(B)) = 0
	 */

	/* Get first vector (tangent to triangle in T, in the direction of D) */
	CrossProduct(targetPos, shooterPos, rotationAxis);
	VectorNormalize(rotationAxis);
	RotatePointAroundVector(tangentVectTS, rotationAxis, targetPos, 90.0f);
	/* Get second vector (tangent to triangle in T, in the direction of S) */
	CrossProduct(targetPos, targetDestPos, rotationAxis);
	VectorNormalize(rotationAxis);
	RotatePointAroundVector(tangentVectTD, rotationAxis, targetPos, 90.0f);

	/* Get angle B of the triangle (in radian) */
	B = acos(DotProduct(tangentVectTS, tangentVectTD));

	/* Look for a value, as long as we don't have a proper value */
	for (a = 0;;) {
		a = AIR_GetDestinationFindRoot(c, B, speedRatio, a);

		if (a < 0.) {
			/* we couldn't find a root on the whole range */
			break;
		}

		/* Get rotation vector */
		CrossProduct(targetPos, targetDestPos, rotationAxis);
		VectorNormalize(rotationAxis);

		/* Rotate target position of dist to find destination point */
		RotatePointAroundVector(shooterDestPos, rotationAxis, targetPos, a * todeg);
		VecToPolar(shooterDestPos, *dest);

		b = GetDistanceOnGlobe(shooter->pos, *dest) * torad;

		if (fabs(b - speedRatio * a) < .1)
			break;

		Com_DPrintf(DEBUG_CLIENT, "AIR_GetDestinationWhilePursuing: reject solution: doesn't fit %.2f == %.2f\n", b, speedRatio * a);
	}

	if (a < 0.) {
		/* did not find solution, go directly to target direction */
		Vector2Copy(target->pos, (*dest));
		return;
	}

	/** @todo add EQUAL_EPSILON here? */
	/* make sure we don't get a NAN value */
	assert((*dest)[0] <= 180.0f && (*dest)[0] >= -180.0f && (*dest)[1] <= 90.0f && (*dest)[1] >= -90.0f);
}

/**
 * @brief Make the specified aircraft purchasing a UFO.
 * @param[in] aircraft Pointer to an aircraft which will hunt for a UFO.
 * @param[in] ufo Pointer to a UFO.
 */
qboolean AIR_SendAircraftPursuingUFO (aircraft_t* aircraft, aircraft_t* ufo)
{
	vec2_t dest;

	if (!aircraft)
		return qfalse;

	/* if aircraft was in base */
	if (AIR_IsAircraftInBase(aircraft)) {
		/* reload its ammunition */
		AII_ReloadAircraftWeapons(aircraft);
	}

	AIR_GetDestinationWhilePursuing(aircraft, ufo, &dest);
	/* check if aircraft has enough fuel */
	if (!AIR_AircraftHasEnoughFuel(aircraft, dest)) {
		/* did not find solution, go directly to target direction if enough fuel */
		if (AIR_AircraftHasEnoughFuel(aircraft, ufo->pos)) {
			Com_DPrintf(DEBUG_CLIENT, "AIR_SendAircraftPursuingUFO: not enough fuel to anticipate target movement: go directly to target position\n");
			Vector2Copy(ufo->pos, dest);
		} else {
			MS_AddNewMessage(_("Notice"), va(_("Craft %s has not enough fuel to intercept UFO: fly back to %s."), aircraft->name, aircraft->homebase->name), qfalse, MSG_STANDARD, NULL);
			AIR_AircraftReturnToBase(aircraft);
			return qfalse;
		}
	}

	MAP_MapCalcLine(aircraft->pos, dest, &aircraft->route);
	aircraft->status = AIR_UFO;
	aircraft->time = 0;
	aircraft->point = 0;
	aircraft->aircraftTarget = ufo;
	return qtrue;
}

/*============================================
Aircraft functions related to team handling.
============================================*/

/**
 * @brief Resets team in given aircraft.
 * @param[in] aircraft Pointer to an aircraft, where the team will be reset.
 */
void AIR_ResetAircraftTeam (aircraft_t *aircraft)
{
	LIST_Delete(&aircraft->acTeam);
}

/**
 * @brief Adds given employee to given aircraft.
 * @param[in] aircraft Pointer to an aircraft, to which we will add employee.
 * @param[in] employee The employee to add to the aircraft.
 * @note this is responsible for adding soldiers to a team in dropship
 */
qboolean AIR_AddToAircraftTeam (aircraft_t *aircraft, employee_t* employee)
{
	if (!employee)
		return qfalse;

	if (!aircraft)
		return qfalse;

	if (AIR_GetTeamSize(aircraft) < aircraft->maxTeamSize) {
		LIST_AddPointer(&aircraft->acTeam, employee);
		return qtrue;
	}

	return qfalse;
}

/**
 * @brief Removes given employee from given aircraft team.
 * @param[in] aircraft The aircraft to remove the employee from.
 * @param[in] employee The employee to remove from the team.
 * @note This is responsible for removing soldiers from a team in a dropship.
 */
qboolean AIR_RemoveFromAircraftTeam (aircraft_t *aircraft, const employee_t *employee)
{
	linkedList_t* l;

	assert(aircraft);
	assert(employee);

	if (AIR_GetTeamSize(aircraft) == 0)
		return qfalse;

	for (l = aircraft->acTeam; l != NULL; l = l->next) {
		const employee_t const *employeeInCraft = (const employee_t *)l->data;
		/* Search for this exact employee in the aircraft and remove him from the team. */
		if (employeeInCraft == employee) {
			LIST_RemoveEntry(&aircraft->acTeam, l);
			return qtrue;
		}
	}

	return qfalse;
}

/**
 * @brief Checks whether given employee is in given aircraft
 * @param[in] aircraft The aircraft to check
 * @param[in] employee Employee to check.
 * @return @c true if the given employee is assigned to the given aircraft.
 */
qboolean AIR_IsInAircraftTeam (const aircraft_t *aircraft, const employee_t *employee)
{
	if (!aircraft)
		return qfalse;

	if (!employee)
		return qfalse;

	return LIST_GetPointer(aircraft->acTeam, employee) != NULL;
}

/**
 * @brief Counts the number of soldiers in given aircraft.
 * @param[in] aircraft Pointer to the aircraft, for which we return the amount of soldiers.
 * @return Amount of soldiers.
 */
int AIR_GetTeamSize (const aircraft_t *aircraft)
{
	assert(aircraft);
	return LIST_Count(aircraft->acTeam);
}

/**
 * @brief Assign a pilot to an aircraft
 * @param[out] aircraft Pointer to the aircraft to add pilot to
 * @param[in] pilot Pointer to the pilot to add
 * @return @c true if the assignment was successful (there wasn't a pilot
 * assigned), @c false if there was already a pilot assigned and we tried
 * to assign a new one (@c pilot isn't @c NULL).
 */
qboolean AIR_SetPilot (aircraft_t *aircraft, employee_t *pilot)
{
	if (aircraft->pilot == NULL || pilot == NULL) {
		aircraft->pilot = pilot;
		return qtrue;
	}

	return qfalse;
}

/**
 * @brief Get pilot of an aircraft
 * @param[in] aircraft Pointer to the aircraft
 * @return @c NULL if there is no pilot assigned to this craft, the employee pointer otherwise
 */
employee_t* AIR_GetPilot (const aircraft_t *aircraft)
{
	const employee_t *e = aircraft->pilot;

	if (!e)
		return NULL;

	return E_GetEmployee(e->baseHired, e->type, e->idx);
}

/**
 * @brief Adds the pilot to the first available aircraft at the specified base.
 * @param[in] base Which base has aircraft to add the pilot to.
 * @param[in] pilot Which pilot to add.
 */
void AIR_AutoAddPilotToAircraft (const base_t* base, employee_t* pilot)
{
	aircraft_t *aircraft;

	aircraft = NULL;
	while ((aircraft = AIR_GetNextFromBase(base, aircraft)) != NULL) {
		if (AIR_SetPilot(aircraft, pilot))
			break;
	}
}

/**
 * @brief Checks to see if the pilot is in any aircraft at this base.
 * If he is then he is removed from that aircraft.
 * @param[in] base Which base has the aircraft to search for the employee in.
 * @param[in] pilot Which pilot to search for.
 */
void AIR_RemovePilotFromAssignedAircraft (const base_t* base, const employee_t* pilot)
{
	aircraft_t *aircraft;

	aircraft = NULL;
	while ((aircraft = AIR_GetNextFromBase(base, aircraft)) != NULL) {
		if (AIR_GetPilot(aircraft) == pilot) {
			AIR_SetPilot(aircraft, NULL);
			break;
		}
	}
}

/**
 * @brief Get the all the unique weapon ranges of this aircraft.
 * @param[in] slot Pointer to the aircrafts weapon slot list.
 * @param[in] maxSlot maximum number of weapon slots in aircraft.
 * @param[out] weaponRanges An array containing a unique list of weapons ranges.
 * @return Number of unique weapons ranges.
 */
int AIR_GetAircraftWeaponRanges (const aircraftSlot_t *slot, int maxSlot, float *weaponRanges)
{
	int idxSlot;
	int idxAllWeap;
	float allWeaponRanges[MAX_AIRCRAFTSLOT];
	int numAllWeaponRanges = 0;
	int numUniqueWeaponRanges = 0;

	assert(slot);

	/* We choose the usable weapon to add to the weapons array */
	for (idxSlot = 0; idxSlot < maxSlot; idxSlot++) {
		const aircraftSlot_t *weapon = slot + idxSlot;
		const objDef_t *ammo = weapon->ammo;

		if (!ammo)
			continue;

		allWeaponRanges[numAllWeaponRanges] = ammo->craftitem.stats[AIR_STATS_WRANGE];
		numAllWeaponRanges++;
	}

	if (numAllWeaponRanges > 0) {
		/* sort the list of all weapon ranges and create an array with only the unique ranges */
		qsort(allWeaponRanges, numAllWeaponRanges, sizeof(allWeaponRanges[0]), Q_FloatSort);

		for (idxAllWeap = 0; idxAllWeap < numAllWeaponRanges; idxAllWeap++) {
			if (allWeaponRanges[idxAllWeap] != weaponRanges[numUniqueWeaponRanges - 1] || idxAllWeap == 0) {
				weaponRanges[numUniqueWeaponRanges] = allWeaponRanges[idxAllWeap];
				numUniqueWeaponRanges++;
			}
		}
	}

	return numUniqueWeaponRanges;
}

/**
 * @brief Saves an route plan of an aircraft
 * @param[out] node XML Node structure, where we write the information to
 * @param[in] route Aircraft route plan
 */
static void AIR_SaveRouteXML (mxml_node_t *node, const mapline_t *route)
{
	int j;
	mxml_node_t *subnode;

	subnode = mxml_AddNode(node, SAVE_AIRCRAFT_ROUTE);
	mxml_AddFloatValue(subnode, SAVE_AIRCRAFT_ROUTE_DISTANCE, route->distance);
	for (j = 0; j < route->numPoints; j++) {
		mxml_AddPos2(subnode, SAVE_AIRCRAFT_ROUTE_POINT, route->point[j]);
	}
}

/**
 * @brief Saves an item slot
 * @param[in] slot Pointer to the slot where item is.
 * @param[in] num Number of slots for this aircraft.
 * @param[out] p XML Node structure, where we write the information to
 * @param[in] p pointer where information are written.
 * @param[in] weapon True if the slot is a weapon slot.
 * @sa B_Save
 * @sa AII_InitialiseSlot
 */
static void AIR_SaveAircraftSlotsXML (const aircraftSlot_t* slot, const int num, mxml_node_t *p, qboolean weapon)
{
	int i;
	mxml_node_t *sub;

	for (i = 0; i < num; i++) {
		sub = mxml_AddNode(p, SAVE_AIRCRAFT_SLOT);
		AII_SaveOneSlotXML(sub, &slot[i], weapon);
	}
}

/**
 * @brief Saves an aircraft
 * @param[out] p XML Node structure, where we write the information to
 * @param[in] aircraft Aircraft we save
 * @param[in] isUfo If this aircraft is a UFO
 */
static qboolean AIR_SaveAircraftXML (mxml_node_t *p, const aircraft_t* const aircraft, qboolean const isUfo)
{
	mxml_node_t *node;
	mxml_node_t *subnode;
	int l;
	linkedList_t *k;
	const employee_t *pilot;

	Com_RegisterConstList(saveAircraftConstants);

	node = mxml_AddNode(p, SAVE_AIRCRAFT_AIRCRAFT);

	mxml_AddString(node, SAVE_AIRCRAFT_ID, aircraft->id);
	mxml_AddString(node, SAVE_AIRCRAFT_NAME, aircraft->name);

	mxml_AddString(node, SAVE_AIRCRAFT_STATUS, Com_GetConstVariable(SAVE_AIRCRAFTSTATUS_NAMESPACE, aircraft->status));
	mxml_AddInt(node, SAVE_AIRCRAFT_FUEL, aircraft->fuel);
	mxml_AddInt(node, SAVE_AIRCRAFT_DAMAGE, aircraft->damage);
	mxml_AddPos3(node, SAVE_AIRCRAFT_POS, aircraft->pos);
	mxml_AddPos3(node, SAVE_AIRCRAFT_DIRECTION, aircraft->direction);
	mxml_AddInt(node, SAVE_AIRCRAFT_POINT, aircraft->point);
	mxml_AddInt(node, SAVE_AIRCRAFT_TIME, aircraft->time);

	subnode = mxml_AddNode(node, SAVE_AIRCRAFT_WEAPONS);
	AIR_SaveAircraftSlotsXML(aircraft->weapons, aircraft->maxWeapons, subnode, qtrue);
	subnode = mxml_AddNode(node, SAVE_AIRCRAFT_SHIELDS);
	AIR_SaveAircraftSlotsXML(&aircraft->shield, 1, subnode, qfalse);
	subnode = mxml_AddNode(node, SAVE_AIRCRAFT_ELECTRONICS);
	AIR_SaveAircraftSlotsXML(aircraft->electronics, aircraft->maxElectronics, subnode, qfalse);

	AIR_SaveRouteXML(node, &aircraft->route);

	if (isUfo) {
#ifdef DEBUG
		if (!aircraft->mission)
			Com_Printf("Error: UFO '%s'is not linked to any mission\n", aircraft->id);
#endif
		mxml_AddString(node, SAVE_AIRCRAFT_MISSIONID, aircraft->mission->id);
		/** detection id and time */
		mxml_AddInt(node, SAVE_AIRCRAFT_DETECTIONIDX, aircraft->detectionIdx);
		mxml_AddDate(node, SAVE_AIRCRAFT_LASTSPOTTED_DATE, aircraft->lastSpotted.day, aircraft->lastSpotted.sec);
	} else {
		if (aircraft->status == AIR_MISSION) {
			assert(aircraft->mission);
			mxml_AddString(node, SAVE_AIRCRAFT_MISSIONID, aircraft->mission->id);
		}
		if (aircraft->homebase) {
			mxml_AddInt(node, SAVE_AIRCRAFT_HOMEBASE, aircraft->homebase->idx);
		}
	}

	if (aircraft->aircraftTarget) {
		if (isUfo)
			mxml_AddInt(node, SAVE_AIRCRAFT_AIRCRAFTTARGET, aircraft->aircraftTarget->idx);
		else
			mxml_AddInt(node, SAVE_AIRCRAFT_AIRCRAFTTARGET, aircraft->aircraftTarget - ccs.ufos);
	}

	subnode = mxml_AddNode(node, SAVE_AIRCRAFT_AIRSTATS);
	for (l = 0; l < AIR_STATS_MAX; l++) {
		mxml_node_t *statNode;
#ifdef DEBUG
		/* UFO HP can be < 0 if the UFO has been destroyed */
		if (!(isUfo && l == AIR_STATS_DAMAGE) && aircraft->stats[l] < 0)
			Com_Printf("Warning: ufo '%s' stats %i: %i is smaller than 0\n", aircraft->id, l, aircraft->stats[l]);
#endif
		if (aircraft->stats[l] != 0) {
			statNode = mxml_AddNode(subnode, SAVE_AIRCRAFT_AIRSTAT);
			mxml_AddString(statNode, SAVE_AIRCRAFT_AIRSTATID, Com_GetConstVariable(SAVE_AIRCRAFTSTAT_NAMESPACE, l));
			mxml_AddLong(statNode, SAVE_AIRCRAFT_VAL, aircraft->stats[l]);
		}
	}

	mxml_AddBoolValue(node, SAVE_AIRCRAFT_DETECTED, aircraft->detected);
	mxml_AddBoolValue(node, SAVE_AIRCRAFT_LANDED, aircraft->landed);

	Com_UnregisterConstList(saveAircraftConstants);

	/* All other informations are not needed for ufos */
	if (isUfo)
		return qtrue;

	mxml_AddInt(node, SAVE_AIRCRAFT_IDX, aircraft->idx);

	mxml_AddIntValue(node, SAVE_AIRCRAFT_RADAR_RANGE, aircraft->radar.range);
	mxml_AddIntValue(node, SAVE_AIRCRAFT_RADAR_TRACKINGRANGE, aircraft->radar.trackingRange);
	mxml_AddInt(node, SAVE_AIRCRAFT_HANGAR, aircraft->hangar);

	subnode = mxml_AddNode(node, SAVE_AIRCRAFT_AIRCRAFTTEAM);

	for (k = aircraft->acTeam; k != NULL; k = k->next) {
		const employee_t const *employee = (const employee_t *)k->data;
		mxml_node_t *ssnode = mxml_AddNode(subnode, SAVE_AIRCRAFT_MEMBER);
		mxml_AddInt(ssnode, SAVE_AIRCRAFT_TEAM_UCN, employee->chr.ucn);
	}

	pilot = AIR_GetPilot(aircraft);
	if (pilot)
		mxml_AddInt(node, SAVE_AIRCRAFT_PILOTUCN, pilot->chr.ucn);

	/* itemcargo */
	subnode = mxml_AddNode(node, SAVE_AIRCRAFT_CARGO);
	for (l = 0; l < aircraft->itemTypes; l++) {
		mxml_node_t *ssnode = mxml_AddNode(subnode, SAVE_AIRCRAFT_ITEM);
		assert(aircraft->itemcargo[l].item);
		mxml_AddString(ssnode, SAVE_AIRCRAFT_ITEMID, aircraft->itemcargo[l].item->id);
		mxml_AddInt(ssnode, SAVE_AIRCRAFT_AMOUNT, aircraft->itemcargo[l].amount);
	}

	/* aliencargo */
	{
		const int alienCargoTypes = AL_GetAircraftAlienCargoTypes(aircraft);
		const aliensTmp_t *cargo  = AL_GetAircraftAlienCargo(aircraft);
		subnode = mxml_AddNode(node, SAVE_AIRCRAFT_ALIENCARGO);
		for (l = 0; l < alienCargoTypes; l++) {
			mxml_node_t *ssnode = mxml_AddNode(subnode, SAVE_AIRCRAFT_CARGO);
			assert(cargo[l].teamDef);
			mxml_AddString(ssnode, SAVE_AIRCRAFT_TEAMDEFID, cargo[l].teamDef->id);
			mxml_AddIntValue(ssnode, SAVE_AIRCRAFT_ALIVE, cargo[l].amountAlive);
			mxml_AddIntValue(ssnode, SAVE_AIRCRAFT_DEAD, cargo[l].amountDead);
		}
	}

	return qtrue;
}

/**
 * @brief Save callback for savegames in xml format
 * @sa AIR_LoadXML
 * @sa B_SaveXML
 * @sa SAV_GameSaveXML
 */
qboolean AIR_SaveXML (mxml_node_t *parent)
{
	int i;
	mxml_node_t * node, *snode;
	base_t *base;

	/* save phalanx aircraft */
	snode = mxml_AddNode(parent, SAVE_AIRCRAFT_PHALANX);
	base = NULL;
	while ((base = B_GetNextFounded(base)) != NULL) {
		aircraft_t *aircraft = NULL;
		while ((aircraft = AIR_GetNextFromBase(base, aircraft)) != NULL)
			AIR_SaveAircraftXML(snode, aircraft, qfalse);
	}

	/* save the ufos on geoscape */
	snode = mxml_AddNode(parent, SAVE_AIRCRAFT_UFOS);
	for (i = 0; i < MAX_UFOONGEOSCAPE; i++) {
		const aircraft_t *ufo = UFO_GetByIDX(i);
		if (!ufo || (ufo->id == NULL))
			continue;
		AIR_SaveAircraftXML(snode, ufo, qtrue);
	}

	/* Save projectiles. */
	node = mxml_AddNode(parent, SAVE_AIRCRAFT_PROJECTILES);
	if (!AIRFIGHT_SaveXML(node))
		return qfalse;

	return qtrue;
}

/**
 * @brief Loads the weapon slots of an aircraft.
 * @param[in] aircraft Pointer to the aircraft.
 * @param[out] slot Pointer to the slot where item should be added.
 * @param[in] p XML Node structure, where we get the information from
 * @param[in] weapon True if the slot is a weapon slot.
 * @param[in] max Maximum number of slots for this aircraft that should be loaded.
 * @sa B_Load
 * @sa B_SaveAircraftSlots
 */
static void AIR_LoadAircraftSlotsXML (aircraft_t *aircraft, aircraftSlot_t* slot, mxml_node_t *p, qboolean weapon, const int max)
{
	mxml_node_t *act;
	int i;
	for (i = 0, act = mxml_GetNode(p, SAVE_AIRCRAFT_SLOT); act && i <= max; act = mxml_GetNextNode(act, p, SAVE_AIRCRAFT_SLOT), i++) {
		slot[i].aircraft = aircraft;
		AII_LoadOneSlotXML(act, &slot[i], weapon);
	}
	if (i > max)
		Com_Printf("Error: Trying to assign more than max (%d) Aircraft Slots (cur is %d)\n", max, i);

}

/**
 * @brief Loads the route of an aircraft
 * @param[in] p XML Node structure, where we get the information from
 * @param[out] route Route points of the aircraft
 */
static qboolean AIR_LoadRouteXML (mxml_node_t *p, mapline_t *route)
{
	mxml_node_t *actual;
	mxml_node_t *snode;
	int count = 0;

	snode = mxml_GetNode(p, SAVE_AIRCRAFT_ROUTE);
	if (!snode)
		return qfalse;

	for (actual = mxml_GetPos2(snode, SAVE_AIRCRAFT_ROUTE_POINT, route->point[count]); actual && count <= LINE_MAXPTS;
			actual = mxml_GetNextPos2(actual, snode, SAVE_AIRCRAFT_ROUTE_POINT, route->point[++count]))
		;
	if (count > LINE_MAXPTS) {
		Com_Printf("AIR_Load: number of points (%i) for UFO route exceed maximum value (%i)\n", count, LINE_MAXPTS);
		return qfalse;
	}
	route->numPoints = count;
	route->distance = mxml_GetFloat(snode, SAVE_AIRCRAFT_ROUTE_DISTANCE, 0.0);
	return qtrue;
}

/**
 * @brief Loads an Aircraft from the savegame
 * @param[in] p XML Node structure, where we get the information from
 * @param[out] craft Pointer to the aircraft
 */
static qboolean AIR_LoadAircraftXML (mxml_node_t *p, aircraft_t *craft)
{
	mxml_node_t *snode;
	mxml_node_t *ssnode;
	const char *statusId;
	/* vars, if aircraft wasn't found */
	int tmpInt;
	int l, status;
	const char *s = mxml_GetString(p, SAVE_AIRCRAFT_ID);
	const aircraft_t *crafttype = AIR_GetAircraft(s);

	/* Copy all datas that don't need to be saved (tpl, hangar,...) */
	*craft = *crafttype;

	tmpInt = mxml_GetInt(p, SAVE_AIRCRAFT_HOMEBASE, MAX_BASES);
	craft->homebase = (tmpInt != MAX_BASES) ? B_GetBaseByIDX(tmpInt) : NULL;

	Com_RegisterConstList(saveAircraftConstants);

	statusId = mxml_GetString(p, SAVE_AIRCRAFT_STATUS);
	if (!Com_GetConstIntFromNamespace(SAVE_AIRCRAFTSTATUS_NAMESPACE, statusId, &status)) {
		Com_Printf("Invalid aircraft status '%s'\n", statusId);
		Com_UnregisterConstList(saveAircraftConstants);
		return qfalse;
	}

	craft->status = status;
	craft->fuel = mxml_GetInt(p, SAVE_AIRCRAFT_FUEL, 0);
	craft->damage = mxml_GetInt(p, SAVE_AIRCRAFT_DAMAGE, 0);
	mxml_GetPos3(p, SAVE_AIRCRAFT_POS, craft->pos);

	mxml_GetPos3(p, SAVE_AIRCRAFT_DIRECTION, craft->direction);
	craft->point = mxml_GetInt(p, SAVE_AIRCRAFT_POINT, 0);
	craft->time = mxml_GetInt(p, SAVE_AIRCRAFT_TIME, 0);

	if (!AIR_LoadRouteXML(p, &craft->route)) {
		Com_UnregisterConstList(saveAircraftConstants);
		return qfalse;
	}

	s = mxml_GetString(p, SAVE_AIRCRAFT_NAME);
	if (s[0] == '\0')
		s = _(craft->defaultName);
	Q_strncpyz(craft->name, s, sizeof(craft->name));

	s = mxml_GetString(p, SAVE_AIRCRAFT_MISSIONID);
	if (s[0] == '\0' && !craft->homebase) {
		Com_Printf("Error: UFO '%s' is not linked to any mission\n", craft->id);
		Com_UnregisterConstList(saveAircraftConstants);
		return qfalse;
	}
	craft->missionID = Mem_PoolStrDup(s, cp_campaignPool, 0);

	if (!craft->homebase) {
		craft->idx = ccs.numUFOs;
		/* detection id and time */
		craft->detectionIdx = mxml_GetInt(p, SAVE_AIRCRAFT_DETECTIONIDX, 0);
		mxml_GetDate(p, SAVE_AIRCRAFT_LASTSPOTTED_DATE, &craft->lastSpotted.day, &craft->lastSpotted.sec);
	}

	snode = mxml_GetNode(p, SAVE_AIRCRAFT_AIRSTATS);
	for (ssnode = mxml_GetNode(snode, SAVE_AIRCRAFT_AIRSTAT); ssnode; ssnode = mxml_GetNextNode(ssnode, snode, SAVE_AIRCRAFT_AIRSTAT)) {
		const char *statId =  mxml_GetString(ssnode, SAVE_AIRCRAFT_AIRSTATID);
		int idx;

		if (!Com_GetConstIntFromNamespace(SAVE_AIRCRAFTSTAT_NAMESPACE, statId, &idx)) {
			Com_Printf("Invalid aircraft stat '%s'\n", statId);
			Com_UnregisterConstList(saveAircraftConstants);
			return qfalse;
		}
		craft->stats[idx] = mxml_GetLong(ssnode, SAVE_AIRCRAFT_VAL, 0);
#ifdef DEBUG
		/* UFO HP can be < 0 if the UFO has been destroyed */
		if (!(!craft->homebase && idx == AIR_STATS_DAMAGE) && craft->stats[idx] < 0)
			Com_Printf("Warning: ufo '%s' stats %i: %i is smaller than 0\n", craft->id, idx, craft->stats[idx]);
#endif
	}

	craft->detected = mxml_GetBool(p, SAVE_AIRCRAFT_DETECTED, qfalse);
	craft->landed = mxml_GetBool(p, SAVE_AIRCRAFT_LANDED, qfalse);

	tmpInt = mxml_GetInt(p, SAVE_AIRCRAFT_AIRCRAFTTARGET, -1);
	if (tmpInt == -1)
		craft->aircraftTarget = NULL;
	else if (!craft->homebase)
		craft->aircraftTarget = AIR_AircraftGetFromIDX(tmpInt);
	else
		craft->aircraftTarget = ccs.ufos + tmpInt;

	/* read equipment slots */
	snode = mxml_GetNode(p, SAVE_AIRCRAFT_WEAPONS);
	AIR_LoadAircraftSlotsXML(craft, craft->weapons, snode, qtrue, craft->maxWeapons);
	snode = mxml_GetNode(p, SAVE_AIRCRAFT_SHIELDS);
	AIR_LoadAircraftSlotsXML(craft, &craft->shield, snode, qfalse, 1);
	snode = mxml_GetNode(p, SAVE_AIRCRAFT_ELECTRONICS);
	AIR_LoadAircraftSlotsXML(craft, craft->electronics, snode, qfalse, craft->maxElectronics);

	Com_UnregisterConstList(saveAircraftConstants);

	/* All other informations are not needed for ufos */
	if (!craft->homebase)
		return qtrue;

	craft->idx = mxml_GetInt(p, SAVE_AIRCRAFT_IDX, -1);
	if (craft->idx == -1)
		return qfalse;
	craft->hangar = mxml_GetInt(p, SAVE_AIRCRAFT_HANGAR, 0);

	snode = mxml_GetNode(p, SAVE_AIRCRAFT_AIRCRAFTTEAM);
	for (ssnode = mxml_GetNode(snode, SAVE_AIRCRAFT_MEMBER); AIR_GetTeamSize(craft) < craft->maxTeamSize && ssnode;
			ssnode = mxml_GetNextNode(ssnode, snode, SAVE_AIRCRAFT_MEMBER)) {
		const int ucn = mxml_GetInt(ssnode, SAVE_AIRCRAFT_TEAM_UCN, -1);
		if (ucn != -1)
			LIST_AddPointer(&craft->acTeam, E_GetEmployeeFromChrUCN(ucn));
	}

	tmpInt = mxml_GetInt(p, SAVE_AIRCRAFT_PILOTUCN, -1);
	/* the employee subsystem is loaded after the base subsystem
	 * this means, that the pilot pointer is not (really) valid until
	 * E_Load was called, too */
	if (tmpInt != -1)
		AIR_SetPilot(craft, E_GetEmployeeFromChrUCN(tmpInt));
	else
		AIR_SetPilot(craft, NULL);

	RADAR_InitialiseUFOs(&craft->radar);
	craft->radar.range = mxml_GetInt(p, SAVE_AIRCRAFT_RADAR_RANGE, 0);
	craft->radar.trackingRange = mxml_GetInt(p, SAVE_AIRCRAFT_RADAR_TRACKINGRANGE, 0);

	/* itemcargo */
	snode = mxml_GetNode(p, SAVE_AIRCRAFT_CARGO);
	for (l = 0, ssnode = mxml_GetNode(snode, SAVE_AIRCRAFT_ITEM); l < MAX_CARGO && ssnode;
			l++, ssnode = mxml_GetNextNode(ssnode, snode, SAVE_AIRCRAFT_ITEM)) {
		const char *const str = mxml_GetString(ssnode, SAVE_AIRCRAFT_ITEMID);
		const objDef_t *od = INVSH_GetItemByID(str);

		if (!od) {
			Com_Printf("AIR_LoadAircraftXML: Could not find aircraftitem '%s'\n", str);
			l--;
			continue;
		}

		craft->itemcargo[l].item = od;
		craft->itemcargo[l].amount = mxml_GetInt(ssnode, SAVE_AIRCRAFT_AMOUNT, 0);
	}
	craft->itemTypes = l;

	/* aliencargo */
	snode = mxml_GetNode(p, SAVE_AIRCRAFT_ALIENCARGO);
	for (l = 0, ssnode = mxml_GetNode(snode, SAVE_AIRCRAFT_CARGO); l < MAX_CARGO && ssnode;
			l++, ssnode = mxml_GetNextNode(ssnode, snode, SAVE_AIRCRAFT_CARGO)) {
		aliensTmp_t *cargo = AL_GetAircraftAlienCargo(craft);
		const char *const str = mxml_GetString(ssnode, SAVE_AIRCRAFT_TEAMDEFID);

		cargo[l].teamDef = Com_GetTeamDefinitionByID(str);
		if (!cargo[l].teamDef) {
			Com_Printf("AIR_LoadAircraftXML: Could not find teamDef '%s'\n", str);
			l--;
			continue;
		}

		cargo[l].amountAlive = mxml_GetInt(ssnode, SAVE_AIRCRAFT_ALIVE, 0);
		cargo[l].amountDead  =	mxml_GetInt(ssnode, SAVE_AIRCRAFT_DEAD, 0);
	}
	AL_SetAircraftAlienCargoTypes(craft, l);

	return qtrue;
}

/**
 * @brief resets aircraftSlots' backreference pointers for aircraft
 * @param[in] aircraft Pointer to the aircraft
 */
static void AIR_CorrectAircraftSlotPointers (aircraft_t *aircraft)
{
	int i;

	assert(aircraft);

	for (i = 0; i < aircraft->maxWeapons; i++) {
		aircraft->weapons[i].aircraft = aircraft;
		aircraft->weapons[i].base = NULL;
		aircraft->weapons[i].installation = NULL;
	}
	for (i = 0; i < aircraft->maxElectronics; i++) {
		aircraft->electronics[i].aircraft = aircraft;
		aircraft->electronics[i].base = NULL;
		aircraft->electronics[i].installation = NULL;
	}
	aircraft->shield.aircraft = aircraft;
	aircraft->shield.base = NULL;
	aircraft->shield.installation = NULL;
}

qboolean AIR_LoadXML (mxml_node_t *parent)
{
	mxml_node_t *snode, *ssnode;
	mxml_node_t *projectiles;
	int i;

	/* load phalanx aircraft */
	snode = mxml_GetNode(parent, SAVE_AIRCRAFT_PHALANX);
	for (ssnode = mxml_GetNode(snode, SAVE_AIRCRAFT_AIRCRAFT); ssnode;
			ssnode = mxml_GetNextNode(ssnode, snode, SAVE_AIRCRAFT_AIRCRAFT)) {
		aircraft_t craft;
		if (!AIR_LoadAircraftXML(ssnode, &craft))
			return qfalse;
		assert(craft.homebase);
		AIR_CorrectAircraftSlotPointers(AIR_Add(craft.homebase, &craft));
	}

	/* load the ufos on geoscape */
	snode = mxml_GetNode(parent, SAVE_AIRCRAFT_UFOS);

	for (i = 0, ssnode = mxml_GetNode(snode, SAVE_AIRCRAFT_AIRCRAFT); i < MAX_UFOONGEOSCAPE && ssnode;
			ssnode = mxml_GetNextNode(ssnode, snode, SAVE_AIRCRAFT_AIRCRAFT), i++) {
		if (!AIR_LoadAircraftXML(ssnode, UFO_GetByIDX(i)))
			return qfalse;
		ccs.numUFOs++;
	}

	/* Load projectiles. */
	projectiles = mxml_GetNode(parent, SAVE_AIRCRAFT_PROJECTILES);
	if (!AIRFIGHT_LoadXML(projectiles))
		return qfalse;

	/* check UFOs - backwards */
	for (i = ccs.numUFOs - 1; i >= 0; i--) {
		aircraft_t *ufo = UFO_GetByIDX(i);
		if (ufo->time < 0 || ufo->stats[AIR_STATS_SPEED] <= 0) {
			Com_Printf("AIR_Load: Found invalid ufo entry - remove it - time: %i - speed: %i\n",
					ufo->time, ufo->stats[AIR_STATS_SPEED]);
			UFO_RemoveFromGeoscape(ufo);
		}
	}

	return qtrue;
}

/**
 * @brief Set the mission pointers for all the aircraft after loading a savegame
 */
static qboolean AIR_PostLoadInitMissions (void)
{
	int i;
	qboolean success = qtrue;
	aircraft_t *ufo;
	base_t *base = NULL;

	/* PHALANX aircraft */
	while ((base = B_GetNextFounded(base)) != NULL) {
		aircraft_t *aircraft = NULL;
		while ((aircraft = AIR_GetNextFromBase(base, aircraft))) {
			if (!aircraft->missionID || aircraft->missionID[0] == '\0')
				continue;
			aircraft->mission = CP_GetMissionByID(aircraft->missionID);
			if (!aircraft->mission) {
				Com_Printf("Aircraft %s (idx: %i) is linked to an invalid mission: %s\n", aircraft->name, aircraft->idx, aircraft->missionID);
				if (aircraft->status == AIR_MISSION)
					AIR_AircraftReturnToBase(aircraft);
			}
			Mem_Free(aircraft->missionID);
			aircraft->missionID = NULL;
		}
	}

	/* UFOs */
	ufo = NULL;
	while ((ufo = UFO_GetNext(ufo)) != NULL) {
		if (!ufo->missionID || ufo->missionID[0] == '\0')
			continue;
		ufo->mission = CP_GetMissionByID(ufo->missionID);
		if (!ufo->mission) {
			Com_Printf("UFO %s (idx: %i) is linked to an invalid mission: %s\n", ufo->name, ufo->idx, ufo->missionID);
			success = qfalse;
		}
		Mem_Free(ufo->missionID);
		ufo->missionID = NULL;
	}

	return success;
}

/**
 * @brief Actions needs to be done after loading the savegame
 * @sa SAV_GameActionsAfterLoad
 */
qboolean AIR_PostLoadInit (void)
{
	return AIR_PostLoadInitMissions();
}

/**
 * @brief Returns true if the current base is able to handle aircraft
 * @sa B_BaseInit_f
 * @note Hangar must be accessible during base attack to make aircraft lift off and to equip soldiers.
 */
qboolean AIR_AircraftAllowed (const base_t* base)
{
	return B_GetBuildingStatus(base, B_HANGAR) || B_GetBuildingStatus(base, B_SMALL_HANGAR);
}

/**
 * @param aircraft The aircraft to check
 * @return @c true if the given aircraft can go on interceptions
 */
qboolean AIR_CanIntercept (const aircraft_t *aircraft)
{
	/* if dependencies of hangar are missing, you can't send aircraft */
	if (aircraft->size == AIRCRAFT_SMALL && !B_GetBuildingStatus(aircraft->homebase, B_SMALL_HANGAR))
		return qfalse;
	if (aircraft->size == AIRCRAFT_LARGE && !B_GetBuildingStatus(aircraft->homebase, B_HANGAR))
		return qfalse;

	/* we need a pilot to intercept */
	if (AIR_GetPilot(aircraft) == NULL)
		return qfalse;

	return qtrue;
}

/**
 * @brief Checks the parsed aircraft for errors
 * @return false if there are errors - true otherwise
 */
qboolean AIR_ScriptSanityCheck (void)
{
	int i, j, k, error = 0;
	aircraft_t* a;

	for (i = 0, a = ccs.aircraftTemplates; i < ccs.numAircraftTemplates; i++, a++) {
		if (a->name[0] == '\0') {
			error++;
			Com_Printf("...... aircraft '%s' has no name\n", a->id);
		}
		if (!a->defaultName) {
			error++;
			Com_Printf("...... aircraft '%s' has no defaultName\n", a->id);
		}

		/* check that every weapons fits slot */
		for (j = 0; j < a->maxWeapons - 1; j++)
			if (a->weapons[j].item && AII_GetItemWeightBySize(a->weapons[j].item) > a->weapons[j].size) {
				error++;
				Com_Printf("...... aircraft '%s' has an item (%s) too heavy for its slot\n", a->id, a->weapons[j].item->id);
			}

		/* check that every slots has a different location for PHALANX aircraft (not needed for UFOs) */
		if (a->type != AIRCRAFT_UFO) {
			for (j = 0; j < a->maxWeapons - 1; j++) {
				const itemPos_t var = a->weapons[j].pos;
				for (k = j + 1; k < a->maxWeapons; k++)
					if (var == a->weapons[k].pos) {
						error++;
						Com_Printf("...... aircraft '%s' has 2 weapons slots at the same location\n", a->id);
					}
			}
			for (j = 0; j < a->maxElectronics - 1; j++) {
				const itemPos_t var = a->electronics[j].pos;
				for (k = j + 1; k < a->maxElectronics; k++)
					if (var == a->electronics[k].pos) {
						error++;
						Com_Printf("...... aircraft '%s' has 2 electronics slots at the same location\n", a->id);
					}
			}
		}
	}

	return !error;
}

/**
 * @brief Calculates free space in hangars in given base.
 * @param[in] aircraftTemplate aircraft in aircraftTemplates list.
 * @param[in] base The base to calc the free space in.
 * @param[in] used Additional space "used" in hangars (use that when calculating space for more than one aircraft).
 * @return Amount of free space in hangars suitable for given aircraft type.
 * @note Returns -1 in case of error. Returns 0 if no error but no free space.
 */
int AIR_CalculateHangarStorage (const aircraft_t *aircraftTemplate, const base_t *base, int used)
{
	assert(base);
	assert(aircraftTemplate);
	assert(aircraftTemplate == aircraftTemplate->tpl);	/* Make sure it's an aircraft template. */

	if (!base->founded)
		return -1;
	else {
		const int aircraftCapacity = AIR_GetCapacityByAircraftWeight(aircraftTemplate);
		/* If this is a small aircraft, we will check space in small hangar.
		 * If this is a large aircraft, we will check space in big hangar. */
		const int freespace = base->capacities[aircraftCapacity].max - base->capacities[aircraftCapacity].cur - used;
		return max(freespace, 0);
	}
}

/**
 * @brief Removes a soldier from an aircraft.
 * @param[in,out] employee The soldier to be removed from the aircraft.
 * @param[in,out] aircraft The aircraft to remove the soldier from.
 * Use @c NULL to remove the soldier from any aircraft.
 * @sa AIR_AddEmployee
 */
qboolean AIR_RemoveEmployee (employee_t *employee, aircraft_t *aircraft)
{
	if (!employee)
		return qfalse;

	/* If no aircraft is given we search if he is in _any_ aircraft and set
	 * the aircraft pointer to it. */
	if (!aircraft) {
		base_t *base = NULL;
		while ((base = B_GetNext(base)) != NULL) {
			aircraft_t *acTemp = NULL;
			while ((acTemp = AIR_GetNextFromBase(base, acTemp))) {
				if (AIR_IsEmployeeInAircraft(employee, acTemp)) {
					aircraft = acTemp;
					break;
				}
			}
			if (aircraft)
				break;
		}
		if (!aircraft)
			return qfalse;
	}

	Com_DPrintf(DEBUG_CLIENT, "AIR_RemoveEmployee: base: %i - aircraft->idx: %i\n",
		aircraft->homebase ? aircraft->homebase->idx : -1, aircraft->idx);

	cls.i.DestroyInventory(&cls.i, &employee->chr.i);
	return AIR_RemoveFromAircraftTeam(aircraft, employee);
}

/**
 * @brief Tells you if a soldier is assigned to an aircraft.
 * @param[in] employee The soldier to search for.
 * @param[in] aircraft The aircraft to search the soldier in. Use @c NULL to
 * check if the soldier is in @b any aircraft.
 * @return true if the soldier was found in the aircraft otherwise false.
 */
const aircraft_t *AIR_IsEmployeeInAircraft (const employee_t *employee, const aircraft_t* aircraft)
{
	if (!employee)
		return NULL;

	if (employee->transfer)
		return NULL;

	/* If no aircraft is given we search if he is in _any_ aircraft and return true if that's the case. */
	if (!aircraft) {
		base_t *base = NULL;
		while ((base = B_GetNext(base)) != NULL) {
			aircraft_t *aircraftByIDX = NULL;
			while ((aircraftByIDX = AIR_GetNextFromBase(base, aircraftByIDX))) {
				if (AIR_IsEmployeeInAircraft(employee, aircraftByIDX))
					return aircraftByIDX;
			}
		}
		return NULL;
	}

	if (employee->type == EMPL_PILOT) {
		if (AIR_GetPilot(aircraft) == employee)
			return aircraft;
		return NULL;
	}

	if (AIR_IsInAircraftTeam(aircraft, employee))
		return aircraft;
	else
		return NULL;
}

/**
 * @brief Removes all soldiers from an aircraft.
 * @param[in,out] aircraft The aircraft to remove the soldiers from.
 * @sa AIR_RemoveEmployee
 */
void AIR_RemoveEmployees (aircraft_t *aircraft)
{
	linkedList_t* l;

	if (!aircraft)
		return;

	for (l = aircraft->acTeam; l != NULL;) {
		employee_t *employee = (employee_t *)l->data;
		l = l->next;
		/* use global aircraft index here */
		AIR_RemoveEmployee(employee, aircraft);
	}

	/* Remove pilot */
	AIR_SetPilot(aircraft, NULL);

	if (AIR_GetTeamSize(aircraft) > 0)
		Com_Error(ERR_DROP, "AIR_RemoveEmployees: Error, there went something wrong with soldier-removing from aircraft.");
}


/**
 * @brief Move all the equipment carried by the team on the aircraft into the given equipment
 * @param[in] aircraft The craft with the team (and thus equipment) onboard.
 * @param[out] ed The equipment definition which will receive all the stuff from the aircraft-team.
 */
void AIR_MoveEmployeeInventoryIntoStorage (const aircraft_t *aircraft, equipDef_t *ed)
{
	containerIndex_t container;
	linkedList_t *l;

	if (!aircraft) {
		Com_Printf("AIR_MoveEmployeeInventoryIntoStorage: Warning: Called with no aircraft (and thus no carried equipment to add).\n");
		return;
	}
	if (!ed) {
		Com_Printf("AIR_MoveEmployeeInventoryIntoStorage: Warning: Called with no equipment definition at add stuff to.\n");
		return;
	}

	if (AIR_GetTeamSize(aircraft) == 0) {
		Com_DPrintf(DEBUG_CLIENT, "AIR_MoveEmployeeInventoryIntoStorage: No team to remove equipment from.\n");
		return;
	}

	for (container = 0; container < csi.numIDs; container++) {
		for (l = aircraft->acTeam; l != NULL; l = l->next) {
			employee_t *employee = (employee_t *)l->data;
			if (employee != NULL) {
				character_t *chr = &employee->chr;
				invList_t *ic = CONTAINER(chr, container);
#if 0
				if (INVDEF(container)->temp)
					continue;
#endif
				while (ic) {
					const item_t item = ic->item;
					const objDef_t *type = item.t;
					invList_t *next = ic->next;

					ed->numItems[type->idx]++;
					if (item.a) {
						assert(type->reload);
						assert(item.m);
						ed->numItemsLoose[item.m->idx] += item.a;
						/* Accumulate loose ammo into clips */
						if (ed->numItemsLoose[item.m->idx] >= type->ammo) {
							ed->numItemsLoose[item.m->idx] -= type->ammo;
							ed->numItems[item.m->idx]++;
						}
					}
					ic = next;
				}
			}
		}
	}
}

/**
 * @brief Assigns a soldier to an aircraft.
 * @param[in] employee The employee to be assigned to the aircraft.
 * @param[in] aircraft What aircraft to assign employee to.
 * @return returns true if a soldier could be assigned to the aircraft.
 * @sa AIR_RemoveEmployee
 * @sa AIR_AddToAircraftTeam
 */
static qboolean AIR_AddEmployee (employee_t *employee, aircraft_t *aircraft)
{
	if (!employee || !aircraft)
		return qfalse;

	if (AIR_GetTeamSize(aircraft) < aircraft->maxTeamSize) {
		/* Check whether the soldier is already on another aircraft */
		if (AIR_IsEmployeeInAircraft(employee, NULL))
			return qfalse;

		/* Assign the soldier to the aircraft. */
		return AIR_AddToAircraftTeam(aircraft, employee);
	}
	return qfalse;
}

/**
 * @brief Adds or removes a soldier to/from an aircraft.
 * @param[in,out] aircraft Aircraft to add to/remove from
 * @param[in] num Index of Soldier (in menu) to add/remove
 * @sa E_EmployeeHire_f
 */
void AIM_AddEmployeeFromMenu (aircraft_t *aircraft, const int num)
{
	employee_t *employee;

	Com_DPrintf(DEBUG_CLIENT, "AIM_AddEmployeeFromMenu: Trying to get employee with hired-idx %i.\n", num);

	/* If this fails it's very likely that employeeList is not filled. */
	employee = E_GetEmployeeByMenuIndex(num);
	if (!employee)
		Com_Error(ERR_DROP, "AIM_AddEmployeeFromMenu: Could not get employee %i", num);

	Com_DPrintf(DEBUG_CLIENT, "AIM_AddEmployeeFromMenu: employee with idx %i selected\n", employee->idx);

	assert(aircraft);

	if (AIR_IsEmployeeInAircraft(employee, aircraft)) {
		/* use the global aircraft index here */
		AIR_RemoveEmployee(employee, aircraft);
	} else {
		/* Assign soldier to aircraft/team if aircraft is not full */
		AIR_AddEmployee(employee, aircraft);
	}
}

/**
 * @brief Assigns initial team of soldiers to aircraft
 * @param[in,out] aircraft soldiers to add to
 */
void AIR_AssignInitial (aircraft_t *aircraft)
{
	int i, num;
	base_t *base;

	if (!aircraft) {
		Com_Printf("AIR_AssignInitial: No aircraft given\n");
		return;
	}

	base = aircraft->homebase;
	assert(base);

	num = min(E_GenerateHiredEmployeesList(base), aircraft->maxTeamSize);
	for (i = 0; i < num; i++)
		AIM_AddEmployeeFromMenu(aircraft, i);
}
