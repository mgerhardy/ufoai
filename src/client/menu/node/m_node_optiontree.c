/**
 * @file m_node_optiontree.c
 * @todo manage disabled option
 */

/*
Copyright (C) 1997-2008 UFO:AI Team

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

#include "../m_main.h"
#include "../m_parse.h"
#include "../m_actions.h"
#include "../m_font.h"
#include "../m_render.h"
#include "m_node_abstractoption.h"
#include "m_node_abstractnode.h"
#include "m_node_optiontree.h"
#include "m_node_panel.h"

#include "../../client.h" /* gettext _() */

#define CORNER_SIZE 25
#define MID_SIZE 1
#define MARGE 3

#define ELEMENT_HEIGHT 20	/**< @todo remove it, should be computed with the font */

#define COLLAPSEBUTTON_WIDTH 20		/**< Size used for the collapse button */
#define DEPTH_WIDTH 25				/**< Width between each depth level */

/**
 * @brief update option cache about child, according to collapse and visible status
 * @note can be a common function for all option node
 * @return number of visible elements
 */
static int MN_OptionTreeNodeUpdateCache (menuOption_t* option)
{
	int count = 0;
	while (option) {
		int localCount = 0;
		if (option->invis) {
			option = option->next;
			continue;
		}
		if (option->collapsed) {
			option->childCount = 0;
			option = option->next;
			count++;
			continue;
		}
		if (option->firstChild)
			localCount = MN_OptionTreeNodeUpdateCache(option->firstChild);
		option->childCount = localCount;
		count += 1 + localCount;
		option = option->next;
	}
	return count;
}

/**
 * @brief Return the first option of the node
 * @todo check versionId and update cached data, and fire events
 */
static menuOption_t*  MN_OptionTreeNodeGetFirstOption (menuNode_t * node)
{
	if (node->u.option.first) {
		return node->u.option.first;
	} else {
		const int v = MN_GetDataVersion(node->u.option.dataId);
		menuOption_t *option = MN_GetOption(node->u.option.dataId);
		if (v != node->u.option.dataId) {
			int count = 0;
			if (option)
				count = MN_OptionTreeNodeUpdateCache(option);
			if (node->u.option.count != count) {
				node->u.option.count = count;
				MN_ExecuteEventActions(node, node->u.option.onViewChange);
			}
			node->u.option.versionId = v;
		}
		return option;
	}
}

#define MAX_DEPTH_OPTIONITERATORCACHE 8

typedef struct {
	menuOption_t* option;	/**< current option */
	menuOption_t* depthCache[MAX_DEPTH_OPTIONITERATORCACHE];	/**< parent link */
	int depthPos;	/**< current cache position */
} menuOptionIterator_t;

/**
 * @brief find an option why index (0 is the first option)
 */
static menuOption_t* MN_OptionTreeFindFirstOption (int pos, menuOption_t* option, menuOptionIterator_t* iterator)
{
	while (option) {
		if (option->invis) {
			option = option->next;
			continue;
		}

		/* we are on the right element */
		if (pos == 0) {
			iterator->option = option;
			return option;
		}

		/* not the parent */
		pos--;

		if (option->collapsed) {
			option = option->next;
			continue;
		}

		/* its a child */
		if (pos < option->childCount) {
			if(iterator->depthPos >= MAX_DEPTH_OPTIONITERATORCACHE)
				assert(qfalse);
			iterator->depthCache[iterator->depthPos] = option;
			iterator->depthPos++;
			return MN_OptionTreeFindFirstOption(pos, option->firstChild, iterator);
		}
		pos -= option->childCount;
		option = option->next;
	}

	iterator->option = NULL;
	return NULL;
}

/**
 * @breif Find the next element from the iterator
 */
