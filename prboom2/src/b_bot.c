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
 *  PRBots, the routine (B_Tic) that runs every tic if
 *  -bot is supplied in the terminal.
 *
 *-----------------------------------------------------------------------------
 */

#include "d_main.h"
#include "b_bot.h"
#include "d_player.h"
#include "p_setup.h"
#include "m_random.h"
#include "lprintf.h"
#include "g_game.h"
#include "sounds.h"
#include "r_main.h"
#include "p_inter.h"
#include "p_pspr.h"
#include "doomstat.h"
#include "d_items.h"
#include "p_map.h"
#include "p_maputl.h"
#include "p_mobj.h"
#include "s_sound.h"
#include "assert.h"
#include "p_tick.h"
#include "p_spec.h"

#define bot_control (!demoplayback && !democontinue && netgame)

#define BOTDEBUG

#ifdef BOTDEBUG
#define DEBUGPRINT(...) printf(__VA_ARGS__)
#else
#define DEBUGPRINT(...) (void)0
#endif

extern player_t players[MAXPLAYERS];
extern dboolean playeringame[MAXPLAYERS];

mbot_t bots[MAXPLAYERS];

static void B_PrintState(mbot_t *bot);
static void B_NextState(mbot_t *bot);
void B_CheckInits(void);

// copied from p_enemy.c. Why static?!
static dboolean P_IsVisible(mobj_t *actor, mobj_t *mo, dboolean allaround) {
    if (!allaround) {
        angle_t an = R_PointToAngle2(actor->x, actor->y,
                                     mo->x, mo->y) -
                     actor->angle;
        if (an > ANG90 && an < ANG270 &&
                P_AproxDistance(mo->x - actor->x, mo->y - actor->y) > MELEERANGE) {
            return false;
        }
    }
    return P_CheckSight(actor, mo);
}

static void D_SetMObj(mobj_t **parm, mobj_t *other) {
    // use e.g. D_SetMObj(&mbot->enemy, new_enemy)
    if (*parm) {
        (*parm)->thinker.references--;
    }

    if (other) {
        other->thinker.references++;
    }

    *parm = other;
}

void B_Clear(mbot_t *mbot) {
    D_SetMObj(&mbot->avoid, NULL);
    D_SetMObj(&mbot->enemy, NULL);
    D_SetMObj(&mbot->want, NULL);

    B_SetState(mbot, BST_LOOK);

    mbot->exploration = 60; // 60 is 240 game tics; 240 / 35 is 6.8 seconds. This is a bot's patience in seconds. ADHD in a nutshell.
    mbot->gametics = 0;

    mbot->lastseenx = 0;
    mbot->lastseeny = 0;

    if (mbot->player) {
        mbot->player->pendingweapon = wp_pistol;
    }
}

void B_CallInit(mbot_t *mbot, int playernum) {
    if (mbot->mobj) {
        // lynch the impostor!
        P_DamageMobj(mbot->mobj, NULL, NULL, 9000000);
    }

    B_Clear(mbot);

    B_SetState(mbot, BST_PREINIT);

    mbot->stcounter = 0;
    mbot->stvalue = 0;

    mbot->player = &players[playernum];
    mbot->playernum = playernum; // though it should be the same index for bots lol
    mbot->mobj = NULL;
}

static void B_DoInit(mbot_t *mbot) {
    B_Clear(mbot); // call a 2nd time to change BST_PREINIT to BST_LOOK

    mbot->mobj = players[mbot->playernum].mo;
    mbot->cmd = &mbot->player->cmd; // (ticcmd_t *) netcmds[mbot->playernum];

    if (!deathmatch) {
        mbot->mobj->flags |= MF_FRIEND;    // happy little coop friends
    }

    // spawn teleport fog and make its noise thing
    S_StartSound(P_SpawnMobj(mbot->mobj->x, mbot->mobj->y, mbot->mobj->z, MT_TFOG), sfx_telept);

    B_NextState(mbot);
}

