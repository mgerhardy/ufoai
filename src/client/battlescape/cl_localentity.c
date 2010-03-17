/**
 * @file cl_localentity.c
 * @brief Local entity management.
 */

/*
Copyright (C) 2002-2009 UFO: Alien Invasion.

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
#include "cl_localentity.h"
#include "../sound/s_main.h"
#include "../sound/s_mix.h"
#include "cl_particle.h"
#include "cl_actor.h"
#include "cl_ugv.h"
#include "cl_parse.h"
#include "cl_hud.h"
#include "../renderer/r_mesh_anim.h"
#include "../../common/tracing.h"

cvar_t *cl_le_debug;

/*===========================================================================
Local Model (LM) handling
=========================================================================== */

static inline void LE_GenerateInlineModelList (void)
{
	le_t *le = NULL;
	int i = 0;

	while ((le = LE_GetNextInUse(le))) {
		if (le->model1 && le->inlineModelName[0] == '*')
			cl.leInlineModelList[i++] = le->inlineModelName;
	}
	cl.leInlineModelList[i] = NULL;
}

/**
 * @sa G_CompleteRecalcRouting
 */
void CL_CompleteRecalcRouting (void)
{
	le_t* le;
	int i;

	LE_GenerateInlineModelList();

	for (i = 0, le = cl.LEs; i < cl.numLEs; i++, le++)
		/* We ALWAYS check against a model, even if it isn't in use.
		 * An unused model is NOT included in the inline list, so it doesn't get
		 * traced against. */
		if (le->model1 && le->inlineModelName[0] == '*')
			Grid_RecalcRouting(clMap, le->inlineModelName, cl.leInlineModelList);
}

/**
 * @sa CL_Explode
 * @param[in] le A local entity of a brush model (func_breakable, func_door, ...)
 */
void CL_RecalcRouting (const le_t* le)
{
	LE_GenerateInlineModelList();
	/* We ALWAYS check against a model, even if it isn't in use.
	 * An unused model is NOT included in the inline list, so it doesn't get
	 * traced against. */
	if (le->model1 && le->inlineModelName[0] == '*')
		Grid_RecalcRouting(clMap, le->inlineModelName, cl.leInlineModelList);

	CL_ActorConditionalMoveCalc(selActor);
}

/**
 * @brief Add the local models to the scene
 * @sa CL_ViewRender
 * @sa LE_AddToScene
 * @sa LM_AddModel
 */
void LM_AddToScene (void)
{
	localModel_t *lm;
	entity_t ent;
	int i;

	for (i = 0, lm = cl.LMs; i < cl.numLMs; i++, lm++) {
		if (!lm->inuse)
			continue;

		/* check for visibility */
		if (!((1 << cl_worldlevel->integer) & lm->levelflags))
			continue;

		/* set entity values */
		memset(&ent, 0, sizeof(ent));
		assert(lm->model);
		ent.model = lm->model;
		ent.skinnum = lm->skin;
		VectorCopy(lm->scale, ent.scale);

		if (lm->parent) {
			/** @todo what if the tagent is not rendered due to different level flags? */
			ent.tagent = R_GetEntity(lm->parent->renderEntityNum);
			if (ent.tagent == NULL)
				Com_Error(ERR_DROP, "Invalid entity num for local model: %i",
						lm->parent->renderEntityNum);
			ent.tagname = lm->tagname;
			ent.lighting = &lm->parent->lighting;
		} else {
			VectorCopy(lm->origin, ent.origin);
			VectorCopy(lm->origin, ent.oldorigin);
			VectorCopy(lm->angles, ent.angles);
			ent.lighting = &lm->lighting;

			if (lm->animname[0] != '\0') {
				ent.as = lm->as;
				/* do animation */
				R_AnimRun(&lm->as, ent.model, cls.frametime * 1000);
				lm->lighting.dirty = qtrue;
			} else {
				ent.as.frame = lm->frame;
			}
		}

		/* renderflags like RF_PULSE */
		ent.flags = lm->renderFlags;

		/* add it to the scene */
		lm->renderEntityNum = R_AddEntity(&ent);
	}
}

/**
 * @brief Checks whether a local model with the same entity number is already registered
 */
static inline localModel_t *LM_Find (int entnum)
{
	int i;

	for (i = 0; i < cl.numLMs; i++)
		if (cl.LMs[i].entnum == entnum)
			return &cl.LMs[i];

	return NULL;
}

/**
 * @brief Checks whether the given le is a living actor
 * @param[in] le The local entity to perform the check for
 * @sa G_IsLivingActor
 */
qboolean LE_IsActor (const le_t *le)
{
	assert(le);
	return le->type == ET_ACTOR || le->type == ET_ACTOR2x2 || le->type == ET_ACTORHIDDEN;
}

/**
 * @brief Checks whether the given le is a living actor (but might be hidden)
 * @param[in] le The local entity to perform the check for
 * @sa G_IsLivingActor
 * @sa LE_IsActor
 */
qboolean LE_IsLivingActor (const le_t *le)
{
	assert(le);
	return LE_IsActor(le) && !LE_IsDead(le);
}

/**
 * @brief Checks whether the given le is a living and visible actor
 * @param[in] le The local entity to perform the check for
 * @sa G_IsLivingActor
 * @sa LE_IsActor
 */
qboolean LE_IsLivingAndVisibleActor (const le_t *le)
{
	assert(le);
	if (le->invis)
		return qfalse;

	assert(le->type != ET_ACTORHIDDEN);

	return LE_IsLivingActor(le);
}

/**
 * @brief Register misc_models
 * @sa CL_ViewLoadMedia
 */
void LM_Register (void)
{
	localModel_t *lm;
	int i;

	for (i = 0, lm = cl.LMs; i < cl.numLMs; i++, lm++) {
		/* register the model */
		lm->model = R_RegisterModelShort(lm->name);
		if (lm->animname[0]) {
			R_AnimChange(&lm->as, lm->model, lm->animname);
			if (!lm->as.change)
				Com_Printf("LM_Register: Could not change anim of model '%s'\n", lm->animname);
		}
		if (!lm->model)
			lm->inuse = qfalse;
	}
}

void LE_SetThink (le_t *le, void (*think) (le_t *le))
{
	Com_DPrintf(DEBUG_EVENTSYS, "LE_SetThink: Set think function for le %i to %p\n",
			le->entnum, think);
	le->think = think;
}

