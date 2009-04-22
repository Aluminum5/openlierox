/////////////////////////////////////////
//
//			 OpenLieroX
//
// code under LGPL, based on JasonBs work,
// enhanced by Dark Charlie and Albert Zeyer
//
//
/////////////////////////////////////////


// Server class
// Created 28/6/02
// Jason Boettcher



#include <stdarg.h>
#include <vector>
#include <sstream>
#include <time.h>

#include "LieroX.h"
#include "Cache.h"
#include "CClient.h"
#include "CServer.h"
#include "console.h"
#include "CBanList.h"
#include "GfxPrimitives.h"
#include "FindFile.h"
#include "StringUtils.h"
#include "CWorm.h"
#include "Protocol.h"
#include "Error.h"
#include "MathLib.h"
#include "DedicatedControl.h"
#include "Physics.h"
#include "CServerNetEngine.h"
#include "CChannel.h"
#include "CServerConnection.h"
#include "Debug.h"
#include "CGameMode.h"
#include "ProfileSystem.h"
#include "FlagInfo.h"

GameServer	*cServer = NULL;

// Bots' clients
CServerConnection *cBots = NULL;

// declare them only locally here as nobody really should use them explicitly
std::string OldLxCompatibleString(const std::string &Utf8String);

GameServer::GameServer() {
	iState = SVS_LOBBY;
	m_flagInfo = NULL;
	cClients = NULL;
	cMap = NULL;
	cWorms = NULL;
	iNumPlayers = 0;
	Clear();
	CScriptableVars::RegisterVars("GameServer")
		( sWeaponRestFile, "WeaponRestrictionsFile" ) // Only for dedicated server
		;
}

GameServer::~GameServer()  {
	CScriptableVars::DeRegisterVars("GameServer");
}

///////////////////
// Clear the server
void GameServer::Clear()
{
	cClients = NULL;
	cMap = NULL;
	//cProjectiles = NULL;
	cWorms = NULL;
	iState = SVS_LOBBY;
	iServerFrame=0; lastClientSendData = 0;
	iNumPlayers = 0;
	bRandomMap = false;
	//iMaxWorms = MAX_PLAYERS;
	bGameOver = false;
	//iGameType = GMT_DEATHMATCH;
	fLastBonusTime = 0;
	for( int i=0; i < MAX_SERVER_SOCKETS; i++ )
		InvalidateSocketState(tSockets[i]);
	for(std::vector<NatConnection>::iterator it = tNatClients.begin(); it != tNatClients.end(); it++)  {
		InvalidateSocketState(it->tConnectHereSocket);
		InvalidateSocketState(it->tTraverseSocket);
		it->fLastUsed = AbsTime();
		it->bClientConnected = false;
	}
	bServerRegistered = false;
	fLastRegister = AbsTime();
	fRegisterUdpTime = AbsTime();
	nPort = LX_PORT;
	bLocalClientConnected = false;
	m_clientsNeedLobbyUpdate = false;
	
	fLastUpdateSent = AbsTime();

	cBanList.loadList("cfg/ban.lst");
	cShootList.Clear();

	iSuicidesInPacket = 0;

	for(int i=0; i<MAX_CHALLENGES; i++) {
		SetNetAddrValid(tChallenges[i].Address, false);
		tChallenges[i].fTime = 0;
		tChallenges[i].iNum = 0;
	}

	tMasterServers.clear();
	tCurrentMasterServer = tMasterServers.begin();
}


///////////////////
// Start a server
int GameServer::StartServer()
{
	// Shutdown and clear any previous server settings
	Shutdown();
	Clear();

	if (!bDedicated && tLX->iGameType == GME_HOST)
		tLX->bHosted = true;

	// Notifications
	if (bDedicated)
		notes << "Server max upload bandwidth is " << tLXOptions->iMaxUploadBandwidth << " bytes/s" << endl;

	// Is this the right place for this?
	sWeaponRestFile = "cfg/wpnrest.dat";
	bLocalClientConnected = false;

	// Disable SSH for non-dedicated servers as it is cheaty
	if (!bDedicated)
		tLXOptions->tGameInfo.bServerSideHealth = false;


	// Open the socket
	nPort = tLXOptions->iNetworkPort;
	tSockets[0] = OpenUnreliableSocket(tLXOptions->iNetworkPort);
	if(!IsSocketStateValid(tSockets[0])) {
		hints << "Server: could not open socket on port " << tLXOptions->iNetworkPort << ", trying rebinding client socket" << endl;
		if( cClient->RebindSocket() ) {	// If client has taken that port, free it
			tSockets[0] = OpenUnreliableSocket(tLXOptions->iNetworkPort);
		}

		if(!IsSocketStateValid(tSockets[0])) {
			hints << "Server: client rebinding didn't work, trying random port" << endl;
			tSockets[0] = OpenUnreliableSocket(0);
		}
		
		if(!IsSocketStateValid(tSockets[0])) {
			hints << "Server: we cannot even open a random port!" << endl;
			SetError("Server Error: Could not open UDP socket");
			return false;
		}
		
		NetworkAddr a; GetLocalNetAddr(tSockets[0], a);
		nPort = GetNetAddrPort(a);
	}
	if(!ListenSocket(tSockets[0])) {
		SetError( "Server Error: cannot start listening" );
		return false;
	}
	
	for( int i = 1; i < MAX_SERVER_SOCKETS; i++ )
	{
		tSockets[i] = OpenUnreliableSocket(0);
		if(!IsSocketStateValid(tSockets[i])) {
			hints << "Server: we cannot open a random port!" << endl;
			SetError("Server Error: Could not open UDP socket");
			return false;
		}
		if(!ListenSocket(tSockets[i])) {
			SetError( "Server Error: cannot start listening" );
			return false;
		}
	}

	NetworkAddr addr;
	GetLocalNetAddr(tSockets[0], addr);
	// TODO: Why is that stored in debug_string ???
	NetAddrToString(addr, tLX->debug_string);
	hints << "server started on " <<  tLX->debug_string << endl;

	// Initialize the clients
	cClients = new CServerConnection[MAX_CLIENTS];
	if(cClients==NULL) {
		SetError("Error: Out of memory!\nsv::Startserver() " + itoa(__LINE__));
		return false;
	}

	// Allocate the worms
	cWorms = new CWorm[MAX_WORMS];
	if(cWorms == NULL) {
		SetError("Error: Out of memory!\nsv::Startserver() " + itoa(__LINE__));
		return false;
	}

	// Initialize the bonuses
	int i;
	for(i=0;i<MAX_BONUSES;i++)
		cBonuses[i].setUsed(false);

	// Shooting list
	if( !cShootList.Initialize() ) {
		SetError("Error: Out of memory!\nsv::Startserver() " + itoa(__LINE__));
		return false;
	}

	// In the lobby
	iState = SVS_LOBBY;
	
	m_flagInfo = new FlagInfo();

	// Load the master server list
	FILE *fp = OpenGameFile("cfg/masterservers.txt","rt");
	if( fp )  {
		// Parse the lines
		while(!feof(fp)) {
			std::string buf = ReadUntil(fp);
			TrimSpaces(buf);
			if(buf.size() > 0 && buf[0] != '#') {
				tMasterServers.push_back(buf);
			}
		}

		fclose(fp);
	} else
		warnings << "cfg/masterservers.txt not found" << endl;

	tCurrentMasterServer = tMasterServers.begin();

	fp = OpenGameFile("cfg/udpmasterservers.txt","rt");
	if( fp )  {

		// Parse the lines
		while(!feof(fp)) {
			std::string buf = ReadUntil(fp);
			TrimSpaces(buf);
			if(buf.size() > 0) {
				tUdpMasterServers.push_back(buf);
			}
		}

		fclose(fp);
	} else
		warnings << "cfg/udpmasterservers.txt not found" << endl;


	if(tLXOptions->bRegServer) {
		bServerRegistered = false;
		fLastRegister = tLX->currentTime;
		RegisterServer();
		
		fRegisterUdpTime = tLX->currentTime + 5.0f; // 5 seconds from now - to give the local client enough time to join before registering the player count		
	}

	// Initialize the clients
	for(i=0;i<MAX_CLIENTS;i++) {
		cClients[i].Clear();
		cClients[i].getUdpFileDownloader()->allowFileRequest(true);

		// Initialize the shooting list
		if( !cClients[i].getShootList()->Initialize() ) {
			SetError( "Server Error: cannot initialize the shooting list of some client" );
			return false;
		}
	}

	return true;
}


bool GameServer::serverChoosesWeapons() {
	// HINT:
	// At the moment, the only cases where we need the bServerChoosesWeapons are:
	// - bForceRandomWeapons
	// - bSameWeaponsAsHostWorm
	// If we make this controllable via mods later on, there are other cases where we have to enable bServerChoosesWeapons.
	return
		tLXOptions->tGameInfo.bForceRandomWeapons ||
		(tLXOptions->tGameInfo.bSameWeaponsAsHostWorm && cClient->getNumWorms() > 0); // only makes sense if we have at least one worm	
}

