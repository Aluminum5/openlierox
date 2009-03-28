#!/usr/bin/python

from __future__ import division

import os
import sys
import StringIO
import gzip
import getopt

dirs = ["src", "include"]

svnrepo = "."
# just some check for my server
if os.path.isdir("/backup/data/openlierox-svn"):
	svnrepo = "file://localhost/backup/data/openlierox-svn"

os.chdir(os.path.dirname(sys.argv[0]) + "/../..")

startrev = 2 # excluding initial commit, that's not our work :)

try:
	opts, args = getopt.getopt(sys.argv[1:], "r:", ["rev="])
except getopt.GetoptError, err:
	# print help information and exit:
	print str(err) # will print something like "option -a not recognized"
	usage()
	sys.exit(2)
for o, a in opts:
	if o == "-r":
		startrev = int(a)
	

def svnInfo(revision):
	first = True
	comment = str()
	author = str()
	date = str()
	for l in os.popen("svn log -r %i %s" % (revision, svnrepo)).read().splitlines():
		if len(l) == 0: continue
		if l.startswith("------------"): continue
		if first:
			first = False
			revname, author, date, cmntsize = l.split(" | ")
			continue
		if len(comment) > 0: comment += "   "
		comment += l
	return (author, date, comment)

def analyseData(preprint, data):
	print preprint, "size:", len(data)
	s = StringIO.StringIO()
	g = gzip.GzipFile(fileobj=s, mode="wb")
	gzipheadersize = s.len
	g.write(data)
	g.close()
	complen = s.len - gzipheadersize
	print preprint, "compressed size:", complen
	print preprint, "compress rate:", (100 * len(data) / complen), "%"
	return min(len(data), complen)

def analyseDiff(dir, diffOut):
	changedLines = str()
	file = ""
	for l in diffOut.splitlines():
		if len(l) == 0: continue
		if l.startswith("======="): continue
		if l.startswith("Index: "):
			file = dir + "/" + l[7:]
			print "  file:", file
			continue
		if file == "": continue
		if l.startswith("+"):
			changedLines += l[1:] + "\n"
		#if l.startswith("-"):
		#	changedLines += l[1:] + "\n"
	changedLen = analyseData("    ", changedLines)
	return changedLen


stats = dict()

def analyseSvnRev(rev):
	print "rev", rev, ":",
	info = svnInfo(rev)
	print info
	if not info[0] in stats:
		print "  new dev:", info[0]
		stats[info[0]] = 0

	for d in dirs:
		diffOut = os.popen("svn diff -r %i:%i -x --ignore-eol-style %s/%s" % (rev - 1, rev, svnrepo, d)).read()
		res = analyseDiff(d, diffOut)
		stats[info[0]] += max(0, res)

	return True

rev = startrev
while True:
	analyseSvnRev(rev)
	print "stats:", stats
	rev += 1