// Replaces an existing player with a PRBot (does not change the player's state, e.g. ammo, location, etc)
mbot_t *B_Replace(int playernum) {
    mbot_t *mbot = &bots[playernum];

    assert(mbot->state == BST_NONE && "This player is already a bot!");
    assert(playeringame[playernum]);
    assert(players[playernum].mo && "Player's mobj not set when B_Replace was called!");

    B_Clear(mbot);

    B_SetState(mbot, BST_PREINIT);
    mbot->player = &players[playernum];
    mbot->playernum = playernum;
    mbot->mobj = players[mbot->playernum].mo;

    mbot->mobj->flags |= MF_FRIEND;

    B_NextState(mbot);

    return mbot;
}

void B_Deinit(mbot_t *mbot) {
    DEBUGPRINT("B_Deinit: Removed bot #%d from the game.\n", mbot->playernum + 1);

    playeringame[mbot->playernum] = false;

    mbot->player = NULL;
    mbot->mobj = NULL;
    mbot->playernum = -1;

    B_Clear(mbot);

    B_SetState(mbot, BST_NONE);
}

mbot_t *B_Spawn(void) {
    // spawns a bot to the game
    int next_player = 0;
    player_t *player;

    while (next_player < MAXPLAYERS) {
        if (playeringame[next_player]) {
            next_player++;
            continue;
        }

        // I think there is a specific function in the PRBoom+ code
        // that does this that I have to call instead (not I_Error),
        // but I don't recall which one right now.
        DEBUGPRINT("B_Spawn: spawned new PRBot at player start #%d\n", next_player + 1);

        player = &players[next_player];
        playeringame[next_player] = true;

        // spawn player
        P_SpawnPlayer(next_player, &playerstarts[next_player]);


        B_CallInit(&bots[next_player], next_player);

        break;
    }

    return NULL; // couldn't spawn bot; not enough player slots!
}

static inline void B_SanitizeState(mbot_t *bot) {
    if (bot->mobj->health <= 0) {
        if (bot->player->playerstate == PST_DEAD && bot->state != BST_DEAD) {
            bot->stcounter = 20;
            B_SetState(bot, BST_DEAD);
        }
        return;
    }

    switch (bot->state) {
BST_HUNT: // don't hunt in dm
        if (!deathmatch) {
            B_SetState(bot, BST_LOOK);
            break; // (don't break if dm)
        }

BST_KILL: // don't hunt or kill if no enemy to hunt or kill!
        if (bot->enemy == NULL) {
            B_SetState(bot, BST_LOOK);
        }
        break;
        //-----^

BST_RETREAT:
        if (bot->enemy == NULL) {
            if (bot->avoid == NULL) {
                B_SetState(bot, BST_LOOK);
            }
            else {
                B_SetState(bot, BST_CAUTIOUS);
            }
        }
        break;
        //-----^
    }
}

static mobj_t *current_actor;
static mbot_t *current_bot;

// Modified version of PIT_FindTarget.
static dboolean PIT_FindBotTarget(mobj_t *mo) {
    mobj_t *actor = current_actor;
    mbot_t *bot = current_bot;

    if (!(
                (
                    ((mo->flags & MF_FRIEND) ^ (actor->flags & MF_FRIEND) && mo->type != MT_PLAYER) ||
                    (deathmatch && mo->type == MT_PLAYER)) &&
                mo->health > 0 &&
                (mo->flags & MF_COUNTKILL || mo->type == MT_SKULL || mo->type == MT_PLAYER))) {
        return true;    // Invalid target
    }

    if (!P_IsVisible(actor, mo, false)) {
        return true;
    }

    D_SetMObj(&bot->enemy, mo);

    // Move the selected monster to the end of its associated
    // list, so that it gets searched last next time.
    {
        thinker_t *mthinker, *next, *prev; // local variables for readability and easier debugging

        thinker_t *cap = &thinkerclasscap[(deathmatch || mo->flags & MF_FRIEND) ? th_enemies : th_friends];

        mthinker = &mo->thinker;

        next = mthinker->cnext;
        prev = mthinker->cprev;

        if (next != NULL) {
            next->cprev = mthinker->cprev;
        }

        if (prev != NULL) {
            prev->cnext = mthinker->cnext;
        }

        // also setting prev, next for debugging (although it'll probably not be needed :p)
        mthinker->cprev = prev = cap->cprev;
        mthinker->cnext = next = cap;
    }

    return false;
}

