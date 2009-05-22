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
 * scte65scan.c - main(), table parsers and output functions
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>

#include "scte65scan.h"
#include "tunerdmx.h"

// SCTE-65 defines 2 minutes max between S-VCT's
#define SVCT_PERIOD 121
#define PATH_MAX 256

int verbosity=2;

struct revdesc {
  unsigned char valid;
  int version;
  char *parts;
};

struct cds_table {
  int           cd[256];
  unsigned char written[256];
  struct revdesc revdesc;
};

struct mms_record {
  char *modulation_fmt;
};

struct mms_table {
  struct mms_record mm[256];
  unsigned char written[256];
  struct revdesc revdesc;
};

struct sns_record {
  struct sns_record *next;
  unsigned char app_type;
  int id;
#ifndef STRICT_SCTE65
  int extra_comcast_junk;
#endif
  int namelen;
  char name[256];
  int descriptor_count;
  char descriptors[256];
};

struct ntt_table {
  struct sns_record *sns_list;
  struct revdesc revdesc;
};

struct vc_record {
  struct vc_record *next;
  int vc;
  char application;
  char path_select;
  char transport_type;
  char channel_type;
  unsigned int id;
  int cds_ref;
  int prognum;
  int mms_ref;
  unsigned char scrambled;
  unsigned char video_std;
  unsigned char desc_cnt;
  unsigned char desc[256];
  char *psip_name;
  int psip_major;
  int psip_minor;
};

struct vcm {
  struct vcm *next;
  struct vc_record *vc_list;
  unsigned int vctid;
  struct revdesc revdesc;
};
  
typedef struct {
  enum {
    OUTPUT_NOTHING = 0x0,
    OUTPUT_VC      = 0x1,
    OUTPUT_NAME    = 0x2,
    OUTPUT_VC_NAME = (OUTPUT_VC | OUTPUT_NAME),
    OUTPUT_FREQ    = 0x4
  } flags;
  enum {
    TXTTABLE_FMT  = 0,
    SZAP_FMT      = 1,
    CSV_FMT       = 2,
    MYTH_INST_FMT = 3,
    MYTH_UP_FMT   = 4,
    INVALID_FMT       // add new formats before this one
  }format;
  unsigned int freq;
  unsigned int myth_srcid;
} outfmt_t;

static char* psip_modfmt_table[] = {
  "RESERVED",
  "ANALOG",
  "QAM_64",
  "QAM_256",
  "8VSB",
  "16VSB",
  "unk",
  "unk"
};

static char* scte_modfmt_table[] = {
  "UNKNOWN",
  "QPSK",
  "BPSK",
  "OQPSK",
  "8VSB",
  "16VSB",
  "QAM_16",
  "QAM_32",
  "QAM_64",
  "QAM_80",
  "QAM_96",
  "QAM_112",
  "QAM_128",
  "QAM_160",
  "QAM_192",
  "QAM_224",
  "QAM_256",
  "QAM_320",
  "QAM_384",
  "QAM_448",
  "QAM_512",
  "QAM_640",
  "QAM_768",
  "QAM_896",
  "QAM_1024",
  "RESERVED",
  "RESERVED",
  "RESERVED",
  "RESERVED",
  "RESERVED",
  "RESERVED",
  "RESERVED"
};

// vc list formed backwards, so reverse order
static struct vc_record *reverse_vc_list(struct vc_record *vc_rec) {
  struct vc_record *vc_new=NULL;

  while (vc_rec) {
    struct vc_record *vc_tmp = vc_rec; 
    vc_rec = vc_rec->next; // pop top from old
    vc_tmp->next = vc_new; // push it on to new
    vc_new=vc_tmp;
  }
  return vc_new;
}

//TODO:
//void
//find_pids(int timeout, int *scte_pid, int *psip_pid)
//{
//}

int
parse_psip_vct(unsigned char *buf, int section_len, struct vc_record **psip_list, int freq)
{
  int num_channels, i, n, desc_len;

  debugp("Parsing ATSC PSIP VCT table\n");

  // short table
  if (section_len < 16) {
    return 0;
    warningp("bad length PSIP VCT section < 16\n");
  }

  num_channels = buf[9];
  n=10;
  for (i=0; i<num_channels; i++) {
    struct vc_record *new_vc = calloc(1, sizeof(*new_vc));

    new_vc->next = *psip_list;
    *psip_list = new_vc;
    new_vc->psip_name = strdup("0123456");
    // cheap and crummy UTF conversion
    n++;
    new_vc->psip_name[0] = buf[n++];
    n++;
    new_vc->psip_name[1] = buf[n++];
    n++;
    new_vc->psip_name[2] = buf[n++];
    n++;
    new_vc->psip_name[3] = buf[n++];
    n++;
    new_vc->psip_name[4] = buf[n++];
    n++;
    new_vc->psip_name[5] = buf[n++];
    n++;
    new_vc->psip_name[6] = buf[n++];
    debugp("PSIP name %s\n", new_vc->psip_name);
    new_vc->psip_major = ((buf[n] & 0xf)<< 6)  | (buf[n+1] >> 2);
    new_vc->psip_minor = ((buf[n+1] & 0x3) << 8)  | buf[n+2];
    debugp("PSIP VC %d.%d\n", new_vc->psip_major, new_vc->psip_minor);
    n +=3;
    new_vc->mms_ref = buf[n++];
    debugp("PSIP modulation mode %d\n", new_vc->mms_ref);
    n +=6;
    new_vc->prognum = (buf[n] << 8) | buf[n+1];
    n +=2;
    n +=4;
    desc_len = ((buf[n] & 0x3) << 8) | buf[n+1];
    n +=(desc_len + 2);
    new_vc->cds_ref = freq;
    debugp("PSIP frequency %d prognum %d\n", new_vc->cds_ref, new_vc->prognum);
  }
  return 1;
}

int
psip_read_and_parse(struct dmx_desc *d, outfmt_t *outfmt, struct vc_record **psip_list)
{
  unsigned char buf[4096];
  int done=0, section_len, n;

  if (demux_start(d))
    exit(1);

  while (!done) {
    if ((n=demux_read(d, buf, sizeof(buf))) < 0) {
      infop("PID 0x1ffb timeout\n");
      return 0;
    }

    section_len = ((buf[1] & 0xf) << 8) | buf[2];
    verbosedebugp("section 0x%x, length %d\n", buf[0], section_len);
    if (section_len != (n-3)) {
      warningp("bad section length (expect %d, got %d)\n", n-3, section_len);
      continue;
    }

    switch (buf[0]) {
      case 0xc9: // CVCT
        done = parse_psip_vct(buf, section_len, psip_list, outfmt->freq);
        break;
      // ignore the following ATSC PSIP tables:
      case 0xc8: // TVCT (we should be running this on a cable system)
      case 0xc7: // MGT
      case 0xca: // RRT
      case 0xcb: // EIT
      case 0xcc: // ETT
      case 0xcd: // STT
      case 0xd3: // DCCT
      case 0xd4: // DCCSCT
        debugp("Ignoring ATSC PSIP table 0x%x\n", buf[0]);
        break;
      default:
        debugp("Unknown section 0x%x\n", buf[0]);
        break;
    }
  }

  if (demux_stop(d))
    exit(1);

  infop("PSIP table found\n");
  return 1;
}

