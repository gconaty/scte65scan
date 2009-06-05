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
 * scte65scan.h - mostly verbosity/debugging stuff
 *
 */
#ifndef __SCTESCAN_H__
#define __SCTESCAN_H__

extern int verbosity;


#ifdef USEFLTK

#define tblout addtblout

#define printdf(level, fmt...) {if (level <= verbosity) scanbrowseradd(fmt);}
#define dprintfp(level, fmt, args...) \
	printdf(level, "%s:%d: " fmt, __FUNCTION__, __LINE__ , ##args)
#define fatalp(fmt, args...) {char ls[1024];sprintf(ls, "FATAL: " fmt , ##args); fl_alertc(ls);exit(1);}

#else

#define tblout printf
#define printdf(level, fmt...) {if (level <= verbosity) fprintf(stderr, fmt);}
#define dprintfp(level, fmt, args...) \
	printdf(level, "%s:%d: " fmt, __FUNCTION__, __LINE__ , ##args)
#define fatalp(fmt, args...) {dprintfp(-1, "FATAL: " fmt , ##args); exit(1);}

#endif



#define errorp(msg...) printdf(0, "ERROR: " msg)
#define errornp(msg) printdf(0, "%s:%d: ERROR: " msg ": %d %m\n", __FUNCTION__, __LINE__, errno)
#define warningp(msg...) printdf(1, "WARNING: " msg)
#define infop(msg...) printdf(2, msg)
#define verbosep(msg...) printdf(3, msg)
#define moreverbosep(msg...) printdf(4, msg)
#define debugp(msg...) dprintfp(5, msg)
#define verbosedebugp(msg...) dprintfp(6, msg)

#ifdef WIN32
  #include <windows.h>
  #define osindep_msleep(x) Sleep(x)
#else
  #define osindep_msleep(x) usleep(x*1000)
#endif

#endif
