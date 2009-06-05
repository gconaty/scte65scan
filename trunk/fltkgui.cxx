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

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <FL/Fl.H>
#include <FL/Fl_Window.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Wizard.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Round_Button.H>
#include <FL/Fl_Multiline_Output.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_File_Chooser.H>
#include <FL/Fl_Browser.H>
#include <FL/Fl_Menu_Bar.H>
#include "tunerdmx.h"
#include "threads.h"
#include "fltkgui.h"

using namespace std;

// Simple 'wizard' using fltk's new Fl_Wizard widget
static Fl_Group *g_hdhrgroup = NULL;
static Fl_Group *g_ifgroup = NULL;
static Fl_Browser *g_scanbrowser = NULL;
static int g_abort=1;
static string g_outputbuf;

// simple wizard callbacks
static void back_cb(Fl_Widget*,void *wiz) { ((Fl_Wizard *)wiz)->prev(); }
static void next_cb(Fl_Widget*,void *wiz) { ((Fl_Wizard *)wiz)->next(); }
static void done_cb(Fl_Widget*,void *win)
{
  g_abort=0;
  ((Fl_Window *)win)->hide();
}

//radio button callback to grey out inactive options
static void rbutton_cb(Fl_Widget *buttoncheck, long which) {
  if (((Fl_Button *)buttoncheck)->value() ^ which) {
    g_ifgroup->deactivate();
    g_hdhrgroup->activate();
  } else {
    g_ifgroup->activate();
    g_hdhrgroup->deactivate();
  }
}

//"All VCT" active checkbox = inactive widget
static void vctbutton_cb(Fl_Widget *buttoncheck, void *userdata) {
  if (((Fl_Button *)buttoncheck)->value())
    ((Fl_Widget *) userdata)->deactivate();
  else
    ((Fl_Widget *) userdata)->activate();
}

//open file dialog.  fills in supplied text input field with result
static void filechooser_cb(Fl_Widget *, void *userdata)
{
  Fl_Input *fname_input = (Fl_Input *) userdata;
  Fl_File_Chooser chooser(".","*",Fl_File_Chooser::SINGLE,"Open...");
  
  chooser.preview(0);
  chooser.show();
  while (chooser.shown())
    Fl::wait();
  if (chooser.value() == NULL)
    return;
  fname_input->value(chooser.value());
}

