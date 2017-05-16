
/*
 *	read live mpeg audio stream on stdin, write it to stdout and
 *	write to hour-long files, splitting properly on frame boundary
 *
 *	Heikki Hannikainen <hessu at hes.iki.fi> 2001
 *
 *	GPL, v2 or higher, as usual, which should have been included, blah
 *	blah.
 */

#define PROGNAME "splitmastream"
#define VERSION "1.1"
#define PROGSTR PROGNAME " " VERSION " by Heikki Hannikainen, hessu at hes.iki.fi 2001"
#define USAGE "\nUsage: " PROGNAME " [-n] <streamname>\n\n" \
	"An MPEG stream will be read from stdin, a copy of it will be written\n" \
	"to a file named YYMMDD.HH-streamname.mp3 and then the stream will be\n" \
	"written to stdout (unless -n is specified). Think of it as a\n" \
	"hourly-file-cutting 'tee' for MPEG audio. Files are switched on every hour\n" \
	"at a frame boundary so that every file should be playable without noise\n" \
	"at the beginning/end.\n" \
	"\n" \
	"Useful for making an archive of an mpeg stream which is, for example,\n" \
	"coming from an encoder and going to an icecast transmitter.\n\n" \
	"GPL, v2 or higher, as usual, which should have been included, blah blah.\n"

#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <time.h>

typedef unsigned char byte;

long long bytes_pos = -1;		/* absolute location we're reading */
int blocksize = 4096;			/* read/write buf size */
int rbufpos, rbuflen, wbufpos, wbuflen;	/* pointers and lengths */
byte *rbuf = NULL;			/* read buffer */
byte *wbuf = NULL;			/* write buffer */
int debug = 0;				/* you guessed it */
int outfd = 1;				/* fd we write all input to (stdout) */
int copyfd = -1;			/* fd we write the copy to (file) */
char *streamname = NULL;		/* stream name */

/* lookup tables for converting mpeg audio header values to actual
   bitrates / sampling frequencies */

int mpeg_bitrate_index [2][3][16] = {
	{
		{0,32,64,96,128,160,192,224,256,288,320,352,384,416,448,0},
		{0,32,48,56,64,80,96,112,128,160,192,224,256,320,384,0},
		{0,32,40,48,56,64,80,96,112,128,160,192,224,256,320,0}
	},
	{
		{0,32,48,56, 64,80 ,96 ,112,128,144,160,176,192,224,256,0},
		{0, 8,16,24,32,40,48, 56,64 ,80 ,96 ,112,128,144,160,0},
		{0, 8,16,24,32,40,48, 56,64 ,80 ,96 ,112,128,144,160,0}
	}
};

int mpeg_sampling_index [3][4] = {
	{44100,48000,32000,0}, //mpeg 1
	{22050,24000,16000,0}, //mpeg 2
	{11025,12000,8000,0}   //mpeg 2.5
};

/*
 *	Parse arguments
 */

void parse_args(int argc, char *argv[])
{
	int s;
	
	while ((s = getopt(argc, argv, "n?h")) != -1) {
		switch (s) {
			case 'n':
				outfd = -1;
				break;
			case '?':
			case 'h':
				fputs(PROGSTR "\n" USAGE, stderr);
				exit(0);
		}
	}
	
	if (argc - optind != 1) {
		fputs(PROGSTR "\n" USAGE, stderr);
		exit(1);
	}
	
	while (optind < argc) {
		switch (argc - optind) {
			case 1:
				streamname = strdup(argv[optind]);
				break;
			default:
				fputs(PROGSTR "\n" USAGE, stderr);
				exit(1);
		}
		optind++;
	}
}

/*
 *	flush write buffer to disk, return value from write().
 *	only write N bytes from the beginning, leaving the
 *	rest of the data (next frame) into the buffer
 */

int writebuf(int f, int n) {
	int w;
	/* if file isn't open, we silently don't write to avoid a lot of
	 * noise and to keep the stream running through
	 */
	if (f >= 0) {
		w = write(f, wbuf, n);
	} else {
		w = n;
	}
	if (w == n) {
		if (n < wbufpos) {
			memmove(wbuf, wbuf + n, wbufpos - n);
			wbufpos -= n;
		} else {
			wbufpos = 0;
		}
	}
	return w;
}

/*
 *	copy n bytes starting from c to wbuf, flushing to file f if
 *	buffer becomes full, return bytes copied or -1 if there's a
 *	a problem with write() while flushing
 */

