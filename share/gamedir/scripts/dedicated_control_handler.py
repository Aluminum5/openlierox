#!/usr/bin/python -u
# Dedicated Control handler script for OpenLieroX
# (http://openlierox.sourceforge.net)

# TODO: move admin/user commands to different file

# Needed for sleeping/pausing execution
import time
# Needed for directory access
import os
import sys
import threading
import traceback

import dedicated_config as cfg # Per-host config like admin password

# Print Python script errors to external file -
# on Windows it cannot print errors to console
if sys.platform == "win32":
	sys.stderr = open(cfg.ERROR_FILE, "w", 0)

import dedicated_control_io as io
setvar = io.setvar
formatExceptionInfo = io.formatExceptionInfo

import dedicated_control_ranking as ranking

import dedicated_control_usercommands as cmds


## Global vars
# Preset stuffies
availablePresets = []
nextPresets = []

availableLevels = list()
availableMods = list()

worms = {} # List of all worms on the server
# Bots don't need to be itterated with the other ones.
bots = {}  # Dictionary of all possible bots

# Function that controls ded server behavior
controlHandler = None

scriptPaused = False

# Uncomment following 3 lines to get lots of debug spam into dedicated_control_errors.log file
#def HeavyDebugTrace(frame,event,arg):
#	sys.stderr.write( 'Trace: ' + str(event) + ': ' + str(arg) + ' at ' + str(frame.f_code) + "\n" )
#sys.settrace(HeavyDebugTrace)

## Local vars
presetDir = os.path.join(os.path.dirname(sys.argv[0]),"presets")
# Should include several searchpaths, but now they contain just OLX root dir
levelDir = os.path.join(os.path.split(os.path.dirname(sys.argv[0]))[0],"levels")
modDir = os.path.split(os.path.dirname(sys.argv[0]))[0]
if modDir == "":
	modDir = "."

# Game states
GAME_READY = -1
GAME_QUIT = 0
GAME_LOBBY = 1
GAME_WEAPONS = 2
GAME_PLAYING = 3

gameState = GAME_READY

sentStartGame = False


# TODO: Expand this class, use it.
class Worm:
	def __init__(self):
		self.Name = ""
		self.Continent = ""
		self.CountryShortcut = ""
		self.Country = ""
		self.Ip = ""
		self.iID = -1
		self.isAdmin = False
		self.Ping = [] # Contains 10 ping values, real ping = average of them
		self.Lives = -1 # -1 means Out
		self.Team = 0
		self.Alive = False
		self.Voted = 0
		self.FailedVoteTime = 0

	def clear(self):
		self.__init__()

class Preset():
	def __init__(self):
		self.Name = None
		self.Level = None
		self.Mod = None
		self.LT = None


def init():
	initPresets()
	initLevelList()
	initModList()

	io.startLobby(0)

	for f in cfg.GLOBAL_SETTINGS.keys():
		io.setvar( f, cfg.GLOBAL_SETTINGS[f] )

	selectNextPreset()


## High-level processing ##

# Parses all signals that are not 2 way (like getip info -> olx returns info)
# Returns False if there's nothing to read
def signalHandler(sig):
	global gameState, oldGameState, scriptPaused, sentStartGame

	oldGameState = gameState

	if len(sig) == 0:
		return False #Didn't get anything
		
	header = sig[0]
	
	try:
		if header == "newworm":
			parseNewWorm(sig)
		elif header == "wormleft":
			parseWormLeft(sig)
		elif header == "privatemessage":
			parsePrivateMessage(sig)
		elif header == "chatmessage":
			parseChatMessage(sig)
		elif header == "wormdied":
			parseWormDied(sig)
		elif header == "wormspawned":
			parseWormSpawned(sig)
		elif header == "wormauthorized":
			parseWormAuthorized(sig)
	
		## Check GameState ##
		elif header == "quit":
			gameState = GAME_QUIT
			exit()
		#elif header == "errorstartlobby": # ignore this for now, we get it if we are already in lobby
		#	gameState = GAME_QUIT
		#	io.messageLog("errorstartlobby",io.LOG_ERROR)
	
		elif header == "backtolobby" or header == "lobbystarted":
			if cfg.RANKING:
				ranking.refreshRank()
			gameState = GAME_LOBBY
			sentStartGame = False
			controlHandler()
		elif header == "errorstartgame":
			gameState = GAME_LOBBY
			io.messageLog("errorstartgame",io.LOG_ERROR)
			sentStartGame = False
	
		elif header == "weaponselections":
			gameState = GAME_WEAPONS
			controlHandler()
		elif header == "gamestarted":
			gameState = GAME_PLAYING
			sentStartGame = False
			controlHandler()
		#TODO: gamestarted && gameloopstart are pretty much duplicates
		# Or are they? Check.
		# Same thing for gameloopend and backtolobby
		elif header == "gameloopstart": #Sent when game starts
			pass
		elif header == "gameloopend": #Sent at game end
			pass
		elif header == "gameloopend": #Sent when OLX starts
			pass
		elif header == "timer": # Sent once per second
			controlHandler()
		else:
			io.messageLog(("I don't understand %s." % (sig)),io.LOG_ERROR)
	
	except Exception:
		traceback.print_exc(None, sys.stderr)

	return True

