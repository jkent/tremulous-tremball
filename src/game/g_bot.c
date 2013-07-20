/*
===========================================================================
Copyright (C) 2007 Amine Haddad

This file is part of Tremulous.

The original works of vcxzet (lamebot3) were used a guide to create TremBot.

Tremulous is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Tremulous is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Tremulous; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/

/* Current version: v0.01 */

#include "g_local.h"

static qboolean botAimAtTarget( gentity_t *self, gentity_t *target );
static qboolean botTargetInRange( gentity_t *self, gentity_t *target );
static int botFindClosestEnemy( gentity_t *self, qboolean includeTeam );
static int botGetDistanceBetweenPlayer( gentity_t *self, gentity_t *player );
static qboolean botShootIfTargetInRange( gentity_t *self, gentity_t *target );

void G_BotAdd( char *name, int team, int skill ) {
  int i;
  int clientNum;
  char userinfo[MAX_INFO_STRING];
  int reservedSlots = 0;
  gentity_t *bot;

  reservedSlots = trap_Cvar_VariableIntegerValue( "sv_privateclients" );

  // find what clientNum to use for bot
  clientNum = -1;
  for( i = 0; i < reservedSlots; i++ ) {
    if( !g_entities[i].inuse ) {
      clientNum = i;
      break;
    }
  }

  if(clientNum < 0) {
    trap_Printf("no more slots for bot\n");
    return;
  }

  bot = &g_entities[ clientNum ];
  bot->r.svFlags |= SVF_BOT;
  bot->inuse = qtrue;

  //default bot data
  bot->botCommand = BOT_REGULAR;
  bot->botFriend = NULL;
  bot->botEnemy = NULL;
  bot->botFriendLastSeen = 0;
  bot->botEnemyLastSeen = 0;
  bot->botSkillLevel = skill;
  bot->botTeam = team;
  bot->isBot = qtrue;

  // register user information
  userinfo[0] = '\0';
  Info_SetValueForKey( userinfo, "name", name );
  Info_SetValueForKey( userinfo, "rate", "25000" );
  Info_SetValueForKey( userinfo, "snaps", "20" );

  trap_SetUserinfo( clientNum, userinfo );

  // have it connect to the game as a normal client
  if(ClientConnect(clientNum, qtrue) != NULL ) {
    // won't let us join
    return;
  }

  ClientBegin( clientNum );
  G_ChangeTeam( bot, team );
}

void G_BotDel( int clientNum ) {
  gentity_t *bot;

  bot = &g_entities[clientNum];
  if( !( bot->r.svFlags & SVF_BOT ) ) {
    trap_Printf( va("'^7%s^7' is not a bot\n", bot->client->pers.netname) );
    return;
  }

  ClientDisconnect(clientNum);
}

void G_BotCmd( gentity_t *master, int clientNum, char *command ) {
  gentity_t *bot;

  bot = &g_entities[clientNum];
  if( !( bot->r.svFlags & SVF_BOT ) ) {
    return;
  }

  bot->botFriend = NULL;
  bot->botEnemy = NULL;
  bot->botFriendLastSeen = 0;
  bot->botEnemyLastSeen = 0;

  if( !Q_stricmp( command, "regular" ) ) {

    bot->botCommand = BOT_REGULAR;
    //trap_SendServerCommand(-1, "print \"regular mode\n\"");

   } else if( !Q_stricmp( command, "idle" ) ) {

    bot->botCommand = BOT_IDLE;
    //trap_SendServerCommand(-1, "print \"idle mode\n\"");

  } else if( !Q_stricmp( command, "attack" ) ) {

    bot->botCommand = BOT_ATTACK;
    //trap_SendServerCommand(-1, "print \"attack mode\n\"");

  } else if( !Q_stricmp( command, "standground" ) ) {

    bot->botCommand = BOT_STAND_GROUND;
    //trap_SendServerCommand(-1, "print \"stand ground mode\n\"");

  } else if( !Q_stricmp( command, "defensive" ) ) {

    bot->botCommand = BOT_DEFENSIVE;
    //trap_SendServerCommand(-1, "print \"defensive mode\n\"");

  } else if( !Q_stricmp( command, "followprotect" ) ) {

    bot->botCommand = BOT_FOLLOW_FRIEND_PROTECT;
    bot->botFriend = master;
    //trap_SendServerCommand(-1, "print \"follow-protect mode\n\"");

  } else if( !Q_stricmp( command, "followattack" ) ) {

    bot->botCommand = BOT_FOLLOW_FRIEND_ATTACK;
    bot->botFriend = master;
    //trap_SendServerCommand(-1, "print \"follow-attack mode\n\"");

  } else if( !Q_stricmp( command, "followidle" ) ) {

    bot->botCommand = BOT_FOLLOW_FRIEND_IDLE;
    bot->botFriend = master;
    //trap_SendServerCommand(-1, "print \"follow-idle mode\n\"");

  } else if( !Q_stricmp( command, "teamkill" ) ) {

    bot->botCommand = BOT_TEAM_KILLER;
    //trap_SendServerCommand(-1, "print \"team kill mode\n\"");

  } else {

    bot->botCommand = BOT_REGULAR;
    //trap_SendServerCommand(-1, "print \"regular (unknown) mode\n\"");

  }

  return;
}

