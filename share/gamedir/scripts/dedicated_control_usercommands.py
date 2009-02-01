#!/usr/bin/python -u
# Dedicated Control handler script for OpenLieroX
# (http://openlierox.sourceforge.net)

import time
import os
import sys
import threading
import traceback
import math

import dedicated_config as cfg

import dedicated_control_io as io
setvar = io.setvar
formatExceptionInfo = io.formatExceptionInfo

import dedicated_control_ranking as ranking

import dedicated_control_handler as hnd

adminCommandHelp_Preset = None
parseAdminCommand_Preset = None
userCommandHelp_Preset = None
parseUserCommand_Preset = None


def adminCommandHelp(wormid):
	io.privateMsg(wormid, "Admin help:")
	io.privateMsg(wormid, "%skick wormID [reason]" % cfg.ADMIN_PREFIX)
	io.privateMsg(wormid, "%sban wormID [reason]" % cfg.ADMIN_PREFIX)
	io.privateMsg(wormid, "%smute wormID" % cfg.ADMIN_PREFIX)
	io.privateMsg(wormid, "%spreset presetName [repeatCount]" % cfg.ADMIN_PREFIX)
	io.privateMsg(wormid, "%smod modName (or part of name)" % cfg.ADMIN_PREFIX)
	io.privateMsg(wormid, "%smap mapName" % cfg.ADMIN_PREFIX)
	io.privateMsg(wormid, "%slt loadingTime" % cfg.ADMIN_PREFIX)
	io.privateMsg(wormid, "%steam wormID teamID (0123 or brgy)" % cfg.ADMIN_PREFIX)
	io.privateMsg(wormid, "%sstart - start game now" % cfg.ADMIN_PREFIX)
	io.privateMsg(wormid, "%sstop - go to lobby" % cfg.ADMIN_PREFIX)
	io.privateMsg(wormid, "%spause - pause ded script" % cfg.ADMIN_PREFIX)
	io.privateMsg(wormid, "%sunpause - resume ded script" % cfg.ADMIN_PREFIX)
	io.privateMsg(wormid, "%ssetvar varname value" % cfg.ADMIN_PREFIX)
	io.privateMsg(wormid, "%sauthorize wormID" % cfg.ADMIN_PREFIX)
	if adminCommandHelp_Preset:
		adminCommandHelp_Preset(wormid)

# Admin interface
def parseAdminCommand(wormid,message):
	try: # Do not check on msg size or anything, exception handling is further down
		if (not message.startswith(cfg.ADMIN_PREFIX)):
			return False # normal chat

		cmd = message.split(" ")[0]
		cmd = cmd.replace(cfg.ADMIN_PREFIX,"",1).lower() #Remove the prefix

		io.messageLog("%i:%s issued %s" % (wormid,hnd.worms[wormid].Name,cmd.replace(cfg.ADMIN_PREFIX,"",1)),io.LOG_ADMIN)

		# Unnecesary to split multiple times, this saves CPU.
		params = message.split(" ")[1:]

		if cmd == "help":
			adminCommandHelp(wormid)
		elif cmd == "kick":
			if len(params) > 1: # Given some reason
				io.kickWorm( int( params[0] ), " ".join(params[1:]) )
			else:
				io.kickWorm( int( params[0] ) )
		elif cmd == "ban":
			if len(params) > 1: # Given some reason
				io.banWorm( int( params[0] ), " ".join(params[1:]) )
			else:
				io.banWorm( int( params[0] ) )
		elif cmd == "mute":
			io.muteWorm( int( params[0] ) )
		elif cmd == "preset":
			preset = -1
			presetCount = 1
			if len(params) > 1:
				presetCount = int(params[1])
			for p in range(len(hnd.availablePresets)):
				if hnd.availablePresets[p].lower().find(params[0].lower()) != -1:
					preset = p
					break
			if preset == -1:
				io.privateMsg(wormid,"Invalid preset name")
			else:
				hnd.selectPreset( Name = hnd.availablePresets[preset], Repeat = presetCount )
		elif cmd == "mod":
			mod = ""
			for m in hnd.availableMods:
				if m.lower().find(" ".join(params[0:]).lower()) != -1:
					mod = m
					break
			if mod == "":
				io.privateMsg(wormid,"Invalid mod name")
			else:
				hnd.selectPreset( Mod = mod )
		elif cmd == "map":
			level = ""
			for l in hnd.availableLevels:
				if l.lower().find(" ".join(params[0:]).lower()) != -1:
					level = l
					break
			if level == "":
				io.privateMsg(wormid,"Invalid map name")
			else:
				hnd.selectPreset( Level = level )
		elif cmd == "lt":
			hnd.selectPreset( LT = int(params[0]) )
		elif cmd == "start":
			io.startGame()
		elif cmd == "stop":
			io.gotoLobby()
		elif cmd == "pause":
			io.privateMsg(wormid,"Ded script paused")
			hnd.scriptPaused = True
		elif cmd == "unpause":
			io.privateMsg(wormid,"Ded script continues")
			hnd.scriptPaused = False
		elif cmd == "setvar":
			io.setvar(params[0], " ".join(params[1:])) # In case value contains spaces
		elif cmd == "authorize":
			try:
				wormID = int(params[0])
				if not hnd.worms[wormID].isAdmin:
					hnd.worms[wormID].isAdmin = True
					io.authorizeWorm(wormID)
					io.messageLog( "Worm %i (%s) added to admins by %i (%s)" % (wormID,hnd.worms[wormID].Name,wormid,hnd.worms[wormid].Name),io.LOG_INFO)
					io.privateMsg(wormID, "%s made you admin! Type %shelp for commands" % (hnd.worms[wormid].Name,cfg.ADMIN_PREFIX))
					io.privateMsg(wormid, "%s added to admins." % hnd.worms[wormID].Name)
			except KeyError:
				io.messageLog("parseAdminCommand: Our local copy of wormses doesn't match the real list.",io.LOG_ERROR)
		elif parseAdminCommand_Preset and parseAdminCommand_Preset(wormid, cmd, params):
			pass
		else:
			raise Exception, "Invalid admin command"

	except: # All python classes derive from main "Exception", but confused me, this has the same effect.
		io.privateMsg(wormid, "Invalid admin command")
		io.messageLog(formatExceptionInfo(),io.LOG_ERROR) #Helps to fix errors
		return False
	return True

