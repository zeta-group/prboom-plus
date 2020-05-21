/* Emacs style mode select   -*- C++ -*-
 *-----------------------------------------------------------------------------
 *
 *
 *  PrBoom: a Doom port merged with LxDoom and LSDLDoom
 *  based on BOOM, a modified and improved DOOM engine
 *  Copyright (C) 1999 by
 *  id Software, Chi Hoang, Lee Killough, Jim Flynn, Rand Phares, Ty Halderman
 *  Copyright (C) 1999-2004 by
 *  Jess Haas, Nicolas Kalkhof, Colin Phipps, Florian Schulze
 *  Copyright 2005, 2006 by
 *  Florian Schulze, Colin Phipps, Neil Stevens, Andrey Budko
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 *  02111-1307, USA.
 *
 * DESCRIPTION:
 *  PRBots, the routine (D_PRBotTic) that runs every tic if
 *  -bot is supplied in the terminal.
 *
 *-----------------------------------------------------------------------------
 */

#define BOTDEBUG

#include "p_mobj.h"
#include "d_player.h"



typedef enum {
  BST_NONE,       // no bot here!
  BST_PREINIT,    // waiting for mobj initialization
  BST_LOOK,       // look around the map for something to do
  BST_HUNT,       // hunt down an enemy (equivalent to BST_LOOK in coop)
  BST_RETREAT,    // retreat from action
  BST_KILL,       // kill enemy
  BST_CAUTIOUS,   // avoid projectiles and/or damaging sectors while only attacking immediate
  BST_LEAVE,      // exit damaging sector
  BST_DEAD,       // dead, no-op
} botstate_t;


// A PRBot.
typedef struct {
  // basic elements
  player_t *player; // player represented
  mobj_t *mobj;     // player object
  int playernum;    // self-explanatory
  int gametics;     // tic counter
  
  // AI
  mobj_t *enemy;    // for monsters or other players
  mobj_t *want;     // for items
  mobj_t *avoid;    // for damaging things or dangerously big monsters
  int exploration;  // added to when enemies killed, secrets found, or special linedefs activated; decremented every 4 tics

  // state machine
  botstate_t state; // bot state
  int stcounter;    // counter for some states

  int lastseenx;
  int lastseeny;    // last seen coordinates of enemy (BST_HUNT)
} mbot_t;

mbot_t bots[MAXPLAYERS];

void D_PRBot_DoReborn(mbot_t *bot);

void D_PRBotClear(mbot_t *mbot);
void D_PRBotCallInit(mbot_t *mbot, int playernum);
void D_PRBotDeinit(mbot_t *mbot);

dboolean D_PRBot_LookToward(mbot_t *bot, mobj_t *lookee);

mbot_t *D_PRBotReplace(int playernum);
mbot_t *D_PRBotSpawn(void); // spawns a bot to the game

void D_PRBotTic_Live(mbot_t *bot);
void D_PRBotTic_Look(mbot_t *bot);
void D_PRBotTic_Retreat(mbot_t *bot);
void D_PRBotTic_Cautious(mbot_t *bot);
void D_PRBotTic_Hunt(mbot_t *bot);
void D_PRBotTic_Kill(mbot_t *bot);
void D_PRBotTic_Leave(mbot_t *bot);

void P_PRBot_Ticker(void);