void G_BotThink( gentity_t *self )
{
  int distance = 0;
  int clicksToStopChase = 30; //5 seconds
  int tooCloseDistance = 100; // about 1/3 of turret range
  int forwardMove = 127; // max speed
  int tempEntityIndex = -1;
  qboolean followFriend = qfalse;

  self->client->pers.cmd.buttons = 0;
  self->client->pers.cmd.forwardmove = 0;
  self->client->pers.cmd.upmove = 0;
  self->client->pers.cmd.rightmove = 0;

  // reset botEnemy if enemy is dead
  if(self->botEnemy && self->botEnemy->health <= 0) {
    self->botEnemy = NULL;
  }

  // if friend dies, reset status to regular
  if(self->botFriend && self->botFriend->health <= 0) {
    self->botCommand = BOT_REGULAR;
    self->botFriend = NULL;
  }

  // what mode are we in?
  switch(self->botCommand) {
    case BOT_REGULAR:
      // if there is enemy around, rush them and attack.
      if(self->botEnemy) {
        // we already have an enemy. See if still in LOS.
        if(!botTargetInRange(self,self->botEnemy)) {
          // if it's been over clicksToStopChase clicks since we last seen him in LOS then do nothing, else follow him!
          if(self->botEnemyLastSeen > clicksToStopChase) {
            // forget him!
            self->botEnemy = NULL;
            self->botEnemyLastSeen = 0;
          } else {
            //chase him
            self->botEnemyLastSeen++;
          }
        } else {
          // we see him!
          self->botEnemyLastSeen = 0;
        }
      }

      if(!self->botEnemy) {
        // try to find closest enemy
        tempEntityIndex = botFindClosestEnemy(self, qfalse);
        if(tempEntityIndex >= 0)
          self->botEnemy = &g_entities[tempEntityIndex];
      }

      if(!self->botEnemy) {
        // no enemy
      } else {
        // enemy!
        distance = botGetDistanceBetweenPlayer(self, self->botEnemy);
        botAimAtTarget(self, self->botEnemy);

        // enable wallwalk
        if( BG_ClassHasAbility( self->client->ps.stats[ STAT_PCLASS ], SCA_WALLCLIMBER ) ) {
          self->client->pers.cmd.upmove = -1;
        }

        botShootIfTargetInRange(self,self->botEnemy);
        self->client->pers.cmd.forwardmove = forwardMove;
        self->client->pers.cmd.rightmove = -100;
        if(self->client->time1000 >= 500)
          self->client->pers.cmd.rightmove = 100;
      }

      break;

    case BOT_IDLE:
      // just stand there and look pretty.
      break;

    case BOT_ATTACK:
      // .. not sure ..
      break;

    case BOT_STAND_GROUND:
      // stand ground but attack enemies if you can reach.
      if(self->botEnemy) {
        // we already have an enemy. See if still in LOS.
        if(!botTargetInRange(self,self->botEnemy)) {
          //we are not in LOS
          self->botEnemy = NULL;
        }
      }

      if(!self->botEnemy) {
        // try to find closest enemy
        tempEntityIndex = botFindClosestEnemy(self, qfalse);
        if(tempEntityIndex >= 0)
          self->botEnemy = &g_entities[tempEntityIndex];
      }

      if(!self->botEnemy) {
        // no enemy
      } else {
        // enemy!
        distance = botGetDistanceBetweenPlayer(self, self->botEnemy);
        botAimAtTarget(self, self->botEnemy);

        // enable wallwalk
        if( BG_ClassHasAbility( self->client->ps.stats[ STAT_PCLASS ], SCA_WALLCLIMBER ) ) {
          self->client->pers.cmd.upmove = -1;
        }

        botShootIfTargetInRange(self,self->botEnemy);
      }

      break;

    case BOT_DEFENSIVE:
      // if there is an enemy around, rush them but not too far from where you are standing when given this command
      break;

    case BOT_FOLLOW_FRIEND_PROTECT:
      // run towards friend, attack enemy
      break;

    case BOT_FOLLOW_FRIEND_ATTACK:
      // run with friend until enemy spotted, then rush enemy
      if(self->botEnemy) {
        // we already have an enemy. See if still in LOS.
        if(!botTargetInRange(self,self->botEnemy)) {
          // if it's been over clicksToStopChase clicks since we last seen him in LOS then do nothing, else follow him!
          if(self->botEnemyLastSeen > clicksToStopChase) {
            // forget him!
            self->botEnemy = NULL;
            self->botEnemyLastSeen = 0;
          } else {
            //chase him
            self->botEnemyLastSeen++;
          }
        } else {
          // we see him!
          self->botEnemyLastSeen = 0;
        }

        //if we are chasing enemy, reset counter for friend LOS .. if its true
        if(self->botEnemy) {
          if(botTargetInRange(self,self->botFriend)) {
            self->botFriendLastSeen = 0;
          } else {
            self->botFriendLastSeen++;
          }
        }
      }

      if(!self->botEnemy) {
        // try to find closest enemy
        tempEntityIndex = botFindClosestEnemy(self, qfalse);
        if(tempEntityIndex >= 0)
          self->botEnemy = &g_entities[tempEntityIndex];
      }

      if(!self->botEnemy) {
        // no enemy
        if(self->botFriend) {
          // see if our friend is in LOS
          if(botTargetInRange(self,self->botFriend)) {
            // go to him!
            followFriend = qtrue;
            self->botFriendLastSeen = 0;
          } else {
            // if it's been over clicksToStopChase clicks since we last seen him in LOS then do nothing, else follow him!
            if(self->botFriendLastSeen > clicksToStopChase) {
              // forget him!
              followFriend = qfalse;
            } else {
              self->botFriendLastSeen++;
              followFriend = qtrue;
            }
          }

          if(followFriend) {
            distance = botGetDistanceBetweenPlayer(self, self->botFriend);
            botAimAtTarget(self, self->botFriend);

            // enable wallwalk
            if( BG_ClassHasAbility( self->client->ps.stats[ STAT_PCLASS ], SCA_WALLCLIMBER ) ) {
              self->client->pers.cmd.upmove = -1;
            }

            //botShootIfTargetInRange(self,self->botEnemy);
            if(distance>tooCloseDistance) {
              self->client->pers.cmd.forwardmove = forwardMove;
              self->client->pers.cmd.rightmove = -100;
              if(self->client->time1000 >= 500)
                self->client->pers.cmd.rightmove = 100;
            }
          }
        }
      } else {
        // enemy!
        distance = botGetDistanceBetweenPlayer(self, self->botEnemy);
        botAimAtTarget(self, self->botEnemy);

        // enable wallwalk
        if( BG_ClassHasAbility( self->client->ps.stats[ STAT_PCLASS ], SCA_WALLCLIMBER ) ) {
          self->client->pers.cmd.upmove = -1;
        }

        botShootIfTargetInRange(self,self->botEnemy);
        self->client->pers.cmd.forwardmove = forwardMove;
        self->client->pers.cmd.rightmove = -100;
        if(self->client->time1000 >= 500)
          self->client->pers.cmd.rightmove = 100;
      }

      break;

    case BOT_FOLLOW_FRIEND_IDLE:
      // run with friend and stick with him no matter what. no attack mode.
      if(self->botFriend) {
        // see if our friend is in LOS
        if(botTargetInRange(self,self->botFriend)) {
          // go to him!
          followFriend = qtrue;
          self->botFriendLastSeen = 0;
        } else {
          // if it's been over clicksToStopChase clicks since we last seen him in LOS then do nothing, else follow him!
          if(self->botFriendLastSeen > clicksToStopChase) {
            // forget him!
            followFriend = qfalse;
          } else {
            //chase him
            self->botFriendLastSeen++;
            followFriend = qtrue;
          }
          
        }

        if(followFriend) {
          distance = botGetDistanceBetweenPlayer(self, self->botFriend);
          botAimAtTarget(self, self->botFriend);

          // enable wallwalk
          if( BG_ClassHasAbility( self->client->ps.stats[ STAT_PCLASS ], SCA_WALLCLIMBER ) ) {
            self->client->pers.cmd.upmove = -1;
          }

          //botShootIfTargetInRange(self,self->botFriend);
          if(distance>tooCloseDistance) {
            self->client->pers.cmd.forwardmove = forwardMove;
            self->client->pers.cmd.rightmove = -100;
            if(self->client->time1000 >= 500)
              self->client->pers.cmd.rightmove = 100;
          }
        }
      }

      break;

    case BOT_TEAM_KILLER:
      // attack enemies, then teammates!
      if(self->botEnemy) {
        // we already have an enemy. See if still in LOS.
        if(!botTargetInRange(self,self->botEnemy)) {
          // if it's been over clicksToStopChase clicks since we last seen him in LOS then do nothing, else follow him!
          if(self->botEnemyLastSeen > clicksToStopChase) {
            // forget him!
            self->botEnemy = NULL;
            self->botEnemyLastSeen = 0;
          } else {
            //chase him
            self->botEnemyLastSeen++;
          }
        } else {
          // we see him!
          self->botEnemyLastSeen = 0;
        }
      }

      if(!self->botEnemy) {
        // try to find closest enemy
        tempEntityIndex = botFindClosestEnemy(self, qtrue);
        if(tempEntityIndex >= 0)
          self->botEnemy = &g_entities[tempEntityIndex];
      }

      if(!self->botEnemy) {
        // no enemy, we're all alone :(
      } else {
        // enemy!
        distance = botGetDistanceBetweenPlayer(self, self->botEnemy);
        botAimAtTarget(self, self->botEnemy);

        // enable wallwalk
        if( BG_ClassHasAbility( self->client->ps.stats[ STAT_PCLASS ], SCA_WALLCLIMBER ) ) {
          self->client->pers.cmd.upmove = -1;
        }

        botShootIfTargetInRange(self,self->botEnemy);
        self->client->pers.cmd.forwardmove = forwardMove;
        self->client->pers.cmd.rightmove = -100;
        if(self->client->time1000 >= 500)
          self->client->pers.cmd.rightmove = 100;
      }

      break;

    default:
      // dunno.
      break;
  }
}

