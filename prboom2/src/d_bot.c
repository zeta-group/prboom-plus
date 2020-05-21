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

#include "d_main.h"
#include "d_bot.h"
#include "d_player.h"
#include "p_setup.h"
#include "m_random.h"
#include "lprintf.h"
#include "g_game.h"
#include "r_main.h"
#include "p_inter.h"
#include "p_pspr.h"
#include "doomstat.h"
#include "d_items.h"
#include "p_map.h"
#include "p_maputl.h"
#include "assert.h"
#include "p_tick.h"

#define bot_control (!demoplayback && !democontinue && netgame)

extern player_t players[MAXPLAYERS];
extern dboolean playeringame[MAXPLAYERS];

static void D_PRBot_NextState(mbot_t *bot);
static void D_PRBot_PrintState(mbot_t *bot);
void P_PRBot_CheckInits(void);

// copied from p_enemy.c. Why static?!
static dboolean P_IsVisible(mobj_t *actor, mobj_t *mo, dboolean allaround)
{
  if (!allaround)
  {
    angle_t an = R_PointToAngle2(actor->x, actor->y,
                                 mo->x, mo->y) -
                 actor->angle;
    if (an > ANG90 && an < ANG270 &&
        P_AproxDistance(mo->x - actor->x, mo->y - actor->y) > MELEERANGE)
      return false;
  }
  return P_CheckSight(actor, mo);
}

static void D_SetMObj(mobj_t **parm, mobj_t *other)
{
  // use e.g. D_SetMObj(&mbot->enemy, new_enemy)
  if (*parm)
  {
    (*parm)->thinker.references--;
  }

  if (other)
  {
    other->thinker.references++;
  }

  *parm = other;
}

void D_PRBotClear(mbot_t *mbot)
{
  D_SetMObj(&mbot->avoid, NULL);
  D_SetMObj(&mbot->enemy, NULL);
  D_SetMObj(&mbot->want, NULL);

  mbot->state = BST_LOOK;
  mbot->stcounter = 0;
}

void D_PRBotCallInit(mbot_t *mbot, int playernum)
{
  if (mbot->mobj)
  {
    // lynch the impostor!
    P_DamageMobj(mbot->mobj, NULL, NULL, 9000000);
  }

  D_PRBotClear(mbot);

  mbot->state = BST_PREINIT;
  mbot->player = &players[playernum];
  mbot->playernum = playernum;
  mbot->mobj = NULL;
}

static void D_PRBotDoInit(mbot_t *mbot)
{
  D_PRBotClear(mbot); // call a 2nd time to change BST_PREINIT to BST_LOOK

  mbot->mobj = players[mbot->playernum].mo;
  mbot->mobj->flags |= MF_FRIEND;

  D_PRBot_NextState(mbot);
#ifdef BOTDEBUG
  D_PRBot_PrintState(mbot);
#endif
}

// Replaces an existing player with a PRBot (does not change the player's state, e.g. ammo, location, etc)
mbot_t *D_PRBotReplace(int playernum)
{
  mbot_t *mbot = &bots[playernum];

  assert(mbot->state == BST_NONE && "This player is already a bot!");
  assert(playeringame[playernum]);
  assert(players[playernum].mo && "Player's mobj not set when D_PRBotReplace was called!");

  D_PRBotClear(mbot);

  mbot->state = BST_PREINIT;
  mbot->player = &players[playernum];
  mbot->playernum = playernum;
  mbot->mobj = players[mbot->playernum].mo;

  mbot->mobj->flags |= MF_FRIEND;

  D_PRBot_NextState(mbot);
#ifdef BOTDEBUG
  D_PRBot_PrintState(mbot);
#endif

  return mbot;
}

void D_PRBotDeinit(mbot_t *mbot)
{
  playeringame[mbot->playernum] = false;

  mbot->player = NULL;
  mbot->mobj = NULL;
  mbot->playernum = 0;

  D_PRBotClear(mbot);

  mbot->state = BST_NONE;
}

mbot_t *D_PRBotSpawn(void)
{ // spawns a bot to the game
  int next_player = 0;

  while (next_player < MAXPLAYERS)
  {
    if (playeringame[next_player])
    {
      next_player++;
      continue;
    }

#ifdef BOTDEUG
    // I think there is a specific function in the PRBoom+ code
    // that does this that I have to call instead (not I_Error),
    // but I don't recall which one right now.
    printf("D_PRBotSpawn: spawned new PRBot at player start #%d\n", next_player);
#endif

    player_t *player = &players[next_player];

    playeringame[next_player] = true;
    P_SpawnPlayer(next_player, &playerstarts[next_player]);

    D_PRBotCallInit(&bots[next_player], next_player);

    break;
  }

  return NULL; // couldn't spawn bot; not enough player slots!
}