void
psip_output_tables(outfmt_t *outfmt, struct vc_record *psip_list)
{
  struct vc_record *vc;

  psip_list = reverse_vc_list(psip_list);
  switch (outfmt->format) {
    case SZAP_FMT:
      verbosep("outputting PSIP SZAP table\n");
      printf("#PSIP section\n");
      for (vc=psip_list; vc; vc=vc->next)
        printf("%s:%d:%s:8192:8192:%d\n",vc->psip_name, vc->cds_ref, psip_modfmt_table[vc->mms_ref & 0x7], vc->prognum);
      break;
    case MYTH_INST_FMT:
      verbosep("outputting PSIP Myth SQL install script\n");
      printf("\n-- PSIP section\n");
      for (vc=psip_list; vc; vc=vc->next) {
        printf("INSERT INTO channel SET sourceid=%d", outfmt->myth_srcid);
        printf(",chanid=%d%02d0%02d", outfmt->myth_srcid, vc->psip_major, vc->psip_minor);
        printf(",channum='%d.%d'", vc->psip_major, vc->psip_minor);
        printf(",callsign='%s'", vc->psip_name);
        printf(",serviceid=%d", vc->prognum);
        printf(",mplexid=(SELECT mplexid FROM dtv_multiplex WHERE frequency=%d);\n", vc->cds_ref);
      }
      break;
    case MYTH_UP_FMT:
      verbosep("outputting PSIP Myth SQL update script\n");
      printf("\n-- PSIP section\n");
      for (vc=psip_list; vc; vc=vc->next)
        printf("UPDATE channel SET callsign='%s',serviceid=%d,mplexid=(SELECT mplexid FROM dtv_multiplex WHERE frequency=%d) WHERE channum='%d.%d' AND sourceid=%d;\n",vc->psip_name, vc->prognum, vc->cds_ref, vc->psip_major, vc->psip_minor, outfmt->myth_srcid);
      break;
    case TXTTABLE_FMT:
      verbosep("outputting PSIP text table\n");
      printf("\nPSIP channels\n");
      printf("   VC     NAME  FREQUENCY   MODULATION PROG\n===========================================\n");
      for (vc=psip_list; vc; vc=vc->next)
        printf("%3d.%-2d %7s %10dhz %7s     %2d\n", vc->psip_major, vc->psip_minor, vc->psip_name, vc->cds_ref, psip_modfmt_table[vc->mms_ref & 0x7], vc->prognum);
      break;
    case CSV_FMT:
      verbosep("outputting PSIP CSV\n");
      printf("VIRTUAL_CHANNEL,NAME,FREQUENCY,MODULATION,PROG\n");
      for (vc=psip_list; vc; vc=vc->next)
        printf("%d.%d,\"%s\",%d,%s,%d\n", vc->psip_major, vc->psip_minor, vc->psip_name, vc->cds_ref, psip_modfmt_table[vc->mms_ref & 0x7], vc->prognum);
      break;
    default:
      warningp("Unsupported PSIP output format\n");
      break;
  }
}

void output_csv(outfmt_t *outfmt, struct cds_table *cds, struct mms_table *mms, struct ntt_table *ntt, struct vcm *vcm_list)
{
  struct vcm *vcm;

  for (vcm = vcm_list; vcm != NULL; vcm=vcm->next) {
    struct vc_record *vc_rec;

    vcm->vc_list = reverse_vc_list(vcm->vc_list);

    printf("VCT_ID:,%d", vcm->vctid);
    if (outfmt->freq != -1)
      printf(",FOUND_AT:,%dhz", outfmt->freq);
    if (vcm->revdesc.valid)
      printf(",version:,%d\n", vcm->revdesc.version);
    else
      printf("\n");

    printf("\n");

    if (outfmt->flags & OUTPUT_VC)
      printf("VIRTUAL_CHANNEL,");
    if (outfmt->flags & OUTPUT_NAME)
      printf("NAME,");
    printf("CARRIER_NUMBER,FREQ,PROGRAM_NUMBER,MODULATION\n");

    for (vc_rec = vcm->vc_list; vc_rec != NULL; vc_rec=vc_rec->next) {
      if (outfmt->flags & OUTPUT_VC)
        printf("%d,", vc_rec->vc);
      if (outfmt->flags & OUTPUT_NAME) {
        // search NTT for ID match
        struct sns_record *sn_rec = ntt->sns_list;
        while (sn_rec && sn_rec->id != vc_rec->id && !vc_rec->application)
          sn_rec=sn_rec->next;
        if (sn_rec) {
          char tmpstr[257];
          snprintf(tmpstr, sn_rec->namelen + 1, "%s\n", sn_rec->name);
          printf("\"%s\",", tmpstr);
        }
      }
      printf("%d,%d,%d,%s\n", vc_rec->cds_ref,cds->cd[vc_rec->cds_ref], vc_rec->prognum, mms->mm[vc_rec->mms_ref].modulation_fmt);
    } // for (vc_rec...
  printf("\n\n\n");
  } // for (vcm...
}