void G_BotSpectatorThink( gentity_t *self ) {
  if( self->client->ps.pm_flags & PMF_QUEUED) {
    //we're queued to spawn, all good
    return;
  }

  if( self->client->sess.sessionTeam == TEAM_SPECTATOR ) {
  int teamnum = self->client->pers.teamSelection;
  int clientNum = self->client->ps.clientNum;

    if( teamnum == PTE_HUMANS ) {
      self->client->pers.classSelection = PCL_HUMAN;
      self->client->ps.stats[ STAT_PCLASS ] = PCL_HUMAN;
      self->client->pers.humanItemSelection = WP_MACHINEGUN;
      G_PushSpawnQueue( &level.humanSpawnQueue, clientNum );
  } else if( teamnum == PTE_ALIENS) {
    self->client->pers.classSelection = PCL_ALIEN_LEVEL0;
    self->client->ps.stats[ STAT_PCLASS ] = PCL_ALIEN_LEVEL0;
    G_PushSpawnQueue( &level.alienSpawnQueue, clientNum );
  }
  }
}

static qboolean botAimAtTarget( gentity_t *self, gentity_t *target ) {
  vec3_t dirToTarget, angleToTarget;
  vec3_t top = { 0, 0, 0};
  int vh = 0;
  BG_FindViewheightForClass(  self->client->ps.stats[ STAT_PCLASS ], &vh, NULL );
  top[2]=vh;
  VectorAdd( self->s.pos.trBase, top, top);
  VectorSubtract( target->s.pos.trBase, top, dirToTarget );
  VectorNormalize( dirToTarget );
  vectoangles( dirToTarget, angleToTarget );
   self->client->ps.delta_angles[ 0 ] = ANGLE2SHORT( angleToTarget[ 0 ] );
  self->client->ps.delta_angles[ 1 ] = ANGLE2SHORT( angleToTarget[ 1 ] );
  self->client->ps.delta_angles[ 2 ] = ANGLE2SHORT( angleToTarget[ 2 ] );
  return qtrue;
}