///////////////////
// Start the game (prepare it for weapon selection, BeginMatch is the actual start)
int GameServer::StartGame()
{	
	// Check that gamespeed != 0
	if (-0.05f <= (float)tLXOptions->tGameInfo.features[FT_GameSpeed] && (float)tLXOptions->tGameInfo.features[FT_GameSpeed] <= 0.05f) {
		warnings << "WARNING: gamespeed was set to " << tLXOptions->tGameInfo.features[FT_GameSpeed].toString() << "; resetting it to 1" << endl;
		tLXOptions->tGameInfo.features[FT_GameSpeed] = 1;
	}
	
		
	checkVersionCompatibilities(true);


	CBytestream bs;
	float timer;
	
	notes << "GameServer::StartGame() mod " << tLXOptions->tGameInfo.sModName << endl;

	// Check
	if (!cWorms) { errors << "StartGame(): Worms not initialized" << endl; return false; }
	
	CWorm *w = cWorms;
	for (int p = 0; p < MAX_WORMS; p++, w++) {
		if(w->isUsed() && w->isPrepared()) {
			warnings << "WARNING: StartGame(): worm " << p << " was already prepared!" << endl;
			w->Unprepare();
		}
	}
	
	// TODO: why delete + create new map instead of simply shutdown/clear map?
	// WARNING: This can lead to segfaults if there are already prepared AI worms with running AI thread (therefore we unprepared them above)

	// Shutdown any previous map instances
	if(cMap) {
		cMap->Shutdown();
		delete cMap;
		cMap = NULL;
		cClient->resetMap();
	}

	// Create the map
mapCreate:
	cMap = new CMap;
	if(cMap == NULL) {
		SetError("Error: Out of memory!\nsv::Startgame() " + itoa(__LINE__));
		if(cCache.GetEntryCount() > 0) {
			hints << "current cache size is " << cCache.GetCacheSize() << ", we are clearing it now" << endl;
			cCache.Clear();
			goto mapCreate;
		}
		return false;
	}
	
	
	bRandomMap = false;
	{
		timer = SDL_GetTicks()/1000.0f;
		std::string sMapFilename = "levels/" + tLXOptions->tGameInfo.sMapFile;
		if(!cMap->Load(sMapFilename)) {
			errors << "Server StartGame: Could not load the level " << tLXOptions->tGameInfo.sMapFile << endl;
			return false;
		}
		notes << "Map loadtime: " << (float)((SDL_GetTicks()/1000.0f) - timer) << " seconds" << endl;
	}
	
	// Load the game script
	timer = SDL_GetTicks()/1000.0f;

	cGameScript = cCache.GetMod( tLXOptions->tGameInfo.sModDir );
	if( cGameScript.get() == NULL )
	{
	gameScriptCreate:
		cGameScript = new CGameScript();
		if(cGameScript.get() == NULL) {
			errors << "Server StartGame: cannot allocate gamescript" << endl;
			if(cCache.GetEntryCount() > 0) {
				hints << "current cache size is " << cCache.GetCacheSize() << ", we are clearing it now" << endl;
				cCache.Clear();
				goto gameScriptCreate;
			}			
			return false;
		}
		int result = cGameScript.get()->Load( tLXOptions->tGameInfo.sModDir );
		cCache.SaveMod( tLXOptions->tGameInfo.sModDir, cGameScript );

		if(result != GSE_OK) {
			errors << "Server StartGame: Could not load the game script \"" << tLXOptions->tGameInfo.sModDir << "\"" << endl;
			return false;
		}
	}
	notes << "Mod loadtime: " << (float)((SDL_GetTicks()/1000.0f) - timer) << " seconds" << endl;

	// Load & update the weapon restrictions
	cWeaponRestrictions.loadList(sWeaponRestFile);
	cWeaponRestrictions.updateList(cGameScript.get());

	// Set some info on the worms
	for(int i=0;i<MAX_WORMS;i++) {
		if(cWorms[i].isUsed()) {
			cWorms[i].setLives(tLXOptions->tGameInfo.iLives);
			cWorms[i].setKills(0);
			cWorms[i].setDamage(0);
			cWorms[i].setGameScript(cGameScript.get());
			cWorms[i].setWpnRest(&cWeaponRestrictions);
			cWorms[i].setLoadingTime( (float)tLXOptions->tGameInfo.iLoadingTime / 100.0f );
			cWorms[i].setWeaponsReady(false);
			cWorms[i].Prepare(true);
		}
	}

	// Clear bonuses
	for(int i=0; i<MAX_BONUSES; i++)
		cBonuses[i].setUsed(false);

	// Clear the shooting list
	cShootList.Clear();

	m_flagInfo->reset();
	
	fLastBonusTime = tLX->currentTime;
	fWeaponSelectionTime = tLX->currentTime;
	iWeaponSelectionTime_Warning = 0;

	// Set all the clients to 'not ready'
	for(int i=0;i<MAX_CLIENTS;i++) {
		cClients[i].getShootList()->Clear();
		cClients[i].setGameReady(false);
		cClients[i].getUdpFileDownloader()->allowFileRequest(false);
	}

	//TODO: Move into CTeamDeathMatch | CGameMode
	// If this is the host, and we have a team game: Send all the worm info back so the worms know what
	// teams they are on
	if( tLX->iGameType == GME_HOST ) {
		if( getGameMode()->GameTeams() > 1 ) {
			
			CWorm *w = cWorms;
			CBytestream b;
			
			for(int i=0; i<MAX_WORMS; i++, w++ ) {
				if( !w->isUsed() )
					continue;
				
				// TODO: move that out here
				// Write out the info
				b.writeByte(S2C_WORMINFO);
				b.writeInt(w->getID(),1);
				w->writeInfo(&b);
			}
			
			SendGlobalPacket(&b);
		}
	}
	
	if( tLXOptions->tGameInfo.features[FT_NewNetEngine] )
	{
		warnings << "New net engine enabled, we are disabling some features" << endl;
		NewNet::DisableAdvancedFeatures();
	}

	iState = SVS_GAME;		// In-game, waiting for players to load
	iServerFrame = 0;
	bGameOver = false;

	for( int i = 0; i < MAX_CLIENTS; i++ )
	{
		if( cClients[i].getStatus() != NET_CONNECTED )
			continue;
		cClients[i].getNetEngine()->SendPrepareGame();

		// Force random weapons for spectating clients
		if( cClients[i].getNumWorms() > 0 && cClients[i].getWorm(0)->isSpectating() )
		{
			for( int i = 0; i < cClients[i].getNumWorms(); i++ )
			{
				cClients[i].getWorm(i)->GetRandomWeapons();
				cClients[i].getWorm(i)->setWeaponsReady(true);
			}
			SendWeapons();	// TODO: we're sending multiple weapons packets, but clients handle them okay
		}
	}
	
	PhysicsEngine::Get()->initGame();

	if( DedicatedControl::Get() )
		DedicatedControl::Get()->WeaponSelections_Signal();

	// remove from notifier; we don't want events anymore, we have a fixed FPS rate ingame
	for( int i = 0; i < MAX_SERVER_SOCKETS; i++ )
		RemoveSocketFromNotifierGroup( tSockets[i] );
	for(std::vector<NatConnection>::iterator it = tNatClients.begin(); it != tNatClients.end(); ++it)  {
		if (IsSocketStateValid(it->tTraverseSocket))
			RemoveSocketFromNotifierGroup(it->tTraverseSocket);
		if (IsSocketStateValid(it->tConnectHereSocket))
			RemoveSocketFromNotifierGroup(it->tConnectHereSocket);
	}
	
	// Re-register the server to reflect the state change
	if( tLXOptions->bRegServer && tLX->iGameType == GME_HOST )
		RegisterServerUdp();

	
	for(int i = 0; i < MAX_WORMS; i++) {
		if(!cWorms[i].isUsed())
			continue;
		PrepareWorm(&cWorms[i]);
	}

	for( int i = 0; i < MAX_CLIENTS; i++ ) {
		if( cClients[i].getStatus() != NET_CONNECTED )
			continue;
		cClients[i].getNetEngine()->SendWormProperties(true); // if we have changed them in prepare or so
	}
	
	return true;
}

void GameServer::PrepareWorm(CWorm* worm) {
	// initial server side weapon handling
	if(tLXOptions->tGameInfo.bSameWeaponsAsHostWorm && cClient->getNumWorms() > 0) {
		if(cClient->getGameReady() && cClient->getWorm(0) != NULL && cClient->getWorm(0)->getWeaponsReady()) {
			worm->CloneWeaponsFrom(cClient->getWorm(0));
			worm->setWeaponsReady(true);
		}
		// in the case that the host worm is not ready, we will get the weapons later
	}
	else if(tLXOptions->tGameInfo.bForceRandomWeapons) {
		worm->GetRandomWeapons();
		worm->setWeaponsReady(true);
	}
	
	if(worm->getWeaponsReady()) {
		// TODO: move that out here
		CBytestream bs;
		bs.writeByte(S2C_WORMWEAPONINFO);
		worm->writeWeapons(&bs);
		SendGlobalPacket(&bs);		
	}
	
	getGameMode()->PrepareWorm(worm);	
}