static menuOption_t* MN_OptionTreeNextOption (menuOptionIterator_t* iterator)
{
	menuOption_t* option;

	option = iterator->option;
	assert(iterator->depthPos < MAX_DEPTH_OPTIONITERATORCACHE);
	iterator->depthCache[iterator->depthPos] = option;
	iterator->depthPos++;

	if (option->collapsed)
		option = NULL;
	else
		option = option->firstChild;

	while (qtrue) {
		while (option) {
			if (!option->invis) {
				iterator->option = option;
				return option;
			}
			option = option->next;
		}
		if (iterator->depthPos == 0)
			break;
		iterator->depthPos--;
		option = iterator->depthCache[iterator->depthPos]->next;
	}

	iterator->option = NULL;
	return NULL;
}

static void MN_OptionTreeNodeDraw (menuNode_t *node)
{
	static const int panelTemplate[] = {
		CORNER_SIZE, MID_SIZE, CORNER_SIZE,
		CORNER_SIZE, MID_SIZE, CORNER_SIZE,
		MARGE
	};
	menuOption_t* option;
	const char *ref;
	const char *font;
	vec2_t pos;
	const char* image;
	int elementHeight = ELEMENT_HEIGHT;	/**< fix it with text font */
	int currentY;
	const float *textColor;
	static vec4_t disabledColor = {0.5, 0.5, 0.5, 1.0};
	int count = 0;
	menuOptionIterator_t iterator;

	memset(&iterator, 0, sizeof(iterator));

	if (!node->cvar)
		return;

	MN_GetNodeAbsPos(node, pos);

	image = MN_GetReferenceString(node, node->image);
	if (image)
		MN_DrawPanel(pos, node->size, image, 0, 0, panelTemplate);

	ref = MN_GetReferenceString(node, node->cvar);
	font = MN_GetFontFromNode(node);
	currentY = pos[1] + node->padding;

	/* skip option over current position */
	option = MN_OptionTreeNodeGetFirstOption(node);
	option = MN_OptionTreeFindFirstOption(node->u.option.pos, option, &iterator);

	/* draw all available options for this selectbox */
	for (; option; option = MN_OptionTreeNextOption(&iterator)) {
		int decX;
		const char *collapseString;
		/* outside the node */
		if (currentY + elementHeight > pos[1] + node->size[1] - node->padding) {
			count++;
			break;
		}

		/* draw the hover effect */
		if (option->hovered)
			MN_DrawFill(pos[0] + node->padding, currentY, node->size[0] - node->padding - node->padding, elementHeight, node->color);

		/* text color */
		if (!strcmp(option->value, ref)) {
			textColor = node->selectedColor;
		} else if (node->disabled) {
			textColor = disabledColor;
		} else {
			textColor = node->color;
		}

		/* print the option label */
		R_Color(textColor);
		decX = iterator.depthPos * DEPTH_WIDTH;

		if (option->firstChild) {
			collapseString = (option->collapsed)?"[+]":"[-]";
			MN_DrawString(font, ALIGN_UL, pos[0] + node->padding + decX, currentY,
				pos[0], currentY, node->size[0] - node->padding - node->padding, node->size[1],
				0, collapseString, 0, 0, NULL, qfalse, LONGLINES_PRETTYCHOP);
		}


		decX += COLLAPSEBUTTON_WIDTH;
		MN_DrawString(font, ALIGN_UL, pos[0] + node->padding + decX, currentY,
			pos[0], currentY, node->size[0] - node->padding - node->padding, node->size[1],
			0, _(option->label), 0, 0, NULL, qfalse, LONGLINES_PRETTYCHOP);

		/* next entries' position */
		currentY += elementHeight;
		count++;
	}
	R_Color(NULL);
}

static menuOption_t* MN_OptionTreeNodeGetOptionAtPosition (menuNode_t * node, int x, int y, int *depth)
{
	menuOption_t* option;
	const int elementHeight = ELEMENT_HEIGHT;	/**< @todo fix it with text font */
	int count = 0;
	menuOptionIterator_t iterator;
	memset(&iterator, 0, sizeof(iterator));

	MN_NodeAbsoluteToRelativePos(node, &x, &y);

	option = MN_OptionTreeNodeGetFirstOption(node);
	count = node->u.option.pos + (y - node->padding) / elementHeight;
	option = MN_OptionTreeFindFirstOption(count, option, &iterator);
	*depth = iterator.depthPos;
	return option;
}