static mobj_t *B_LookFind(mbot_t *bot) {
    mobj_t *actor = bot->mobj;

    thinker_t *th;
    thinker_t *cap = &thinkerclasscap[th_friends | th_enemies];

    // Search for new enemy
    if (cap->cnext == cap) {
        return NULL;    // Empty list? bail out early
    }

    {
        int x = P_GetSafeBlockX(actor->x - bmaporgx);
        int y = P_GetSafeBlockY(actor->y - bmaporgy);
        int d;

        current_actor = actor;
        current_bot = bot;

        // Search first in the immediate vicinity.
        if (!P_BlockThingsIterator(x, y, PIT_FindBotTarget)) {
            return current_actor;
        }

        for (d = 1; d < 5; d++) {
            int i = 1 - d;
            do
                if (!P_BlockThingsIterator(x + i, y - d, PIT_FindBotTarget) ||
                        !P_BlockThingsIterator(x + i, y + d, PIT_FindBotTarget)) {
                    return current_actor;
                }
            while (++i < d);
            do
                if (!P_BlockThingsIterator(x - d, y + i, PIT_FindBotTarget) ||
                        !P_BlockThingsIterator(x + d, y + i, PIT_FindBotTarget)) {
                    return current_actor;
                }
            while (--i + d >= 0);
        }

        {
            // Random number of monsters, to prevent patterns from forming
            int n = (P_Random(pr_bot) & 31) + 15;

            for (th = cap->cnext; th != cap; th = th->cnext)
                if (--n < 0) {
                    // Only a subset of the monsters were searched. Move all of
                    // the ones which were searched so far, to the end of the list.

                    (cap->cnext->cprev = cap->cprev)->cnext = cap->cnext;
                    (cap->cprev = th->cprev)->cnext = cap;
                    (th->cprev = cap)->cnext = th;
                    break;
                }

                else if (!PIT_FindBotTarget((mobj_t *)th)) { // If target sighted
                    return current_actor;
                }
        }
    }

    return NULL;
}

#define MAXAIMOFFS (10 * (ANG1))
#define TOAIMOFFS(a) ((a) / ANG1 * MAXAIMOFFS)

// Aims a bot toward its target.
// Returns whether it thinks it has aimed
// close enough to attempt firing.
dboolean B_Action_LookToward(mbot_t *bot, int x, int y) {
    ticcmd_t *cmd = &bot->cmd[maketic % BACKUPTICS];

    angle_t ang_targ = R_PointToAngle2(bot->mobj->x, bot->mobj->y, x, y);
    angle_t ang_curr = bot->mobj->angle;

    // check for unsigned wrap-arounds
    angle_t ang_diff = (ang_targ >= ang_curr ? ang_targ - ang_curr : ang_curr - ang_targ); // absolute          (indirected)
    int ang_delta = (ang_targ >= ang_curr ? ang_diff : -ang_diff);                         // from curr to targ (directed)

    if (ang_diff > ANG45) {
        // turn toward
        if (ang_delta > 0) { // targ - curr > 0; therefore, n > 0, where curr + n = targ
            cmd->angleturn += 3.5;
        }

        else { // targ - curr < 0; therefore, n < 0, where curr + n = targ
            cmd->angleturn -= 3.5;
        }
    }

    else {
        // adjust slightly (with 'realistic' aiming errors)

        int aim_amount = ang_diff * 0.8;

        // calculate aiming errors
        {
            // get random byte
            int turn_offs = P_Random(pr_bot);

            // convert byte to scaled proper angle measure and add to aim amount
            aim_amount += TOAIMOFFS(turn_offs * (ANG45 / 255) - ANG45 / 2);
        }

        cmd->angleturn += aim_amount;
    }

    return ang_diff <= ANG45;
}