localModel_t *LM_GetByID (const char *id)
{
	int i;

	if (id == NULL || id[0] == '\0')
		return NULL;

	for (i = 0; i < cl.numLMs; i++) {
		if (!strcmp(cl.LMs[i].id, id))
			return &cl.LMs[i];
	}
	return NULL;
}

/**
 * @brief Prepares local (not known or handled by the server) models to the map, which will be added later in LM_AddToScene().
 * @param[in] model The model name.
 * @param[in] origin Origin of the model (position on map).
 * @param[in] angles Angles of the model (how it should be rotated after adding to map).
 * @param[in] scale Scaling of the model (how it should be scaled after adding to map).
 * @param[in] entnum Entity number.
 * @param[in] levelflags The levels in which the entity resides/is visible.
 * @param[in] renderFlags The flags for the renderer, eg. 'translucent'.
 * @note misc_model
 * @sa CL_SpawnParseEntitystring
 * @sa LM_AddToScene
 */
localModel_t *LM_AddModel (const char *model, const vec3_t origin, const vec3_t angles, int entnum, int levelflags, int renderFlags, const vec3_t scale)
{
	localModel_t *lm;

	lm = &cl.LMs[cl.numLMs++];

	if (cl.numLMs >= MAX_LOCALMODELS)
		Com_Error(ERR_DROP, "Too many local models\n");

	memset(lm, 0, sizeof(*lm));
	Q_strncpyz(lm->name, model, sizeof(lm->name));
	VectorCopy(origin, lm->origin);
	VectorCopy(angles, lm->angles);
	/* check whether there is already a model with that number */
	if (LM_Find(entnum))
		Com_Error(ERR_DROP, "Already a local model with the same id (%i) loaded\n", entnum);
	lm->entnum = entnum;
	lm->levelflags = levelflags;
	lm->renderFlags = renderFlags;
	lm->lighting.dirty = qtrue;
	lm->inuse = qtrue;
	VectorCopy(scale, lm->scale);

	return lm;
}

/*===========================================================================
LE thinking
=========================================================================== */

/**
 * @brief Call think function for the given local entity if its still in use
 */
void LE_ExecuteThink (le_t *le)
{
	if (le->inuse && le->think) {
		Com_DPrintf(DEBUG_EVENTSYS, "LE_ExecuteThink: Execute think function %p for le %i\n",
					le->think, le->entnum);
		le->think(le);
	}
}

/**
 * @brief Calls the le think function and updates the animation. The animation updated even if the
 * particular local entity is invisible for the client. This ensures, that an animation is always
 * lerped correctly and won't magically start again once the local entity gets visible again.
 * @sa LET_StartIdle
 * @sa LET_PathMove
 * @sa LET_StartPathMove
 * @sa LET_Projectile
 */
void LE_Think (void)
{
	le_t *le = NULL;

	if (cls.state != ca_active)
		return;

	while ((le = LE_GetNext(le))) {
		LE_ExecuteThink(le);
		/* do animation - even for invisible entities */
		R_AnimRun(&le->as, le->model1, cls.frametime * 1000);
	}
}

void LM_Think (void)
{
	int i;
	localModel_t *lm;

	for (i = 0, lm = cl.LMs; i < cl.numLMs; i++, lm++) {
		if (lm->think)
			lm->think(lm);
	}
}


/*===========================================================================
 LE think functions
=========================================================================== */

static char retAnim[MAX_VAR];

/**
 * @brief Get the correct animation for the given actor state and weapons
 * @param[in] anim Type of animation (for example "stand", "walk")
 * @param[in] right ods index to determine the weapon in the actors right hand
 * @param[in] left ods index to determine the weapon in the actors left hand
 * @param[in] state the actors state - e.g. STATE_CROUCHED (crounched animations)
 * have a 'c' in front of their animation definitions (see *.anm files for
 * characters)
 */
const char *LE_GetAnim (const char *anim, int right, int left, int state)
{
	char *mod;
	qboolean akimbo;
	char animationIndex;
	const char *type;
	size_t length = sizeof(retAnim);

	if (!anim)
		return "";

	mod = retAnim;

	/* add crouched flag */
	if (state & STATE_CROUCHED) {
		*mod++ = 'c';
		length--;
	}

	/* determine relevant data */
	akimbo = qfalse;
	if (right == NONE) {
		animationIndex = '0';
		if (left == NONE)
			type = "item";
		else {
			/* left hand grenades look OK with default anim; others don't */
			if (strcmp(csi.ods[left].type, "grenade"))
				akimbo = qtrue;
			type = csi.ods[left].type;
		}
	} else {
		animationIndex = csi.ods[right].animationIndex;
		type = csi.ods[right].type;
		if (left != NONE && !strcmp(csi.ods[right].type, "pistol") && !strcmp(csi.ods[left].type, "pistol"))
			akimbo = qtrue;
	}

	if (!strncmp(anim, "stand", 5) || !strncmp(anim, "walk", 4)) {
		Q_strncpyz(mod, anim, length);
		mod += strlen(anim);
		*mod++ = animationIndex;
		*mod++ = 0;
	} else {
		Com_sprintf(mod, length, "%s_%s", anim, akimbo ? "pistol_d" : type);
	}

	return retAnim;
}

/**
 * @brief Change the animation of an actor to the idle animation (which can be
 * panic, dead or stand)
 * @note We have more than one animation for dead - the index is given by the
 * state of the local entity
 * @note Think function
 * @note See the *.anm files in the models dir
 */
void LET_StartIdle (le_t * le)
{
	/* hidden actors don't have models assigned, thus we can not change the
	 * animation for any model */
	if (le->type != ET_ACTORHIDDEN) {
		if (LE_IsDead(le))
			R_AnimChange(&le->as, le->model1, va("dead%i", LE_GetAnimationIndexForDeath(le)));
		else if (LE_IsPaniced(le))
			R_AnimChange(&le->as, le->model1, "panic0");
		else
			R_AnimChange(&le->as, le->model1, LE_GetAnim("stand", le->right, le->left, le->state));
	}

	le->pathPos = le->pathLength = 0;

	/* keep this animation until something happens */
	LE_SetThink(le, NULL);
}

/**
 * @brief Plays sound of content for moving actor.
 * @param[in] le Pointer to local entity being an actor.
 * @param[in] contents The contents flag of the brush we are currently in
 * @note Currently it supports only CONTENTS_WATER, any other special contents
 * can be added here anytime.
 */