static qboolean botTargetInRange( gentity_t *self, gentity_t *target ) {
  trace_t   trace;
  gentity_t *traceEnt;
  //int myGunRange;
  //myGunRange = MGTURRET_RANGE * 3;

  if( !self || !target )
    return qfalse;

  if( !self->client || !target->client )
    return qfalse;

  if( target->client->ps.stats[ STAT_STATE ] & SS_HOVELING )
    return qfalse;

  if( target->health <= 0 )
    return qfalse;

  //if( Distance( self->s.pos.trBase, target->s.pos.trBase ) > myGunRange )
  //  return qfalse;

  //draw line between us and the target and see what we hit
  trap_Trace( &trace, self->s.pos.trBase, NULL, NULL, target->s.pos.trBase, self->s.number, MASK_SHOT );
  traceEnt = &g_entities[ trace.entityNum ];

  // check that we hit a human and not an object
  //if( !traceEnt->client )
  //  return qfalse;

  //check our target is in LOS
  if(!(traceEnt == target))
    return qfalse;

  return qtrue;
}

static int botFindClosestEnemy( gentity_t *self, qboolean includeTeam ) {
  // return enemy entity index, or -1
  int vectorRange = MGTURRET_RANGE * 3;  
  int i;
  int total_entities;
  int entityList[ MAX_GENTITIES ];
  vec3_t    range;
  vec3_t    mins, maxs;
  gentity_t *target;

  VectorSet( range, vectorRange, vectorRange, vectorRange );
  VectorAdd( self->client->ps.origin, range, maxs );
  VectorSubtract( self->client->ps.origin, range, mins );

  total_entities = trap_EntitiesInBox( mins, maxs, entityList, MAX_GENTITIES );

  // check list for enemies
  for( i = 0; i < total_entities; i++ ) {
    target = &g_entities[ entityList[ i ] ];

    if( target->client && self != target && target->client->ps.stats[ STAT_PTEAM ] != self->client->ps.stats[ STAT_PTEAM ] ) {
      // aliens ignore if it's in LOS because they have radar
      if(self->client->ps.stats[ STAT_PTEAM ] == PTE_ALIENS) {
        return entityList[ i ];
      } else {
        if( botTargetInRange( self, target ) ) {
          return entityList[ i ];
        }
      }
    }
  }

  if(includeTeam) {
    // check list for enemies in team
    for( i = 0; i < total_entities; i++ ) {
      target = &g_entities[ entityList[ i ] ];

      if( target->client && self !=target && target->client->ps.stats[ STAT_PTEAM ] == self->client->ps.stats[ STAT_PTEAM ] ) {
        // aliens ignore if it's in LOS because they have radar
        if(self->client->ps.stats[ STAT_PTEAM ] == PTE_ALIENS) {
          return entityList[ i ];
        } else {
          if( botTargetInRange( self, target ) ) {
            return entityList[ i ];
          }
        }
      }
    }
  }

  return -1;
}

// really an int? what if it's too long?
static int botGetDistanceBetweenPlayer( gentity_t *self, gentity_t *player ) {
  return Distance( self->s.pos.trBase, player->s.pos.trBase );
}

static qboolean botShootIfTargetInRange( gentity_t *self, gentity_t *target ) {
  if(botTargetInRange(self,target)) {
    self->client->pers.cmd.buttons |= BUTTON_ATTACK;
    return qtrue;
  }
  return qfalse;
}