dboolean B_Action_LookAt(mbot_t *bot, mobj_t *lookee) {
    return B_Action_LookToward(bot, lookee->x, lookee->y);
}

// Either kill or retreat a target, depending on the
// perceived feasibility of doing either.
static void B_Action_KillOrRetreat(mbot_t *bot) {
    mobj_t *targ = bot->enemy;

    if (bot->enemy->health > bot->mobj->health * (bot->enemy->target == bot->mobj ? 2 : 3)) {
        B_SetState(bot, BST_RETREAT);
    }

    else {
        B_SetState(bot, BST_KILL);
    }
}

// Inquire a change of bot states.
static const char *B_GetStateName(mbot_t *bot) {
    const char *st;

    switch (bot->state) {
    case BST_PREINIT:
        st = (const char *)"BST_PREINIT";
        break;
    case BST_RETREAT:
        st = (const char *)"BST_RETREAT";
        break;
    case BST_KILL:
        st = (const char *)"BST_KILL";
        break;
    case BST_DEAD:
        st = (const char *)"BST_DEAD";
        break;
    case BST_HUNT:
        st = (const char *)"BST_HUNT";
        break;
    case BST_LEAVE:
        st = (const char *)"BST_LEAVE";
        break;
    case BST_LOOK:
        st = (const char *)"BST_LOOK";
        break;
    case BST_CAUTIOUS:
        st = (const char *)"BST_CAUTIOUS";
        break;
    case BST_NONE:
        st = (const char *)"(none)";
        break;
    default:
        st = (const char *)"(unknown)";
        break;
    }

    return st;
}

static void B_PrintState(mbot_t *bot) {
    DEBUGPRINT("B_NextState: bot #%d is in state %s\n", bot->playernum + 1, B_GetStateName(bot));
}

static void B_SetState(mbot_t *bot, botstate_t state) {
    if (bot->player && bot->state == state) {
        return;
    }

    bot->state = state;
    bot->stcounter = 0;

    if (bot->player) {
        bot->player->cmd.buttons &= ~(BT_USE | BT_ATTACK);
        bot->player->cmd.sidemove = 0;
        bot->player->cmd.forwardmove = 0;
        bot->player->cmd.angleturn = 0;

#ifdef BOTDEBUG
        B_PrintState(bot);
#endif
    }
}

// Checks whether standing on damaging sector
dboolean B_AssessSectorDanger(mbot_t *bot) {
    sector_t *sector;

    assert(bot->mobj && "B_AssessSectorDanger called on bot with mobj not set!");

    if (bot->mobj->z > bot->mobj->floorz || !bot->mobj->subsector) {
        return false;    // no mid-air damage
    }

    assert(bot->mobj && "B_AssessSectorDanger called on bot whose mobj->subsector has a broken sector reference!");

    sector = bot->mobj->subsector->sector;

    switch (sector->special) {
    case 5:
    case 7:
    case 16:
    case 4:
        // not sector special 11 because E1M8 is glorious!
        return true;
    }

    switch ((sector->special & DAMAGE_MASK) >> DAMAGE_SHIFT) {
    case 0:
        break;
    default:
        return true;
    }

    return false;
}

static void B_NextState(mbot_t *bot) {
    if (bot->enemy && P_IsVisible(bot->mobj, bot->enemy, true)) {
        B_SetState(bot, BST_KILL);
        return;
    }

    bot->enemy = NULL; // no grudges.

    if ((bot->enemy = B_LookFind(bot))) {
        B_KillOrRetreat(bot);
    }

    else if (bot->avoid && P_IsVisible(bot->mobj, bot->avoid, true)) {
        B_SetState(bot, BST_CAUTIOUS);
        return;
    }

    bot->avoid = NULL;

    if (B_AssessSectorDanger(bot)) {
        B_SetState(bot, BST_LEAVE);
    }

    //else
    //  B_SetState(bot, BST_LOOK);
}