static void LE_PlaySoundFileForContents (le_t* le, int contents)
{
	/* only play those water sounds when an actor jumps into the water - but not
	 * if he enters carefully in crouched mode */
	if (le->state & ~STATE_CROUCHED) {
		if (contents & CONTENTS_WATER) {
			/* were we already in the water? */
			if (le->positionContents & CONTENTS_WATER) {
				/* play water moving sound */
				S_PlaySample(le->origin, cls.soundPool[SOUND_WATER_OUT], SOUND_ATTN_IDLE, SND_VOLUME_FOOTSTEPS);
			} else {
				/* play water entering sound */
				S_PlaySample(le->origin, cls.soundPool[SOUND_WATER_IN], SOUND_ATTN_IDLE, SND_VOLUME_FOOTSTEPS);
			}
			return;
		}

		if (le->positionContents & CONTENTS_WATER) {
			/* play water leaving sound */
			S_PlaySample(le->origin, cls.soundPool[SOUND_WATER_MOVE], SOUND_ATTN_IDLE, SND_VOLUME_FOOTSTEPS);
		}
	}
}

/**
 * @brief Plays step sounds and draw particles for different terrain types
 * @param[in] le The local entity to play the sound and draw the particle for
 * @param[in] textureName The name of the texture the actor is standing on
 * @sa LET_PathMove
 */
static void LE_PlaySoundFileAndParticleForSurface (le_t* le, const char *textureName)
{
	const terrainType_t *t;
	vec3_t origin;

	t = Com_GetTerrainType(textureName);
	if (!t)
		return;

	/* origin might not be up-to-date here - but pos should be */
	PosToVec(le->pos, origin);

	/** @todo use the Grid_Fall method (ACTOR_SIZE_NORMAL) to ensure, that the particle is
	 * drawn at the ground (if needed - maybe the origin is already ground aligned)*/
	if (t->particle) {
		/* check whether actor is visible */
		if (LE_IsLivingAndVisibleActor(le))
			CL_ParticleSpawn(t->particle, 0, origin, NULL, NULL);
	}
	if (t->footStepSound) {
		s_sample_t *sample = S_LoadSample(t->footStepSound);
		Com_DPrintf(DEBUG_SOUND, "LE_PlaySoundFileAndParticleForSurface: volume %.2f\n", t->footStepVolume);
		S_PlaySample(origin, sample, SOUND_ATTN_STATIC, t->footStepVolume);
	}
}

/**
 * @brief Searches the closest actor to the given world vector
 * @param[in] origin World position to get the closest actor to
 * @note Only your own team is searched
 */
le_t* LE_GetClosestActor (const vec3_t origin)
{
	int dist = 999999;
	le_t *actor = NULL, *le = NULL;
	vec3_t leOrigin;

	while ((le = LE_GetNextInUse(le))) {
		int tmp;
		if (le->pnum != cl.pnum)
			continue;
		/* visible because it's our team - so we just check for living actor here */
		if (!LE_IsLivingActor(le))
			continue;
		VectorSubtract(origin, le->origin, leOrigin);
		tmp = VectorLength(leOrigin);
		if (tmp < dist) {
			actor = le;
			dist = tmp;
		}
	}

	return actor;
}

/**
 * sqrt(2) for diagonal movement
 */
int LE_ActorGetStepTime (const le_t *le, const pos3_t pos, const pos3_t oldPos, const int dir, const int speed)
{
	if (dir != DIRECTION_FALL) {
		return (((dir & (CORE_DIRECTIONS - 1)) >= BASE_DIRECTIONS ? UNIT_SIZE * 1.41 : UNIT_SIZE) * 1000 / speed);
	} else {
		vec3_t start, dest;
		/* This needs to account for the distance of the fall. */
		Grid_PosToVec(clMap, le->fieldSize, oldPos, start);
		Grid_PosToVec(clMap, le->fieldSize, pos, dest);
		/* 1/1000th of a second per model unit in height change */
		return (start[2] - dest[2]);
	}
}

static void LE_PlayFootStepSound (le_t *le)
{
	/* walking in water will not play the normal footstep sounds */
	if (!le->pathContents[le->pathPos]) {
		trace_t trace;
		vec3_t from, to;

		/* prepare trace vectors */
		PosToVec(le->pos, from);
		VectorCopy(from, to);
		/* we should really hit the ground with this */
		to[2] -= UNIT_HEIGHT;

		trace = CL_Trace(from, to, vec3_origin, vec3_origin, NULL, NULL, MASK_SOLID, cl_worldlevel->integer);
		if (trace.surface)
			LE_PlaySoundFileAndParticleForSurface(le, trace.surface->name);
	} else
		LE_PlaySoundFileForContents(le, le->pathContents[le->pathPos]);
}

static void LE_DoPathMove (le_t *le)
{
	/* next part */
	const byte fulldv = le->path[le->pathPos];
	const byte dir = getDVdir(fulldv);
	const byte crouchingState = LE_IsCrouched(le) ? 1 : 0;
	/* newCrouchingState needs to be set to the current crouching state
	 * and is possibly updated by PosAddDV. */
	byte newCrouchingState = crouchingState;
	PosAddDV(le->pos, newCrouchingState, fulldv);

	LE_PlayFootStepSound(le);

	/* only change the direction if the actor moves horizontally. */
	if (dir < CORE_DIRECTIONS || dir >= FLYING_DIRECTIONS)
		le->dir = dir & (CORE_DIRECTIONS - 1);
	le->angles[YAW] = directionAngles[le->dir];
	le->startTime = le->endTime;
	/* check for straight movement or diagonal movement */
	assert(le->speed[le->pathPos]);
	le->endTime += LE_ActorGetStepTime(le, le->pos, le->oldPos, dir, le->speed[le->pathPos]);

	le->positionContents = le->pathContents[le->pathPos];
	le->pathPos++;
}

/**
 * @brief Ends the move of an actor
 */
void LE_DoEndPathMove (le_t *le)
{
	le_t *floor;

	/* Verify the current position */
	if (!VectorCompare(le->pos, le->newPos))
		Com_Error(ERR_DROP, "LE_DoEndPathMove: Actor movement is out of sync: %i:%i:%i should be %i:%i:%i (step %i of %i) (team %i)",
				le->pos[0], le->pos[1], le->pos[2], le->newPos[0], le->newPos[1], le->newPos[2], le->pathPos, le->pathLength, le->team);

	CL_ActorConditionalMoveCalc(le);

	/* link any floor container into the actor temp floor container */
	floor = LE_Find(ET_ITEM, le->pos);
	if (floor)
		FLOOR(le) = FLOOR(floor);

	le->lighting.dirty = qtrue;
	LE_SetThink(le, LET_StartIdle);
	LE_ExecuteThink(le);
	LE_Unlock(le);
}

