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

#include "d_bot.h"
#include "d_player.h"
#include "m_random.h"
#include "g_game.h"
#include "doomstat.h"
#include "assert.h"

#define bot_control (!demoplayback && !democontinue && netgame)


extern  player_t  players[MAXPLAYERS];
extern  dboolean  playeringame[MAXPLAYERS];



void D_PRBotInit(mbot_t *mbot, int playernum) {
  mbot->avoid = NULL;
  mbot->enemy = NULL;
  mbot->want = NULL;

  mbot->player = &players[playernum];
  mbot->mobj = players[playernum].mo;
  mbot->playernum = playernum;
  
  mbot->state = BST_LOOK; // initial state, may change
  mbot->stcounter = 0;

  // note: add more assignments as the mbot_t struct grows
}


mbot_t *D_PRBotSpawn(void) { // spawns a bot to the game
  int next_player = 0;

  while (next_player < MAXPLAYERS) {
    if (playeringame[next_player])
      continue;
    
    player_t *player = &players[next_player];

    player->playerstate = PST_REBORN;

    D_PRBotInit(&bots[next_player], next_player);
    // D_PRBotTic_Live
  }

  return NULL; // couldn't spawn bot; not enough player slots!
}

static inline void D_PRBotSanitizeState(mbot_t *bot) {
  if (bot->mobj->health <= 0) {
    if (bot->player->playerstate == PST_DEAD && bot->state != BST_DEAD) {
      bot->stcounter = 20;
      bot->state = BST_DEAD;
    }
    return;
  }

  switch (bot->state) {
    BST_HUNT: // don't hunt in dm
      if (!deathmatch) {
        bot->state = BST_LOOK;
        break; // (don't break if dm)
      }

    BST_KILL: // don't hunt or kill if no enemy to hunt or kill!
      if (bot->enemy == NULL)
        bot->state = BST_LOOK;
      break;
    //-----^

    BST_RETREAT:
      if (bot -> enemy == NULL) {
        if (bot -> avoid == NULL)
          bot->state = BST_LOOK;
        else
          bot->state = BST_CAUTIOUS;
      }
      break;
    //-----^
  }
}

inline void D_PRBotTic_Look(mbot_t *bot) {
  // todo
}

inline void D_PRBotTic_Retreat(mbot_t *bot) {
  // todo
}

inline void D_PRBotTic_Live(mbot_t *bot) {
  // todo
}

inline void D_PRBotTic_Cautious(mbot_t *bot) {
  // todo
}

void D_PRBot_DoReborn(mbot_t *bot) {
  if (!netgame) {
    // remove ticker and forget
    
  }

  else {
    // respawn and reinitialize bot
    bot->player->playerstate = PST_REBORN;
    D_PRBotInit(bot, bot->playernum);
  }
}

void D_PRBotTic(mbot_t *bot) {
    assert(bot->mobj != NULL && "tried to tic bot with mbot_t::mobj not set");

    if (!bot_control) return;

    if (bot->stcounter > 0)
      bot->stcounter--;

    // todo: have bots do something!

    D_PRBotSanitizeState(bot); // don't attack or retreat from imaginary enemies, etc
    D_PRBotTic_Live(bot); // shudder and turn a bit so bots don't look like inanimate voodoo dolls when lazying around

    switch (bot->state) {
      case BST_DEAD:
        if (bot->stcounter <= 0) D_PRBot_DoReborn(bot);
        return;
      //======!

      case BST_LOOK:
        D_PRBotTic_Look(bot);
        break;
      //-----^

      case BST_RETREAT:
        D_PRBotTic_Retreat(bot);
        break;
      //-----^

      case BST_CAUTIOUS:
        D_PRBotTic_Cautious(bot);
        break;
      //-----^

      case BST_HUNT:
        D_PRBotTic_Hunt(bot);
        break;
      //-----^

      case BST_KILL:
        D_PRBotTic_Kill(bot);
        break;
      //-----^
    }
}

void P_PRBotThinker(void) {
  int i;
  for (i = 0; i < MAXPLAYERS; i++) {
    if (bots[i].mobj != NULL && bots[i].player != NULL)
      D_PRBotTic(&bots[i]);
  }
}