///////////////////
// Begin the match
void GameServer::BeginMatch(CServerConnection* receiver)
{
	hints << "Server: BeginMatch";
	if(receiver) hints << " for " << receiver->debugName();
	hints << endl;

	bool firstStart = false;
	
	if(iState != SVS_PLAYING) {
		// game has started for noone yet and we get the first start signal
		firstStart = true;
		iState = SVS_PLAYING;
		if( DedicatedControl::Get() )
			DedicatedControl::Get()->GameStarted_Signal();
		
		// Initialize some server settings
		fServertime = 0;
		iServerFrame = 0;
		bGameOver = false;
		fGameOverTime = AbsTime();
		cShootList.Clear();
	}
	
	// Send the connected clients a startgame message
	// IMPORTANT: Clients will interpret this that they were forced to be ready,
	// so they will stop the weapon selection and start the game and wait for
	// getting spawned. Thus, this should only be sent if we got ParseImReady from client.
	CBytestream bs;
	bs.writeInt(S2C_STARTGAME,1);
	if (tLXOptions->tGameInfo.features[FT_NewNetEngine])
		bs.writeInt(NewNet::netRandom.getSeed(), 4);
	if(receiver)
		receiver->getNetEngine()->SendPacket(&bs);
	else
		SendGlobalPacket(&bs);
	

	if(receiver) {
		// inform new client about other ready clients
		CServerConnection *cl = cClients;
		for(int c = 0; c < MAX_CLIENTS; c++, cl++) {
			// Client not connected or no worms
			if( cl == receiver || cl->getStatus() == NET_DISCONNECTED || cl->getStatus() == NET_ZOMBIE )
				continue;
			
			if(cl->getGameReady()) {				
				// spawn all worms for the new client
				for(int i = 0; i < cl->getNumWorms(); i++) {
					if(!cl->getWorm(i)) continue;
					
					receiver->getNetEngine()->SendWormScore( cl->getWorm(i) );
					
					if(cl->getWorm(i)->getAlive()) {
						receiver->getNetEngine()->SendSpawnWorm( cl->getWorm(i), cl->getWorm(i)->getPos() );
					}
				}
			}
		}
		// Spawn receiver's worms
		cl = receiver;
		for(int i = 0; i < receiver->getNumWorms(); i++) {
			if(!cl->getWorm(i)) continue;
			for(int ii = 0; ii < MAX_CLIENTS; ii++)
				cClients[ii].getNetEngine()->SendWormScore( cl->getWorm(i) );
					
			if(cl->getWorm(i)->getAlive() && !cl->getWorm(i)->haveSpawnedOnce()) {
				SpawnWorm( cl->getWorm(i) );
				if( tLXOptions->tGameInfo.bEmptyWeaponsOnRespawn )
					SendEmptyWeaponsOnRespawn( cl->getWorm(i) );
			}
		}
	}
	
	if(firstStart) {
		for(int i=0;i<MAX_WORMS;i++) {
			if(cWorms[i].isUsed())
				cWorms[i].setAlive(false);
		}
		for(int i=0;i<MAX_WORMS;i++) {
			if( cWorms[i].isUsed() && cWorms[i].getWeaponsReady() && cWorms[i].getLives() != WRM_OUT )
				SpawnWorm( & cWorms[i] );
				if( tLXOptions->tGameInfo.bEmptyWeaponsOnRespawn )
					SendEmptyWeaponsOnRespawn( & cWorms[i] );
		}

		DumpGameState();
		
		// Prepare the gamemode
		notes << "preparing game mode " << getGameMode()->Name() << endl;
		getGameMode()->PrepareGame();
	}

	if(firstStart)
		iLastVictim = -1;
	
	// For spectators: set their lives to out and tell clients about it
	for (int i = 0; i < MAX_WORMS; i++)  {
		if (cWorms[i].isUsed() && cWorms[i].isSpectating() && cWorms[i].getLives() != WRM_OUT)  {
			cWorms[i].setLives(WRM_OUT);
			cWorms[i].setKills(0);
			cWorms[i].setDamage(0);
			if(receiver)
				receiver->getNetEngine()->SendWormScore( & cWorms[i] );
			else
				for(int ii = 0; ii < MAX_CLIENTS; ii++)
					cClients[ii].getNetEngine()->SendWormScore( & cWorms[i] );
		}
	}

	// perhaps the state is already bad
	RecheckGame();

	if(firstStart) {
		// Re-register the server to reflect the state change in the serverlist
		if( tLXOptions->bRegServer && tLX->iGameType == GME_HOST )
			RegisterServerUdp();
	}
}


////////////////
// End the game
void GameServer::GameOver()
{
	// The game is already over
	if (bGameOver)
		return;

	bGameOver = true;
	fGameOverTime = tLX->currentTime;

	hints << "Server: gameover"; 

	int winner = getGameMode()->Winner();
	if(winner >= 0) {
		if (networkTexts->sPlayerHasWon != "<none>")
			SendGlobalText((replacemax(networkTexts->sPlayerHasWon, "<player>",
												cWorms[winner].getName(), 1)), TXT_NORMAL);
		hints << ", worm " << winner << " has won the match";
	}
	
	int winnerTeam = getGameMode()->WinnerTeam();
	if(winnerTeam >= 0) {
		if(networkTexts->sTeamHasWon != "<none>")
			SendGlobalText((replacemax(networkTexts->sTeamHasWon,
									 "<team>", getGameMode()->TeamName(winnerTeam), 1)), TXT_NORMAL);
		hints << ", team " << winnerTeam << " has won the match";
	}
	
	hints << endl;

	// TODO: move that out here!
	// Let everyone know that the game is over
	for(int c = 0; c < MAX_CLIENTS; c++) {
		CServerConnection *cl = &cClients[c];
		if(!cl->getNetEngine()) continue;
		
		CBytestream bs;
		bs.writeByte(S2C_GAMEOVER);
		if(cl->getClientVersion() < OLXBetaVersion(9)) {
			int winLX = winner;
			if(getGameMode()->isTeamGame()) {
				// we have to send always the worm-id (that's the LX56 protocol...)
				if(winLX < 0)
					for(int i = 0; i < getNumPlayers(); ++i) {
						if(cWorms[i].getTeam() == winnerTeam) {
							winLX = i;
							break;
						}
					}
			}
			if(winLX < 0) winLX = 0; // we cannot have no winner in LX56
			bs.writeInt(winLX, 1);
		}
		else { // >= Beta9
			bs.writeByte(winner);
			if(getGameMode()->GeneralGameType() == GMT_TEAMS) {
				bs.writeByte(winnerTeam);
				bs.writeByte(getGameMode()->GameTeams());
				for(int i = 0; i < getGameMode()->GameTeams(); ++i) {
					bs.writeInt16(getGameMode()->TeamScores(i));
				}
			}
		}
		
		cl->getNetEngine()->SendPacket(&bs);
	}

	// Reset the state of all the worms so they don't keep shooting/moving after the game is over
	// HINT: next frame will send the update to all worms
	CWorm *w = cWorms;
	int i;
	for ( i=0; i < MAX_WORMS; i++, w++ )  {
		if (!w->isUsed())
			continue;

		w->clearInput();
		
		if( !getGameMode()->isTeamGame() )
		{
			if( w->getID() == winner )
				w->addTotalWins();
			else
				w->addTotalLosses();
		}
		else
		{
			if( w->getTeam() == winnerTeam )
				w->addTotalWins();
			else
				w->addTotalLosses();
		}
	}
	
	DumpGameState();
}


bool GameServer::isTeamEmpty(int t) const {
	for(int i = 0; i < MAX_WORMS; ++i) {
		if(cWorms[i].isUsed() && cWorms[i].getTeam() == t) {
			return false;
		}
	}
	return true;
}

int GameServer::getFirstEmptyTeam() const {
	int team = 0;
	while(team < getGameMode()->GameTeams()) {
		if(isTeamEmpty(team)) return team;
		team++;
	}
	return -1;
}


///////////////////
// Main server frame
void GameServer::Frame()
{
	// Playing frame
	if(iState == SVS_PLAYING) {
		fServertime += tLX->fRealDeltaTime;
		iServerFrame++;
	}

	// Process any http requests (register, deregister)
	if( tLXOptions->bRegServer && !bServerRegistered )
		ProcessRegister();

	if(m_clientsNeedLobbyUpdate && tLX->currentTime - m_clientsNeedLobbyUpdateTime >= 0.2f) {
		m_clientsNeedLobbyUpdate = false;
		
		if(!cClients) { // can happen if server was not started correctly
			errors << "GS::UpdateGameLobby: cClients == NULL" << endl;
		}
		else {
			CServerConnection* cl = cClients;
			for(int i = 0; i < MAX_CLIENTS; i++, cl++) {
				if(cl->getStatus() != NET_CONNECTED)
					continue;
				cl->getNetEngine()->SendUpdateLobbyGame();
			}	
		}
	}
	
	ReadPackets();

	SimulateGame();

	CheckTimeouts();

	CheckRegister();

	SendFiles();

	SendPackets();
}

////////////////////
// Reads packets from the given sockets
bool GameServer::ReadPacketsFromSocket(NetworkSocket &sock)
{
	if (!IsSocketReady(sock))
		return false;

	NetworkAddr adrFrom;
	CBytestream bs;

	bool anythingNew = false;
	while(bs.Read(sock)) {
		anythingNew = true;
		
		// Set out address to addr from where last packet was sent, used for NAT traverse
		GetRemoteNetAddr(sock, adrFrom);
		SetRemoteNetAddr(sock, adrFrom);

		// Check for connectionless packets (four leading 0xff's)
		if(bs.readInt(4) == -1) {
			std::string address;
			NetAddrToString(adrFrom, address);
			bs.ResetPosToBegin();
			// parse all connectionless packets
			// For example lx::openbeta* was sent in a way that 2 packages were sent at once.
			// <rev1457 (incl. Beta3) versions only will parse one package at a time.
			// I fixed that now since >rev1457 that it parses multiple packages here
			// (but only for new net-commands).
			// Same thing in CClient.cpp in ReadPackets
			while(!bs.isPosAtEnd() && bs.readInt(4) == -1)
				ParseConnectionlessPacket(sock, &bs, address);
			continue;
		}
		bs.ResetPosToBegin();

		// Reset the suicide packet count
		iSuicidesInPacket = 0;

		// Read packets
		CServerConnection *cl = cClients;
		for (int c = 0; c < MAX_CLIENTS; c++, cl++) {

			// Player not connected
			if(cl->getStatus() == NET_DISCONNECTED)
				continue;

			// Check if the packet is from this player
			if(!AreNetAddrEqual(adrFrom, cl->getChannel()->getAddress()))
				continue;

			// Check the port
			if (GetNetAddrPort(adrFrom) != GetNetAddrPort(cl->getChannel()->getAddress()))
				continue;

			// Parse the packet - process continuously in case we've received multiple logical packets on new CChannel
			while (cl->getChannel()->Process(&bs))  {
				// Only process the actual packet for playing clients
				if( cl->getStatus() != NET_ZOMBIE )
					cl->getNetEngine()->ParsePacket(&bs);
				bs.Clear();
			}
		}
	}

	return anythingNew;
}