void output_txt(outfmt_t *outfmt, struct cds_table *cds, struct mms_table *mms, struct ntt_table *ntt, struct vcm *vcm_list)
{
  struct vcm *vcm = vcm_list;
  int i;
  for (vcm = vcm_list; vcm != NULL; vcm=vcm->next) {
    struct vc_record *vc_rec;

    vcm->vc_list = reverse_vc_list(vcm->vc_list);
    printf("VCT_ID %d (0x%04x)", vcm->vctid, vcm->vctid);
    if (outfmt->freq != -1)
      printf(" at %dhz", outfmt->freq);
    if (vcm->revdesc.valid)
      printf(", version %d\n", vcm->revdesc.version);
    else
      printf("\n");
    if (outfmt->flags & OUTPUT_VC)
      printf("  VC ");
    printf("%7s %2s", "CD.PROG", "M#");
    if (outfmt->flags & OUTPUT_NAME)
      printf(" NAME\n");
    else
      printf("\n");
    if (outfmt->flags & OUTPUT_VC)
      printf("=====");
    if (outfmt->flags & OUTPUT_NAME)
      printf("=====");
    printf("==========\n");
    for (vc_rec = vcm->vc_list; vc_rec != NULL; vc_rec=vc_rec->next) {
      if (outfmt->flags & OUTPUT_VC)
        printf("%4d ", vc_rec->vc);
      printf("%4d.%-2d %2d", vc_rec->cds_ref, vc_rec->prognum, vc_rec->mms_ref);
      if (outfmt->flags & OUTPUT_NAME) {
        // search NTT for ID match
        struct sns_record *sn_rec = ntt->sns_list;
        while (sn_rec && sn_rec->id != vc_rec->id && !vc_rec->application)
          sn_rec=sn_rec->next;
        if (sn_rec) {
          char tmpstr[257];
          snprintf(tmpstr, sn_rec->namelen + 1, "%s\n", sn_rec->name);
          printf(" %s\n", tmpstr);
        } else {
          printf("\n");
        }
      } else {
        printf("\n");
      }
    } // for (vc_rec...
    printf("\n\n\n");
  } // for (vcm...

  // print cds
  printf(" CD    FREQUENCY\n");
  printf("================\n");
  for (i=0; i< 256; i++) {
    if (cds->written[i])
      printf("%3d %10dhz\n", i, cds->cd[i]);
  }

  // print mms
  printf("\n\n\nM#     MODE\n");
  printf("===========\n");
  for (i=0; i< 256; i++) {
    if (mms->written[i])
      printf("%2d  %7s\n", i, mms->mm[i].modulation_fmt);
  }
}

void output_szap(outfmt_t *outfmt, struct cds_table *cds, struct mms_table *mms, struct ntt_table *ntt, struct vcm *vcm_list)
{
  struct vcm *vcm;

  for (vcm = vcm_list; vcm != NULL; vcm=vcm->next) {
    struct vc_record *vc_rec;

    vcm->vc_list = reverse_vc_list(vcm->vc_list);

    // comments
    printf("#VCT_ID %d (0x%04x)", vcm->vctid, vcm->vctid);
    if (outfmt->freq != -1)
      printf(" at %dhz", outfmt->freq);
    if (vcm->revdesc.valid)
      printf(", version %d\n", vcm->revdesc.version);
    else
      printf("\n");

    for (vc_rec = vcm->vc_list; vc_rec != NULL; vc_rec=vc_rec->next) {
      if (outfmt->flags & OUTPUT_FREQ)
        printf("%d-", outfmt->freq);
      if (outfmt->flags & OUTPUT_VC)
        printf("%d", vc_rec->vc);
      if ((outfmt->flags & OUTPUT_VC_NAME) == OUTPUT_VC_NAME)
        printf("-");
      if (outfmt->flags & OUTPUT_NAME) {
        // search NTT for ID match
        struct sns_record *sn_rec = ntt->sns_list;
        while (sn_rec && sn_rec->id != vc_rec->id && !vc_rec->application)
          sn_rec=sn_rec->next;
        if (sn_rec) {
          char tmpstr[257];
          snprintf(tmpstr, sn_rec->namelen + 1, "%s\n", sn_rec->name);
          printf("%s", tmpstr);
        }
      }
      printf(":%d:%s:8192:8192:%d\n", cds->cd[vc_rec->cds_ref], mms->mm[vc_rec->mms_ref].modulation_fmt, vc_rec->prognum);
    } // for (vc_rec...
    printf("#end VCT_ID %d (0x%04x)\n", vcm->vctid, vcm->vctid);
  } // for (vcm...
}

void output_mythsql_setup(outfmt_t *outfmt, struct cds_table *cds, struct mms_table *mms, struct ntt_table *ntt, struct vcm *vcm_list)
{
  struct vcm *vcm;
  int i;

  // direct map CDS to dtv_multiplex table
  printf("-- WARNING: The following line clears out dtv_multiplex table\n");
  printf("DELETE FROM dtv_multiplex WHERE sourceid=%d;\n", outfmt->myth_srcid);
  printf("-- Frequency table from SCTE-65 CDS\n");
  for (i=0; i< 256; i++) {
    if (cds->written[i])
      printf("INSERT INTO dtv_multiplex SET sistandard='atsc',mplexid=%d,frequency=%d,modulation='qam_256',sourceid=%d;\n", i, cds->cd[i], outfmt->myth_srcid);
  }

  printf("\n\n\n");

  for (vcm = vcm_list; vcm != NULL; vcm=vcm->next) {
    struct vc_record *vc_rec;

    vcm->vc_list = reverse_vc_list(vcm->vc_list);
    printf("-- WARNING: The following line clears out channel table\n");
    printf("DELETE FROM channel WHERE sourceid=%d;\n", outfmt->myth_srcid);
    printf("-- VCT_ID %d (0x%04x)", vcm->vctid, vcm->vctid);
    if (outfmt->freq != -1)
      printf(" at %dhz", outfmt->freq);
    if (vcm->revdesc.valid)
      printf(", version %d\n", vcm->revdesc.version);
    else
      printf("\n");

    for (vc_rec = vcm->vc_list; vc_rec != NULL; vc_rec=vc_rec->next) {
      printf("INSERT INTO channel");
      printf(" SET mplexid=%d", vc_rec->cds_ref);
      printf(",serviceid=%d", vc_rec->prognum);
      printf(",freqid=%d", vc_rec->vc);
      printf(",chanid=%d%03d", outfmt->myth_srcid, vc_rec->vc);
      printf(",sourceid=%d", outfmt->myth_srcid);

      if (outfmt->flags & OUTPUT_VC)
        printf(",channum='%d'", vc_rec->vc);
      else
        printf(",channum='%d.%d'", vc_rec->cds_ref, vc_rec->prognum);

      // if VC doesn't go into channum, put it in name
      if (!(outfmt->flags & OUTPUT_VC) && outfmt->flags != OUTPUT_VC_NAME)
        printf(",name='%d'", vc_rec->vc);

      if (outfmt->flags & OUTPUT_NAME) {
        // search NTT for ID match
        struct sns_record *sn_rec = ntt->sns_list;
        while (sn_rec && sn_rec->id != vc_rec->id && !vc_rec->application)
          sn_rec=sn_rec->next;
        if (sn_rec) {
          char tmpstr[257];
          snprintf(tmpstr, sn_rec->namelen + 1, "%s\n", sn_rec->name);
          printf(",callsign='%s'", tmpstr);
        }
      }
      printf(";\n");
      if (0==strcmp("QAM_64", mms->mm[vc_rec->mms_ref].modulation_fmt))
        printf("UPDATE dtv_multiplex SET modulation='qam_64' WHERE mplexid=%d;\n",vc_rec->cds_ref);
      else if (0==strcmp("16VSB", mms->mm[vc_rec->mms_ref].modulation_fmt))
        printf("UPDATE dtv_multiplex SET modulation='16vsb' WHERE mplexid=%d;\n",vc_rec->cds_ref);
      else if (0==strcmp("8VSB", mms->mm[vc_rec->mms_ref].modulation_fmt))
        printf("UPDATE dtv_multiplex SET modulation='8vsb' WHERE mplexid=%d;\n",vc_rec->cds_ref);
    } // for (vc_rec...
    printf("\n\n\n");
  } // for (vcm...

}