static inline void D_PRBotSanitizeState(mbot_t *bot)
{
  if (bot->mobj->health <= 0)
  {
    if (bot->player->playerstate == PST_DEAD && bot->state != BST_DEAD)
    {
      bot->stcounter = 20;
      bot->state = BST_DEAD;
    }
    return;
  }

  switch (bot->state)
  {
  BST_HUNT: // don't hunt in dm
    if (!deathmatch)
    {
      bot->state = BST_LOOK;
#ifdef BOTDEBUG
      D_PRBot_PrintState(bot);
#endif
      break; // (don't break if dm)
    }

  BST_KILL: // don't hunt or kill if no enemy to hunt or kill!
    if (bot->enemy == NULL)
      bot->state = BST_LOOK;
#ifdef BOTDEBUG
    D_PRBot_PrintState(bot);
#endif
    break;
    //-----^

  BST_RETREAT:
    if (bot->enemy == NULL)
    {
      if (bot->avoid == NULL)
        bot->state = BST_LOOK;
      else
        bot->state = BST_CAUTIOUS;
#ifdef BOTDEBUG
      D_PRBot_PrintState(bot);
#endif
    }
    break;
    //-----^
  }
}

static mobj_t *current_actor;
static mbot_t *current_bot;

// Modified version of PIT_FindTarget.
static dboolean PIT_FindBotTarget(mobj_t *mo)
{
  mobj_t *actor = current_actor;
  mbot_t *bot = current_bot;

  if (!(
          (
              !(mo->flags & MF_FRIEND) ||
              (deathmatch && mo->type == MT_PLAYER)
          ) &&
      mo->health > 0 &&
      (mo->flags & MF_COUNTKILL || mo->type == MT_SKULL || mo->type == MT_PLAYER)
  ))
    return true; // Invalid target

  if (!P_IsVisible(actor, mo, false))
    return true;

  D_SetMObj(&bot->enemy, mo);

  // Move the selected monster to the end of its associated
  // list, so that it gets searched last next time.
  {
    thinker_t *cap = &thinkerclasscap[mo->flags & MF_FRIEND ? th_friends : th_enemies];
    (mo->thinker.cprev->cnext = mo->thinker.cnext)->cprev = mo->thinker.cprev;
    (mo->thinker.cprev = cap->cprev)->cnext = &mo->thinker;
    (mo->thinker.cnext = cap)->cprev = &mo->thinker;
  }

  return false;
}

static dboolean D_PRBot_LookFind(mbot_t *bot)
{
  mobj_t *actor = bot->mobj;

  thinker_t *th;
  thinker_t *cap = &thinkerclasscap[deathmatch ? th_friends : th_enemies];

  // Search for new enemy
  if (cap->cnext != cap) // Empty list? bail out early
  {
    int x = P_GetSafeBlockX(actor->x - bmaporgx);
    int y = P_GetSafeBlockY(actor->y - bmaporgy);
    int d;

    current_actor = actor;
    current_bot = bot;

    // Search first in the immediate vicinity.
    if (!P_BlockThingsIterator(x, y, PIT_FindBotTarget))
      return true;

    for (d = 1; d < 5; d++)
    {
      int i = 1 - d;
      do
        if (!P_BlockThingsIterator(x + i, y - d, PIT_FindBotTarget) ||
            !P_BlockThingsIterator(x + i, y + d, PIT_FindBotTarget))
          return true;
      while (++i < d);
      do
        if (!P_BlockThingsIterator(x - d, y + i, PIT_FindBotTarget) ||
            !P_BlockThingsIterator(x + d, y + i, PIT_FindBotTarget))
          return true;
      while (--i + d >= 0);
    }

    { // Random number of monsters, to prevent patterns from forming
      int n = (P_Random(pr_friends) & 31) + 15;

      for (th = cap->cnext; th != cap; th = th->cnext)
        if (--n < 0)
        {
          // Only a subset of the monsters were searched. Move all of
          // the ones which were searched so far, to the end of the list.

          (cap->cnext->cprev = cap->cprev)->cnext = cap->cnext;
          (cap->cprev = th->cprev)->cnext = cap;
          (th->cprev = cap)->cnext = th;
          break;
        }

        else if (!PIT_FindBotTarget((mobj_t *)th)) // If target sighted
          return true;
    }
  }

  return false;
}

#define MAXAIMOFFS (20 * (ANG45) / 45)
#define TOAIMOFFS(a) ((a)*MAXAIMOFFS / ANG45)