///////////////////
// Read packets
bool GameServer::ReadPackets()
{
	bool anythingNew = false;
	// Main sockets
	for( int i = 0; i < MAX_SERVER_SOCKETS; i++ )
		if( ReadPacketsFromSocket(tSockets[i]) )
			anythingNew = true;

	// Traverse sockets
	for (std::vector<NatConnection>::iterator it = tNatClients.begin(); it != tNatClients.end(); ++it)  {
		if (ReadPacketsFromSocket(it->tTraverseSocket))  {
			anythingNew = true;
			if (!it->bClientConnected)  {
				std::string addr;
				NetworkAddr a;
				GetRemoteNetAddr(it->tTraverseSocket, a);
				NetAddrToString(a, addr);
				notes << "A client " << addr << " successfully connected using NAT traversal" << endl;
				it->bClientConnected = true;
			}
			it->fLastUsed = tLX->currentTime;
		}

		if (ReadPacketsFromSocket(it->tConnectHereSocket))  {
			anythingNew = true;
			if (!it->bClientConnected)  {
				std::string addr;
				NetworkAddr a;
				GetRemoteNetAddr(it->tConnectHereSocket, a);
				NetAddrToString(a, addr);
				notes << "A client " << addr << " successfully connected using connect_here traversal" << endl;
				it->bClientConnected = true;
			}
			it->fLastUsed = tLX->currentTime;
		}
	}
	
	return anythingNew;
}


///////////////////
// Send packets
void GameServer::SendPackets()
{
	int c;
	CServerConnection *cl = cClients;

	// If we are playing, send update to the clients
	if (iState == SVS_PLAYING)
		SendUpdate();

	// Randomly send a random packet :)
#if defined(FUZZY_ERROR_TESTING) && defined(FUZZY_ERROR_TESTING_S2C)
	if (GetRandomInt(50) > 24)
		SendRandomPacket();
#endif


	// Go through each client and send them a message
	for(c=0;c<MAX_CLIENTS;c++,cl++) {
		if(cl->getStatus() == NET_DISCONNECTED || !IsSocketReady(cl->getChannel()->getSocket()))
			continue;

		// Send out the packets if we haven't gone over the clients bandwidth
		cl->getChannel()->Transmit(cl->getUnreliable());

		// Clear the unreliable bytestream
		cl->getUnreliable()->Clear();
	}
}


///////////////////
// Register the server
void GameServer::RegisterServer()
{
	if (tMasterServers.size() == 0)
		return;

	// Create the url
	std::string addr_name;

	// We don't know the external IP, just use the local one
	// Doesn't matter what IP we use because the masterserver finds it out by itself anyways
	NetworkAddr addr;
	GetLocalNetAddr(tSockets[0], addr);
	NetAddrToString(addr, addr_name);

	// Remove port from IP
	size_t pos = addr_name.rfind(':');
	if (pos != std::string::npos)
		addr_name.erase(pos, std::string::npos);

	sCurrentUrl = std::string(LX_SVRREG) + "?port=" + itoa(nPort) + "&addr=" + addr_name;

	bServerRegistered = false;

	// Start with the first server
	notes << "Registering server at " << *tCurrentMasterServer << endl;
	tCurrentMasterServer = tMasterServers.begin();
	tHttp.RequestData(*tCurrentMasterServer + sCurrentUrl, tLXOptions->sHttpProxy);
}


///////////////////
// Process the registering of the server
void GameServer::ProcessRegister()
{
	if(!tLXOptions->bRegServer || bServerRegistered || tMasterServers.size() == 0 || tLX->iGameType != GME_HOST)
		return;

	int result = tHttp.ProcessRequest();

	switch(result)  {
	// Normal, keep going
	case HTTP_PROC_PROCESSING:
		return; // Processing, no more work for us
	break;

	// Failed
	case HTTP_PROC_ERROR:
		notifyLog("Could not register with master server: " + tHttp.GetError().sErrorMsg);
	break;

	// Completed ok
	case HTTP_PROC_FINISHED:
		fLastRegister = tLX->currentTime;
	break;
	}

	// Server failed or finished, anyway, go on
	tCurrentMasterServer++;
	if (tCurrentMasterServer != tMasterServers.end())  {
		notes << "Registering server at " << *tCurrentMasterServer << endl;
		tHttp.RequestData(*tCurrentMasterServer + sCurrentUrl, tLXOptions->sHttpProxy);
	} else {
		// All servers are processed
		bServerRegistered = true;
		tCurrentMasterServer = tMasterServers.begin();
	}

}

void GameServer::RegisterServerUdp()
{
	// Don't register a local play
	if (tLX->iGameType == GME_LOCAL)
		return;

	for( uint f=0; f<tUdpMasterServers.size(); f++ )
	{
		if( f >= MAX_SERVER_SOCKETS )
		{
			notes << "UDP masterserver list too big, max " << int(MAX_SERVER_SOCKETS) << " entries supported" << endl;
			break;
		}
		NetworkAddr addr;
		if( tUdpMasterServers[f].find(":") == std::string::npos )
			continue;
		std::string domain = tUdpMasterServers[f].substr( 0, tUdpMasterServers[f].find(":") );
		int port = atoi(tUdpMasterServers[f].substr( tUdpMasterServers[f].find(":") + 1 ));
		if( !GetFromDnsCache(domain, addr) )
		{
			GetNetAddrFromNameAsync(domain, addr);
			fRegisterUdpTime = tLX->currentTime + 5.0f;
			continue;
		}

		notes << "Registering on UDP masterserver " << tUdpMasterServers[f] << endl;
		SetNetAddrPort( addr, port );
		SetRemoteNetAddr( tSockets[f], addr );

		CBytestream bs;

		bs.writeInt(-1,4);
		bs.writeString("lx::dummypacket");	// So NAT/firewall will understand we really want to connect there
		bs.Send(tSockets[f]);
		bs.Send(tSockets[f]);
		bs.Send(tSockets[f]);

		bs.Clear();
		bs.writeInt(-1, 4);
		bs.writeString("lx::register");
		bs.writeString(OldLxCompatibleString(tLXOptions->sServerName));
		bs.writeByte(iNumPlayers);
		bs.writeByte(tLXOptions->tGameInfo.iMaxPlayers);
		bs.writeByte(iState);
		// Beta8+
		bs.writeString(GetGameVersion().asString());
		bs.writeByte(serverAllowsConnectDuringGame());
		

		bs.Send(tSockets[f]);
	}
}

void GameServer::DeRegisterServerUdp()
{
	for( uint f=0; f<tUdpMasterServers.size(); f++ )
	{
		if( f >= MAX_SERVER_SOCKETS )
		{
			notes << "UDP masterserver list too big, max " << int(MAX_SERVER_SOCKETS) << " entries supported" << endl;
			break;
		}
		NetworkAddr addr;
		if( tUdpMasterServers[f].find(":") == std::string::npos )
			continue;
		std::string domain = tUdpMasterServers[f].substr( 0, tUdpMasterServers[f].find(":") );
		int port = atoi(tUdpMasterServers[f].substr( tUdpMasterServers[f].find(":") + 1 ));
		if( !GetFromDnsCache(domain, addr) )
		{
			GetNetAddrFromNameAsync(domain, addr);
			continue;
		}
		SetNetAddrPort( addr, port );
		SetRemoteNetAddr( tSockets[f], addr );

		CBytestream bs;

		bs.writeInt(-1,4);
		bs.writeString("lx::dummypacket");	// So NAT/firewall will understand we really want to connect there
		bs.Send(tSockets[f]);
		bs.Send(tSockets[f]);
		bs.Send(tSockets[f]);

		bs.Clear();
		bs.writeInt(-1, 4);
		bs.writeString("lx::deregister");

		bs.Send(tSockets[f]);
	}
}


///////////////////
// This checks the registering of a server
void GameServer::CheckRegister()
{
	// If we don't want to register, just leave
	if(!tLXOptions->bRegServer || tLX->iGameType != GME_HOST)
		return;

	// If we registered over n seconds ago, register again
	// The master server will not add duplicates, instead it will update the last ping time
	// so we will have another 5 minutes before our server is cleared
	if( tLX->currentTime - fLastRegister > 4*60.0f ) {
		bServerRegistered = false;
		fLastRegister = tLX->currentTime;
		RegisterServer();
	}
	// UDP masterserver will remove our registry in 2 minutes
	if( tLX->currentTime > fRegisterUdpTime ) {
		fRegisterUdpTime = tLX->currentTime + 40.0f;
		RegisterServerUdp();
	}
}


///////////////////
// De-register the server
bool GameServer::DeRegisterServer()
{
	// If we aren't registered, or we didn't try to register, just leave
	if( !tLXOptions->bRegServer || !bServerRegistered || tMasterServers.size() == 0 || tLX->iGameType != GME_HOST)
		return false;

	// Create the url
	std::string addr_name;
	NetworkAddr addr;

	GetLocalNetAddr(tSockets[0], addr);
	NetAddrToString(addr, addr_name);

	sCurrentUrl = std::string(LX_SVRDEREG) + "?port=" + itoa(nPort) + "&addr=" + addr_name;

	// Initialize the request
	bServerRegistered = false;

	// Start with the first server
	printf("De-registering server at " + *tCurrentMasterServer + "\n");
	tCurrentMasterServer = tMasterServers.begin();
	tHttp.RequestData(*tCurrentMasterServer + sCurrentUrl, tLXOptions->sHttpProxy);

	DeRegisterServerUdp();

	return true;
}


///////////////////
// Process the de-registering of the server
bool GameServer::ProcessDeRegister()
{
	if (tHttp.ProcessRequest() != HTTP_PROC_PROCESSING)  {

		// Process the next server (if any)
		tCurrentMasterServer++;
		if (tCurrentMasterServer != tMasterServers.end())  {
			printf("De-registering server at " + *tCurrentMasterServer + "\n");
			tHttp.RequestData(*tCurrentMasterServer + sCurrentUrl, tLXOptions->sHttpProxy);
			return false;
		} else {
			tCurrentMasterServer = tMasterServers.begin();
			return true;  // No more servers, we finished
		}
	}

	return false;
}