/**
 * @brief Spawns particle effects for a hit actor.
 * @param[in] le The actor to spawn the particles for.
 * @param[in] impact The impact location (where the particles are spawned).
 * @param[in] normal The index of the normal vector of the particles (think: impact angle).
 */
static void LE_ActorBodyHit (const le_t * le, const vec3_t impact, int normal)
{
	if (le->teamDef) {
		/* Spawn "hit_particle" if defined in teamDef. */
		if (le->teamDef->hitParticle[0] != '\0')
			CL_ParticleSpawn(le->teamDef->hitParticle, 0, impact, bytedirs[normal], NULL);
	}
}

/**
 * @brief Move the actor along the path to the given location
 * @note Think function
 * @sa CL_ActorDoMove
 */
static void LET_PathMove (le_t * le)
{
	float frac;
	vec3_t start, dest, delta;

	/* check for start of the next step */
	if (cl.time < le->startTime)
		return;

	/* move ahead */
	while (cl.time >= le->endTime) {
		/* Ensure that we are displayed where we are supposed to be, in case the last frame came too quickly. */
		Grid_PosToVec(clMap, le->fieldSize, le->pos, le->origin);

		/* Record the last position of movement calculations. */
		VectorCopy(le->pos, le->oldPos);

		if (le->pathPos < le->pathLength) {
			LE_DoPathMove(le);
		} else {
			LE_DoEndPathMove(le);
			return;
		}
	}

	/* interpolate the position */
	Grid_PosToVec(clMap, le->fieldSize, le->oldPos, start);
	Grid_PosToVec(clMap, le->fieldSize, le->pos, dest);
	VectorSubtract(dest, start, delta);

	frac = (float) (cl.time - le->startTime) / (float) (le->endTime - le->startTime);

	le->lighting.dirty = qtrue;

	/* calculate the new interpolated actor origin in the world */
	VectorMA(start, frac, delta, le->origin);
}

/**
 * @note Think function
 * @brief Change the actors animation to walking
 * @note See the *.anm files in the models dir
 */
void LET_StartPathMove (le_t *le)
{
	/* initial animation or animation change */
	R_AnimChange(&le->as, le->model1, LE_GetAnim("walk", le->right, le->left, le->state));
	if (!le->as.change)
		Com_Printf("LET_StartPathMove: Could not change anim of le: %i, team: %i, pnum: %i\n",
			le->entnum, le->team, le->pnum);

	LE_SetThink(le, LET_PathMove);
	LE_ExecuteThink(le);
}

/**
 * @note Think function
 */
static void LET_Projectile (le_t * le)
{
	if (cl.time >= le->endTime) {
		vec3_t impact;
		VectorCopy(le->origin, impact);
		CL_ParticleFree(le->ptl);
		/* don't run the think function again */
		le->inuse = qfalse;
		if (le->ref1 && le->ref1[0] != '\0') {
			VectorCopy(le->ptl->s, impact);
			le->ptl = CL_ParticleSpawn(le->ref1, 0, impact, bytedirs[le->dir], NULL);
			VecToAngles(bytedirs[le->state], le->ptl->angles);
		}
		if (le->ref2 && le->ref2[0] != '\0') {
			s_sample_t *sample = S_LoadSample(le->ref2);
			S_PlaySample(impact, sample, le->fd->impactAttenuation, SND_VOLUME_WEAPONS);
		}
		if (le->ref3) {
			/* Spawn blood particles (if defined) if actor(-body) was hit. Even if actor is dead. */
			/** @todo Special particles for stun attack (mind you that there is
			 * electrical and gas/chemical stunning)? */
			if (le->fd->obj->dmgtype != csi.damStunGas)
				LE_ActorBodyHit(le->ref3, impact, le->dir);
			CL_ActorPlaySound(le->ref3, SND_HURT);
		}
	} else if (CL_OutsideMap(le->ptl->s, UNIT_SIZE * 10)) {
		le->endTime = cl.time;
		CL_ParticleFree(le->ptl);
		/* don't run the think function again */
		le->inuse = qfalse;
	}
}

/*===========================================================================
 LE Special Effects
=========================================================================== */

void LE_AddProjectile (const fireDef_t *fd, int flags, const vec3_t muzzle, const vec3_t impact, int normal, le_t *leVictim)
{
	le_t *le;
	vec3_t delta;
	float dist;

	/* add le */
	le = LE_Add(0);
	if (!le)
		return;
	le->invis = !cl_leshowinvis->integer;
	/* bind particle */
	le->ptl = CL_ParticleSpawn(fd->projectile, 0, muzzle, NULL, NULL);
	if (!le->ptl) {
		le->inuse = qfalse;
		return;
	}

	/* calculate parameters */
	VectorSubtract(impact, muzzle, delta);
	dist = VectorLength(delta);

	VecToAngles(delta, le->ptl->angles);
	/* direction - bytedirs index */
	le->dir = normal;
	le->fd = fd;

	/* infinite speed projectile? */
	if (!fd->speed) {
		le->inuse = qfalse;
		le->ptl->size[0] = dist;
		VectorMA(muzzle, 0.5, delta, le->ptl->s);
		if (flags & (SF_IMPACT | SF_BODY) || (fd->splrad && !fd->bounce)) {
			ptl_t *ptl = NULL;
			if (flags & SF_BODY) {
				if (fd->hitBodySound[0]) {
					s_sample_t *sample = S_LoadSample(fd->hitBodySound);
					S_PlaySample(le->origin, sample, le->fd->impactAttenuation, SND_VOLUME_WEAPONS);
				}
				if (fd->hitBody[0])
					ptl = CL_ParticleSpawn(fd->hitBody, 0, impact, bytedirs[le->dir], NULL);

				/* Spawn blood particles (if defined) if actor(-body) was hit. Even if actor is dead. */
				/** @todo Special particles for stun attack (mind you that there is
				 * electrical and gas/chemical stunning)? */
				if (leVictim) {
					if (fd->obj->dmgtype != csi.damStunGas)
						LE_ActorBodyHit(leVictim, impact, le->dir);
					CL_ActorPlaySound(leVictim, SND_HURT);
				}
			} else {
				if (fd->impactSound[0]) {
					s_sample_t *sample = S_LoadSample(fd->impactSound);
					S_PlaySample(le->origin, sample, le->fd->impactAttenuation, SND_VOLUME_WEAPONS);
				}
				if (fd->impact[0])
					ptl = CL_ParticleSpawn(fd->impact, 0, impact, bytedirs[le->dir], NULL);
			}
			if (ptl)
				VecToAngles(bytedirs[le->dir], ptl->angles);
		}
		return;
	}
	/* particle properties */
	VectorScale(delta, fd->speed / dist, le->ptl->v);
	le->endTime = cl.time + 1000 * dist / fd->speed;

	/* think function */
	if (flags & SF_BODY) {
		le->ref1 = fd->hitBody;
		le->ref2 = fd->hitBodySound;
		le->ref3 = leVictim;
	} else if (flags & SF_IMPACT || (fd->splrad && !fd->bounce)) {
		le->ref1 = fd->impact;
		le->ref2 = fd->impactSound;
	} else {
		le->ref1 = NULL;
		if (flags & SF_BOUNCING)
			le->ref2 = fd->bounceSound;
	}

	LE_SetThink(le, LET_Projectile);
	LE_ExecuteThink(le);
}