def parseNewWorm(sig):
	global worms

	wormID = int(sig[1])
	name = sig[2].replace("\t", " ").strip() # Do not allow tab in names, it will screw up our ranking tab-separated text-file database
	exists = False
	try:
		worm = worms[wormID]
		exists = True
	except KeyError: #Worm doesn't exist.
		worm = Worm()
	worm.Name = name
	worm.iID = wormID

	if not exists:
		worms[wormID] = worm

	# Balance teams
	teams = [0,0,0,0]
	for w in worms.keys():
		teams[worms[w].Team] += 1
	minTeam = 0
	minTeamCount = teams[0]
	for f in range(cfg.MAX_TEAMS):
		if minTeamCount > teams[f]:
			minTeamCount = teams[f]
			minTeam = f

	io.setWormTeam(wormID, minTeam)
	#io.messageLog("New worm " + str(wormID) + " set team " + str(minTeam) + " teamcount " + str(teams), io.LOG_INFO)
	if cfg.RANKING_AUTHENTICATION:
		if not name in ranking.auth:
			ranking.auth[name] = getWormSkin(wormID)
			try:
				f = open("pwn0meter_auth.txt","a")
				f.write( name + "\t" + str(ranking.auth[name][0]) + " " + ranking.auth[name][1] + "\n" )
				f.close()
			except IOError:
				msg("ERROR: Unable to open pwn0meter_auth.txt")
		else:
			if ranking.auth[name] != getWormSkin(wormID):
				io.kickWorm(wormID, "Player with name %s already registered" % name)

	wormIP = io.getWormIP(wormID).split(":")[0]
	# io.messageLog("Curtime " + str(time.time()) + " IP " + str(wormIP) + " Kicked worms: " + str(cmds.kickedUsers), io.LOG_INFO)
	if wormIP in cmds.kickedUsers and cmds.kickedUsers[ wormIP ] > time.time():
			io.kickWorm( wormID, "You can join in " + str(int(cmds.kickedUsers[ wormIP ] - time.time())/60 + 1) + " minutes" )
			return
	cmds.recheckVote()


def parseWormLeft(sig):
	global worms, scriptPaused

	wormID = int(sig[1])
	name = sig[2:]

	try:
		if worms[wormID].isAdmin:
			io.messageLog(("Worm %i (%s) removed from admins" % (wormID,name)),io.LOG_ADMIN)
	except KeyError:
		io.messageLog("AdminRemove: Our local copy of wormses doesn't match the real list.",io.LOG_ERROR)

	# Call last, that way we still have the data active.
	worms.pop(wormID)

	cmds.recheckVote()

	# If all admins left unpause ded server (or it will be unusable)
	isAdmins = False
	for w in worms.keys():
		if worms[w].isAdmin:
			isAdmins = True
	if not isAdmins:
		scriptPaused = False


def parsePrivateMessage(sig):
	pass
	
def parseWormAuthorized(sig):	
	global worms

	wormID = int(sig[1])
	try:
		if not worms[wormID].isAdmin:
			worms[wormID].isAdmin = True
			io.messageLog(("Worm %i (%s) added to admins" % (wormID,worms[wormID].Name)),io.LOG_ADMIN)
			# TODO: Send the last part in a PM to the admin. (Needs new backend for private messaging. Add teamchat too!)
			io.authorizeWorm(wormID)
			io.privateMsg(wormID, "%s authenticated for admin! Type %shelp for command info" % (worms[wormID].Name,cfg.ADMIN_PREFIX))
	except KeyError:
		io.messageLog("AdminAdd: Our local copy of wormses doesn't match the real list.",io.LOG_ERROR)

def parseChatMessage(sig):
	global worms

	wormID = int(sig[1])
	message = sig[2]
	io.msg("Chat msg from worm %i: %s" % (wormID, message))
	if worms[wormID].isAdmin:
		if not cmds.parseAdminCommand(wormID,message):
			cmds.parseUserCommand(wormID,message)
	else:
		cmds.parseUserCommand(wormID,message)