# User interface

def userCommandHelp(wormid):
	if cfg.ALLOW_TEAM_CHANGE:
		msg = "%steam [b/r" % (cfg.USER_PREFIX)
		if cfg.MAX_TEAMS >= 3:
			msg += "/g"
		if cfg.MAX_TEAMS >= 4:
			msg += "/y"
		msg += "] - set your team"
		io.privateMsg(wormid, msg + " - set your team")
	if cfg.RANKING:
		io.privateMsg(wormid, "%stoprank - display the best players" % cfg.USER_PREFIX )
		io.privateMsg(wormid, "%srank [name] - display your or other player rank" % cfg.USER_PREFIX )
		io.privateMsg(wormid, "%sranktotal - display the number of players in the ranking" % cfg.USER_PREFIX )
	if cfg.VOTING:
		io.privateMsg(wormid, "%skick wormID - add vote to kick player etc" % cfg.ADMIN_PREFIX)
		io.privateMsg(wormid, "%smute wormID - add vote" % cfg.ADMIN_PREFIX)
		io.privateMsg(wormid, "%spreset presetName - add vote" % cfg.ADMIN_PREFIX)
		io.privateMsg(wormid, "%smod modName (or part of name) - add vote" % cfg.ADMIN_PREFIX)
		io.privateMsg(wormid, "%smap mapName - add vote" % cfg.ADMIN_PREFIX)
		io.privateMsg(wormid, "%slt loadingTime - add vote" % cfg.ADMIN_PREFIX)
		io.privateMsg(wormid, "%sy / %sn - vote yes / no" % (cfg.ADMIN_PREFIX, cfg.ADMIN_PREFIX) )
	if userCommandHelp_Preset:
		userCommandHelp_Preset(wormid)


voteCommand = None
voteTime = 0
votePoster = -1
voteDescription = ""

def addVote( command, poster, description ):
	global voteCommand, voteTime, votePoster, voteDescription
	if time.time() - voteTime < cfg.VOTING_TIME:
		io.privateMsg(poster, "Previous vote still running, " + str(int( cfg.VOTING_TIME + voteTime - time.time() )) + " seconds left")
		return
	if time.time() - hnd.worms[poster].FailedVoteTime < cfg.VOTING_TIME * 2:
		io.privateMsg(poster, "You cannot add vote for " + str(int( cfg.VOTING_TIME * 2 + hnd.worms[poster].FailedVoteTime - time.time() )) + " seconds")
		return
	voteCommand = command
	for w in hnd.worms.keys():
		hnd.worms[w].Voted = 0
	votePoster = poster
	hnd.worms[poster].Voted = 1
	voteTime = time.time()
	voteDescription = description
	recheckVote()

def recheckVote():
	global voteCommand, voteTime, votePoster, voteDescription
	if not voteCommand:
		return

	voteCount = 0
	notVoted = 0

	if cfg.VOTING_COUNT_NEGATIVE:
		for w in hnd.worms.keys():
			voteCount += hnd.worms[w].Voted
			if hnd.worms[w].Voted == 0:
				notVoted += 1
	else:
		for w in hnd.worms.keys():
			if hnd.worms[w].Voted == 1:
				voteCount += 1
			else:
				notVoted += 1

	needVoices = int( math.ceil( len(hnd.worms) * cfg.VOTING_PERCENT / 100.0 ) )

	if voteCount >= needVoices:
		exec(voteCommand)
		voteCommand = None
		voteTime = 0
		return
	if time.time() - voteTime >= cfg.VOTING_TIME or needVoices - voteCount > notVoted:
		voteCommand = None
		voteTime = 0
		io.chatMsg("Vote failed: " + voteDescription )
		if hnd.worms[votePoster].Voted == 1: # Check if worm left and another worm joined with same ID
			hnd.worms[votePoster].FailedVoteTime = time.time()
		return

	io.chatMsg("Vote: " + voteDescription + ", " + str( needVoices - voteCount ) + " voices to go, " +
				str(int( cfg.VOTING_TIME + voteTime - time.time() )) + ( " seconds, say %sy or %sn" % ( cfg.ADMIN_PREFIX, cfg.ADMIN_PREFIX ) ) )