int writebytes(byte *c, int n, int f)
{
	int tw, w, i = 0;
	
	while (i < n) {
		if (wbufpos == wbuflen) {
			tw = wbufpos;
			w = writebuf(f, wbufpos);
			if (w != tw) {
				fprintf(stderr, PROGNAME ": writebytes: writebuf returned %d: %s\n", w, strerror(errno));
				return -1;
			}
			wbufpos = 0;
		}
		
		w = n - i;
		if (w > wbuflen - wbufpos)
			w = wbuflen - wbufpos;
		memcpy(wbuf + wbufpos, c, w);
		wbufpos += w;
		i += w;
		c += w;
	}
	
	return i;
}

/*
 *	read a single byte from f to c, return value from read()
 *	if there's a problem, 1 if ok
 */

int readbyte(int f, byte *c)
{
	bytes_pos++;
	
	if (rbufpos == rbuflen) {
		/* read more */
		rbuflen = read(f, rbuf, blocksize);
		rbufpos = 0;
		if (rbuflen <= 0)
			return rbuflen;
		if (outfd >= 0)
			if (write(outfd, rbuf, rbuflen) == -1)
				fprintf(stderr, PROGNAME ": write on fd %d failed: %s\n", outfd, strerror(errno));
	}
	
	*c = rbuf[rbufpos];
	if (writebytes(c, 1, copyfd) < 1)
		fprintf(stderr, PROGNAME ": could not write a byte to copy file: %s\n", strerror(errno));
	rbufpos++;
	return 1;
}

/*
 *	skip n bytes of input from f, return value from read() if there's
 *	a problem, or number of bytes read if there are no problems
 */

int skipbytes(int f, int n)
{
	int i = 0, c;
	
	while (i < n) {
		c = n - i;
		if (rbufpos + c > rbuflen)
			c = rbuflen - rbufpos;
		if (c > 0)
			if (writebytes(rbuf + rbufpos, c, copyfd) < 1)
				fprintf(stderr, PROGNAME ": skipbytes: could not write to copy file: %s\n", strerror(errno));
		bytes_pos += c;
		rbufpos += c;
		i += c;
		
		if (rbufpos == rbuflen) {
			/* read more */
			rbuflen = read(f, rbuf, blocksize);
			rbufpos = 0;
			if (rbuflen <= 0)
				return rbuflen;
			if (outfd >= 0)
				if (write(outfd, rbuf, rbuflen) == -1)
					fprintf(stderr, PROGNAME ": write on fd %d failed: %s\n", outfd, strerror(errno));
		}
	}
	
	return i;
}

/*
 *	main
 */