// move around in a somewhat random manner
void B_Action_Wander(mbot_t *bot, int turn_density, sidesteppiness_t sidesteppy, botmoveflags_t moveflags) {
    dboolean turn_anyway;
    ticcmd_t *cmd = &bot->cmd[maketic % BACKUPTICS];
    sidesteppiness_t straightsteppy = (200 - sidesteppy / 2);

    if (straightsteppy < 20) {
        straightsteppy = 20;    // at least some, c'mon!
    }

    if (bot->stcounter == 0) {
        bot->stcounter = -10 - P_Random(pr_bot) / 80;
        bot->stvalue ^= P_Random(pr_bot) & 0xF;
        cmd->forwardmove = 0;
    }

    else if (bot->stcounter == -1) {
        bot->stcounter = 7 + P_Random(pr_bot) / 120 + 8 * turn_density;
    }

    turn_anyway = P_Random(pr_bot) < 40 + 15 * turn_density;

    if (turn_anyway) {
        if (!(moveflags & BMF_NOTURN)) {
            cmd->angleturn += (bot->stvalue & 1 ? 1 + turn_density / 2 : -1 - turn_density / 2);
        }
    }

    else if (bot->stcounter > 0) {
        if (!(moveflags & BMF_NOTURN)) {
            cmd->angleturn += (bot->stvalue & 1 ? 2 + turn_density : -2 - turn_density);
        }
    }

    else {
        cmd->angleturn = 0;
    }

    if (bot->stcounter < -1) {
        bot->stcounter += 2; // don't worry, it'll be decremented later in PRBotTic

        if (bot->stvalue & 0xA) {
            int movement = bot->stvalue & 0xE >> 1;

            if (movement & 4) {
                if (movement & 1) {
                    if (!(moveflags & BMF_NOSIDES)) {
                        cmd->sidemove += sidesteppy * 3;
                    }
                }

                else {
                    if (!(moveflags & BMF_NOSIDES)) {
                        cmd->sidemove -= sidesteppy * 3;
                    }
                }
            }

            if (movement & 2 && !(moveflags & BMF_NOFORWARD)) {
                cmd->forwardmove += straightsteppy * 4;
            }

            else if (!(moveflags & BMF_NOBACKWARD)) {
                cmd->forwardmove -= straightsteppy * 3;
            }
        }
    }

    else {
        cmd->sidemove = 0;
        cmd->forwardmove = 0;
    }
}

// force the bot to move and not just stare at things
void B_Action_Move(mbot_t *bot, int turn_density, sidesteppiness_t sidesteppy, botmoveflags_t moveflags) {
    if (bot->stcounter >= 0) {
        bot->stcounter = -15 - bot->stcounter;
    }

    B_Action_Wander(bot, turn_density, sidesteppy, moveflags);
}

inline void B_State_Look(mbot_t *bot) {
    B_NextState(bot);

    if (bot->state == BST_LOOK) {
        ticcmd_t *cmd = &bot->cmd[maketic % BACKUPTICS];

        // no, nothing interesting yet
        if (!(bot->gametics & 0x3) && bot->exploration > 0) {
            bot->exploration--;
        }

        if (bot->exploration > 60) {
            // turn around more
            B_Action_Wander(bot, 3, SSTP_SOME, 0);

            cmd->buttons &= ~BT_USE;
        }

        else if (bot->exploration > 40) {
            // turn around less, and use sometimes
            B_Action_Wander(bot, 2, SSTP_MODERATE, 0);

            if (!(cmd->buttons & BT_USE) && !(bot->gametics & 0x1F) && P_Random(pr_bot) < 60) {
                cmd->buttons |= BT_USE;
            }

            else {
                cmd->buttons &= ~BT_USE;
            }
        }

        else if (bot->exploration > 20) {
            // turn around little, and use all the time
            B_Action_Wander(bot, 1, SSTP_MODERATE, 0);

            if (!(cmd->buttons & BT_USE) && !(bot->gametics & 0xF) && P_Random(pr_bot) < 128) {
                cmd->buttons |= BT_USE;
            }

            else {
                cmd->buttons &= ~BT_USE;
            }
        }

        else {
            // turn around lots, and use and shoot compulsively
            B_Action_Wander(bot, 5, SSTP_LITTLE, 0);

            cmd->buttons &= ~(BT_USE | BT_ATTACK);

            if (!(cmd->buttons & BT_USE) && !(bot->gametics & 0x7) && P_Random(pr_bot) < 128) {
                cmd->buttons |= BT_USE | (P_Random(pr_bot) < 30 ? BT_ATTACK : 0);    // OOMPH OOMPH OOMPH BANG
            }
        }
    }
}