// main wizard to get params
int getparmswiz(tuners_t *tunertype, char **srcstr, char **itfile, int *vctid, char *psip) {
    int retval;
    Fl_Window *win = new Fl_Window(500,400,"scte65scan");
    Fl_Wizard *wiz = new Fl_Wizard(0,0,500,400);

    // Wizard: page 1
        Fl_Group *g1 = new Fl_Group(0,0,500,400);
        Fl_Button *next1 = new Fl_Button(390,370,100,25,"&Next");
        next1->callback(next_cb, wiz);
        Fl_Round_Button *if_rbutton = new Fl_Round_Button(10,10,85,15,"&Input File");
            g_ifgroup = new Fl_Group(80,30,410,30);
            Fl_Input *ifname_input = new Fl_Input(85,30,200,25,"File:");
            Fl_Button *browse_button1 = new Fl_Button(300,30,100,25,"Browse...");
            browse_button1->callback(filechooser_cb, ifname_input);
            if_rbutton->type(FL_RADIO_BUTTON);
            if_rbutton->set();
            g_ifgroup->end();
        Fl_Round_Button *hdhr_rbutton = new Fl_Round_Button(10,65,105,15,"&HDHomeRun");
        hdhr_rbutton->type(FL_RADIO_BUTTON);
            g_hdhrgroup = new Fl_Group(45,85,420,175);
            Fl_Input *hdhr_id = new Fl_Input(90,85,255,20,"ID:");
            hdhr_id->value("FFFFFFFFF");
            Fl_Round_Button *hdhrtuner0_rbutton = new Fl_Round_Button(150,105,95,25,"Tuner&0");
            hdhrtuner0_rbutton->type(FL_RADIO_BUTTON);
            hdhrtuner0_rbutton->set();
            Fl_Round_Button *hdhrtuner1_rbutton = new Fl_Round_Button(150,130,95,25,"Tuner&1");
            hdhrtuner1_rbutton->type(FL_RADIO_BUTTON);
            Fl_Input *hdhr_itf = new Fl_Input(165,155,180,25,"Initial Tuning File");
            Fl_Button *browse_button2 = new Fl_Button(360,155,100,25,"Browse...");
            browse_button2->callback(filechooser_cb, hdhr_itf);
            g_hdhrgroup->end();
            g_hdhrgroup->deactivate();
        if_rbutton->when(FL_WHEN_CHANGED);
        if_rbutton->callback(rbutton_cb, (long) 1);
        hdhr_rbutton->when(FL_WHEN_CHANGED);
        hdhr_rbutton->callback(rbutton_cb, (long) 0);
        g1->end();
    // Wizard: page 2
        Fl_Group *g2 = new Fl_Group(0,0,500,400);
        Fl_Button *next2 = new Fl_Button(390,370,100,25,"&Next");
        next2->callback(done_cb, win);
        Fl_Button *back2 = new Fl_Button(280,370,100,25,"&Back");
        back2->callback(back_cb, wiz);
        Fl_Input *vct_input = new Fl_Input(75,25,85,25,"VCT_ID:");
        vct_input->deactivate();
        Fl_Check_Button *allvct_button = new Fl_Check_Button(165,25,105,25, "&All VCT_ID\'s");
        allvct_button->callback(vctbutton_cb, vct_input);
        allvct_button->set();
        Fl_Check_Button *psip_button = new Fl_Check_Button(75,55,105,25, "Scan for &PSIP");
        g2->end();
    wiz->end();
    win->end();
    win->show();
    retval=Fl::run();
    if (g_abort)
      exit(1);
    *tunertype = if_rbutton->value() ? INFILE : HDHOMERUN;
    *srcstr = strdup(if_rbutton->value() ? ifname_input->value() : hdhr_id->value());
    set_hdhr_tuner_number(hdhrtuner0_rbutton->value() ? 0 : 1);
    *itfile = strdup(hdhr_itf->value());
    *vctid = allvct_button->value() ? -1 : strtol(vct_input->value(), NULL, 0);
    *psip = psip_button->value();
    return retval;
}

// CSV parse at offset, fill in record vector, return new offset for next record
static int
get_csvline(vector<string> &record, const string& line, int offset=0)
{
  int pos=offset;
  int len=line.length();
  bool inquotes=false;
  char c;
  string field;

  record.clear();

  while(line[pos]!='\0' && pos < len)
  {
    c = line[pos];

    if (!inquotes && field.length()==0 && c=='"')
      inquotes=true;
    else if (inquotes && c=='"')
      inquotes=false;
    else if (!inquotes && c==',') {
      record.push_back( field );
      field.clear();
    } else if (!inquotes && (c=='\r' || c=='\n') ) {
      record.push_back( field );
      return ++pos;
    } else 
      field += c;
    pos++;
  }
  record.push_back( field );
  return pos;
}

// File/saveAs callback.  *p=Browser (for saving as text file)
static void saveas_cb(Fl_Widget*, void *p)
{
  Fl_File_Chooser chooser(".","*",Fl_File_Chooser::CREATE,"SaveAs...");
  Fl_Browser *output_browser = (Fl_Browser *) p;
  
  chooser.preview(0);
  chooser.ok_label("Save");
  chooser.show();
  chooser.filter("Text (*.txt)\tComma Separated Values (*.csv)");
  while (chooser.shown())
    Fl::wait();
  if (chooser.value() == NULL)
    return;

  ofstream outfile(chooser.value());
  if (1==chooser.filter_value()) { // Save as CSV
    outfile << g_outputbuf;
  } else { // Save text from output table browser object
    for (int i=1, size=output_browser->size(); i< size; i++) {
      char *c = (char *) output_browser->text(i);
      c += 2; // skip over formatting characters
      outfile << c << endl;
    }
  }
  if (outfile.bad())
    fl_alert("Unable to save %s",chooser.value());
  outfile.close();
}

