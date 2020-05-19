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

#include "p_mobj.h"
#include "d_player.h"



typedef enum {
  BST_LOOK,       // look around the map for something to do
  BST_HUNT,       // hunt down an enemy (equivalent to BST_LOOK in coop)
  BST_RETREAT,    // retreat from action
  BST_KILL,       // kill enemy
  BST_CAUTIOUS,   // avoid projectiles and/or damaging sectors while only attacking immediate
  BST_DEAD,       // dead, no-op
} botstate_t;


typedef struct {
  // the player the bot represents
  // (NULL if no bot)
  player_t *player;
  mobj_t *mobj;
  int playernum;
  
  // bot AI
  mobj_t *enemy; // for monsters or other players
  mobj_t *want;  // for items
  mobj_t *avoid; // for damaging things or dangerously big monsters

  botstate_t state; // bot state
  int stcounter;    // counter for some states
} mbot_t;

mbot_t bots[MAXPLAYERS];

void D_PRBot_DoReborn(mbot_t *bot);

void D_PRBotInit(mbot_t *mbot, int playernum);
mbot_t *D_PRBotSpawn(void); // spawns a bot to the game

inline void D_PRBotTic_Live(mbot_t *bot);
inline void D_PRBotTic_Look(mbot_t *bot);
inline void D_PRBotTic_Retreat(mbot_t *bot);
inline void D_PRBotTic_Cautious(mbot_t *bot);

void D_PRBotTic(mbot_t *bot);