///////////////////
// Check if any clients haved timed out or are out of zombie state
void GameServer::CheckTimeouts()
{
	int c;

	// Check
	if (!cClients)
		return;

	// Check for NAT traversal sockets that are too old
	for (std::vector<NatConnection>::iterator it = tNatClients.begin(); it != tNatClients.end(); ++it)  {
		if ((tLX->currentTime - it->fLastUsed) >= 10.0f)  {
			NetworkAddr a;
			GetRemoteNetAddr(it->tTraverseSocket, a);
			std::string addr;
			NetAddrToString(a, addr);
			notes << "A NAT traverse connection timed out: " << addr << endl;
			it->Close();
			tNatClients.erase(it);
			it = tNatClients.begin();

			if (tNatClients.empty())
				break;
		}
	}

	// Cycle through clients
	CServerConnection *cl = cClients;
	for(c = 0; c < MAX_CLIENTS; c++, cl++) {
		// Client not connected or no worms
		if(cl->getStatus() == NET_DISCONNECTED)
			continue;

		// Don't disconnect the local client
		if (cl->isLocalClient())
			continue;

		// Check for a drop
		if( cl->getLastReceived() + LX_SVTIMEOUT < tLX->currentTime && ( cl->getStatus() != NET_ZOMBIE ) ) {
			DropClient(cl, CLL_TIMEOUT);
		}

		// Is the client out of zombie state?
		if(cl->getStatus() == NET_ZOMBIE && tLX->currentTime > cl->getZombieTime() ) {
			cl->setStatus(NET_DISCONNECTED);
		}
	}
	CheckWeaponSelectionTime();	// This is kinda timeout too
}

void GameServer::CheckWeaponSelectionTime()
{
	if( iState != SVS_GAME || tLX->iGameType != GME_HOST ) return;
	if( serverChoosesWeapons() ) return;
	if( tLXOptions->tGameInfo.features[FT_ImmediateStart] ) return;
	
	float timeLeft = float(tLXOptions->tGameInfo.iWeaponSelectionMaxTime) - ( tLX->currentTime - fWeaponSelectionTime ).seconds();
	
	int warnIndex = 100;
#define CHECKTIME(time) { \
	warnIndex--; \
	if( timeLeft < float(time) + 0.2f && iWeaponSelectionTime_Warning <= warnIndex ) { \
		iWeaponSelectionTime_Warning = warnIndex + 1; \
		int t = Round(timeLeft); \
		SendGlobalText("You have " + itoa(t) + " seconds to select your weapons" + \
			(time <= 5 ? ", hurry or you'll be kicked." : "."), TXT_NOTICE); \
	} }

	// Issue some sort of warning to clients
	CHECKTIME(5);
	CHECKTIME(10);
	CHECKTIME(30);
	CHECKTIME(60);
#undef CHECKTIME
	
	// Kick retards who still mess with their weapons, we'll start on next frame
	CServerConnection *cl = cClients;
	for(int c = 0; c < MAX_CLIENTS; c++, cl++)
	{
		if( cl->getStatus() == NET_DISCONNECTED || cl->getStatus() == NET_ZOMBIE )
			continue;
		if( cl->getGameReady() )
			continue;

		AbsTime weapTime = MAX(fWeaponSelectionTime, cl->getConnectTime());
		if( tLX->currentTime < weapTime + TimeDiff(float(tLXOptions->tGameInfo.iWeaponSelectionMaxTime)) )
			continue;
		
		if( cl->isLocalClient() ) {
			if(cl->getNumWorms() == 0) {
				warnings << "CheckWeaponSelectionTime: local client is not ready but doesn't have any worms" << endl;
				cl->getNetEngine()->SendClientReady(NULL);
			}
			for(int i = 0; i < cClient->getNumWorms(); i++) {
				if(!cClient->getWorm(i)->getWeaponsReady()) {
					warnings << "CheckWeaponSelectionTime: own worm " <<  cl->getWorm(i)->getID() << ":" << cl->getWorm(i)->getName() << " is selecting weapons too long, forcing random weapons" << endl;
					cClient->getWorm(i)->setWeaponsReady(true);
				}
			}
			if(cClient->getNumWorms() == 0) {
				warnings << "CheckWeaponSelectionTime: local client (cClient) is not ready but doesn't have any worms" << endl;
				cl->getNetEngine()->SendClientReady(NULL);
			}
			// after we set all worms to ready, the client should sent the ImReady in next frame
			continue;
		}
		DropClient( cl, CLL_KICK, "selected weapons too long" );
	}
}

void GameServer::CheckForFillWithBots() {
	if((int)tLXOptions->tGameInfo.features[FT_FillWithBotsTo] <= 0) return; // feature not activated
	
	// check if already too much players
	if(getNumPlayers() > (int)tLXOptions->tGameInfo.features[FT_FillWithBotsTo] && getNumBots() > 0) {
		int kickAmount = MIN(getNumPlayers() - (int)tLXOptions->tGameInfo.features[FT_FillWithBotsTo], getNumBots());
		notes << "CheckForFillWithBots: removing " << kickAmount << " bots" << endl;
		if(kickAmount > 0)
			kickWorm(getLastBot(), "too much players, bot not needed anymore");
		// HINT: we will do the next check in kickWorm, thus stop here with further kicks
		return;
	}
	
	if(iState != SVS_LOBBY && !tLXOptions->tGameInfo.bAllowConnectDuringGame) {
		notes << "CheckForFillWithBots: not in lobby and connectduringgame not allowed" << endl;
		return;
	}
	
	if(iState == SVS_PLAYING && !allWormsHaveFullLives()) {
		notes << "CheckForFillWithBots: in game, cannot add new worms now" << endl;
		return;
	}
	
	if((int)tLXOptions->tGameInfo.features[FT_FillWithBotsTo] > getNumPlayers()) {
		int fillUpTo = MIN(tLXOptions->tGameInfo.iMaxPlayers, (int)tLXOptions->tGameInfo.features[FT_FillWithBotsTo]);
		int fillNr = fillUpTo - getNumPlayers();
		SendGlobalText("Too less players: Adding " + itoa(fillNr) + " bot" + (fillNr > 1 ? "s" : "") + " to the server.", TXT_NETWORK);
		notes << "CheckForFillWithBots: adding " << fillNr << " bots" << endl;
		cClient->AddRandomBot(fillNr);
	}
}

///////////////////
// Drop a client
void GameServer::DropClient(CServerConnection *cl, int reason, const std::string& sReason)
{
	// Never ever drop a local client
	if (cl->isLocalClient())  {
		warnings << "DropClient: An attempt to drop a local client (reason " << reason << ": " << sReason << ") was ignored" << endl;
		return;
	}

	// send out messages
	std::string cl_msg;
	std::string buf;
	int i;
	for(i=0; i<cl->getNumWorms(); i++) {
		switch(reason) {

			// Quit
			case CLL_QUIT:
				replacemax(networkTexts->sHasLeft,"<player>", cl->getWorm(i)->getName(), buf, 1);
				cl_msg = sReason.size() ? sReason : networkTexts->sYouQuit;
				break;

			// Timeout
			case CLL_TIMEOUT:
				replacemax(networkTexts->sHasTimedOut,"<player>", cl->getWorm(i)->getName(), buf, 1);
				cl_msg = sReason.size() ? sReason : networkTexts->sYouTimed;
				break;

			// Kicked
			case CLL_KICK:
				if (sReason.size() == 0)  { // No reason
					replacemax(networkTexts->sHasBeenKicked,"<player>", cl->getWorm(i)->getName(), buf, 1);
					cl_msg = networkTexts->sKickedYou;
				} else {
					replacemax(networkTexts->sHasBeenKickedReason,"<player>", cl->getWorm(i)->getName(), buf, 1);
					replacemax(buf,"<reason>", sReason, buf, 5);
					replacemax(buf,"your", "their", buf, 5); // TODO: dirty...
					replacemax(buf,"you", "they", buf, 5);
					replacemax(networkTexts->sKickedYouReason,"<reason>",sReason, cl_msg, 1);
				}
				break;

			// Banned
			case CLL_BAN:
				if (sReason.size() == 0)  { // No reason
					replacemax(networkTexts->sHasBeenBanned,"<player>", cl->getWorm(i)->getName(), buf, 1);
					cl_msg = networkTexts->sBannedYou;
				} else {
					replacemax(networkTexts->sHasBeenBannedReason,"<player>", cl->getWorm(i)->getName(), buf, 1);
					replacemax(buf,"<reason>", sReason, buf, 5);
					replacemax(buf,"your", "their", buf, 5); // TODO: dirty...
					replacemax(buf,"you", "they", buf, 5);
					replacemax(networkTexts->sBannedYouReason,"<reason>",sReason, cl_msg, 1);
				}
				break;
		}

		// Send only if the text isn't <none>
		if(buf != "<none>")
			SendGlobalText((buf),TXT_NETWORK);
	}
	
	// remove the client and drop worms
	RemoveClient(cl);
	
	// Go into a zombie state for a while so the reliable channel can still get the
	// reliable data to the client
	cl->setStatus(NET_ZOMBIE);
	cl->setZombieTime(tLX->currentTime + 3);

	// Send the client directly a dropped packet
	// TODO: move this out here
	CBytestream bs;
	bs.writeByte(S2C_DROPPED);
	bs.writeString(OldLxCompatibleString(cl_msg));
	cl->getChannel()->AddReliablePacketToSend(bs);
	
	/*
	if( NewNet::Active() )
	{
		gotoLobby();
		SendGlobalText("New net engine doesn't support client leaving yet!",TXT_NETWORK);
	}
	*/
}

