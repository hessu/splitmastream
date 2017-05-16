
splitmastream
----------------

splitmastream 1.2 by Heikki Hannikainen, hessu at hes.iki.fi 2001-2017

Released to the general public on 6.7.2003 at http://he.fi/splitmastream/

Usage: splitmastream <streamname>

An MPEG stream will be read from stdin, a copy of it will be written to a
file named YYMMDD.HH-streamname.mp3 in the current working directory, and
then the stream will be written to stdout.  Think of it as a
hourly-file-cutting 'tee' for MPEG audio.  Files are switched on every hour
at a frame boundary so that every file should be playable without noise at
the beginning/end.

Useful for making an archive of an mpeg stream which is, for example,
coming from an encoder and going to an icecast transmitter.

GPL, v2 or higher, as usual, which should have been included, blah blah.


Compiling:

	type 'make'

this results in the 'splitmastream' binary, hopefully.
	
Tested on various RedHat Linux and Ubuntu/Debian distributions, but should
compile (with maybe a couple trivial #include changes) on any modern
Unix-like operating system.

