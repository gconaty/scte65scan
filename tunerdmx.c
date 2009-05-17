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
 * tunderdmx.c - hardware/arch dependent stuff (tuner and demuxer)
 *
 */

#ifndef WIN32
#include <linux/dvb/frontend.h>
#include <linux/dvb/dmx.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <string.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include "tunerdmx.h"

#ifdef HDHR
#include "hdhomerun.h"

static struct hdhomerun_device_t *hd = NULL;

#endif

static void open_hdhr(char *idstring)
{
#ifdef HDHR
  if (!hd) {
    hd = hdhomerun_device_create_from_str(idstring, NULL);
    if (!hd) {
      fprintf(stderr, "invalid HDHR device id: %s\n", id_str);
      exit(-1);
    }
    uint32_t device_id_requested = hdhomerun_device_get_device_id_requested(hd);
    if (!hdhomerun_discover_validate_device_id(device_id_requested)) {
      fprintf(stderr, "invalid device id: %08lX\n", (unsigned long)device_id_requested);
    }
    const char *model = hdhomerun_device_get_model_str(hd);
    if (!model) {
      fprintf(stderr, "unable to connect to device\n");
      hdhomerun_device_destroy(hd);
      exit (-1);
    }
  }
#endif
}

static int
pkt_to_section(unsigned char *sbuf, int slen, unsigned char *pbuf, int plen)
{
  static unsigned char ringbuf[2*4096];
  static int head=0, tail=0, sbuflen=0, lastcc=-1;
  static enum {FIND_PUSI, GET_SECT} state = FIND_PUSI;
  int i, rlen;

  // xfer to ringbuf
  for (i=0; i< plen; i++) {
    if (0==(i%188) && pbuf[i] != 0x47) {
      fprintf(stderr, "ERROR: lost TS sync\n");
      exit(-1);
    }
    ringbuf[tail] = pbuf[i];
    tail = (tail +1) % sizeof(ringbuf);
  }

  rlen = (tail >= head) ? (tail - head) : (tail + sizeof(ringbuf) - head);
  // process new data
  if (FIND_PUSI == state) {
    while (rlen >= 188) {
      int tei  = ringbuf[(head+1)%sizeof(ringbuf)] & 0x80;
      int pusi = ringbuf[(head+1)%sizeof(ringbuf)] & 0x40;
      //int pri  = ringbuf[(head+1)%sizeof(ringbuf)] & 0x20;
      //int pid  = ((ringbuf[(head+1)%sizeof(ringbuf)] << 8) | ringbuf[(head+2)%sizeof(ringbuf)] & 0x1fff);
      int tsc  = ringbuf[(head+3)%sizeof(ringbuf)] & 0xc0;
      int afc  = (ringbuf[(head+3)%sizeof(ringbuf)] & 0x30) >> 4;
      int cc   = ringbuf[(head+3)%sizeof(ringbuf)] & 0x0f;

      if (lastcc != -1 && cc != ((lastcc+1) & 0xf))
        fprintf(stderr, "Continuity interruption\n");
      if (pusi && !tei && !tsc && afc == 1) {
        state = GET_SECT;
        sbuflen=0;
        break;
      }
      rlen -= 188;
      head = (head + 188) % sizeof(ringbuf);
    } // while
  } // if FIND_PUSI

  if (GET_SECT == state) {
    while (rlen >= 188) {
      //int tei  = ringbuf[(head+1)%sizeof(ringbuf)] & 0x80;
      //int pusi = ringbuf[(head+1)%sizeof(ringbuf)] & 0x40;
      //int pri  = ringbuf[(head+1)%sizeof(ringbuf)] & 0x20;
      //int pid  = ((ringbuf[(head+1)%sizeof(ringbuf)] << 8) | ringbuf[(head+2)%sizeof(ringbuf)] & 0x1fff);
      //int tsc  = ringbuf[(head+3)%sizeof(ringbuf)] & 0xc0;
      //int afc  = (ringbuf[(head+3)%sizeof(ringbuf)] & 0x30) >> 4;
      int cc   = ringbuf[(head+3)%sizeof(ringbuf)] & 0x0f;
      int sectionlen, j;

      if (lastcc != -1 && cc != ((lastcc+1) & 0xf)) {
        fprintf(stderr, "Continuity interruption\n");
        state=FIND_PUSI;
        return 0;
      }
      i = (head + 4) % sizeof(ringbuf);
      j = 4;
      if (0==sbuflen) {
        j += ringbuf[i] + 1; // add pointer to counter
        i = (ringbuf[i] + i + 1) % sizeof(ringbuf); // use pointer
      }
      while (j<188 && sbuflen <= slen) {
        sbuf[sbuflen++] = ringbuf[i++];
        i = i % sizeof(ringbuf);
        j++;
      }
      if (sbuflen > slen) {
        fprintf(stderr, "FATAL ERROR: section buffer overflow\n");
        exit(1);
      }
      sectionlen = ((sbuf[1] <<8) | sbuf[2]) & 0xfff;
      if (sbuflen >= sectionlen) { // got it all
        state=FIND_PUSI;
        if (sectionlen <= 188)
          head = (head + 188) % sizeof(ringbuf);
        return 1;
      }
    head = (head + 188) % sizeof(ringbuf);
    rlen -= 188;
    } // while rlen>187
  } // if get_sect
  return 0;
}