// WARNING: We are using SendWormsOut here, that means that we cannot use the specific client anymore
// because it has a different local worm amount and it would screw up the network.
void GameServer::RemoveClientWorms(CServerConnection* cl, const std::set<CWorm*>& worms) {
	std::list<byte> wormsOutList;
	
	int i;
	for(std::set<CWorm*>::const_iterator w = worms.begin(); w != worms.end(); ++w) {
		if(!*w) {
			errors << "RemoveClientWorms: worm unset" << endl;
			continue;
		}
		
		if(!(*w)->isUsed()) {
			errors << "RemoveClientWorms: worm not used" << endl;
			continue;			
		}
		
		cl->RemoveWorm((*w)->getID());
		
		hints << "Worm left: " << (*w)->getName() << " (id " << (*w)->getID() << ")" << endl;
		
		if( DedicatedControl::Get() )
			DedicatedControl::Get()->WormLeft_Signal( (*w) );

		// Notify the game mode that the worm has been dropped
		getGameMode()->Drop((*w));
				
		wormsOutList.push_back((*w)->getID());
		
		// Reset variables
		(*w)->setUsed(false);
		(*w)->setAlive(false);
		(*w)->setSpectating(false);
	}
	
	// Tell everyone that the client's worms have left both through the net & text
	// (Except the client himself because that wouldn't work anyway.)
	for(int c = 0; c < MAX_CLIENTS; c++) {
		CServerConnection* con = &cClients[c];
		if(con->getStatus() != NET_CONNECTED) continue;
		if(cl == con) continue;
		con->getNetEngine()->SendWormsOut(wormsOutList);
	}
	
	// Re-Calculate number of players
	iNumPlayers=0;
	CWorm *w = cWorms;
	for(i=0;i<MAX_WORMS;i++,w++) {
		if(w->isUsed())
			iNumPlayers++;
	}
	
	// Now that a player has left, re-check the game status
	RecheckGame();
}

void GameServer::RemoveAllClientWorms(CServerConnection* cl) {
	cl->setMuted(false);

	int i;
	std::set<CWorm*> worms;
	for(i=0; i<cl->getNumWorms(); i++) {		
		if(!cl->getWorm(i)) {
			warnings << "WARNING: worm " << i << " of " << cl->debugName() << " is not set" << endl;
			continue;
		}
		
		if(!cl->getWorm(i)->isUsed()) {
			warnings << "WARNING: worm " << i << " of " << cl->debugName() << " is not used" << endl;
			cl->setWorm(i, NULL);
			continue;
		}

		worms.insert(cl->getWorm(i));
	}
	RemoveClientWorms(cl, worms);
	
	if( cl->getNumWorms() != 0 ) {
		errors << "RemoveAllClientWorms: very strange, client " << cl->debugName() << " has " << cl->getNumWorms() << " left worms (but should not have any)" << endl;
		cl->setNumWorms(0);
	}
}

void GameServer::RemoveClient(CServerConnection* cl) {
	// Never ever drop a local client
	if (cl->isLocalClient())  {
		warnings << "An attempt to remove a local client was ignored" << endl;
		return;
	}
	
	RemoveAllClientWorms(cl);
	cl->setStatus(NET_DISCONNECTED);
	
	CheckForFillWithBots();
}

int GameServer::getNumBots() const {
	int num = 0;
	CWorm *w = cWorms;
	for(int i = 0; i < MAX_WORMS; i++, w++) {
		if(w->isUsed() && w->getType() == PRF_COMPUTER)
			num++;
	}
	return num;
}

int GameServer::getLastBot() const {
	CWorm *w = cWorms + MAX_WORMS - 1;
	for(int i = MAX_WORMS - 1; i >= 0; i--, w--) {
		if(w->isUsed() && w->getType() == PRF_COMPUTER)
			return i;
	}
	return -1;
}


bool GameServer::serverAllowsConnectDuringGame() {
	return tLXOptions->tGameInfo.bAllowConnectDuringGame;
}

void GameServer::checkVersionCompatibilities(bool dropOut) {
	// Cycle through clients
	CServerConnection *cl = cClients;
	for(int c = 0; c < MAX_CLIENTS; c++, cl++) {
		// Client not connected or no worms
		if(cl->getStatus() == NET_DISCONNECTED || cl->getStatus() == NET_ZOMBIE)
			continue;

		// HINT: It doesn't really make sense to check local clients, though we can just do it to check for strange errors.
		//if (cl->isLocalClient())
		//	continue;
		
		checkVersionCompatibility(cl, dropOut);
	}
}

bool GameServer::checkVersionCompatibility(CServerConnection* cl, bool dropOut, bool makeMsg, std::string* msg) {
	if(serverChoosesWeapons()) {
		if(!forceMinVersion(cl, OLXBetaVersion(7), "server chooses the weapons", dropOut, makeMsg, msg))
			return false;	
	}
	
	if(serverAllowsConnectDuringGame()) {
		if(!forceMinVersion(cl, OLXBetaVersion(8), "connecting during game is allowed", dropOut, makeMsg, msg))
			return false;
	}
	
	if(getGameMode() == GameMode(GM_CTF)) {
		if(!forceMinVersion(cl, OLXBetaVersion(9), "CaptureTheFlag gamemode", dropOut, makeMsg, msg))
			return false;
	}
	
	// These are some serverside settings which make old clients impossible though.
	
	if((float)tLXOptions->tGameInfo.features[FT_WormSpeedFactor] != 1.0f) {
		if(!forceMinVersion(cl, OLXBetaVersion(9), "WormSpeedFactor = " + ftoa(tLXOptions->tGameInfo.features[FT_WormSpeedFactor]), dropOut, makeMsg, msg))
			return false;		
	}

	if((float)tLXOptions->tGameInfo.features[FT_WormDamageFactor] != 1.0f) {
		if(!forceMinVersion(cl, OLXBetaVersion(9), "WormDamageFactor = " + ftoa(tLXOptions->tGameInfo.features[FT_WormDamageFactor]), dropOut, makeMsg, msg))
			return false;		
	}
	
	if((bool)tLXOptions->tGameInfo.features[FT_InstantAirJump]) {
		if(!forceMinVersion(cl, OLXBetaVersion(9), "InstantAirJump activated", dropOut, makeMsg, msg))
			return false;
	}

	if((bool)tLXOptions->tGameInfo.features[FT_RelativeAirJump]) {
		if(!forceMinVersion(cl, OLXBetaVersion(9), "RelativeAirJump activated", dropOut, makeMsg, msg))
			return false;
	}
	
	foreach( Feature*, f, Array(featureArray,featureArrayLen()) ) {
		if(!tLXOptions->tGameInfo.features.olderClientsSupportSetting(f->get())) {
			if(!forceMinVersion(cl, f->get()->minVersion, f->get()->humanReadableName + " is set to " + tLXOptions->tGameInfo.features.hostGet(f->get()).toString(), dropOut, makeMsg, msg))
				return false;
		}
	}
	
	return true;
}

bool GameServer::forceMinVersion(CServerConnection* cl, const Version& ver, const std::string& reason, bool dropOut, bool makeMsg, std::string* msg) {
	if(cl->getClientVersion() < ver) {
		std::string kickReason = "Your OpenLieroX version is too old, please update.\n" + reason;
		if(msg) *msg = kickReason;
		std::string playerName = (cl->getNumWorms() > 0) ? cl->getWorm(0)->getName() : cl->debugName();
		if(dropOut)
			DropClient(cl, CLL_KICK, kickReason);
		if(makeMsg)
			SendGlobalText((playerName + " is too old: " + reason), TXT_NOTICE);
		return false;
	}
	return true;
}

bool GameServer::clientsConnected_less(const Version& ver) {
	CServerConnection *cl = cClients;
	for(int c = 0; c < MAX_CLIENTS; c++, cl++)
		if( cl->getStatus() == NET_CONNECTED && cl->getClientVersion() < ver )
			return true;
	return false;
}



ScriptVar_t GameServer::isNonDamProjGoesThroughNeeded(const ScriptVar_t& preset) {
	if(!(bool)preset) return ScriptVar_t(false);
	if(!tLXOptions->tGameInfo.features[FT_TeamInjure] || !tLXOptions->tGameInfo.features[FT_SelfInjure])
		return preset;
	else
		return ScriptVar_t(false);
}


CWorm* GameServer::AddWorm(const WormJoinInfo& wormInfo) {
	CWorm* w = cWorms;
	for (int j  = 0; j < MAX_WORMS; j++, w++) {
		if (w->isUsed())
			continue;
		
		w->Clear();
		w->setUsed(true);
		w->setID(j);
		wormInfo.applyTo(w);
		w->setupLobby();
		w->setDamage(0);
		if( tLX->iGameType == GME_HOST ) // in local play, we use the team-nr from the WormJoinInfo
			w->setTeam(0);
		else
			w->setTeam(wormInfo.iTeam);
		
		if(w->isPrepared()) {
			warnings << "WARNING: connectduringgame: worm " << w->getID() << " was already prepared! ";
			if(!w->isUsed()) warnings << "AND it is even not used!";
			warnings << endl;
			w->Unprepare();
		}
		
		// If the game has limited lives all new worms are spectators
		if( tLXOptions->tGameInfo.iLives == WRM_UNLIM || iState != SVS_PLAYING || allWormsHaveFullLives() ) // Do not set WRM_OUT if we're in weapon selection screen
			w->setLives(tLXOptions->tGameInfo.iLives);
		else {
			w->setLives(WRM_OUT);
		}
		w->setKills(0);
		w->setGameScript(cGameScript.get());
		w->setWpnRest(&cWeaponRestrictions);
		w->setLoadingTime( (float)tLXOptions->tGameInfo.iLoadingTime / 100.0f );
		w->setWeaponsReady(false);
		
		iNumPlayers++;
		
		if( DedicatedControl::Get() )
			DedicatedControl::Get()->NewWorm_Signal(w);
				
		if(tLX->iGameType == GME_HOST && tLXOptions->iRandomTeamForNewWorm > 0 && getGameMode()->GameTeams() > 1) {
			w->setTeam(-1); // set it invalid to have correct firstEmpty
			
			int firstEmpty = getFirstEmptyTeam();
			//notes << "random(" << tLXOptions->iRandomTeamForNewWorm << "): firstempty=" << firstEmpty << endl;
			if(firstEmpty >= 0 && firstEmpty <= tLXOptions->iRandomTeamForNewWorm)
				w->setTeam(firstEmpty);
			else {
				int team = GetRandomInt(MIN(tLXOptions->iRandomTeamForNewWorm, getGameMode()->GameTeams() - 1));
				//notes << "   randomteam=" << team << endl;
				w->setTeam(team);
			}
			// we will send a WormLobbyUpdate later anyway
		}
		
		return w;
	}
	
	return NULL;
}