void output_mythsql_update(outfmt_t *outfmt, struct cds_table *cds, struct mms_table *mms, struct ntt_table *ntt, struct vcm *vcm_list)
{
  struct vcm *vcm;
  int i;

  // Assume dtv_multiplex is already setup and dive straight into VC map 

  for (vcm = vcm_list; vcm != NULL; vcm=vcm->next) {
    struct vc_record *vc_rec;

    vcm->vc_list = reverse_vc_list(vcm->vc_list);
    printf("-- VCT_ID %d (0x%04x)", vcm->vctid, vcm->vctid);
    if (outfmt->freq != -1)
      printf(" at %dhz", outfmt->freq);
    if (vcm->revdesc.valid)
      printf(", version %d\n", vcm->revdesc.version);
    else
      printf("\n");

    for (vc_rec = vcm->vc_list; vc_rec != NULL; vc_rec=vc_rec->next) {
      printf("UPDATE channel");
      printf(" SET mplexid=%d", vc_rec->cds_ref);
      printf(",serviceid=%d", vc_rec->prognum);

      if (outfmt->flags & OUTPUT_VC)
        printf(",channum='%d'", vc_rec->vc);
      else
        printf(",channum='%d.%d'", vc_rec->cds_ref, vc_rec->prognum);

      // if VC doesn't go into channum, put it in name
      if (!(outfmt->flags & OUTPUT_VC) && outfmt->flags != OUTPUT_VC_NAME)
        printf(",name='%d'", vc_rec->vc);

      if (outfmt->flags & OUTPUT_NAME) {
        // search NTT for ID match
        struct sns_record *sn_rec = ntt->sns_list;
        while (sn_rec && sn_rec->id != vc_rec->id && !vc_rec->application)
          sn_rec=sn_rec->next;
        if (sn_rec) {
          char tmpstr[257];
          snprintf(tmpstr, sn_rec->namelen + 1, "%s\n", sn_rec->name);
          printf(",callsign='%s'", tmpstr);
        }
      }
      printf(" WHERE freqid=%d AND sourceid=%d;\n", vc_rec->vc, outfmt->myth_srcid);
      if (0==strcmp("QAM_64", mms->mm[vc_rec->mms_ref].modulation_fmt))
        printf("UPDATE dtv_multiplex SET modulation='qam_64' where mplexid=%d;\n",vc_rec->cds_ref);
      else if (0==strcmp("16VSB", mms->mm[vc_rec->mms_ref].modulation_fmt))
        printf("UPDATE dtv_multiplex SET modulation='16vsb' where mplexid=%d;\n",vc_rec->cds_ref);
      else if (0==strcmp("8VSB", mms->mm[vc_rec->mms_ref].modulation_fmt))
        printf("UPDATE dtv_multiplex SET modulation='8vsb' where mplexid=%d;\n",vc_rec->cds_ref);
    } // for (vc_rec...
    printf("\n\n\n");
  } // for (vcm...
}

void output_tables(outfmt_t *outfmt, struct cds_table *cds, struct mms_table *mms, struct ntt_table *ntt, struct vcm *vcm_list)
{
  switch (outfmt->format) {
    case SZAP_FMT:
      verbosep("outputting SZAP table\n");
      output_szap(outfmt, cds, mms, ntt, vcm_list);
      break;
    case MYTH_INST_FMT:
      verbosep("outputting Myth SQL install script\n");
      output_mythsql_setup(outfmt, cds, mms, ntt, vcm_list);
      break;
    case MYTH_UP_FMT:
      verbosep("outputting Myth SQL update script\n");
      output_mythsql_update(outfmt, cds, mms, ntt, vcm_list);
      break;
    case TXTTABLE_FMT:
      verbosep("outputting text table\n");
      output_txt(outfmt, cds, mms, ntt, vcm_list);
      break;
    case CSV_FMT:
      verbosep("outputting CSV\n");
      output_csv(outfmt, cds, mms, ntt, vcm_list);
      break;
    default:
      warningp("Unknown output format specified\n");
      break;
  }
}

/* 
 * allocates memory for a new transponder node, copies data, appends to list
 *   freq = parameters to add
 *   mod  = parameters to add
 *   t    = pointer to node to append to (if NULL don't append)
 * returns pointer to new node
 */
static struct transponder*
alloc_transponder (int freq, char* mod, struct transponder *t)
{
  struct transponder *new_t = calloc(1, sizeof(*new_t));

  new_t->frequency = freq;
  new_t->modulation = strdup(mod);
  if (NULL != t)
    t->next = new_t;
  return new_t;
}

/* 
 * reads tuning data from dvb-apps style initial turning file, appends to list
 *   itfname = name of file
 *   t_list  = pointer to node to append (overwrite with new head if NULL)
 * returns pointer to new tail
 */
static struct transponder* read_itfile (const char *itfname, struct transponder **t_list)
{
  FILE *it_fp;
  unsigned int f, sr;
  char buf[200];
  char mod[20];
  struct transponder *t=*t_list;

  it_fp = fopen(itfname, "r");
  if (!it_fp) {
    perror(itfname);
    return;
  }
  while (fgets(buf, sizeof(buf), it_fp)) {
    if (buf[0] == '#' || buf[0] == '\n')
      ;
    else if (sscanf(buf, "A %u %7s\n", &f,mod) == 2) {
      verbosedebugp("adding %dhz %s to transponder list\n", f, mod);
      t= alloc_transponder(f, mod, t);
      if (NULL == *t_list)
        *t_list = t;
    } else
      errorp("cannot parse '%s'\n", buf);
  }
  return t;
}