int
demux_start(struct dmx_desc *d)
{
  if (INFILE == d->type) {
    return 0;
  } else if (HDHOMERUN == d->type) {
#ifdef HDHR
    char *ret_err, arg[256], cmd[256];

    snprintf(cmd, sizeof(cmd), "/tuner0/filter");
    snprintf(arg, sizeof(arg), "%d", d->pid);
    if (hdhomerun_device_set_var(hd, cmd, arg, NULL, &ret_err) < 0) {
      fprintf(stderr, "Error PID filtering HDHomerun\n");
      return 1;
    }

    if (ret_err) {
      fprintf(stderr, "%s\n", ret_error);
      return 1;
    }

    snprintf(cmd, sizeof(cmd), "/tuner0");
    if (hdhomerun_device_set_tuner_from_str(hd, cmd) <= 0) {
      fprintf(stderr, "error setting tuner\n");
      return -1;
    }
    int ret = hdhomerun_device_stream_start(hd);
    if (ret <= 0) {
      fprintf(stderr, "unable to start stream\n");
      return ret;
    }
    return 0;
#endif
  } else {
#ifdef WIN32
#else
    if( ioctl( d->dmx_fd, DMX_START) < 0 ) {   
      perror("DMX_START");
      return 1;
    }
#endif
  }
  return 0;
}

int
demux_stop(struct dmx_desc *d)
{
  if (INFILE == d->type) {
    return 0;
  } else if (HDHOMERUN == d->type) {
#ifdef HDHR
        hdhomerun_device_stream_stop(hd);
#endif
  } else {
#ifdef WIN32
#else
    if( ioctl( d->dmx_fd, DMX_STOP) < 0 ) {   
      perror("DMX_STOP");
      return 1;
    }
#endif
  }
  return 0;
}

int
demux_read(struct dmx_desc *d, unsigned char *sbuf, int slen)
{
  if (INFILE == d->type) {
    time_t timeout = time(NULL) + d->timeout + 1;
    int done=0;
    do {
      char pbuf[188];
      if (fread(pbuf, 1, 188, d->fp) < 188) {
        fprintf(stderr, "infile EOF\n");
        return -2;
      }
      // PID filter
      if ((((pbuf[1] << 8) | pbuf[2]) & 0x1fff) != d->pid)
        continue;
      done = pkt_to_section(sbuf, slen, pbuf, 188);
    } while (done == 0 && time(NULL) < timeout);
    return done ? ((((sbuf[1]<<8) | sbuf[2]) & 0xfff) +3) : -1;
  } else if (HDHOMERUN == d->type) {
#ifdef HDHR
    time_t timeout = time(NULL) + d->timeout + 1;
    int done=0;
    do {
      size_t plen;
      uint8_t *pbuf = hdhomerun_device_stream_recv(hd, VIDEO_DATA_BUFFER_SIZE_1S, &plen);
      if (!pbuf)
        usleep(64 * 1000);
      else
        done = pkt_to_section(sbuf, slen, pbuf, plen);
    } while (done == 0 && time(NULL) < timeout);
    return done ? ((((sbuf[1]<<8) | sbuf[2]) & 0xfff) +3) : -1;
#endif
  } else {
#ifdef WIN32
#else
    int n;
    if ((n = read(d->dmx_fd, sbuf, slen)) < 0) {
      if (ETIMEDOUT == errno) {
        return -1;
      } else {
        perror ("while reading demux");
        exit(1);
      }
    }
  return n;
#endif
  }
  return -1;
}