///////////////////
// Kick a worm out of the server
void GameServer::kickWorm(int wormID, const std::string& sReason)
{
	if (!cWorms) {
		errors << "kickWorm: worms not initialised" << endl;
		return;
	}
	
	if( wormID < 0 || wormID >= MAX_PLAYERS )  {
		hints << "kickWorm: worm ID " << itoa(wormID) << " is invalid" << endl;
		return;
	}

	if ( !bDedicated && cClient && cClient->getNumWorms() > 0 && cClient->getWorm(0) && cClient->getWorm(0)->getID() == wormID )  {
		hints << "You can't kick yourself!" << endl;
		return;  // Don't kick ourself
	}

	// Get the worm
	CWorm *w = cWorms + wormID;
	if( !w->isUsed() )  {
		hints << "Could not find worm with ID " << itoa(wormID) << endl;
		return;
	}

	if(w->getID() != wormID) {
		warnings << "serverrepresentation of worm " << wormID << " has wrong ID set" << endl;
		w->setID(wormID);
	}
	
	// Get the client
	CServerConnection *cl = w->getClient();
	if( !cl ) {
		errors << "worm " << wormID << " cannot be kicked, the client is unknown" << endl;
		return;
	}
	
	// Local worms are handled another way
	if (cl->isLocalClient())  {
		if (cl->OwnsWorm(w->getID()))  {
			
			// to avoid a broken stream
			SyncServerAndClient();
			
			// check if we didn't already removed the worm from inside that SyncServerAndClient
			if(cClient->OwnsWorm(wormID)) {
				// Send the message
				if (sReason.size() == 0)
					SendGlobalText((replacemax(networkTexts->sHasBeenKicked,
											   "<player>", w->getName(), 1)),	TXT_NETWORK);
				else
					SendGlobalText((replacemax(replacemax(networkTexts->sHasBeenKickedReason,
														  "<player>", w->getName(), 1), "<reason>", sReason, 1)),	TXT_NETWORK);
				
				notes << "Worm was kicked (" << sReason << "): " << w->getName() << " (id " << w->getID() << ")" << endl;
				
				// Delete the worm from client/server
				cClient->RemoveWorm(wormID);			
				std::set<CWorm*> wormList; wormList.insert(w);
				RemoveClientWorms(cl, wormList);

				// Now that a player has left, re-check the game status
				RecheckGame();
			}
			
			// End here
			return;
		}
		
		warnings << "worm " << wormID << " from local client cannot be kicked (" << sReason << "), local client does not have it" << endl;
		return;
	}


	// Drop the whole client
	// TODO: only kick this worm, not the whole client
	DropClient(cl, CLL_KICK, sReason);
}


///////////////////
// Kick a worm out of the server (by name)
void GameServer::kickWorm(const std::string& szWormName, const std::string& sReason)
{
	if (!cWorms)
		return;

	// Find the worm name
	CWorm *w = cWorms;
	for(int i=0; i < MAX_WORMS; i++, w++) {
		if(!w->isUsed())
			continue;

		if(stringcasecmp(w->getName(), szWormName) == 0) {
			kickWorm(i, sReason);
			return;
		}
	}

	// Didn't find the worm
	hints << "Could not find worm '" << szWormName << "'" << endl;
}


///////////////////
// Ban and kick the worm out of the server
void GameServer::banWorm(int wormID, const std::string& sReason)
{
	if (!cWorms)
		return;

	if( wormID < 0 || wormID >= MAX_PLAYERS )  {
		if (Con_IsVisible())
			Con_AddText(CNC_NOTIFY, "Could not find worm with ID '" + itoa(wormID) + "'");
		return;
	}

	if (!wormID && !bDedicated)  {
		Con_AddText(CNC_NOTIFY, "You can't ban yourself!");
		return;  // Don't ban ourself
	}

	// Get the worm
	CWorm *w = cWorms + wormID;
	if (!w)
		return;

	if( !w->isUsed() )  {
		if (Con_IsVisible())
			Con_AddText(CNC_NOTIFY, "Could not find worm with ID '" + itoa(wormID) + "'");
		return;
	}

	// Get the client
	CServerConnection *cl = w->getClient();
	if( !cl )
		return;

	// Local worms are handled another way
	// We just kick the worm, banning makes no sense
	if (cl->isLocalClient())  {
		if (cl->OwnsWorm(w->getID()))  {			
						
			// TODO: share the same code with kickWorm here

			// to avoid a broken stream
			SyncServerAndClient();
			
			// check if we didn't already removed the worm from inside that SyncServerAndClient
			if(cClient->OwnsWorm(wormID)) {
				// Send the message
				if (sReason.size() == 0)
					SendGlobalText((replacemax(networkTexts->sHasBeenBanned,
											   "<player>", w->getName(), 1)),	TXT_NETWORK);
				else
					SendGlobalText((replacemax(replacemax(networkTexts->sHasBeenBannedReason,
														  "<player>", w->getName(), 1), "<reason>", sReason, 1)),	TXT_NETWORK);
				
				notes << "Worm was banned (e.g. kicked, it's local) (" << sReason << "): " << w->getName() << " (id " << w->getID() << ")" << endl;
				
				// Delete the worm from client/server
				cClient->RemoveWorm(wormID);			
				std::set<CWorm*> wormList; wormList.insert(w);
				RemoveClientWorms(cl, wormList);
				
				// Now that a player has left, re-check the game status
				RecheckGame();
			}
			
			// End here
			return;
		}
	}

	std::string szAddress;
	NetAddrToString(cl->getChannel()->getAddress(),szAddress);

	getBanList()->addBanned(szAddress,w->getName());

	// Drop the client
	DropClient(cl, CLL_BAN, sReason);
}


void GameServer::banWorm(const std::string& szWormName, const std::string& sReason)
{
	// Find the worm name
	CWorm *w = cWorms;
	if (!w)
		return;

	for(int i=0; i<MAX_WORMS; i++, w++) {
		if(!w->isUsed())
			continue;

		if(stringcasecmp(w->getName(), szWormName) == 0) {
			banWorm(i, sReason);
			return;
		}
	}

	// Didn't find the worm
	Con_AddText(CNC_NOTIFY, "Could not find worm '" + szWormName + "'");
}

///////////////////
// Mute the worm, so no messages will be delivered from him
// Actually, mutes a client
void GameServer::muteWorm(int wormID)
{
	if( wormID < 0 || wormID >= MAX_PLAYERS )  {
		if (Con_IsVisible())
			Con_AddText(CNC_NOTIFY, "Could not find worm with ID '" + itoa(wormID) + "'");
		return;
	}

	// Get the worm
	CWorm *w = cWorms + wormID;
	if (!cWorms)
		return;

	if( !w->isUsed() )  {
		if (Con_IsVisible())
			Con_AddText(CNC_NOTIFY,"Could not find worm with ID '" + itoa(wormID) + "'");
		return;
	}

	// Get the client
	CServerConnection *cl = w->getClient();
	if( !cl )
		return;

	// Local worms are handled in an other way
	// We just say, the worm is muted, but do not do anything actually
	if (cClient)  {
		if (cClient->OwnsWorm(w->getID()))  {
			// Send the message
			SendGlobalText((replacemax(networkTexts->sHasBeenMuted,"<player>", w->getName(), 1)),
							TXT_NETWORK);

			// End here
			return;
		}
	}

	// Mute
	cl->setMuted(true);

	// Send the text
	if (networkTexts->sHasBeenMuted!="<none>")  {
		SendGlobalText((replacemax(networkTexts->sHasBeenMuted,"<player>",w->getName(),1)),
						TXT_NETWORK);
	}
}


void GameServer::muteWorm(const std::string& szWormName)
{
	// Find the worm name
	CWorm *w = cWorms;
	if (!w)
		return;

	for(int i=0; i<MAX_WORMS; i++, w++) {
		if(!w->isUsed())
			continue;

		if(stringcasecmp(w->getName(), szWormName) == 0) {
			muteWorm(i);
			return;
		}
	}

	// Didn't find the worm
	Con_AddText(CNC_NOTIFY, "Could not find worm '" + szWormName + "'");
}

///////////////////
// Unmute the worm, so the messages will be delivered from him
// Actually, unmutes a client
void GameServer::unmuteWorm(int wormID)
{
	if( wormID < 0 || wormID >= MAX_PLAYERS )  {
		if (Con_IsVisible())
			Con_AddText(CNC_NOTIFY, "Could not find worm with ID '" + itoa(wormID) + "'");
		return;
	}

	// Get the worm
	CWorm *w = cWorms + wormID;
	if (!cWorms)
		return;

	if( !w->isUsed() )  {
		if (Con_IsVisible())
			Con_AddText(CNC_NOTIFY, "Could not find worm with ID '" + itoa(wormID) + "'");
		return;
	}

	// Get the client
	CServerConnection *cl = w->getClient();
	if( !cl )
		return;

	// Unmute
	cl->setMuted(false);

	// Send the message
	if (networkTexts->sHasBeenUnmuted!="<none>")  {
		SendGlobalText((replacemax(networkTexts->sHasBeenUnmuted,"<player>",w->getName(),1)),
						TXT_NETWORK);
	}
}


void GameServer::unmuteWorm(const std::string& szWormName)
{
	// Find the worm name
	CWorm *w = cWorms;
	if (!w)
		return;

	for(int i=0; i<MAX_WORMS; i++, w++) {
		if(!w->isUsed())
			continue;

		if(stringcasecmp(w->getName(), szWormName) == 0) {
			unmuteWorm(i);
			return;
		}
	}

	// Didn't find the worm
	Con_AddText(CNC_NOTIFY, "Could not find worm '" + szWormName + "'");
}