// S-VCT's CDS_reference is 8 bits; there's a problem if cds>256entries
int parse_cds(unsigned char *buf, int len, struct cds_table *cds)
{
  unsigned char first_index = buf[4];
  unsigned char recs        = buf[5];
  int index = first_index;
  int i, n;

  verbosep("Parsing CDS\n");
  verbosedebugp("first_index = %d, %d recs\n", first_index, recs);
  n=7;

  for (i=0; i< recs; i++) {
    int j;
    int num_carriers = buf[n];
    int spacing_unit = (buf[n+1] & 0x80) ? 125000 : 10000;
    int freq_spacing = (((buf[n+1] & 0x3f) << 8) | buf[n+2]) * spacing_unit;
    int freq_unit    = (buf[n+3] & 0x80) ? 125000 : 10000;
    int freq   = (((buf[n+3] & 0x3f) << 8) | buf[n+4]) * freq_unit;

    n +=5;
    for (j=0; j< num_carriers; j++) {
      cds->cd[index] = freq;
      cds->written[index] = 1;
      debugp("rf channel %d = %dhz\n", index, freq);
      index++;
      if (index >  255) fatalp("CDS too big; noncompliant datastream?\n");
      freq += freq_spacing;
    }
    // skip CD descriptors (should only be stuffing here anyway)
    n+=(buf[n]+1);
  }

  // parse NIT descriptors
  while (n < (len - 4 + 3)) { // 4 bytes CRC, 3 bytes header
    unsigned char d_tag = buf[n++];
    unsigned char d_len = buf[n++];

    if (d_tag == 0x93 && d_len >= 3) {
      unsigned char thissec = buf[n+1];
      unsigned char lastsec = buf[n+2];
      int i;

      cds->revdesc.valid = 1;
      cds->revdesc.version = buf[n] & 0x1f;
      debugp("CDS table version %d\n", cds->revdesc.version);
      debugp("CDS table section %d/%d\n", thissec, lastsec);
      lastsec++;
      if (NULL == cds->revdesc.parts)
        cds->revdesc.parts = calloc(lastsec,sizeof(*cds->revdesc.parts));
      cds->revdesc.parts[thissec]=1;
      for (i=0; i<lastsec && cds->revdesc.parts[i]; i++) {;} // check for unseen parts
      if (i == lastsec)
        return 1;
      else
        return 0;
    } else if (d_tag == 0x80) {
      debugp("ignoring CDS stuffing descriptor\n");
    } else {
      warningp("Unknown CDS descriptor/len 0x%x %d\n", d_tag, d_len);
    }
    n+=d_len;
  }
  // if no revision descriptor, then we must be done
  return 1;
}

int parse_mms(unsigned char *buf, int len, struct mms_table *mms)
{
  int first_index = buf[4];
  int recs        = buf[5];
  int i, n;

  verbosep("Parsing MMS\n");
  verbosedebugp("first_index = %d, %d recs\n", first_index, recs);
  n=7;

  for (i=0; i< recs; i++) {
    int p = first_index + i;
 
    if (p >  255) fatalp("MMS too big; noncompliant datastream?\n");
    mms->mm[p].modulation_fmt = scte_modfmt_table[buf[n+1] & 0x1f];
    mms->written[p] = 1;
    debugp("MMS index %d = %s\n", p, mms->mm[p].modulation_fmt);
    n +=6;
    // skip over any descriptors
    // should only be stuffing and revision here anyway
    n+=(buf[n]+1);
  }

  // parse NIT descriptors
  while (n < (len - 4 + 3)) { // 4 bytes CRC, 3 bytes header
    unsigned char d_tag = buf[n++];
    unsigned char d_len = buf[n++];

    if (d_tag == 0x93 && d_len >= 3) {
      unsigned char thissec = buf[n+1];
      unsigned char lastsec = buf[n+2];
      int i;

      mms->revdesc.valid=1;
      mms->revdesc.version=buf[n] & 0x1f;
      debugp("MMS table version %d\n", mms->revdesc.version);
      debugp("MMS table section %d/%d\n", thissec, lastsec);
      lastsec++;
      if (NULL == mms->revdesc.parts)
        mms->revdesc.parts = calloc(lastsec,sizeof(*mms->revdesc.parts));
      mms->revdesc.parts[thissec]=1;
      for (i=0; i<lastsec && mms->revdesc.parts[i]; i++) {;} // check for unseen parts
      if (i == lastsec)
        return 1;
      else
        return 0;
    } else if (d_tag == 0x80) {
      debugp("ignoring MMS stuffing descriptor\n");
    } else {
      warningp("Unknown MMS descriptor/len 0x%x %d\n", d_tag, d_len);
    }
    n+=d_len;
  }
  // if no revision descriptor, then we must be done
  return 1;
}

int parse_ntt(unsigned char *buf, int len, struct ntt_table *ntt)
{
  char table_subtype = buf[7] & 0xf;
  char recs;
  char tmpstr[257];
  int i, n;

  verbosep("Parsing NTT\n");
  for (i=0; i<3; i++)
    tmpstr[i] = buf[i+4];
  tmpstr[i] = '\0';
  debugp("ISO_639_language_code = %s\n", tmpstr);

  if (table_subtype != 6) {
    warningp("Invalid NTT table_subtype: got %d, expect 6\n", table_subtype);
    return 0;
  }

  // begin SNS
  n=8;
  recs = buf[n++];
  for (i=0; i< recs; i++) {
    struct sns_record *sn = calloc(1, sizeof(*sn));

    sn->app_type = buf[n++] & 0x80;
    sn->id = (buf[n] << 8) | buf[n+1];
    n+=2;
#ifndef STRICT_SCTE65
    sn->extra_comcast_junk = (buf[n] << 8) | buf[n+1];
    n+=2;
    verbosedebugp("extra comcast junk: 0x%04x\n", sn->extra_comcast_junk);
#endif
    sn->namelen = buf[n++];
    memcpy(sn->name, &buf[n], sn->namelen);
    memcpy(tmpstr, &buf[n], sn->namelen);
    tmpstr[sn->namelen] = '\0';
    debugp("%s_ID: 0x%04x=%s\n", sn->app_type ? "App" : "Src", sn->id, tmpstr);
    n+=sn->namelen;
    sn->descriptor_count = buf[n++];
    memcpy(sn->descriptors, &buf[n], sn->descriptor_count);
    n+=sn->descriptor_count;
    //push to head of list
    sn->next = ntt->sns_list;
    ntt->sns_list = sn;
  }

  // parse NTT descriptors
  while (n < (len - 4 + 3)) { // 4 bytes CRC, 3 bytes header
    unsigned char d_tag = buf[n++];
    unsigned char d_len = buf[n++];

    if (d_tag == 0x93 && d_len >= 3) {
      unsigned char thissec = buf[n+1];
      unsigned char lastsec = buf[n+2];
      int i;

      ntt->revdesc.valid = 1;
      ntt->revdesc.version = buf[n] & 0x1f;
      debugp("NTT table version %d\n", ntt->revdesc.version);
      debugp("NTT table section %d/%d\n", thissec, lastsec);
      lastsec++;
      if (NULL == ntt->revdesc.parts)
        ntt->revdesc.parts = calloc(lastsec,sizeof(*ntt->revdesc.parts));
      ntt->revdesc.parts[thissec]=1;
      for (i=0; i<lastsec && ntt->revdesc.parts[i]; i++) {;} // check for unseen parts
      if (i == lastsec)
        return 1;
      else
        return 0;
    } else if (d_tag == 0x80) {
      debugp("ignoring NTT stuffing descriptor\n");
    } else {
      warningp("Unknown NTT descriptor/len 0x%x %d\n", d_tag, d_len);
    }
    n+=d_len;
  }
  // if no revision descriptor, then we must be done
  return 1;
}