/**
 * @brief Returns the index of the biggest item in the inventory list
 * @note This item is the only one that will be drawn when lying at the floor
 * @sa LE_PlaceItem
 * @return the item index in the @c csi.ods array
 * @note Only call this for none empty invList_t - see FLOOR, LEFT, RIGHT and so on macros
 */
static objDef_t *LE_BiggestItem (const invList_t *ic)
{
	objDef_t *max;
	int maxSize = 0;

	for (max = ic->item.t; ic; ic = ic->next) {
		const int size = INVSH_ShapeSize(ic->item.t->shape);
		if (size > maxSize) {
			max = ic->item.t;
			maxSize = size;
		}
	}

	/* there must be an item in the invList_t */
	assert(max);
	return max;
}

/**
 * @sa CL_BiggestItem
 * @param[in] le The local entity (ET_ITEM) with the floor container
 */
void LE_PlaceItem (le_t *le)
{
	le_t *actor = NULL;

	assert(LE_IsItem(le));

	/* search owners (there can be many, some of them dead) */
	while ((actor = LE_GetNextInUse(actor))) {
		if ((actor->type == ET_ACTOR || actor->type == ET_ACTOR2x2)
		 && VectorCompare(actor->pos, le->pos)) {
			if (FLOOR(le))
				FLOOR(actor) = FLOOR(le);
		}
	}

	/* the le is an ET_ITEM entity, this entity is there to render dropped items
	 * if there are no items in the floor container, this entity can be
	 * deactivated */
	if (FLOOR(le)) {
		const objDef_t *biggest = LE_BiggestItem(FLOOR(le));
		le->model1 = cls.modelPool[biggest->idx];
		if (!le->model1)
			Com_Error(ERR_DROP, "Model for item %s is not precached in the cls.model_weapons array",
				biggest->id);
		Grid_PosToVec(clMap, le->fieldSize, le->pos, le->origin);
		VectorSubtract(le->origin, biggest->center, le->origin);
		le->angles[ROLL] = 90;
		/*le->angles[YAW] = 10*(int)(le->origin[0] + le->origin[1] + le->origin[2]) % 360; */
		le->origin[2] -= GROUND_DELTA;
	} else {
		/* If no items in floor inventory, don't draw this le - the container is
		 * maybe empty because an actor picked up the last items here */
		le->removeNextFrame = qtrue;
	}
}

/**
 * @param[in] fd The grenade fire definition
 * @param[in] flags bitmask: SF_BODY, SF_IMPACT, SF_BOUNCING, SF_BOUNCED
 * @param[in] muzzle starting/location vector
 * @param[in] v0 velocity vector
 * @param[in] dt delta seconds
 */
void LE_AddGrenade (const fireDef_t *fd, int flags, const vec3_t muzzle, const vec3_t v0, int dt, le_t* leVictim)
{
	le_t *le;
	vec3_t accel;

	/* add le */
	le = LE_Add(0);
	if (!le)
		return;
	le->invis = !cl_leshowinvis->integer;

	/* bind particle */
	VectorSet(accel, 0, 0, -GRAVITY);
	le->ptl = CL_ParticleSpawn(fd->projectile, 0, muzzle, v0, accel);
	if (!le->ptl) {
		le->inuse = qfalse;
		return;
	}
	/* particle properties */
	VectorSet(le->ptl->angles, 360 * crand(), 360 * crand(), 360 * crand());
	VectorSet(le->ptl->omega, 500 * crand(), 500 * crand(), 500 * crand());

	/* think function */
	if (flags & SF_BODY) {
		le->ref1 = fd->hitBody;
		le->ref2 = fd->hitBodySound;
		le->ref3 = leVictim;
	} else if (flags & SF_IMPACT || (fd->splrad && !fd->bounce)) {
		le->ref1 = fd->impact;
		le->ref2 = fd->impactSound;
	} else {
		le->ref1 = NULL;
		if (flags & SF_BOUNCING)
			le->ref2 = fd->bounceSound;
	}

	le->endTime = cl.time + dt;
	/* direction - bytedirs index (0,0,1) */
	le->dir = 5;
	le->fd = fd;
	assert(fd);
	LE_SetThink(le, LET_Projectile);
	LE_ExecuteThink(le);
}

/**
 * @brief Add function for brush models
 * @sa LE_AddToScene
 */
qboolean LE_BrushModelAction (le_t * le, entity_t * ent)
{
	switch (le->type) {
	case ET_ROTATING:
	case ET_DOOR:
		/* These cause the model to render correctly */
		VectorCopy(ent->mins, le->mins);
		VectorCopy(ent->maxs, le->maxs);
		VectorCopy(ent->origin, le->origin);
		VectorCopy(ent->angles, le->angles);
		break;
	case ET_BREAKABLE:
		break;
	default:
		break;
	}

	return qtrue;
}

void LET_BrushModel (le_t *le)
{
	/** @todo what is le->speed for a brush model? */
	if (cl.time - le->thinkDelay < le->speed[0]) {
		le->thinkDelay = cl.time;
		return;
	}

	if (le->type == ET_ROTATING) {
		const float angle = le->angles[le->dir] + (1.0 / le->rotationSpeed);
		le->angles[le->dir] = (angle >= 360.0 ? angle - 360.0 : angle);
	}
}

void LMT_Init (localModel_t* localModel)
{
	if (localModel->target[0] != '\0') {
		localModel->parent = LM_GetByID(localModel->target);
		if (!localModel->parent)
			Com_Error(ERR_DROP, "Could not find local model entity with the id: '%s'.", localModel->target);
	}

	/* no longer needed */
	localModel->think = NULL;
}