def parseWormDied(sig):
	global worms

	deaderID = int(sig[1])
	killerID = int(sig[2])
	worms[deaderID].Lives -= 1
	worms[deaderID].Alive = False

	if not cfg.RANKING:
		return

	try:
		f = open("pwn0meter.txt","a")
		f.write( time.strftime("%Y-%m-%d %H:%M:%S") + "\t" + worms[deaderID].Name + "\t" + worms[killerID].Name + "\n" )
		f.close()
	except IOError:
		io.msg("ERROR: Unable to open pwn0meter.txt")

	if(deaderID == killerID):
		try:
			ranking.rank[worms[killerID].Name][2] += 1
		except KeyError:
			ranking.rank[worms[killerID].Name] = [0,0,1,len(ranking.rank)+1]
	else:
		try:
			ranking.rank[worms[killerID].Name][0] += 1
		except KeyError:
			ranking.rank[worms[killerID].Name] = [1,0,0,len(ranking.rank)+1]
	try:
		ranking.rank[worms[deaderID].Name][1] += 1
	except KeyError:
		ranking.rank[worms[deaderID].Name] = [0,1,0,len(ranking.rank)+1]

def parseWormSpawned(sig):
	global worms

	wormID = int(sig[1])
	worms[wormID].Alive = True

## Preset loading functions ##
def initPresets():
	global availablePresets

	# Reset - incase we get called a second time
	availablePresets = []

	for f in os.listdir(presetDir):
		if f.lower() != "defaults" and f.lower() != ".svn":
			availablePresets.append(f)

	for p in cfg.PRESETS:
		if availablePresets.count(p) == 0:
			availablePresets.append(p)

	if (len(availablePresets) == 0):
		io.messageLog("There are no presets available - nothing to do. Exiting.",io.LOG_CRITICAL)
		exit()

# Ensures the nextPresets is not empty
def fillNextPresets():
	global availablePresets, nextPresets
	if len( nextPresets ) == 0:
		for n in cfg.PRESETS:
			p = Preset()
			p.Name = n
			nextPresets.append(p)
		if len( nextPresets ) == 0:
			for n in availablePresets:
				p = Preset()
				p.Name = n
				nextPresets.append(p)

# initPresets must be called before this - or it will crash
def selectNextPreset():
	global availablePresets, nextPresets, controlHandler, gameState

	fillNextPresets()

	preset = nextPresets[0]
	nextPresets.pop(0)

	fillNextPresets()

	io.msg("Preset " + preset.Name)
	io.chatMsg("Preset " + preset.Name)

	sFile = os.path.join(presetDir,preset.Name)
	sDefaults = os.path.join(presetDir,"Defaults")
	try:
		execfile(sDefaults)
		fPreset = file(sFile,"r")
		line = fPreset.readline()
		if line.find("python") != -1:
			fPreset.close()
			execfile(sFile)
		else:
			print line.strip().replace('"','')
			for line in fPreset.readlines():
				print line.strip().replace('"','')
			fPreset.close()
	except IOError:
		# File does not exist, perhaps it was removed.
		preset.Mod = preset.Name
		#io.messageLog(("Unable to load %s, forcing rehash of all presets" % sFile),io.LOG_WARN)
		initPresets()
	except:
		io.messageLog("Error in preset: " + str(formatExceptionInfo()),io.LOG_ERROR)


	if preset.Mod:
		io.setvar("GameOptions.GameInfo.ModName", preset.Mod)
	if preset.Level:
		io.setvar("GameOptions.GameInfo.LevelName", preset.Level)
	if preset.LT:
		io.setvar("GameOptions.GameInfo.LoadingTime", preset.LT)


def selectPreset( Name = None, Level = None, Mod = None, LT = None, Repeat = 0 ):
	global availablePresets, nextPresets, controlHandler, gameState

	fillNextPresets()
	preset = nextPresets[0]

	if Name:
		preset.Name = Name
	if Level:
		preset.Level = Level
	if Mod:
		preset.Mod = Mod
	if LT:
		preset.LT = LT

	if Repeat > 0:
		nextPresets = []
		for f in range(Repeat):
			nextPresets.append(preset)
	else:
		nextPresets[0] = preset

	if gameState != GAME_LOBBY:
		msg = "Preset " + str(preset.Name)
		if preset.Level:
			msg += " map " + preset.Level
		if preset.Mod:
			msg += " mod " + preset.Mod
		if preset.LT:
			msg += " LT " + str(preset.LT)
		io.chatMsg( msg + " will be selected for next game")
	else:
		selectNextPreset()

def initLevelList():
	global levelDir, availableLevels
	for f in os.listdir(levelDir):
		if os.path.isdir(f):
			#io.messageLog("initLevelList: Ignoring \"%s\" - It's a directory" % f, io.LOG_INFO)
			continue
		#io.messageLog("Adding level " + f, io.LOG_INFO)
		availableLevels.append(f)