/**
 * @brief Handles selectboxes clicks
 */
static void MN_OptionTreeNodeClick (menuNode_t * node, int x, int y)
{
	menuOption_t* option;
	int depth;

	/* the cvar string is stored in dataModelSkinOrCVar */
	/* no cvar given? */
	if (!node->cvar || !*(char*)node->cvar) {
		Com_Printf("MN_OptionTreeNodeClick: node '%s' doesn't have a valid cvar assigned\n", MN_GetPath(node));
		return;
	}

	/* no cvar? */
	if (strncmp((const char *)node->cvar, "*cvar", 5))
		return;

	/* select the right option */
	option = MN_OptionTreeNodeGetOptionAtPosition(node, x, y, &depth);

	MN_NodeAbsoluteToRelativePos(node, &x, &y);

	/* extend/collapse*/
	x -= depth * DEPTH_WIDTH;
	if (x >= 0 && x < COLLAPSEBUTTON_WIDTH) {
		if (option && option->firstChild) {
			int count;
			option->collapsed = !option->collapsed;
			option = MN_OptionTreeNodeGetFirstOption(node);
			count = MN_OptionTreeNodeUpdateCache(option);
			if (node->u.option.count != count) {
				node->u.option.count = count;
				MN_ExecuteEventActions(node, node->u.option.onViewChange);
			}
		}
		return;
	}

	/* update the status */
	if (option) {
		const char *cvarName = &((const char *)node->cvar)[6];
		MN_SetCvar(cvarName, option->value, 0);
		if (option->action[0] != '\0') {
#ifdef DEBUG
			if (option->action[strlen(option->action) - 1] != ';')
				Com_Printf("MN_OptionTreeNodeClick: Option with none terminated action command (%s)\n", MN_GetPath(node));
#endif
			Cbuf_AddText(option->action);
		}
	}
}

/**
 * @brief Called before loading. Used to set default attribute values
 */
static void MN_OptionTreeNodeLoading (menuNode_t *node)
{
	Vector4Set(node->color, 1, 1, 1, 1);
	node->u.option.versionId = -1;
	node->padding = 3;
}

static void MN_OptionTreeNodeLoaded (menuNode_t *node)
{
}

#ifdef DEBUG
static void MN_InitInlineTest (void)
{
	menuOption_t* options;
	int i;

	options = MN_AllocOption(30);
	for (i = 0; i < 30; i++) {
		const char *value = va("Foo%i", i);
		MN_InitOption(&options[i], value, value, value);
	}

	/* 6*5 sisters options */
	for (i = 0; i < 30; i += 5) {
		options[i + 0].next = &options[i + 1];
		options[i + 1].next = &options[i + 2];
		options[i + 2].next = &options[i + 3];
		options[i + 3].next = &options[i + 4];
	}

	/* create a tree */
	options[0].firstChild = &options[5];
	options[1].firstChild = &options[10];
	options[3].firstChild = &options[15];
	options[5].firstChild = &options[20];
	options[6].firstChild = &options[25];

	MN_RegisterOption(OPTION_TEST, options);
}
#endif

void MN_RegisterOptionTreeNode (nodeBehaviour_t *behaviour)
{
	behaviour->name = "optiontree";
	behaviour->extends = "abstractoption";
	behaviour->draw = MN_OptionTreeNodeDraw;
	behaviour->leftClick = MN_OptionTreeNodeClick;
	behaviour->loading = MN_OptionTreeNodeLoading;
	behaviour->loaded = MN_OptionTreeNodeLoaded;
#ifdef DEBUG
	MN_InitInlineTest();
#endif
}