inline void B_State_Retreat(mbot_t *bot) {
    if (bot->enemy->health <= 0 || bot->enemy->flags & MTF_FRIEND || bot->enemy->target != bot->mobj || !P_IsVisible(bot->mobj, bot->enemy, true)) {
        D_SetMObj(&bot->enemy, NULL);

        B_NextState(bot);
    }

    // todo: movement part of retreating
}

void B_Action_Stumble(mbot_t *bot) {
    ticcmd_t *cmd = &bot->cmd[maketic % BACKUPTICS];

    if (bot->gametics & 1) {
        cmd->sidemove -= P_Random(pr_bot) / 8;
    }

    else {
        cmd->sidemove += P_Random(pr_bot) / 8;
    }

    if (P_Random(pr_bot) < 180) {
        if (bot->gametics & 1) {
            cmd->sidemove -= P_Random(pr_bot) / 6;
        }

        else {
            cmd->sidemove += P_Random(pr_bot) / 6;
        }
    }
}

void B_State_Live(mbot_t *bot) {
    if (bot->mobj->floorz != bot->lastheight) {
        int diff1 = bot->lastheight - bot->mobj->floorz;
        int diff2 = bot->mobj->floorz - bot->lastheight;
        int absdiff = diff1 < diff2 ? diff1 : diff2;

        //bot->exploration += absdiff / 2;

        // do some stumbling, unless still in mid-air
        if (absdiff > 4 && bot->mobj->z - bot->mobj->floorz < 16 && (bot->mobj->z > bot->mobj->floorz || P_Random(pr_bot) < 55)) {
            B_Action_Stumble(bot);
        }
    }

    if (P_Random(pr_bot) < 80 && B_AssessSectorDanger(bot)) {
        B_Action_Stumble(bot);
    }

    bot->lastheight = bot->mobj->floorz;
}

inline void B_State_Cautious(mbot_t *bot) {
    // todo: cautious
    B_NextState(bot);
}

inline void B_State_Leave(mbot_t *bot) {
    int absdiff = 0;

    if (bot->mobj->floorz != bot->lastheight) {
        int diff1 = bot->lastheight - bot->mobj->floorz;
        int diff2 = bot->mobj->floorz - bot->lastheight;
        int absdiff = diff1 < diff2 ? diff1 : diff2;
    }

    // get outta there!
    B_Action_Wander(bot, 3, SSTP_MORE, 0);
    B_NextState(bot);
}

void B_State_Hunt(mbot_t *bot) {
    if (bot->enemy->health <= 0) {
        D_SetMObj(&bot->enemy, NULL);

        B_NextState(bot);
    }

    else {
        int vis = P_IsVisible(bot->mobj, bot->enemy, false);

        //B_Action_Move
        if (vis) {
            B_SetState(bot, BST_KILL);
        }

        else {
            B_Action_Move(bot, 2, (B_Action_LookToward(bot, bot->lastseenx, bot->lastseeny) ? SSTP_LITTLE : SSTP_SOME), BMF_NOTURN);
        }

        B_NextState(bot);
    }
}

