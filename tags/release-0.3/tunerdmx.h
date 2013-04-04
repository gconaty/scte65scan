/* 
 * Copyright 2009, Dennis Lou dlou99 at yahoo punctuation-mark com
 * in-band scan for SCTE-65 tables (Annex A profile 1&2 only, so far)
 *
 * This file is part of the scte65scan project
 *
 *  scte65scan is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  scte65scan is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with scte65scan.  If not, see <http://www.gnu.org/licenses/>.
 *
 * tunderdmx.h - prototypes for tuner/demux related stuff
 *
 */

// ring buffer used for devices returning TS pkts instead of
// whole sections (namely stdin and hdhomerun)
#define RINGBUFSIZE 65536

typedef enum {INFILE, LINUX_DVB, WINDOWS_BDA, HDHOMERUN} tuners_t;

struct dmx_desc {
  tuners_t type;
  int pid;
  int timeout;
  FILE *fp;
#ifdef WIN32
#else
  int dmx_fd;
#endif
};

struct tuner_desc {
  tuners_t type;
#ifdef WIN32
#else
int fe_fd;
#endif
};

struct transponder {
  struct transponder *next;
  int frequency;
  char *modulation;
};

#ifdef __cplusplus
extern "C" {
#endif
int demux_open(unsigned char *, int, int, struct dmx_desc *, tuners_t);
int demux_start(struct dmx_desc *);
int demux_stop(struct dmx_desc *);
int demux_read(struct dmx_desc *, unsigned char *, int);
int tuner_open(char *, struct tuner_desc*, tuners_t);
int tuner_tune(struct tuner_desc*, struct transponder *);
int tuner_checklock(struct tuner_desc*);
void set_hdhr_tuner_number(int);
#ifdef __cplusplus
}
#endif