// fills vc_rec from buf, returns number of bytes processed
int read_vc(unsigned char *buf, struct vc_record *vc_rec, unsigned char desc_inc) {
  int n;
  vc_rec->vc             = ((buf[0] & 0xf) << 8) | buf[1];
  vc_rec->application    = buf[2] & 0x80;
  vc_rec->path_select    = buf[2] & 0x20;
  vc_rec->transport_type = buf[2] & 0x10;
  vc_rec->channel_type   = buf[2] & 0xf;
  vc_rec->id             = (buf[3] << 8) | buf[4];

  debugp("vc %4d, %s_ID: 0x%04d, %s\n", vc_rec->vc, vc_rec->application ? "app" : "src", vc_rec->id, vc_rec->channel_type ? "hidden/reserved" : "normal"); 
  n=5;
  if (vc_rec->transport_type == 0) {
    vc_rec->cds_ref = buf[n++];
    vc_rec->prognum = (buf[n] << 8) | buf[n+1];
    n+=2;
    vc_rec->mms_ref = buf[n++];
    debugp("CDS_ref=%d, program=%d, MMS_ref=%d\n", vc_rec->cds_ref, vc_rec->prognum, vc_rec->mms_ref);
  } else {
    vc_rec->cds_ref   = buf[n++];
    vc_rec->scrambled = buf[n] & 0x80;
    vc_rec->video_std = buf[n++] & 0xf;
    n+=2; // 2 bytes of zero
    debugp("CDS_ref=%d, scrambled=%c, vid_std=%d\n", vc_rec->cds_ref, vc_rec->scrambled ? 'Y' : 'N', vc_rec->video_std);
  }

  if (desc_inc) {
    vc_rec->desc_cnt = buf[n++];
    memcpy(vc_rec->desc, &buf[n], vc_rec->desc_cnt);
    n += vc_rec->desc_cnt;
  }

  return n;
}
      
int parse_svct(unsigned char *buf, int len, struct vcm **vcmlist, int vctidfilter)
{
  unsigned char descriptors_included, splice, vc_recs;
  unsigned int activation_time, vctid;
  struct vcm *vcm = *vcmlist;
  int i, n=4;
  
  enum svct_table_subtype {
    SCTE_SVCT_VCM = 0x00,
    SCTE_SVCT_DCM = 0x01,
    SCTE_SVCT_ICM = 0x02
  } table_subtype;

  verbosep("Parsing SVCT\n");
  table_subtype = buf[n++] & 0xf;
  vctid = (buf[n] << 8) | buf[n+1];
  n+=2;

  if (table_subtype > 2)
    warningp("Invalid SVCT subtype %d\n", table_subtype);

  // filter on VCMs and, if requested, matching VCT_IDs
  if (table_subtype != SCTE_SVCT_VCM || (vctidfilter != -1 && vctidfilter!=vctid))
    return 0;

  // walk the list for matching vctid
  while (vcm && vcm->vctid != vctid) {
    vcm = vcm->next;
  }
  // add new vct/vcm to head if not found
  if (NULL == vcm) {
    struct vcm *new_vcm = calloc(1, sizeof(*new_vcm));

    new_vcm->next = *vcmlist;
    new_vcm->vctid = vctid;
    *vcmlist = vcm = new_vcm;
    verbosedebugp("adding to list new VCT_ID=0x%04x\n", vctid);
  }

  // now process the rest of the VCT we got from the demod (VCM starts here)
  descriptors_included = buf[n++] & 0x20;
  splice = buf[n++] & 0x80;
  activation_time = (buf[n]<<24) | (buf[n+1]<<16) | (buf[n+2]<<8) | buf[n+3];
  n +=4;
  vc_recs = buf[n++];
  debugp("VCT_ID =0x%04x, descriptors_incl=%c, splice=%c, activation_time=%d, recs=%d\n", vctid, descriptors_included ? 'Y' : 'N', splice ? 'Y' : 'N', activation_time, vc_recs);
  for (i=0; i< vc_recs; i++) {
    struct vc_record demodvc_rec, *vc_rec;

    n+=read_vc(&buf[n], &demodvc_rec, descriptors_included);

    // done reading record, now search for vc in vcm's vc list
    vc_rec = vcm->vc_list;
    while (vc_rec && demodvc_rec.vc != vc_rec->vc)
      vc_rec = vc_rec->next;

    // if not found, add it to head; otherwise, do nothing
    if (NULL == vc_rec) {
      struct vc_record *newvc_rec = calloc(1, sizeof(*newvc_rec));

      memcpy(newvc_rec, &demodvc_rec, sizeof(demodvc_rec));
      newvc_rec->next = vcm->vc_list;
      vcm->vc_list = newvc_rec;
    }
  }

  // parse S-VCT descriptors
  while (n < (len - 4 + 3)) { // 4 bytes CRC, 3 bytes header
    unsigned char d_tag = buf[n++];
    unsigned char d_len = buf[n++];

    if (d_tag == 0x93 && d_len >= 3) {
      unsigned char thissec = buf[n+1];
      unsigned char lastsec = buf[n+2];
      int i;

      vcm->revdesc.valid = 1;
      vcm->revdesc.version = buf[n] & 0x1f;
      debugp("VCM VCTID 0x%04x table version %d, section %d/%d\n", vctid, vcm->revdesc.version, thissec, lastsec);
      lastsec++;
      if (NULL == vcm->revdesc.parts)
        vcm->revdesc.parts = calloc(lastsec,sizeof(*vcm->revdesc.parts));
      vcm->revdesc.parts[thissec]=1;
      for (i=0; i<lastsec && vcm->revdesc.parts[i]; i++) {;} // check for unseen parts
      if (i == lastsec)
        return 1;
      else
        return 0;
    } else if (d_tag == 0x80) {
      debugp("ignoring S-VCT stuffing descriptor\n");
    } else {
      warningp("Unknown S-VCT descriptor/len 0x%x %d\n", d_tag, d_len);
    }
    n+=d_len;
  }
}

