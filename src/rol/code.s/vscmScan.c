
/* vscmScan.c */

#if defined(Linux_vme)

#include <stdio.h>
#include "vscmLib.h"
#include "jvme.h"


#define LSWAP(x)        ((((x) & 0x000000ff) << 24) | \
                         (((x) & 0x0000ff00) <<  8) | \
                         (((x) & 0x00ff0000) >>  8) | \
                         (((x) & 0xff000000) >> 24))

typedef struct {
  unsigned int triggers;
  unsigned int adc[8];
} data_counts;

data_counts
vscmParseFIFO(DMANODE* event, FILE *f)
{
  int i, n = event->length;
  long long trig1 = 0, trigtime = 0;
  data_counts cnt = {0, {0,0,0,0,0,0,0,0}};

  for(i = 0; i < n; i++) {
    uint32_t word = LSWAP(event->data[i]);
#ifdef DEBUG
    printf("0x%08X - ", word);
#endif
  
    if(word & 0x80000000) {
      int type = (word >> 27) & 0xF;
      switch(type) {
        case DATA_TYPE_BLKHDR:
#ifdef DEBUG
          printf(" {BLKHDR} SLOTID: %d", (word>>22)&0x1f);
          printf(" NEVENTS: %d", (word>>11)&0x7ff);
          printf(" BLOCK: %d\n", (word>>0)&0x7ff);
#endif
          break;
        case DATA_TYPE_BLKTLR:
#ifdef DEBUG
          printf(" {BLKTLR} SLOTID: %d", (word>>22)&0x1f);
          printf(" NWORDS: %d\n", (word>>0)&0x3fffff);
#endif
          break;
        case DATA_TYPE_EVTHDR:
/*
          if(((word & 0x7ffffff) % 100000) == 0)
            printf("Event: %d\n", word & 0x7ffffff);
*/
#ifdef DEBUG
          printf(" {EVTHDR} EVENT: %d\n", (word>>0)&0x7ffffff);
#endif
          break;
        case DATA_TYPE_TRGTIME:
          cnt.triggers++;
          trig1 = word & 0xffffff;
#ifdef DEBUG
          printf(" {TRGTIME1} %lld\n", trig1);
#endif
          break;
        case DATA_TYPE_BCOTIME:
/*
          if(f) 
            fprintf(f, "* %d, %d\n", (word & 0xff), (word >> 16) & 0xff);
*/
#ifdef DEBUG
          printf(" {BCOTIME}  %d, %d\n", (word & 0xff), (word >> 16) & 0xff); 
#endif
          break;
        case DATA_TYPE_FSSREVT:
          if(f) {
            fprintf(f, "%u, %u, %u, %u, %u\n",
                  (word>>22) & 1, (word>>19) & 7, (word>>12) & 0x7f,
                  (word>>4) & 0xff, word & 7);
          }
          cnt.adc[(word & 0x7)]++;
#ifdef DEBUG
          printf(" {FSSREVT}");
          printf(" HFCB: %1u", (word>>22)&1);
          printf(" CHIPID: %1u", (word>>19)&0x7);
          printf(" CH: %3u", (word>>12)&0x7F);
          printf(" BCO: %3u", (word>>4)&0xFF);
          printf(" ADC: %1u\n", (word>>0)&0x7);
#endif
          break;
        case DATA_TYPE_DNV:
#ifdef DEBUG
          printf(" {***DNV***}\n");
#endif
          break;
        case DATA_TYPE_FILLER:
#ifdef DEBUG
          printf(" {FILLER}\n");
#endif
          break;
        default:
#ifdef DEBUG
          printf(" {***DATATYPE ERROR***}\n");
#endif
          break;
      }
    }
    else {
      trigtime = (trig1 << 24) + word;
/*
      if(f)
        fprintf(f, "%lld\n", trigtime);
*/
#ifdef DEBUG
      printf(" {TRGTIME2} %lld\n", trigtime);
#endif
    }
  }
  return cnt;
}