int main(int argc, char **argv)
{
	byte c;
	ssize_t r = 1;
	char fname[FILENAME_MAX] = "";
	// char newfname[FILENAME_MAX] = "";
	time_t t;
	struct tm *tm;
	int old_tm_hour = -1;
	long long frame_start = -1;
	long long next_frame = -1;
	int mpeg2_5;
	int layer, version, protect, bitrate_index, sampling_index, frame_length, padding;
	int bitrate, sampling_rate;
	
	parse_args(argc, argv);
	
	/* actually we use a larger write buffer, but still flush the
	   buffer after every frame... */
	wbuflen = blocksize * 10;
	
	/* allocate buffers */
	if (!(rbuf = malloc(blocksize))) {
		fprintf(stderr, PROGNAME ": Out of memory\n");
		return 1;
	}
	
	if (!(wbuf = malloc(wbuflen))) {
		fprintf(stderr, PROGNAME ": Out of memory\n");
		return 1;
	}
	
	/* init buffer pointers */
	rbufpos = rbuflen = blocksize;
	wbufpos = 0;
	
	/* open up the copy file for writing */
	time(&t);
	tm = gmtime(&t);
	old_tm_hour = tm->tm_hour;
	sprintf(fname, "%04d%02d%02d.%02d-%s.mp3",
		tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
		tm->tm_hour, streamname);
	copyfd = open(fname, O_WRONLY|O_CREAT|O_APPEND, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH);
	if (copyfd < 0)
		fprintf(stderr, PROGNAME ": Could not open %s for writing: %s\n", fname, strerror(errno));
	
	/* loop until eof */
	while (r > 0) {
		/* skip until suspected start of next frame */
		if (next_frame - bytes_pos > 1)
			skipbytes(0, next_frame - bytes_pos - 1);
		if ((r = readbyte(0, &c)) < 1) break;
		if (debug)
			fprintf(stderr, "looking for frame start at %lld\n", bytes_pos);
		frame_start = bytes_pos;
		if (c != 0xff)
			continue;
			
		if ((r = readbyte(0, &c)) < 1) break;
		if ((c & 0xe0) != 0xe0)
			continue;
		if ((c & 0xf0) != 0xf0)
			mpeg2_5 = 1;
		else
			mpeg2_5 = 0;
		if (c & 0x08) {
			if (!mpeg2_5) {
				version = 1;
			} else {
				continue; // invalid 01 encountered
			}
		} else {
			if (!mpeg2_5)
				version = 2;
			else
				version = 3; // for mpeg 2.5
		}
		
		layer = (c & 0x06) >> 1;
		switch (layer) {
			case 0: layer = -1; break;
			case 1: layer = 3; break;
			case 2: layer = 2; break;
			case 3: layer = 1; break;
		}
		
		if (c & 0x01) protect = 0; else protect = 1; // weird but true
		if (debug) fprintf(stderr, "Header at %lld: version %d layer %d protect %d\n", frame_start, version, layer, protect);
		
		if ((r = readbyte(0, &c)) < 1) break;
		bitrate_index = c >> 4;
		sampling_index = (c & 0x0f) >> 2;
		if (sampling_index >= 3) {
			if (debug) fprintf(stderr, "\tBad samplerate\n");
			continue;
		}
		if (bitrate_index == 15) {
			if (debug) fprintf(stderr, "\tBad bitrate\n");
			continue;
		}
		bitrate = mpeg_bitrate_index[version-1][layer-1][bitrate_index];
		sampling_rate = mpeg_sampling_index[version-1][sampling_index];
		if (c & 0x02) padding = 1; else padding = 0;
		if (debug) fprintf(stderr, "\tpadding %d bitrate %d sampling_rate %d\n", padding, bitrate, sampling_rate);
		
		if (version == 1) {
			if (layer == 1) {
				frame_length = ((48000.0 * bitrate) / sampling_rate) + 4 * padding;
			} else {
				frame_length = ((144000.0 * bitrate) / sampling_rate) + padding;
			}
		} else if (version == 2) {
			if (layer == 1) {
				frame_length = ((24000.0 * bitrate) / sampling_rate) + 4 * padding;
			} else {
				frame_length = ((72000.0 * bitrate) / sampling_rate) + padding;
			}
		} else {
			if (debug) fprintf(stderr, "\tversion %d invalid: should be 1 or 2\n", version);
			continue;
		}
		if (protect) frame_length += 2; // size of CRC
		next_frame = frame_start + frame_length;
		if (debug) fprintf(stderr, "\tframe length %d, next frame at %lld\n", frame_length, next_frame);
		
		/* We have a proper frame header - flush write buffer to disk at this point */
		if (debug) fprintf(stderr, "Writing buffer: wbufpos %d bytes_pos %lld frame_start %lld\n", wbufpos, bytes_pos, frame_start);
		writebuf(copyfd, wbufpos - (bytes_pos+1 - frame_start));
		time(&t);
		tm = gmtime(&t);
		if (tm->tm_hour != old_tm_hour) {
			old_tm_hour = tm->tm_hour;
			if (close(copyfd))
				fprintf(stderr, PROGNAME ": Could not close output file %s: %s\n", fname, strerror(errno));
			/*
			 * we used to write to a temp file and then rename - now we don't.
			sprintf(newfname, "done-%s", fname);
			if (rename(fname, newfname))
				fprintf(stderr, PROGNAME ": Could not rename %s to %s: %s\n", fname, newfname, strerror(errno));
			*/
			sprintf(fname, "%04d%02d%02d.%02d-%s.mp3",
				tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
				tm->tm_hour, streamname);
			copyfd = open(fname, O_WRONLY|O_CREAT|O_APPEND, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH);
			if (copyfd < 0)
				fprintf(stderr, PROGNAME ": Could not open %s for writing: %s\n", fname, strerror(errno));
		}
	}
	
	if (r < 0)
		fprintf(stderr, PROGNAME ": Could not read from stdin: %s\n", strerror(errno));
	
	writebuf(copyfd, wbufpos-1);

	if (copyfd >= 0)
		if (close(copyfd))
			fprintf(stderr, PROGNAME ": Could not close output file %s: %s\n", fname, strerror(errno));
	
	return 0;
}

