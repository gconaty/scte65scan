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
 * fltkgui.h - FLTK GUI C-interface function prototypes
 *
 */

#ifdef __cplusplus
extern "C" {
#endif

int getparmswiz(tuners_t *, char **, char **, int *, char*);
void scanbrowseradd(char *, ...);
void threaddone(void);
void addtblout(char *, ...);
void dothreads(void *(*)(void*));
void fl_alertc(char *msg, ...);

#ifdef __cplusplus
}
#endif
