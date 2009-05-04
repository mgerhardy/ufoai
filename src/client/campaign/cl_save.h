/**
 * @file cl_save.h
 * @brief Defines some savefile structures
 */

/*
Copyright (C) 2002-2007 UFO: Alien Invasion team.

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

#ifndef CLIENT_CL_SAVE_H
#define CLIENT_CL_SAVE_H

#include "../../common/msg.h"
#include "../mxml/mxml_ufoai.h"

extern cvar_t *cl_lastsave;

#define MAX_SAVESUBSYSTEMS 32

/**
 * HISTORY
 * version id   | game version | compatible with trunk
 * ===================================================
 *          1   | 2.1.1        | no
 *          2   | 2.2          | yes
 *          3   | 2.3          | yes
 */
#define SAVE_FILE_VERSION 3

/* MAX_GAMESAVESIZE has room for 3MB for dynamic data, eg geoscape messages */
#define MAX_GAMESAVESIZE	3145728
#define MAX_COMMENTLENGTH	32

#include <zlib.h>

qboolean SAV_QuickSave(void);
void SAV_Init(void);

/* and now the save and load prototypes for every subsystem */
qboolean B_SaveXML(mxml_node_t *parent);
qboolean B_LoadXML(mxml_node_t *parent);
qboolean CP_SaveXML(mxml_node_t *parent);
qboolean CP_LoadXML(mxml_node_t *parent);
qboolean HOS_LoadXML(mxml_node_t *parent);
qboolean HOS_SaveXML(mxml_node_t *parent);
qboolean BS_SaveXML(mxml_node_t *parent);
qboolean BS_LoadXML(mxml_node_t *parent);
qboolean AIR_SaveXML(mxml_node_t *parent);
qboolean AIR_LoadXML(mxml_node_t *parent);
qboolean AC_SaveXML(mxml_node_t *parent);
qboolean AC_LoadXML(mxml_node_t *parent);
qboolean E_SaveXML(mxml_node_t *parent);
qboolean E_LoadXML(mxml_node_t *parent);
qboolean RS_SaveXML(mxml_node_t *parent);
qboolean RS_LoadXML(mxml_node_t *parent);
qboolean PR_SaveXML(mxml_node_t *parent);
qboolean PR_LoadXML(mxml_node_t *parent);
qboolean MS_SaveXML(mxml_node_t *parent);
qboolean MS_LoadXML(mxml_node_t *parent);
qboolean STATS_SaveXML(mxml_node_t *parent);
qboolean STATS_LoadXML(mxml_node_t *parent);
qboolean NAT_SaveXML(mxml_node_t *parent);
qboolean NAT_LoadXML(mxml_node_t *parent);
qboolean TR_SaveXML(mxml_node_t *parent);
qboolean TR_LoadXML(mxml_node_t *parent);
qboolean AB_SaveXML(mxml_node_t *parent);
qboolean AB_LoadXML(mxml_node_t *parent);
qboolean XVI_SaveXML(mxml_node_t *parent);
qboolean XVI_LoadXML(mxml_node_t *parent);
qboolean INS_SaveXML(mxml_node_t *parent);
qboolean INS_LoadXML(mxml_node_t *parent);
qboolean MSO_SaveXML(mxml_node_t *parent);
qboolean MSO_LoadXML(mxml_node_t *parent);

void B_PostLoadInit(void);

#endif /* CLIENT_CL_SAVE_H */