void GameServer::authorizeWorm(int wormID)
{
	if( wormID < 0 || wormID >= MAX_PLAYERS )  {
		if (Con_IsVisible())
			Con_AddText(CNC_NOTIFY, "Could not find worm with ID '" + itoa(wormID) + "'");
		return;
	}

	// Get the worm
	CWorm *w = cWorms + wormID;
	if (!cWorms)
		return;

	if( !w->isUsed() )  {
		if (Con_IsVisible())
			Con_AddText(CNC_NOTIFY, "Could not find worm with ID '" + itoa(wormID) + "'");
		return;
	}

	// Get the client
	CServerConnection *cl = getClient(wormID);
	if( !cl )
		return;

	cl->getRights()->Everything();
	cServer->SendGlobalText((getWorms() + wormID)->getName() + " has been authorised", TXT_NORMAL);
}


void GameServer::cloneWeaponsToAllWorms(CWorm* worm) {
	if (!cWorms)  {
		errors << "cloneWeaponsToAllWorms called when the server is not running" << endl;
		return;
	}

	CWorm *w = cWorms;
	for (int i = 0; i < MAX_WORMS; i++, w++) {
		if(w->isUsed()) {
			w->CloneWeaponsFrom(worm);
			w->setWeaponsReady(true);
		}
	}

	SendWeapons();
}

bool GameServer::allWormsHaveFullLives() const {
	CWorm *w = cWorms;
	for (int i = 0; i < MAX_WORMS; i++, w++) {
		if(w->isUsed()) {
			if(w->getLives() < tLXOptions->tGameInfo.iLives) return false;
		}
	}
	return true;
}


CMap* GameServer::getPreloadedMap() {
	if(cMap) return cMap;
	
	std::string sMapFilename = "levels/" + tLXOptions->tGameInfo.sMapFile;
	
	// Try to get the map from cache.
	CMap* cachedMap = cCache.GetMap(sMapFilename).get();
	if(cachedMap) return cachedMap;
	
	// Ok, the map was not in the cache.
	// Just load the map in that case. (It'll go into the cache,
	// so GS::StartGame() or the next access to it is fast.)
	cMap = new CMap;
	if(cMap == NULL) {
		errors << "GameServer::getPreloadedMap(): out of mem while init map" << endl;
		return NULL;
	}
	if(!cMap->Load(sMapFilename)) {
		warnings << "GameServer::getPreloadedMap(): cannot load map " << tLXOptions->tGameInfo.sMapFile << endl;
		delete cMap;
		cMap = NULL;
		return NULL; // nothing we can do anymore
	}
	
	return cMap;
}


///////////////////
// Notify the host about stuff
void GameServer::notifyLog(const std::string& msg)
{
	// Local hosting?
	// Add it to the clients chatbox
	if(cClient) {
		CChatBox *c = cClient->getChatbox();
		if(c)
			c->AddText(msg, tLX->clNetworkText, TXT_NETWORK, tLX->currentTime);
	}

}

//////////////////
// Get the client owning this worm
CServerConnection *GameServer::getClient(int iWormID)
{
	if (iWormID < 0 || iWormID >= MAX_WORMS || !cWorms)
		return NULL;

	CWorm *w = cWorms;

	for(int p=0;p<MAX_WORMS;p++,w++) {
		if(w->isUsed())
			if (w->getID() == iWormID)
				return w->getClient();
	}

	return NULL;
}


///////////////////
// Get the download rate in bytes/s for all non-local clients
float GameServer::GetDownload()
{
	if(!cClients) return 0;
	float result = 0;
	CServerConnection *cl = cClients;

	// Sum downloads from all clients
	for (int i=0; i < MAX_CLIENTS; i++, cl++)  {
		if (cl->getStatus() != NET_DISCONNECTED && cl->getStatus() != NET_ZOMBIE && !cl->isLocalClient() && cl->getChannel() != NULL)
			result += cl->getChannel()->getIncomingRate();
	}

	return result;
}

///////////////////
// Get the upload rate in bytes/s for all non-local clients
float GameServer::GetUpload(float timeRange)
{
	if(!cClients) return 0;
	float result = 0;
	CServerConnection *cl = cClients;

	// Sum downloads from all clients
	for (int i=0; i < MAX_CLIENTS; i++, cl++)  {
		if (cl->getStatus() != NET_DISCONNECTED && cl->getStatus() != NET_ZOMBIE && !cl->isLocalClient() && cl->getChannel() != NULL)
			result += cl->getChannel()->getOutgoingRate(timeRange);
	}

	return result;
}

///////////////////
// Shutdown the server
void GameServer::Shutdown()
{
	// If we've hosted this session, set the FirstHost option to false
	if (tLX->bHosted)  {
		tLXOptions->bFirstHosting = false;
	}

	// Kick clients if they still connected (sends just one packet which may be lost, but whatever, we're shutting down)
	if(cClients && tLX->iGameType == GME_HOST)
	{
		SendDisconnect();
	}

	for( int i = 0; i < MAX_SERVER_SOCKETS; i++ )
	{
		if(IsSocketStateValid(tSockets[i]))
		{
			CloseSocket(tSockets[i]);
		}
		InvalidateSocketState(tSockets[i]);
	}

	for (std::vector<NatConnection>::iterator it = tNatClients.begin(); it != tNatClients.end(); ++it)
		it->Close();

	if(cClients) {
		delete[] cClients;
		cClients = NULL;
	}

	if(cWorms) {
		delete[] cWorms;
		cWorms = NULL;
	}

	if(cMap) {
		cMap->Shutdown();
		delete cMap;
		cMap = NULL;
	}

	if(m_flagInfo) {
		delete m_flagInfo;
		m_flagInfo = NULL;
	}
	
	cShootList.Shutdown();

	cWeaponRestrictions.Shutdown();


	cBanList.Shutdown();

	// HINT: the gamescript is shut down by the cache
}

float GameServer::getMaxUploadBandwidth() {
	// Modem, ISDN, DSL, local
	// (Bytes per second)
	const float	Rates[4] = {2500, 7500, 20000, 50000};
	
	float fMaxRate = Rates[tLXOptions->iNetworkSpeed];
	if(tLXOptions->iNetworkSpeed >= 2) { // >= DSL
		// only use Network.MaxServerUploadBandwidth option if we set Network.Speed to DSL (or higher)
		fMaxRate = MAX(fMaxRate, (float)tLXOptions->iMaxUploadBandwidth);
	}
	
	return fMaxRate;
}

void GameServer::DumpGameState() {
	notes << "Server '" << this->getName() << "' game state:" << endl;
	switch(iState) {
		case SVS_LOBBY: notes << " * in lobby"; break;
		case SVS_GAME: notes << " * weapon selection"; break;
		case SVS_PLAYING: notes << " * playing"; break;
		default: notes << " * INVALID STATE " << iState; break;
	}
	if(iState != SVS_LOBBY && bGameOver) notes << ", game is over";
	bool teamGame = true;
	if(getGameMode()) {
		teamGame = getGameMode()->GameTeams() > 1;
		notes << ", " << getGameMode()->Name() << endl;
	} else
		notes << ", GAMEMODE UNSET" << endl;
	notes << " * maxkills=" << tLXOptions->tGameInfo.iKillLimit;
	notes << ", lives=" << tLXOptions->tGameInfo.iLives;
	notes << ", timelimit=" << (tLXOptions->tGameInfo.fTimeLimit * 60.0f);
	notes << " (curtime=" << fServertime.seconds() << ")" << endl;
	if(cWorms) {
		for(int i = 0; i < MAX_WORMS; ++i) {
			CWorm* w = &cWorms[i];
			if(w->isUsed()) {
				notes << " + " << i;
				if(i != w->getID()) notes << "(WRONG ID:" << w->getID() << ")";
				notes << ":'" << w->getName() << "'";
				if(w->getType() == PRF_COMPUTER) notes << "(bot)";
				if(teamGame) notes << ", team " << w->getTeam();
				if(w->getAlive())
					notes << ", alive";
				else
					notes << ", dead";
				if(!w->getWeaponsReady()) notes << ", still weapons selecting";
				notes << ", lives=" << w->getLives();
				notes << ", kills=" << w->getKills();
				if(w->getClient())
					notes << " on " << w->getClient()->debugName(false);
				else
					notes << " WITH UNSET CLIENT";
				notes << endl;
			}
		}
	} else
		notes << " - cWorms not set" << endl;
}


void SyncServerAndClient() {
	if(tLX->iGameType == GME_JOIN) {
		warnings << "SyncServerAndClient: cannot sync in join-mode" << endl;
		return;
	}
	
	//notes << "Syncing server and client ..." << endl;

	{
		// Read packets
		CServerConnection *cl = cServer->getClients();
		for(int c=0;c<MAX_CLIENTS;c++,cl++) {
						
			// Player not connected
			if(cl->getStatus() == NET_DISCONNECTED)
				continue;
			
			if(!cl->isLocalClient())
				continue;

			// Parse the packet - process continuously in case we've received multiple logical packets on new CChannel
			CBytestream bs;
			while( cl->getChannel()->Process(&bs) )
			{
				// Only process the actual packet for playing clients
				if( cl->getStatus() != NET_ZOMBIE )
					cl->getNetEngine()->ParsePacket(&bs);
				bs.Clear();
			}
		}
	}
	
	cClient->SendPackets();
	cServer->SendPackets();
	
	//SDL_Delay(200);
	cClient->ReadPackets();
	cServer->ReadPackets();
	
	/*
	bool needUpdate = true;
	while(needUpdate) {
		needUpdate = false;
		SDL_Delay(200);
		if(cClient->ReadPackets()) {
			needUpdate = true;
			cClient->SendPackets();
		}
		SDL_Delay(200);
		if(cServer->ReadPackets()) {
			needUpdate = true;
			cServer->SendPackets();
		}
	}
	*/
	//notes << "Syncing done" << endl; 
}