/**
 * @brief Adds ambient sounds from misc_sound entities
 * @sa CL_SpawnParseEntitystring
 */
void LE_AddAmbientSound (const char *sound, const vec3_t origin, int levelflags, float volume)
{
	le_t* le;
	s_sample_t* sample;

	if (strstr(sound, "sound/"))
		sound += 6;

	sample = S_LoadSample(sound);
	if (!sample)
		return;

	le = LE_Add(0);
	if (!le) {
		Com_Printf("Could not add ambient sound entity\n");
		return;
	}
	le->type = ET_SOUND;
	le->sample = sample;
	VectorCopy(origin, le->origin);
	le->invis = !cl_leshowinvis->integer;
	le->levelflags = levelflags;

	if (volume < 0.0f || volume > 1.0f) {
		le->volume = SND_VOLUME_DEFAULT;
		Com_Printf("Invalid volume for local entity given - only values between 0.0 and 1.0 are valid\n");
	} else {
		le->volume = volume;
	}

	Com_DPrintf(DEBUG_SOUND, "Add ambient sound '%s' with volume %f\n", sound, volume);
}

/*===========================================================================
 LE Management functions
=========================================================================== */

/**
 * @brief Add a new local entity to the scene
 * @param[in] entnum The entity number (server side)
 * @sa LE_Get
 */
le_t *LE_Add (int entnum)
{
	le_t *le = NULL;

	while ((le = LE_GetNext(le))) {
		if (!le->inuse)
			/* found a free LE */
			break;
	}

	/* list full, try to make list longer */
	if (!le) {
		if (cl.numLEs >= MAX_EDICTS) {
			/* no free LEs */
			Com_Error(ERR_DROP, "Too many LEs");
		}

		/* list isn't too long */
		le = &cl.LEs[cl.numLEs];
		cl.numLEs++;
	}

	/* initialize the new LE */
	memset(le, 0, sizeof(*le));
	le->inuse = qtrue;
	le->entnum = entnum;
	le->fieldSize = ACTOR_SIZE_NORMAL;
	return le;
}

void _LE_NotFoundError (const int entnum, const char *file, const int line)
{
	Cmd_ExecuteString("debug_listle");
	Com_Error(ERR_DROP, "LE_NotFoundError: Could not get LE with entnum %i (%s:%i)\n", entnum, file, line);
}

/**
 * @brief Center the camera on the local entity's origin
 * @param le The local entity which origin is used to center the camera
 * @sa CL_CenterView
 * @sa CL_ViewCenterAtGridPosition
 * @sa CL_CameraRoute
 */
void LE_CenterView (const le_t *le)
{
	/* if (cl_centerview->integer == 1 && cl.actTeam != cls.team) */
	if (!cl_centerview->integer)
		return;

	assert(le);
	Cvar_SetValue("cl_worldlevel", le->pos[2]);
	VectorCopy(le->origin, cl.cam.origin);
}

/**
 * @brief Searches all local entities for the one with the searched entnum
 * @param[in] entnum The entity number (server side)
 * @sa LE_Add
 */
le_t *LE_Get (int entnum)
{
	le_t *le = NULL;

	if (entnum == SKIP_LOCAL_ENTITY)
		return NULL;

	while ((le = LE_GetNextInUse(le))) {
		if (le->entnum == entnum)
			/* found the LE */
			return le;
	}

	/* didn't find it */
	return NULL;
}

/**
 * @brief Checks if a given le_t structure is locked, i.e., used by another event at this time.
 * @param entnum the entnum of the le_t struct involved.
 * @return true if the le_t is locked (used by another event), false if it's not or if it doesn't exist.
 */
qboolean LE_IsLocked (int entnum)
{
	le_t *le = LE_Get(entnum);
	return (le != NULL && le->locked);
}

/**
 * @brief Markes a le_t struct as locked.  Should be called at the
 *  beginning of an event handler on this le_t, and paired with a LE_Unlock at the end.
 * @param le The struct to be locked.
 * @note Always make sure you call LE_Unlock at the end of the event
 *  (might be in a different function), to allow other events on this le_t.
 */
void LE_Lock (le_t *le)
{
	if (le->locked)
		Com_Error(ERR_DROP, "LE_Lock: Trying to lock %i which is already locked\n", le->entnum);

	le->locked = qtrue;
}

/**
 * @brief Unlocks a previously locked le_t struct.
 * @param le The le_t to unlock.
 * @note Make sure that this is always paired with the corresponding
 *  LE_Lock around the conceptual beginning and ending of a le_t event.
 *  Should never be called by the handler(s) of a different event than
 *  the one that locked le.  The owner of the lock is currently not
 *  checked.
 * @todo If the event loop ever becomes multithreaded, this should
 *  be a real mutex lock.
 */
void LE_Unlock (le_t *le)
{
	if (!le->locked)
		Com_Error(ERR_DROP, "LE_Unlock: Trying to unlock %i which is already unlocked\n", le->entnum);

	le->locked = qfalse;
}

/**
 * @brief Searches a local entity on a given grid field
 * @param[in] pos The grid pos to search for an item of the given type
 */
le_t *LE_GetFromPos (const pos3_t pos)
{
	le_t *le = NULL;

	while ((le = LE_GetNextInUse(le))) {
		if (VectorCompare(le->pos, pos))
			return le;
	}

	/* didn't find it */
	return NULL;
}

/**
 * @brief Iterate through the list of entities
 * @param lastLE The entity found in the previous iteration; if NULL, we start at the beginning
 */
le_t* LE_GetNext (le_t* lastLE)
{
	le_t* endOfLEs = &cl.LEs[cl.numLEs];
	le_t* le;

	if (!cl.numLEs)
		return NULL;

	if (!lastLE)
		return cl.LEs;

	assert(lastLE >= cl.LEs);
	assert(lastLE < endOfLEs);

	le = lastLE;

	le++;
	if (le >= endOfLEs)
		return NULL;
	else
		return le;
}

/**
 * @brief Iterate through the entities that are in use
 * @note we can hopefully get rid of this function once we know when it makes sense
 * to iterate through entities that are NOT in use
 * @param lastLE The entity found in the previous iteration; if NULL, we start at the beginning
 */
le_t* LE_GetNextInUse (le_t* lastLE)
{
	le_t* le = lastLE;

	while ((le = LE_GetNext(le))) {
		if (le->inuse)
			break;
	}
	return le;
}