void B_State_Kill(mbot_t *bot) {
    int vis;

    if (bot->enemy == NULL) {
        B_NextState(bot); // find someone, nelly!
    }

    vis = P_IsVisible(bot->mobj, bot->enemy, true);

    if (vis) {
        bot->lastseenx = bot->enemy->x;
        bot->lastseeny = bot->enemy->y;
    }

    if (bot->enemy->health <= 0 || bot->enemy->flags & MTF_FRIEND || (!vis && !deathmatch)) {
        D_SetMObj(&bot->enemy, NULL);

        bot->player->cmd.buttons &= ~BT_ATTACK;

        B_NextState(bot);
    }

    else if (!vis /* deathmatch implied because condition above failed */) {
        bot->player->cmd.buttons &= ~BT_ATTACK;
        B_SetState(bot, BST_HUNT);
    }

    else if (bot->enemy->health > bot->mobj->health * 2 && bot->enemy->target == bot->mobj) {
        bot->player->cmd.buttons &= ~BT_ATTACK;
        B_SetState(bot, BST_RETREAT);
    }

    else if (B_Action_LookAt(bot, bot->enemy)) {
        bot->player->cmd.buttons |= BT_ATTACK;
        B_Action_Move(bot, 2, SSTP_SOME, BMF_NOFORWARD | BMF_NOTURN);
    }

    else {
        B_Action_Move(bot, 2, SSTP_ALL, BMF_NOBACKWARD | BMF_NOTURN);
    }

    // todo: add movement part of attacking
}

void B_DoReborn(mbot_t *bot) {
    if (!netgame) {
        // forget about this bot
        B_Deinit(bot);
    }

    else {
        // respawn and reinitialize bot
        bot->player->playerstate = PST_REBORN;
        B_Clear(bot);
        G_DoReborn(bot->playernum);
    }
}

void B_Tic(mbot_t *bot) {
    assert(bot->mobj != NULL && "tried to tic bot with mbot_t::mobj not set");
    assert(bot->player != NULL && "tried to tic bot with mbot_t::player not set");

    // add consistency setters?

    //if (!bot_control)
    //  return;

    bot->stcounter--;

    B_SanitizeState(bot); // don't attack or retreat from imaginary enemies, etc
    B_State_Live(bot);      // shudder and turn a bit so bots don't look like inanimate voodoo dolls when lazying around

    bot->gametics++;

    switch (bot->state) {
    case BST_DEAD:
        if (bot->stcounter <= 0) {
            B_DoReborn(bot);
        }
        return;
    //======!

    case BST_LOOK:
        B_State_Look(bot);
        break;
    //-----^

    case BST_RETREAT:
        B_State_Retreat(bot);
        break;
    //-----^

    case BST_CAUTIOUS:
        B_State_Cautious(bot);
        break;
    //-----^

    case BST_HUNT:
        B_State_Hunt(bot);
        break;
    //-----^

    case BST_KILL:
        B_State_Kill(bot);
        break;
    //-----^

    case BST_LEAVE:
        B_State_Leave(bot);
        break;
        //-----^
    }
}

void B_CheckInits(void) {
    int i;

    for (i = 0; i < MAXPLAYERS; i++) {
        if (bots[i].player && bots[i].state == BST_PREINIT) {
            if (bots[i].player->mo) {
                B_DoInit(&bots[i]);
            }

            else {
                I_Error("B_CheckInits called prematurely: bot #%d player has no mobj set yet!", i + 1);
            }
        }
    }
}

void B_Ticker(void) {
    int i;

    B_CheckInits();

    for (i = 0; i < MAXPLAYERS; i++) {
        if (bots[i].state != BST_PREINIT && bots[i].state != BST_NONE) {
            assert(bots[i].player && "Player unset in ticked bot!");
            assert(bots[i].mobj && "Mobj unset in ticked bot!");

            B_Tic(&bots[i]);
        }
    }
}