int scte_read_and_parse(struct dmx_desc *d, outfmt_t *outfmt, int vctid)
{
  unsigned char buf[4096];
  struct cds_table cds;
  // 8bit S-VCT MMS_reference means...
  struct mms_table mms;
  // infinite sized SNS and VCM, so use linked lists
  struct ntt_table ntt;
  struct vcm *vcm_list= NULL;
  char vcmdone;
  int n, section_len;
  time_t start_time;
  enum {
    NOTHING_DONE = 0x0,
    FOUND_PID    = 0x1,
    CDS_DONE     = 0x2,
    MMS_DONE     = 0x4,
    NIT_DONE     = CDS_DONE | MMS_DONE,
    NTT_DONE     = 0x8,
    SVCT_DONE    = 0x10,
    ALL_BUT_NTT  = (FOUND_PID | NIT_DONE | SVCT_DONE),
    ALL_DONE     = (ALL_BUT_NTT | NTT_DONE)
  } done=NOTHING_DONE;

  if (demux_start(d))
    exit(1);

  memset(&cds, 0, sizeof(cds) );
  memset(&mms, 0, sizeof(mms) );
  memset(&ntt, 0, sizeof(ntt) );
  time(&start_time);

  while (done != ALL_DONE && !((~outfmt->flags & OUTPUT_NAME) && done == ALL_BUT_NTT)) {
    if (time(NULL) - start_time > SVCT_PERIOD)
      done |= SVCT_DONE;

    if ((n=demux_read(d, buf, sizeof(buf))) == -1) { // got timeout
      infop("PID 0x1ffc timeout\n");
      return 0;
    } else if (n < 0) { // got EOF
      break;
    }

    if (NOTHING_DONE == done) {
      infop("PID 0x1ffc found\n");
      if (OUTPUT_NOTHING == outfmt->flags) {
        return 1;
      } else {
        infop("Collecting data (may take up to 2 minutes)\n");
        done |= FOUND_PID;
      }
    }

    section_len = ((buf[1] & 0xf) << 8) | buf[2];
    verbosedebugp("section 0x%x, length %d\n", buf[0], section_len);
    if (section_len != (n-3)) {
      warningp("bad section length (expect %d, got %d)\n", n-3, section_len);
      continue;
    }

    switch (buf[0]) {
      case 0xc2:
        if (section_len < 8) {
          warningp("short NIT, got %d bytes (expected >7)\n", section_len);
        } else if (1 == (buf[6] & 0xf)) {
          done |=parse_cds(buf, section_len, &cds) ? CDS_DONE : 0;
        } else if (2 == (buf[6] & 0xf)) {
          done |=parse_mms(buf, section_len, &mms) ? MMS_DONE : 0;
        } else {
          warningp("NIT tbl_subtype=%s\n", (buf[6] & 0xf) ? "RSVD" : "INVLD");
        }
        break;
      case 0xc3:
        done |= parse_ntt(buf, section_len, &ntt) ? NTT_DONE : 0;
        break;
      case 0xc4:
        vcmdone = parse_svct(buf, section_len, &vcm_list, vctid);
        // we're done when vcm is done if filtering vctid; else timeout
        if (vctid != -1)
          done |= vcmdone ? SVCT_DONE : 0;
        break;
      case 0xc5:
        if (section_len >=10) {
          time_t stt = ((buf[5]<<24) | (buf[6]<<16) | (buf[7]<<8) | buf[8]) + 315964800;
          infop("System Time Table thinks it is %s", ctime(&stt));
        }
        break;
      default:
        debugp("Unknown section 0x%x\n", buf[0]);
        break;
    }
  }

  if (demux_stop(d))
    exit(1);

  output_tables(outfmt, &cds, &mms, &ntt, vcm_list);

  // dealloc memory
  while (ntt.sns_list) {
    struct sns_record *sns_next = ntt.sns_list->next;
    free (ntt.sns_list);
    ntt.sns_list=sns_next;
  }
  while (vcm_list) {
    struct vcm *vcm_next = vcm_list->next;
    while (vcm_list->vc_list) {
      struct vc_record *vc_next = vcm_list->vc_list->next;
      free(vcm_list->vc_list);
      vcm_list->vc_list = vc_next;
    }
    free(vcm_list);
    vcm_list=vcm_next;
  }

  if (outfmt->flags & OUTPUT_FREQ)
    return 0;
  else
    return 1;
}

static void
usage (FILE * output, const char *myname)
{
  fprintf (output,
	   "Desc:  In-band scan for SCTE-65 tables\n"
	   "Usage: %s [OPTION]... [FILE]...\n"
	   "	FILE 	dvb-apps style initial tuning file(s)\n"
#ifndef WIN32
	   "	-A N	use DVB /dev/dvb/adapterN/ (default N=0)\n"
	   "	-D N	use DVB /dev/dvb/adapter?/demuxN (default N=0)\n"
	   "	-F N	use DVB /dev/dvb/adapter?/frontendN (default N=0)\n"
#endif
#ifdef HDHR
	   "	-H N,t	use HDHomerun id=N, tuner=t (any HDHR=FFFFFFFF, default tuner=0)\n"
#endif
	   "	-i f	input ts packets from file f ('-' for stdin)\n"
	   "	-t f,m	scan at (f)hertz, m={QAM64,8VSB,etc} modulation (default=QAM256)\n"
	   "	-s N	N second timeouts (default N=5)\n"
	   "	-V N	only output VCT_ID=N or -1 for all (default N=-1)\n"
	   "	-n N	name: 0=no output, 1=Vchannel#, 2=callsign, 3=both (default N=3)\n"
	   "	-k	keep going (scan all freqs for all SCTE-65 tables, not just 1st)\n"
	   "	-p	also scan for ATSC PSIP data\n"
	   "	-f N	output format. Myth scripts, specify sourceid by adding a comma\n"
           "		e.g. '-f 4,3' for Myth update, sourceid=3 (default sourceid=1)\n"
	   "				0=generic text table (default)\n"
	   "				1=szap style channels.conf\n"
	   "				2=CSV (Comma Separated Values)\n"
	   "				3=MythTV SQL install script\n"
	   "				4=MythTV SQL update script\n"
	   "	-v/q	verbose/quiet (repeat for more)\n"
	   "	-h	display this help\n", myname);
}