/**
 * @brief Returns entities that have origins within a spherical area.
 * @param[in] from The entity to start the search from. @c NULL will start from the beginning.
 * @param[in] org The origin that is the center of the circle.
 * @param[in] rad radius to search an edict in.
 * @param[in] type Type of local entity. @c ET_NULL to ignore the type.
 */
le_t *LE_FindRadius (le_t *from, const vec3_t org, float rad, entity_type_t type)
{
	le_t *le = from;

	while ((le = LE_GetNextInUse(le))) {
		int j;
		vec3_t eorg;
		for (j = 0; j < 3; j++)
			eorg[j] = org[j] - (le->origin[j] + (le->mins[j] + le->maxs[j]) * 0.5);
		if (VectorLength(eorg) > rad)
			continue;
		if (type != ET_NULL && le->type != type)
			continue;
		return le;
	}

	return NULL;
}

/**
 * @brief Searches a local entity on a given grid field
 * @param[in] type Entity type
 * @param[in] pos The grid pos to search for an item of the given type
 */
le_t *LE_Find (int type, const pos3_t pos)
{
	le_t *le = NULL;

	while ((le = LE_GetNextInUse(le))) {
		if (le->type == type && VectorCompare(le->pos, pos))
			/* found the LE */
			return le;
	}

	/* didn't find it */
	return NULL;
}

/** @sa BoxOffset in cl_actor.c */
#define ModelOffset(i, target) (target[0]=(i-1)*(UNIT_SIZE+BOX_DELTA_WIDTH)/2, target[1]=(i-1)*(UNIT_SIZE+BOX_DELTA_LENGTH)/2, target[2]=0)

/**
 * Origin brush entities are bmodel entities that have their mins/maxs relative to the world origin.
 * The origin vector of the entity will be used to calculate e.g. the culling (and not the mins/maxs like
 * for other entities).
 * @param le The local entity to check
 * @return @c true if the given local entity is a func_door or func_rotating
 */
static inline qboolean LE_IsOriginBrush (const le_t *const le)
{
	return (le->type == ET_DOOR || le->type == ET_ROTATING);
}

/**
 * @sa CL_ViewRender
 * @sa CL_AddUGV
 * @sa CL_AddActor
 */
void LE_AddToScene (void)
{
	le_t *le;
	entity_t ent;
	vec3_t modelOffset;
	int i;

	for (i = 0, le = cl.LEs; i < cl.numLEs; i++, le++) {
		if (le->removeNextFrame) {
			le->inuse = qfalse;
			le->removeNextFrame = qfalse;
		}
		if (le->inuse && !le->invis) {
			if (le->contents & CONTENTS_SOLID) {
				if (!((1 << cl_worldlevel->integer) & le->levelflags))
					continue;
			} else if (le->contents & CONTENTS_DETAIL) {
				/* show them always */
			} else if (le->pos[2] > cl_worldlevel->integer)
				continue;

			memset(&ent, 0, sizeof(ent));

			ent.alpha = le->alpha;

			VectorCopy(le->angles, ent.angles);
			ent.model = le->model1;
			ent.skinnum = le->skinnum;

			switch (le->contents) {
			/* Only breakables do not use their origin; func_doors and func_rotating do!!!
			 * But none of them have animations. */
			case CONTENTS_SOLID:
			case CONTENTS_DETAIL: /* they use mins/maxs */
				break;
			default:
				/* set entity values */
				VectorCopy(le->origin, ent.origin);
				VectorCopy(le->origin, ent.oldorigin);
				/* store animation values */
				ent.as = le->as;
				break;
			}

			if (LE_IsOriginBrush(le)) {
				ent.isOriginBrushModel = qtrue;
				VectorCopy(le->angles, ent.angles);
				VectorCopy(le->origin, ent.origin);
				VectorCopy(le->origin, ent.oldorigin);
			}

			/* Offset the model to be inside the cursor box */
			switch (le->fieldSize) {
			case ACTOR_SIZE_NORMAL:
			case ACTOR_SIZE_2x2:
				ModelOffset(le->fieldSize, modelOffset);
				VectorAdd(ent.origin, modelOffset, ent.origin);
				VectorAdd(ent.oldorigin, modelOffset, ent.oldorigin);
				break;
			default:
				break;
			}

			ent.lighting = &le->lighting;

			/* call add function */
			/* if it returns false, don't draw */
			if (le->addFunc)
				if (!le->addFunc(le, &ent))
					continue;

			/* add it to the scene */
			R_AddEntity(&ent);

			if (cl_le_debug->integer)
				CL_ParticleSpawn("cross", 0, le->origin, NULL, NULL);
		}
	}
}

/**
 * @brief Cleanup unused LE inventories that the server sent to the client
 * also free some unused LE memory
 */
void LE_Cleanup (void)
{
	int i;
	le_t *le;

	Com_DPrintf(DEBUG_CLIENT, "LE_Cleanup: Clearing up to %i unused LE inventories\n", cl.numLEs);
	for (i = cl.numLEs - 1, le = &cl.LEs[cl.numLEs - 1]; i >= 0; i--, le--) {
		if (!le->inuse)
			continue;
		if (LE_IsActor(le))
			CL_ActorCleanup(le);
		else if (LE_IsItem(le))
			cls.i.EmptyContainer(&cls.i, &le->i, INVDEF(csi.idFloor));

		le->inuse = qfalse;
	}
}

#ifdef DEBUG
/**
 * @brief Shows a list of current know local entities with type and status
 */
void LE_List_f (void)
{
	int i;
	le_t *le;

	Com_Printf("number | entnum | type | inuse | invis | pnum | team | size |  HP | state | level | model/ptl\n");
	for (i = 0, le = cl.LEs; i < cl.numLEs; i++, le++) {
		Com_Printf("#%5i | #%5i | %4i | %5i | %5i | %4i | %4i | %4i | %3i | %5i | %5i | ",
			i, le->entnum, le->type, le->inuse, le->invis, le->pnum, le->team,
			le->fieldSize, le->HP, le->state, le->levelflags);
		if (le->type == ET_PARTICLE) {
			if (le->ptl)
				Com_Printf("%s\n", le->ptl->ctrl->name);
			else
				Com_Printf("no ptl\n");
		} else if (le->model1)
			Com_Printf("%s\n", le->model1->name);
		else
			Com_Printf("no mdl\n");
	}
}

/**
 * @brief Shows a list of current know local models
 */