void
fssrGainScan(int id, char *filename, \
              int beg_chip, int end_chip, \
              int beg_chan, int end_chan,
              int start_thr)
{
  if (vscmIsNotInit(&id, __func__))
    return;

  FILE *fd;
  char fname[256], reg27[20];
  int thr, ichip, ich;
#ifdef DEBUG
  float ratio, ref;
  uint32_t gothitref;
  uint32_t statusword, event, words, idle;
  uint32_t markerror, worderror, iderror;
#endif
  int zero_count, amp, min_hits;
  int base_thr = start_thr;

  uint32_t pulser_rate = vscmGetPulserRate(id);
  uint32_t scaler[128];

  /* Pulser Settings */
  const int start_amp = 75;
  const int end_amp = 125;
  const int step_amp = 25;

#ifdef VXWORKS
  int ticks = 1;
#else
  struct timespec ts; 
  ts.tv_sec = 0;
  ts.tv_nsec = 16666667; /* ~16.6 ms (similar to 1 tick on VxWorks) */
#endif

  /* Temporarily disable both pulser outputs */
  vscmPulser(id, 0, 0, 1);

#ifdef VXWORKS
  min_hits = (int)(pulser_rate * (ticks / (float)sysClkRateGet()) * 0.99);
#else
  min_hits = (int)(pulser_rate * (float)(ts.tv_nsec * 1.0e-9) * 0.99);
#endif
  logMsg("INFO: %s: Min Hits = %d\n", __func__, min_hits);

  for (ichip = beg_chip; ichip <= end_chip; ichip++) {
    fssrStatus(id, ichip);

    if (filename) {
      sprintf(fname,"dat/%s_chip%1d.dat", filename, ichip);
      fd = fopen(fname, "w");
      if (!fd) {
        logMsg("ERROR: %s: Error opening file: %s\n", __func__, fname);
        return;
      }
      else {
        logMsg("INFO: %s: Output file: %s\n", __func__, fname);
      }
    }

    fssrParseControl(id, ichip, reg27);
    if (filename) {
      fprintf(fd, "%s\n", reg27);
      fclose(fd);
    }
    else
      printf("%s\n", reg27);

    for (ich = beg_chan; ich <= end_chan; ich++) {
      if (filename) {
        fd = fopen(fname, "a");
        fprintf(fd, "%d\n", ich);
      }

      printf("Chan %d\n", ich);

      start_thr = base_thr;
      thr = start_thr;

      for (amp = start_amp; amp <= end_amp; amp += step_amp) {
        thr = start_thr;

        if (filename) {
          fprintf(fd, "%d", amp);
        }

        printf("Amp: %d", amp);

        vscmPulser(id, ((ichip > 3) ? 2 : 1), amp, (int)(min_hits / 0.99));

        fssrKillMaskDisableAll(id, ichip);
        fssrKillMaskEnableSingle(id, ichip, ich);
        fssrInjectMaskDisableAll(id, ichip);
        fssrInjectMaskEnableSingle(id, ichip, ich);
        scaler[ich] = 100000;
        zero_count = 0;

        if (filename) {
          fprintf(fd, ", %d", thr);
        }

        printf(" Start: %d\n", thr);

        while(thr < 256 && zero_count < 2) {
          fssrSetThreshold(id, ichip, 0, thr);

          vscmDisableScalers(id, ichip);
          vscmClearScalers(id, ichip);
          vscmEnableScalers(id);
          vscmDisableScalers(id, ichip);
          vscmEnableScalers(id);

          vscmPulserStart(id);
   
          /* Gather data by sleeping */
#ifdef VXWORKS
          taskDelay(ticks);
#else
          nanosleep(&ts, NULL);
#endif

          vscmDisableScalers(id, ichip);
          vscmReadScalers(id, ichip, scaler);

#ifdef DEBUG
          ref = (float)vmeRead32(&VSCMpr[id]->Fssr[ichip].ScalerRef);
          ratio = (scaler[ich]) / (ref * (float)pulser_rate / VSCM_SYS_CLK);
          gothitref = vmeRead32(&VSCMpr[id]->Fssr[ichip].ScalerGotHit);

          statusword = vmeRead32(&VSCMpr[id]->Fssr[ichip].ScalerStatusWord);
          event = vmeRead32(&VSCMpr[id]->Fssr[ichip].ScalerEvent);
          words = vmeRead32(&VSCMpr[id]->Fssr[ichip].ScalerWords);
          idle = vmeRead32(&VSCMpr[id]->Fssr[ichip].ScalerIdle);
          markerror = vmeRead32(&VSCMpr[id]->Fssr[ichip].ScalerMarkErr);
          worderror = vmeRead32(&VSCMpr[id]->Fssr[ichip].ScalerEncErr);
          iderror = vmeRead32(&VSCMpr[id]->Fssr[ichip].ScalerChipIdErr);

          printf("ichip=%d ich=%3d thr=%3d scaler=%7u gothit=%7u ref=%.1f -> ratio=%f\n", \
                  ichip, ich, thr, scaler[ich], gothitref, ref, ratio);
          printf("status=%u event=%u total=%u idle=%u markerr=%u worderr=%u iderr=%u\n", \
                  statusword, event, words, idle, markerror, worderror, iderror);
#endif 
          if (scaler[ich] > min_hits)
            start_thr = thr;

          if (scaler[ich] == 0)
            zero_count++;
          else
            zero_count = 0;

          if (filename)
            fprintf(fd, ", %d", scaler[ich]);

           thr++;
        }
        if (filename)
          fprintf(fd, "\n");
      }
      if (filename) {
        fprintf(fd, "\n");
        fclose(fd);
      }
    } /* End of Channel loop */
  } /* End of Chip loop */
}

void
vscmRefScalerTest(int id, int ticks, int loop)
{
  uint32_t ref;
  int c;
#ifndef VXWORKS
  struct timespec ts;
  ts.tv_sec = 0;
  ts.tv_nsec = 16666667 * ticks;
#endif

  if (vscmIsNotInit(&id, __func__))
    return;

  for (c = 0; c < loop; c++) {
    /*vmeWrite32(&VSCMpr[id]->ScalerLatch, 1<<31);*/
    /* Gather data by sleeping */
#ifdef VXWORKS
    taskDelay(ticks);
#else
    nanosleep(&ts, NULL);
#endif
	/*
    vmeWrite32(&VSCMpr[id]->ScalerLatch, 1<<31);
	ref = vmeRead32(&VSCMpr[id]->ScalerVmeClk);
	*/
    printf("%u\n", ref);
  }
}

#else /* dummy version*/

void
vscmScan_dummy()
{
  return;
}

#endif