int
main (int argc, char **argv)
{
  char dmx_devname[PATH_MAX], fe_devname[PATH_MAX];
  char *comma, *hdhr=NULL, *infilename=NULL;
  char scte_stop = 0, psip = 0;
  int adapternum = 0, demuxnum = 0, frontendnum = 0;
  int timeout=5, vctid=-1;
  struct tuner_desc mytuner;
  struct dmx_desc scte_dmx, psip_dmx;
  int opt;
  int rdcnt, scte_pid = 0x1ffc, psip_pid = 0x1ffb;
  struct transponder *t_list = NULL;
  struct transponder *t_tail;
  struct vc_record *psip_list = NULL;
  int tuner_locked;
  outfmt_t outfmt = {OUTPUT_VC_NAME, TXTTABLE_FMT, -1, 1};
  tuners_t usetuner;

  while ((opt = getopt (argc, argv, "A:D:F:H:i:t:s:V:n:f:qpkvh")) != -1)
    {
      switch (opt)
	{
#ifndef WIN32
	case 'A':
	  adapternum = atoi (optarg);
	  break;
	case 'D':
	  demuxnum = atoi (optarg);
	  break;
	case 'F':
	  frontendnum = atoi (optarg);
	  break;
#endif
#ifdef HDHR
	case 'H':
          comma = strchr(optarg,',');
          if (comma) {
            *comma++='\0';
            set_hdhr_tuner_number( strtol(comma, NULL, 0) );
          }
	  hdhr = strdup(optarg);
	  break;
#endif
	case 'i':
	  infilename = strdup(optarg);
	  break;
	case 't':
          {
            struct transponder *t = t_list;

            comma = strchr(optarg,',');
            if (comma)
	      ++comma;
            
            // find the tail
            while (t && t->next)
              t = t->next;
            t = alloc_transponder(strtol(optarg, NULL, 0), comma ? comma : "QAM256", t);
            if (NULL == t_list)
              t_list = t;
          }
	  break;
	case 's':
          timeout = strtol(optarg, NULL, 0);
          verbosep("timeout set to %d seconds\n", timeout);
	  break;
	case 'V':
	  vctid = strtol(optarg, NULL, 0);
          verbosep("filtering on VCT_ID = %d (0x%04x)\n", vctid, vctid);
	  break;
	case 'f':
          outfmt.format = strtol(optarg, NULL, 0);
          if (outfmt.format >= INVALID_FMT) {
            warningp("Invalid format specified, defaulting to text table\n");
            outfmt.format = TXTTABLE_FMT;
          }
          comma = strchr(optarg,',');
          if (comma)
            outfmt.myth_srcid = strtol(++comma, NULL, 0);;
	  break;
	case 'n':
          outfmt.flags &= ~OUTPUT_VC_NAME;
          outfmt.flags |= strtol(optarg, NULL, 0);
	  break;
	case 'k':
          outfmt.flags |= OUTPUT_FREQ;
	  break;
	case 'p':
	  psip = 1;
	  break;
	case 'v':
	  verbosity++;
	  break;
	case 'q':
	  verbosity--;
	  break;
	case 'h':
	  usage (stdout, argv[0]);
	  exit (0);
	default:
	  usage (stderr, argv[0]);
	  exit (1);
	}
    }

  // find the tail
  for ( t_tail = t_list; (t_tail && t_tail->next); t_tail = t_tail->next);
    
  // append initial tuning file; initialize list head if necessary 
  while (optind < argc)
      t_tail = read_itfile(argv[optind++], (t_list) ? &t_tail : &t_list);

  if (infilename) {
    usetuner = INFILE;
    snprintf (dmx_devname, sizeof dmx_devname, "%s", infilename);
    snprintf (fe_devname, sizeof fe_devname, "%s", infilename);
  } else if (hdhr) {
    usetuner = HDHOMERUN;
    snprintf (dmx_devname, sizeof dmx_devname, "%s", hdhr);
    snprintf (fe_devname, sizeof fe_devname, "%s", hdhr);
  } else {
#ifdef WIN32
    usetuner = WINDOWS_BDA;
#else
    usetuner = LINUX_DVB;
    snprintf (dmx_devname, sizeof dmx_devname,
	      "/dev/dvb/adapter%d/demux%d", adapternum, demuxnum);
    snprintf (fe_devname, sizeof fe_devname,
	      "/dev/dvb/adapter%d/frontend%d", adapternum, frontendnum);
#endif
  }

  verbosep("Demux device: %s, type=%d\n", dmx_devname, usetuner);
  verbosep("Tuner device: %s, type=%d\n", fe_devname, usetuner);
  if (demux_open(dmx_devname, 0x1ffc, timeout, &scte_dmx, usetuner))
    exit (-1);

  if (psip) {
    verbosep("Adding PSIP demux\n");
    if (demux_open(dmx_devname, 0x1ffb, timeout, &psip_dmx, usetuner))
      exit(-1);
  }

  if (t_list) {
    debugp("Opening tuner %s (type %d)\n", fe_devname, usetuner);
    if (tuner_open(fe_devname, &mytuner, usetuner) )
      exit(-1);

    while (t_list) {
      infop("tuning %dhz", t_list->frequency);
      if (tuner_tune(&mytuner, t_list) )
        exit (-1);

      tuner_locked = 0;
      for (rdcnt=0; rdcnt < timeout && !tuner_locked; rdcnt++) {   
          infop(".");
          usleep (1000000);
          tuner_locked = tuner_checklock(&mytuner);
      }
      if (!tuner_locked) {   
          infop("no lock\n");
          t_list = t_list->next;
          continue;
      }
      infop("locked...");
      outfmt.freq = t_list->frequency;
      //TODO:
      //find_pids(timeout, &scte_pid, &psip_pid);
      if (scte_pid && !scte_stop)
        scte_stop = scte_read_and_parse(&scte_dmx, &outfmt, vctid);
      if (psip && psip_pid) {
        verbosep("checking for psip...");
        psip_read_and_parse(&psip_dmx, &outfmt, &psip_list);
      }
      if (scte_stop && !psip)
        break;

      t_list = t_list->next;
    } // while
  } else {
  // only scan current transponder without tuning frontend
    outfmt.freq = -1;
    scte_read_and_parse(&scte_dmx, &outfmt, vctid);
    if (psip) {
      verbosep("checking for psip...");
      psip_read_and_parse(&psip_dmx, &outfmt, &psip_list);
    }
  }
  if (psip)
    psip_output_tables(&outfmt, psip_list);
}