void LM_List_f (void)
{
	int i;
	localModel_t *lm;

	Com_Printf("number | entnum | skin | frame | lvlflg | renderflags | origin          | name\n");
	for (i = 0, lm = cl.LMs; i < cl.numLMs; i++, lm++) {
		Com_Printf("#%5i | #%5i | #%3i | #%4i | %6i | %11i | %5.0f:%5.0f:%3.0f | %s\n",
			i, lm->entnum, lm->skin, lm->frame, lm->levelflags, lm->renderFlags,
			lm->origin[0], lm->origin[1], lm->origin[2], lm->name);
	}
}

#endif

/*===========================================================================
 LE Tracing
=========================================================================== */

/** @brief Client side moveclip */
typedef struct {
	vec3_t boxmins, boxmaxs;	/**< enclose the test object along entire move */
	const float *mins, *maxs;	/**< size of the moving object */
	const float *start, *end;
	trace_t trace;
	const le_t *passle, *passle2;		/**< ignore these for clipping */
	int contentmask;			/**< search these in your trace - see MASK_* */
} moveclip_t;

/**
 * @brief Returns a headnode that can be used for testing or clipping an
 * object of mins/maxs size.
 * Offset is filled in to contain the adjustment that must be added to the
 * testing object's origin to get a point to use with the returned hull.
 * @param[in] le The local entity to get the bmodel from
 * @param[out] tile The maptile the bmodel belongs, too
 * @param[out] rmaShift the shift vector in case of an RMA (needed for doors)
 * @return The headnode for the local entity
 * @sa SV_HullForEntity
 */
static int CL_HullForEntity (const le_t *le, int *tile, vec3_t rmaShift, vec3_t angles)
{
	/* special case for bmodels */
	if (le->contents & CONTENTS_SOLID) {
		cBspModel_t *model = cl.model_clip[le->modelnum1];
		/* special value for bmodel */
		assert(le->modelnum1 < MAX_MODELS);
		if (!model)
			Com_Error(ERR_DROP, "CL_HullForEntity: Error - le with NULL bmodel (%i)\n", le->type);
		*tile = model->tile;
		VectorCopy(le->angles, angles);
		VectorCopy(model->shift, rmaShift);
		return model->headnode;
	} else {
		/* might intersect, so do an exact clip */
		*tile = 0;
		VectorCopy(vec3_origin, angles);
		VectorCopy(vec3_origin, rmaShift);
		return CM_HeadnodeForBox(*tile, le->mins, le->maxs);
	}
}

/**
 * @brief Clip against solid entities
 * @sa CL_Trace
 * @sa SV_ClipMoveToEntities
 */
static void CL_ClipMoveToLEs (moveclip_t * clip)
{
	le_t *le = NULL;

	if (clip->trace.allsolid)
		return;

	while ((le = LE_GetNextInUse(le))) {
		int tile = 0;
		trace_t trace;
		int headnode;
		vec3_t angles;
		vec3_t origin, shift;

		if (!(le->contents & clip->contentmask))
			continue;
		if (le == clip->passle || le == clip->passle2)
			continue;

		headnode = CL_HullForEntity(le, &tile, shift, angles);
		assert(headnode < MAX_MAP_NODES);

		VectorCopy(le->origin, origin);

		trace = CM_HintedTransformedBoxTrace(tile, clip->start, clip->end, clip->mins, clip->maxs, headnode, clip->contentmask, 0, origin, angles, shift, 1.0);

		if (trace.fraction < clip->trace.fraction) {
			qboolean oldStart;

			/* make sure we keep a startsolid from a previous trace */
			oldStart = clip->trace.startsolid;
			trace.le = le;
			clip->trace = trace;
			clip->trace.startsolid |= oldStart;
		/* if true, plane is not valid */
		} else if (trace.allsolid) {
			trace.le = le;
			clip->trace = trace;
		/* if true, the initial point was in a solid area */
		} else if (trace.startsolid) {
			trace.le = le;
			clip->trace.startsolid = qtrue;
		}
	}
}


/**
 * @brief Create the bounding box for the entire move
 * @param[in] start Start vector to start the trace from
 * @param[in] mins Bounding box used for tracing
 * @param[in] maxs Bounding box used for tracing
 * @param[in] end End vector to stop the trace at
 * @param[out] boxmins The resulting box mins
 * @param[out] boxmaxs The resulting box maxs
 * @sa CL_Trace
 * @note Box is expanded by 1
 */
static inline void CL_TraceBounds (const vec3_t start, const vec3_t mins, const vec3_t maxs, const vec3_t end, vec3_t boxmins, vec3_t boxmaxs)
{
	int i;

	for (i = 0; i < 3; i++) {
		if (end[i] > start[i]) {
			boxmins[i] = start[i] + mins[i] - 1;
			boxmaxs[i] = end[i] + maxs[i] + 1;
		} else {
			boxmins[i] = end[i] + mins[i] - 1;
			boxmaxs[i] = start[i] + maxs[i] + 1;
		}
	}
}

/**
 * @brief Moves the given mins/maxs volume through the world from start to end.
 * @note Passedict and edicts owned by passedict are explicitly not checked.
 * @sa CL_TraceBounds
 * @sa CL_ClipMoveToLEs
 * @sa SV_Trace
 * @param[in] start Start vector to start the trace from
 * @param[in] end End vector to stop the trace at
 * @param[in] mins Bounding box used for tracing
 * @param[in] maxs Bounding box used for tracing
 * @param[in] passle Ignore this local entity in the trace (might be NULL)
 * @param[in] passle2 Ignore this local entity in the trace (might be NULL)
 * @param[in] contentmask Searched content the trace should watch for
 * @param[in] worldLevel The worldlevel (0-7) to calculate the levelmask for the trace from
 */
trace_t CL_Trace (const vec3_t start, const vec3_t end, const vec3_t mins, const vec3_t maxs, const le_t * passle, le_t * passle2, int contentmask, int worldLevel)
{
	moveclip_t clip;

	/* clip to world */
	clip.trace = TR_CompleteBoxTrace(start, end, mins, maxs, (1 << (worldLevel + 1)) - 1, contentmask, 0);
	clip.trace.le = NULL;
	if (clip.trace.fraction == 0)
		return clip.trace;		/* blocked by the world */

	clip.contentmask = contentmask;
	clip.start = start;
	clip.end = end;
	clip.mins = mins;
	clip.maxs = maxs;
	clip.passle = passle;
	clip.passle2 = passle2;

	/* create the bounding box of the entire move */
	CL_TraceBounds(start, mins, maxs, end, clip.boxmins, clip.boxmaxs);

	/* clip to other solid entities */
	CL_ClipMoveToLEs(&clip);

	return clip.trace;
}