// Aims a bot toward its target.
// Returns whether it thinks it has aimed
// close enough to attempt firing.
dboolean D_PRBot_LookToward(mbot_t *bot, mobj_t *lookee)
{
  ticcmd_t *cmd = &bot->player->cmd;

  angle_t ang_targ = R_PointToAngle2(bot->mobj->x, bot->mobj->y, lookee->x, lookee->y);
  angle_t ang_curr = bot->mobj->angle;

  // check for unsigned wrap-arounds
  angle_t ang_diff = (ang_targ >= ang_curr ? ang_targ - ang_curr : ang_curr - ang_targ); // absolute          (indirected)
  int ang_delta = (ang_targ >= ang_curr ? ang_diff : -ang_diff);                         // from curr to targ (directed)

  if (ang_diff > ANG45)
  {
    // turn toward
    if (ang_delta > 0) // targ - curr > 0; therefore, n > 0, where curr + n = targ
      cmd->angleturn += 3.5;

    else // targ - curr < 0; therefore, n < 0, where curr + n = targ
      cmd->angleturn -= 3.5;
  }

  else
  {
    // adjust slightly (with 'realistic' aiming errors)

    int aim_amount = ang_diff * 0.8;

    // aiming errors (the call to P_Random is why this
    // only executes if not recording a compatible demo)
    if (!demorecording || !clbotparm || compatibility_level == prboom_6_compatibility)
    {
      // get aiming error
      int turn_offs = P_Random(pr_friends);

      // convert angle measure and add to aim amount
      aim_amount += TOAIMOFFS(turn_offs * ANG45 / 255 - ANG45 / 2);
    }

    cmd->angleturn += aim_amount;
  }

  return ang_diff <= ANG45;
}

// Either kill or retreat a target, depending on the
// perceived feasibility of doing either.
static void D_PRBot_KillOrRetreat(mbot_t *bot)
{
  mobj_t *targ = bot->enemy;

  if (bot->enemy->health > bot->mobj->health * (bot->enemy->target == bot->mobj ? 2 : 3))
    bot->state = BST_RETREAT;

  else
    bot->state = BST_KILL;
}

// Inquire a change of bot states.
static void D_PRBot_PrintState(mbot_t *bot)
{
  char *st;

  switch (bot->state)
  {
  case BST_PREINIT:
    st = (char *)"BST_PREINIT";
    break;
  case BST_RETREAT:
    st = (char *)"BST_RETREAT";
    break;
  case BST_KILL:
    st = (char *)"BST_KILL";
    break;
  case BST_DEAD:
    st = (char *)"BST_DEAD";
    break;
  case BST_HUNT:
    st = (char *)"BST_HUNT";
    break;
  case BST_LEAVE:
    st = (char *)"BST_LEAVE";
    break;
  case BST_LOOK:
    st = (char *)"BST_LOOK";
    break;
  case BST_NONE:
    st = (char *)"(none)";
    break;
  case BST_CAUTIOUS:
    st = (char *)"BST_CAUTIOUS";
    break;
  default:
    st = (char *)"(unknown)";
    break;
  }

  printf("D_PRBotSpawn: bot #%d is in state %s\n", bot->playernum, st);
}

static void D_PRBot_NextState(mbot_t *bot)
{
  if (bot->enemy && P_IsVisible(bot->mobj, bot->enemy, true))
  {
    bot->state = BST_KILL;
    return;
  }

  bot->enemy = NULL; // no grudges.

  if (D_PRBot_LookFind(bot))
  {
    D_PRBot_KillOrRetreat(bot);
  }

  else if (bot->avoid && P_IsVisible(bot->mobj, bot->avoid, true))
  {
    bot->state = BST_CAUTIOUS;
    return;
  }

  bot->avoid = NULL;

  switch (bot->mobj->subsector->sector->special)
  {
  case 5:
  case 7:
  case 16:
  case 4:
    bot->state = BST_CAUTIOUS; // run from damaging sector
    break;

  default:
    bot->state = BST_LOOK;
    break;
  }
}

inline void D_PRBotTic_Look(mbot_t *bot)
{
  // todo: look around (call D_PRBot_NextState first, but not D_PRBot_LookFind)
  D_PRBot_NextState(bot);
#ifdef BOTDEBUG
  D_PRBot_PrintState(bot);
#endif
}

inline void D_PRBotTic_Retreat(mbot_t *bot)
{
  if (bot->enemy->health <= 0 || bot->enemy->flags & MTF_FRIEND || bot->enemy->target != bot->mobj || !P_IsVisible(bot->mobj, bot->enemy, true))
  {
    D_SetMObj(&bot->enemy, NULL);

    D_PRBot_NextState(bot);
#ifdef BOTDEBUG
    D_PRBot_PrintState(bot);
#endif
  }

  // todo: movement part of retreating
}

