/*
 *  CaptureTheFlag.cpp
 *  OpenLieroX
 *
 *  Created by Albert Zeyer on 20.03.09.
 *  code under LGPL
 *
 */

#include "CGameMode.h"
#include "CServer.h"
#include "CWorm.h"
#include "FlagInfo.h"

struct CaptureTheFlag : public CGameMode {
	
	virtual std::string Name() {
		return "Capture The Flag";
	}
	
	static const int MAXTEAMS = 4;
	int teamScore[MAXTEAMS];
	
	virtual int GameTeams() {
		return MAXTEAMS;
	}

	virtual int TeamScores(int t) {
		if(t >= 0 && t < MAXTEAMS) return teamScore[t];
		return -1;
	}

	virtual int CompareTeamScore(int t1, int t2) {
		// team score is most important in CTF, more important than left lifes
		{
			int d = TeamScores(t1) - TeamScores(t2);
			if(d != 0) return d;
		}
		
		// ok, fallback to default
		return CGameMode::CompareTeamsScore(t1, t2);
	}

	CaptureTheFlag() { reset(); }
	
	void reset() {
		for(int i = 0; i < MAXTEAMS; ++i)
			teamScore[i] = 0;		
	}
	
	bool isTeamUsed(int t) {
		return !cServer->isTeamEmpty(t);
	}
	
	void initFlag(int t) {
		// currently, just use this easy method to find a spot for the flag
		CVec spawnPoint = cServer->FindSpotCloseToTeam(t);
		spawnPoint = cServer->getMap()->groundPos(spawnPoint) - CVec(0,SKIN_HEIGHT/4);
		cServer->flagInfo()->applyInitFlag(t, spawnPoint);
	}
	
	std::string flagName(int t) {
		return TeamName(t) + " flag";
	}
	
	virtual void PrepareGame() {
		CGameMode::PrepareGame();
		reset();
	}
	
	virtual bool Spawn(CWorm* worm, CVec pos) {
		if(!CGameMode::Spawn(worm, pos)) return false;
		
		if(!cServer->flagInfo()->getFlag(worm->getTeam())) {
			// we have to create the new flag, there isn't any yet
			initFlag(worm->getTeam());
		}
		return true;
	}
	
	virtual void Kill(CWorm* victim, CWorm* killer) {
		Flag* victimsFlag = cServer->flagInfo()->getFlagOfWorm(victim->getID());
		if(victimsFlag) {
			CVec pos = cServer->getMap()->groundPos(victim->getPos()) - CVec(0,SKIN_HEIGHT/4);
			cServer->flagInfo()->applyCustomPos(victimsFlag, pos);
			cServer->SendGlobalText(victim->getName() + " lost " + flagName(victimsFlag->id), TXT_NORMAL);
		}

		if(killer != victim) {
			killer->AddKill();
		}
		
		// Victim is out of the game
		if(victim->Kill() && networkTexts->sPlayerOut != "<none>")
			cServer->SendGlobalText(replacemax(networkTexts->sPlayerOut, "<player>",
											   victim->getName(), 1), TXT_NORMAL);
	}
	
	virtual void hitFlag(CWorm* worm, Flag* flag) {
		if(flag->id == worm->getTeam()) { // own flag
			if(!flag->atSpawnPoint && flag->holderWorm < 0) {
				cServer->flagInfo()->applySetBack(flag);
				cServer->SendGlobalText(worm->getName() + " returned " + flagName(flag->id), TXT_NORMAL);
			}
		}
		else { // enemy flag
			cServer->flagInfo()->applyHolderWorm(flag, worm->getID());
			cServer->SendGlobalText(worm->getName() + " catched " + flagName(flag->id), TXT_NORMAL);
		}
	}
	
	virtual void hitFlagSpawnPoint(CWorm* worm, Flag* flag) {
		if(worm->getTeam() == flag->id) { // own base
			Flag* wormsFlag = cServer->flagInfo()->getFlagOfWorm(worm->getID());
			if(wormsFlag && flag->atSpawnPoint) {
				// yay, we scored!
				teamScore[worm->getTeam()]++;
				cServer->flagInfo()->applySetBack(wormsFlag);
				cServer->SendGlobalText(worm->getName() + " scored for " + TeamName(worm->getTeam()), TXT_NORMAL);
				cServer->SendTeamScoreUpdate();
				cServer->RecheckGame();
			}
		}
	}

	virtual bool CheckGameOver() {
		// currently we use killlimit as teamscorelimit
		for(int i = 0; i < MAXTEAMS; ++i) {
			if(teamScore[i] >= tLXOptions->tGameInfo.iKillLimit) {
				return true;
			}
		}
		
		bool allOut = true;
		for(int i = 0; i < MAX_WORMS; i++)
			if(cServer->getWorms()[i].isUsed()) {
				if(cServer->getWorms()[i].getLives() != WRM_OUT || cServer->getWorms()[i].getAlive()) {
					allOut = false;
					break;
				}
			}
		if(allOut) return true;

		// Check if the timelimit has been reached
		if(TimeLimit() > 0) {
			if (cServer->getServerTime() > TimeLimit()) {
				if(networkTexts->sTimeLimit != "<none>")
					cServer->SendGlobalText(networkTexts->sTimeLimit, TXT_NORMAL);
				notes << "time limit (" << (tLXOptions->tGameInfo.fTimeLimit*60.0f) << ") reached with current time " << cServer->getServerTime().seconds();
				notes << " -> game over" << endl;
				return true;
			}
		}

		if(GameTeams() > 1) {
			// Only one team left?
			int teams = 0;
			for(int i = 0; i < GameTeams(); i++)  {
				if(WormsAliveInTeam(i)) {
					teams++;
				}
			}
			
			// Only one team left
			if(teams <= 1) {
				return true;
			}
		}
		
		return false;
	}
	
	
	
};

static CaptureTheFlag gameMode;
CGameMode* gameMode_CaptureTheFlag = &gameMode;