def initModList():
	global modDir, availableMods
	for f in os.listdir(modDir):
		fullDir = os.path.join(modDir,f)
		if not os.path.isdir(fullDir):
			continue
		for ff in os.listdir(fullDir):
			if ff.lower() == "script.lgs":
				availableMods.append(f)

## Control functions

def average(a):
	r = 0
	for i in a:
		r += i
	return r / len(a)

def checkMaxPing():
	global worms

	for f in worms.keys():
		if worms[f].iID == -1 or not worms[f].Alive:
			continue
		ping = int(io.getWormPing(worms[f].iID))
		if ping > 0:
			worms[f].Ping.insert( 0, ping )
			if len(worms[f].Ping) > 25:
				worms[f].Ping.pop()
				if average(worms[f].Ping) > cfg.MAX_PING:
					io.kickWorm( worms[f].iID, "Your ping is " + str(average(worms[f].Ping)) + " allowed is " + str(cfg.MAX_PING) )


lobbyChangePresetTimeout = time.time() + cfg.PRESET_TIMEOUT
lobbyWaitBeforeGame = time.time() + cfg.WAIT_BEFORE_GAME
lobbyWaitAfterGame = time.time()
lobbyWaitGeneral = time.time() + cfg.WAIT_BEFORE_SPAMMING_TOO_FEW_PLAYERS_MESSAGE
lobbyEnoughPlayers = False
oldGameState = GAME_LOBBY

def controlHandlerDefault():

	global worms, gameState, lobbyChangePresetTimeout, lobbyWaitBeforeGame, lobbyWaitAfterGame
	global lobbyWaitGeneral, lobbyEnoughPlayers, oldGameState, scriptPaused, sentStartGame
	
	if scriptPaused:
		return

	curTime = time.time()

	if gameState == GAME_LOBBY:

		# Do not check ping in lobby - it's wrong

		if oldGameState != GAME_LOBBY or lobbyChangePresetTimeout <= curTime:
			lobbyChangePresetTimeout = curTime + cfg.PRESET_TIMEOUT
			selectNextPreset()
			lobbyEnoughPlayers = False # reset the state
			lobbyWaitGeneral = curTime + cfg.WAIT_BEFORE_SPAMMING_TOO_FEW_PLAYERS_MESSAGE
			lobbyWaitAfterGame = curTime
			if oldGameState == GAME_PLAYING:
				lobbyWaitAfterGame = curTime + cfg.WAIT_AFTER_GAME

		if lobbyWaitAfterGame <= curTime:

			if not lobbyEnoughPlayers and lobbyWaitGeneral <= curTime:
				lobbyWaitGeneral = curTime + cfg.WAIT_BEFORE_SPAMMING_TOO_FEW_PLAYERS_MESSAGE
				io.chatMsg(cfg.TOO_FEW_PLAYERS_MESSAGE)

			if not lobbyEnoughPlayers and len(worms) >= cfg.MIN_PLAYERS: # Enough players already - start game
				lobbyEnoughPlayers = True
				io.chatMsg(cfg.WAIT_BEFORE_GAME_MESSAGE)
				lobbyWaitBeforeGame = curTime + cfg.WAIT_BEFORE_GAME

			if lobbyEnoughPlayers and len(worms) < cfg.MIN_PLAYERS: # Some players left when game not yet started
				lobbyEnoughPlayers = False
				io.chatMsg(cfg.TOO_FEW_PLAYERS_MESSAGE)

			if lobbyEnoughPlayers and not sentStartGame:

				if lobbyWaitBeforeGame <= curTime: # Start the game

					if getGameType() == 0:
						if len(worms) >= cfg.MIN_PLAYERS_TEAMS: # Split in teams
							setvar("GameOptions.GameInfo.GameType", "1")
							if not cfg.ALLOW_TEAM_CHANGE:
								counter = 0
								for w in worms.values():
									if w.iID != -1:
										io.setWormTeam( w.iID, counter % cfg.MAX_TEAMS )
										counter += 1
						else:
							io.setvar("GameOptions.GameInfo.GameType", "0")

					io.startGame()
					sentStartGame = True
					if cfg.ALLOW_TEAM_CHANGE and len(worms) >= cfg.MIN_PLAYERS_TEAMS:
						io.chatMsg(cfg.TEAM_CHANGE_MESSAGE)

	if gameState == GAME_WEAPONS:

		#checkMaxPing()

		if len(worms) < cfg.MIN_PLAYERS: # Some players left when game not yet started
			io.gotoLobby()
			sentStartGame = False

	if gameState == GAME_PLAYING:

		checkMaxPing()

controlHandler = controlHandlerDefault