// File/Quit menuitem callback
static void quit_cb(Fl_Widget*, void*)
{
  exit(0);
}

// make a new output window with table output
// main thread calls this when scan thread finishes
void output_cb (void *p)
{
int offset=0;
vector<string> rec;
Fl_Window *w = new Fl_Window (625,450, "Table Output...");
Fl_Menu_Bar *menubar = new Fl_Menu_Bar (0,0,625,25);
Fl_Browser *output_browser = new Fl_Browser(0,25,625,425);

menubar->add("&File/Save&As...", 0, saveas_cb, output_browser);
menubar->add("&File/&Quit", 0, quit_cb);
w->resizable(output_browser);

//convert internal CSV to human-readable
while (offset < g_outputbuf.size() ) {
  ostringstream outputline;
  outputline << "@f";
  offset = get_csvline(rec, g_outputbuf, offset);
  switch(rec.size()) {
    case 4: // VCT_ID, ####, version:, ##
      outputline << rec[0] << " " << rec[1] << ", ";
      outputline << rec[2] << " " << rec[3];
      break;
    case 6: // VC, NAME, CDS, FREQ, PROG, MOD
      if ("VCT_ID" == rec[0]) {// VCT_ID, ####, FOUND_AT, ### version:, ##
        for (int i=0; i<6; i++)
          outputline << rec[i] << " ";
      } else if ("VIRTUAL_CHANNEL" == rec[0]) {
        outputline << "VC  CDS.PROG NAME                               FREQ/MODULATION";
      } else {
        outputline << setw(4)  << left  << rec[0];
        outputline << setw(4)  << right << rec[2] << ".";
        outputline << setw(4)  << left  << rec[4];
        outputline << setw(35) << left  << rec[1];
        outputline << rec[3] << "hz/" << rec[5];
      }
      break;
    default:
      for (int i=0, len=rec.size(); i< len; i++)
        outputline << setw(10) << rec[i];
  }
  output_browser->add(outputline.str().c_str());
}

w->end();
w->show();
}

// child thread calls this when it wants to display some scanning related info
void scanbrowseradd(char *fmt, ...)
{
  char str[1024];
  va_list ap;

  va_start(ap,fmt);
  vsprintf(str, fmt, ap);
  va_end(ap);

  if (g_scanbrowser) { // scanbrowser window if it's available
    Fl::lock();
    g_scanbrowser->add(str);
    g_scanbrowser->bottomline(g_scanbrowser->size());
    Fl::unlock();
    Fl::awake((void*) NULL);
  } else { // use stdout if not yet created the child thread yet
    printf(str);
    fflush(stdout);
  }
}

//child thread calls this when done with the scan
void threaddone(void)
{
  Fl::awake(output_cb, NULL);
}

// make a browser window from mainthread to show scan messages from childthread
// (call scanbrowseradd() from childthread to write into this window)
void makescanbrowser()
{
Fl_Window *w = new Fl_Window(300,200, "Scanning...");
g_scanbrowser = new Fl_Browser(0,0,300,200);
w->resizable(g_scanbrowser);
w->end();
w->show();
g_scanbrowser->add("Scanning started");
}

// C interface for appending to C++ string (g_outputbuf)
void addtblout(char *fmt, ...)
{
  char str[1024];
  va_list ap;

  va_start(ap,fmt);
  vsprintf(str, fmt, ap);
  va_end(ap);
  g_outputbuf.append(str);
}

// C interface for C++ function fl_alert()
void fl_alertc(char *str)
{
  fl_alert(str);
}

// prior to scan, main thread calls this to create child thread
// and scan msgs browser window
void dothreads(void *(*scan_func)(void*))
{
Fl_Thread scan_thread;

makescanbrowser();
Fl::lock();
fl_create_thread(scan_thread, scan_func, NULL);

Fl::run();
}