int
demux_open(unsigned char *buf, int pid, int timeout, struct dmx_desc *d, tuners_t type)
{
  d->type = type;
  if (INFILE == type) {
    d->pid = pid;
    d->timeout = timeout;
    if (0==strcmp(buf,"-"))
      d->fp = stdin;
    else if (NULL == (d->fp=fopen(buf,"r"))) {
      perror(buf);
      exit(-1);
    }
    return 0;
  } else if (HDHOMERUN == type) {
    d->pid = pid;
    d->timeout = timeout;
    open_hdhr(buf);
  } else {
#ifdef WIN32
#else
    struct dmx_sct_filter_params flt;

    d->dmx_fd = open (buf, O_RDWR);
    if (d->dmx_fd < 0) {
      perror(buf);
      return (1);
    }

    memset(&flt, 0, sizeof(flt));
    flt.pid = pid;
    flt.timeout = timeout * 1000;

    if (ioctl (d->dmx_fd, DMX_SET_FILTER, &flt) < 0)
      {
        perror ("DMX_SET_FILTER");
        return(1);
      }
    return 0;
#endif
  }
  return 1;
}

int tuner_open(char *buf, struct tuner_desc *t, tuners_t type)
{
  t->type = type;
  if (INFILE == type) {
    return 0;
  } else if (HDHOMERUN == type) {
    open_hdhr(buf);
  } else {
#ifdef WIN32
#else
    if ((t->fe_fd = open(buf, O_RDWR)) < 0) {
      perror (buf);
      return 1;
    }
    return 0;
#endif
  }
  return 1;
}

#ifndef WIN32
static enum fe_modulation str2dvbmod(const char *str)
{
  struct {
    char *str;
    fe_modulation_t val;
  } mod[] = {
    { "QPSK",   QPSK },
    { "QAM16",  QAM_16 },
    { "QAM32",  QAM_32 },
    { "QAM64",  QAM_64 },
    { "QAM128", QAM_128 },
    { "QAM256", QAM_256 },
    { "AUTO",   QAM_AUTO },
    { "8VSB",   VSB_8 },
    { "16VSB",  VSB_16 },
    { NULL, 0 }
  };
  int n=0;

  while (mod[n].str) {
    if (!strcmp(mod[n].str, str))
      return mod[n].val;
    n++;
  }
  fprintf(stderr, "invalid modulation value '%s'\n", str);
  return QAM_AUTO;
}
#endif

int
tuner_tune(struct tuner_desc *t, struct transponder *tp)
{
  if (INFILE == t->type) {
    return 0;
  } else if (HDHOMERUN == t->type) {
#ifdef HDHR
    char *ret_err, arg[256], cmd[256];

    snprintf(cmd, sizeof(cmd), "/tuner0/channel");
    snprintf(arg, sizeof(arg), "%s:%d", tp->modulation, tp->frequency);
    if (hdhomerun_device_set_var(hd, cmd, arg, NULL, &ret_err) < 0) {
      fprintf(stderr, "Error tuning HDHomerun\n");
      return 1;
    }

    if (ret_err) {
      fprintf(stderr, "%s\n", ret_error);
      return 1;
    }

    return 0;
#endif
  } else {
#ifdef WIN32
#else
    struct dvb_frontend_parameters fep;

    bzero(&fep, sizeof(fep));
    fep.frequency = tp->frequency;
    fep.u.vsb.modulation = str2dvbmod(tp->modulation);
    if( ioctl(t->fe_fd, FE_SET_FRONTEND, &fep ) < 0 ) {
      perror("failed FE_SET_FRONTEND");
      return 1;
    }
    return 0;
#endif
  }
  return 1;
}

int
tuner_checklock(struct tuner_desc *t)
{
  if (HDHOMERUN == t->type) {
#ifdef HDHR
    char *ret_err, *ret_val, cmd[256], *lock;

    snprintf(cmd, sizeof(cmd), "/tuner0/channel");
    snprintf(arg, sizeof(arg), "%s:%d", tp->modulation, tp->frequency);
    if (hdhomerun_device_get_var(hd, cmd, &ret_val, &ret_error) < 0) {
      fprintf(stderr, "Error checking lock on HDHomerun\n");
      return 0;
    }

    if (ret_error) {
      fprintf(stderr, "%s\n", ret_error);
      return 0;
    }
    lock= strstr(ret_val, "lock=");
    lock= strchr(lock, "=");
    lock++;

    // TODO: What does HDHR say when it's not locked???
    return ('q'==*lock || 't'==*lock || '1'==*lock || '8'==*lock);
#endif
  } else {
#ifdef WIN32
#else
    fe_status_t fe_status;

    ioctl(t->fe_fd, FE_READ_STATUS, &fe_status);
    return (fe_status & FE_HAS_LOCK);
#endif
  }
  return 0;
}