def parseUserCommand(wormid,message):
	try: # Do not check on msg size or anything, exception handling is further down
		if (not message.startswith(cfg.USER_PREFIX)):
			return False # normal chat

		cmd = message.split(" ")[0]
		cmd = cmd.replace(cfg.USER_PREFIX,"",1).lower() #Remove the prefix

		io.messageLog("%i:%s issued %s" % (wormid,hnd.worms[wormid].Name,cmd.replace(cfg.USER_PREFIX,"",1)),io.LOG_USRCMD)

		# Unnecesary to split multiple times, this saves CPU.
		params = message.split(" ")[1:]

		if cmd == "help":
			userCommandHelp(wormid)
		elif cmd == "team" and cfg.ALLOW_TEAM_CHANGE:
			if not params:
				io.privateMsg(wormid, "You need to specify a team" )
				raise Exception, "You need to specify a team"
			if hnd.gameState == GAME_PLAYING: #TODO: Errors, GAME_PLAYING not defined.
				io.privateMsg(wormid, "You cannot change team when playing" )
			else:
				if params[0].lower() == "blue" or params[0].lower() == "b":
					io.setWormTeam(wormid, 0)
				elif params[0].lower() == "red" or params[0].lower() == "r":
					io.setWormTeam(wormid, 1)
				elif ( params[0].lower() == "green" or params[0].lower() == "g" ) and cfg.MAX_TEAMS >= 3:
					io.setWormTeam(wormid, 2)
				elif ( params[0].lower() == "yellow" or params[0].lower() == "y" ) and cfg.MAX_TEAMS >= 4:
					io.setWormTeam(wormid, 3)
		elif cmd == "toprank" and cfg.RANKING:
			ranking.firstRank(wormid)
		elif cmd == "rank" and cfg.RANKING:
			if wormid in hnd.worms:
				wormName = hnd.worms[wormid].Name
				if params:
					wormName = " ".join(params)
				ranking.myRank(wormName, wormid)
		elif cmd == "ranktotal" and cfg.RANKING:
			io.privateMsg(wormid, "There are " + str(len(ranking.rank)) + " players in the ranking.")


		elif cmd == "kick" and cfg.VOTING:
			kicked = int( params[0] )
			addVote( "io.kickWorm(" + str(kicked) +")", wormid, "Kick %i: %s" % ( kicked, hnd.worms[kicked].Name ) )
		elif cmd == "mute" and cfg.VOTING:
			kicked = int( params[0] )
			addVote( "io.muteWorm(" + str(kicked) +")", wormid, "Mute %i: %s" % ( kicked, hnd.worms[kicked].Name ) )

		elif cmd == "mod":
			mod = ""
			for m in hnd.availableMods:
				if m.lower().find(" ".join(params[0:]).lower()) != -1:
					mod = m
					break
			if mod == "":
				io.privateMsg(wormid,"Invalid mod name")
			else:
				addVote( 'hnd.selectPreset( Mod = "%s" )' % mod, wormid, "Mod %s" % mod )
		elif cmd == "map":
			level = ""
			for l in hnd.availableLevels:
				if l.lower().find(" ".join(params[0:]).lower()) != -1:
					level = l
					break
			if level == "":
				io.privateMsg(wormid,"Invalid map name")
			else:
				addVote( 'hnd.selectPreset( Level = "%s" )' % level, wormid, "Map %s" % level )
		elif cmd == "lt":
			addVote( 'hnd.selectPreset( LT = %i )' % int(params[0]), wormid, "Loading time %i" % int(params[0]) )
		elif cmd == "preset":
			preset = -1
			for p in range(len(hnd.availablePresets)):
				if hnd.availablePresets[p].lower().find(params[0].lower()) != -1:
					preset = p
					break
			if preset == -1:
				io.privateMsg(wormid,"Invalid preset name")
			else:
				addVote( 'hnd.selectPreset( Name = "%s" )' % hnd.availablePresets[preset], wormid, "Preset %s" % hnd.availablePresets[preset] )

		elif ( cmd == "y" or cmd == "yes" ) and cfg.VOTING:
			if hnd.worms[wormid].Voted != 1:
				hnd.worms[wormid].Voted = 1
				recheckVote()
		elif ( cmd == "n" or cmd == "no" ) and cfg.VOTING:
			if hnd.worms[wormid].Voted != -1:
				hnd.worms[wormid].Voted = -1
				recheckVote()

		elif parseUserCommand_Preset and parseUserCommand_Preset(wormid, cmd, params):
			pass
		else:
			raise Exception, "Invalid user command"

	except: # All python classes derive from main "Exception", but confused me, this has the same effect.
			# TODO, send what's passed in the exception to the user?
		io.privateMsg(wormid, "Invalid user command")
		io.messageLog(formatExceptionInfo(),io.LOG_ERROR) #Helps to fix errors
		return False
	return True