inline void D_PRBotTic_Live(mbot_t *bot)
{
  // todo: live (stumbling and stuff)
}

inline void D_PRBotTic_Cautious(mbot_t *bot)
{
  // todo: cautious
  D_PRBot_NextState(bot);
#ifdef BOTDEBUG
  D_PRBot_PrintState(bot);
#endif
}

inline void D_PRBotTic_Leave(mbot_t *bot)
{
  // todo: leave sector
}

void D_PRBotTic_Hunt(mbot_t *bot)
{
  if (bot->enemy->health <= 0)
  {
    D_SetMObj(&bot->enemy, NULL);

    D_PRBot_NextState(bot);
#ifdef BOTDEBUG
    D_PRBot_PrintState(bot);
#endif
  }

  else
  {
    // todo: finish hunt code
    D_PRBot_NextState(bot);
#ifdef BOTDEBUG
    D_PRBot_PrintState(bot);
#endif
  }
}

void D_PRBotTic_Kill(mbot_t *bot)
{
  int vis = P_IsVisible(bot->mobj, bot->enemy, true);

  if (vis)
  {
    bot->lastseenx = bot->enemy->x;
    bot->lastseeny = bot->enemy->y;
  }

  if (bot->enemy->health <= 0 || bot->enemy->flags & MTF_FRIEND || (!vis && !deathmatch))
  {
    D_SetMObj(&bot->enemy, NULL);

    bot->player->cmd.buttons &= ~BT_ATTACK;

    D_PRBot_NextState(bot);
#ifdef BOTDEBUG
    D_PRBot_PrintState(bot);
#endif
  }

  else if (!vis)
  {
    // this is deathmatch, activate hunt mode!
    bot->player->cmd.buttons &= ~BT_ATTACK;
    bot->state = BST_HUNT;
#ifdef BOTDEBUG
    D_PRBot_PrintState(bot);
#endif
  }

  else if (bot->enemy->health > bot->mobj->health * 2 && bot->enemy->target == bot->mobj)
  {
    bot->player->cmd.buttons &= ~BT_ATTACK;
    bot->state = BST_RETREAT;
#ifdef BOTDEBUG
    D_PRBot_PrintState(bot);
#endif
  }

  else //if (D_PRBot_LookToward(bot, bot->enemy))
    bot->player->cmd.buttons |= BT_ATTACK;

  // todo: add movement part of attacking
}

void D_PRBot_DoReborn(mbot_t *bot)
{
  if (!netgame)
  {
    // forget about this bot
    D_PRBotDeinit(bot);
  }

  else
  {
    // respawn and reinitialize bot
    bot->player->playerstate = PST_REBORN;
    D_PRBotClear(bot);
    G_DoReborn(bot->playernum);
  }
}

void D_PRBotTic(mbot_t *bot)
{
  assert(bot->mobj != NULL && "tried to tic bot with mbot_t::mobj not set");
  assert(bot->player != NULL && "tried to tic bot with mbot_t::player not set");

  // add consistency setters?

  if (!bot_control)
    return;

  if (bot->stcounter > 0)
    bot->stcounter--;

  // todo: have bots do something!

  D_PRBotSanitizeState(bot); // don't attack or retreat from imaginary enemies, etc
  D_PRBotTic_Live(bot);      // shudder and turn a bit so bots don't look like inanimate voodoo dolls when lazying around

  switch (bot->state)
  {
  case BST_DEAD:
    if (bot->stcounter <= 0)
      D_PRBot_DoReborn(bot);
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

  case BST_LEAVE:
    D_PRBotTic_Leave(bot);
    break;
    //-----^
  }

  if (!netgame && bot->player && bot->state != BST_NONE)
    G_BuildTiccmd(&netcmds[bot->playernum][maketic % BACKUPTICS]);
}

void P_PRBot_CheckInits(void)
{
  int i;

  for (i = 0; i < MAXPLAYERS; i++)
  {
    if (bots[i].player && bots[i].state == BST_PREINIT)
    {
      if (bots[i].player->mo)
        D_PRBotDoInit(&bots[i]);

      else
        I_Error("P_PRBot_CheckInits called prematurely: bot #%d player has no mobj set yet!", i);
    }
  }
}

void P_PRBot_Ticker(void)
{
  int i;

  P_PRBot_CheckInits();

  for (i = 0; i < MAXPLAYERS; i++)
  {
    if (bots[i].state != BST_PREINIT && bots[i].state != BST_NONE)
    {
      assert(bots[i].player && "Player unset in ticked bot!");
      assert(bots[i].mobj && "Mobj unset in ticked bot!");

      D_PRBotTic(&bots[i]);
      break;
    }
  }
}
