
/* vscmLib.c */

#undef CODA3DMA

#if defined(VXWORKS) || defined(Linux_vme)

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "vscmLib.h"

#ifdef VXWORKS
/*#include "dmainit.h"*/

#include <vxWorks.h>
#include <taskLib.h>
/*#include <ansiStdlib.h>*/

#include "sockLib.h"
#include "inetLib.h"
#include "hostLib.h"
#include "ioLib.h"

#include "wdLib.h"

#define SYNC()		{ __asm__ volatile("eieio"); __asm__ volatile("sync"); }
#define VSCMLOCK
#define VSCMUNLOCK

#else

#define SYNC()
#define sysClkRateGet() CLOCKS_PER_SEC


#ifdef CODA3DMA
#include "jvme.h"
#endif


#define myDelay(ticks) usleep(ticks * 100)


#include <math.h>
#include <string.h>
#include <time.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <pthread.h>

pthread_mutex_t vscmMutex = PTHREAD_MUTEX_INITIALIZER;
#define VSCMLOCK		if(pthread_mutex_lock(&vscmMutex) < 0) \
								perror("pthread_mutex_lock");
#define VSCMUNLOCK	if(pthread_mutex_unlock(&vscmMutex) < 0) \
								perror("pthread_mutex_unlock");

#ifdef CODA3DMA
DMA_MEM_ID vmeIN, vmeOUT;
extern DMANODE *the_event;
extern unsigned int *dma_dabufp;
#endif

#endif


#define LSWAP(x)        ((((x) & 0x000000ff) << 24) | \
                         (((x) & 0x0000ff00) <<  8) | \
                         (((x) & 0x00ff0000) >>  8) | \
                         (((x) & 0xff000000) >> 24))


#define VSCM_A32_BASE 0x09000000


/* Define Global Variables */
int nvscm = 0;                                          /* Number of VSCMs in Crate */
volatile unsigned int *VSCMpmb;                         /* pointer to Multblock window */
volatile struct VSCM_regs *VSCMpr[VSCM_MAX_BOARDS + 1]; /* pointers to VSCM memory map */
volatile uintptr_t *VSCMpf[VSCM_MAX_BOARDS + 1];        /* pointers to VSCM FIFO memory */
int vscmA32Offset = 0x0;                                /* Difference in CPU A32 Base - VME A32 Base */
int vscmA24Offset = 0x0;                                /* Difference in CPU A24 Base - VME A24 Base */
int vscmID[VSCM_MAX_BOARDS];                            /* array of slot numbers for VSCMs */
int vscmInited = 0;
int minSlot = 21;
int maxSlot = 1;
int vscmMaxSlot=0;                                   /* Highest Slot hold a VSCM */
int vscmMinSlot=0;                                   /* Lowest Slot holding a VSCM */

unsigned int gTriggerLimit;
unsigned int gTriggerCount;

extern int  tiDisableTriggerSource();

void
vscmSetMaxTriggerLimit(unsigned int val)
{
   gTriggerLimit = val;
}

void
vscmClearTriggerCount()
{
   gTriggerCount = 0;
}

void
vscmIncTriggerCount()
{
  if(gTriggerCount < gTriggerLimit)
    gTriggerCount++;

  if((gTriggerCount >= gTriggerLimit) && gTriggerLimit)
  {
    tiDisableTriggerSource();
    printf("*************************************\n");
    printf("****** TRIGGER SOURCE DISABLED ******\n");
    printf("*************************************\n");
  }
}

#define STRLEN 1024

/*read config file */

#define VAL_DECODER \
      for(ii=0; ii<nval; ii++) \
	  { \
        if(!strncmp(charval[ii],"0x",2)) sscanf((char *)&charval[ii][2],"%8x",&val[ii]); \
        else                             sscanf(charval[ii],"%9u",&val[ii]); \
		/*if(!strncmp(charval[ii],"0x",2)) val[ii] = strtol((char *)&charval[ii][2], NULL, 16); \
		else                             val[ii] = strtol((char *)&charval[ii][0], NULL, 10); \
		*/ \
        /*printf("   [%2d] charval >%s<, val=%d (0x%08x)\n",ii,charval[ii],val[ii],val[ii]);*/ \
	  } \
      nval = 0

int
vscmConfigDownload(int id, char *fname)
{
  FILE *fd;
  char *ch, str[STRLEN], keyword[STRLEN], charval[10][STRLEN];
  unsigned int val[10];
  int ii, nval;

  if (vscmIsNotInit(&id, __func__))
    return EXIT_FAILURE;

  printf("1\n");

  if ((fd = fopen(fname,"r")) == NULL) {
    printf("VSCM_ConfigDownload: Can't open config file >%s<\n",fname);
    return(-1);
  }

  nval = 0;
  while ((ch = fgets(str, STRLEN, fd)) != NULL) {
/*	printf(">%s< %d\n",str,strlen(ch)); */
    if (ch[0] == '#' || ch[0] == ' ' || ch[0] == '\t' || \
        ch[0] == '\n' || ch[0] == '\r') {
      continue;
    }
    else {
      sscanf(str,"%30s", keyword);
/*      printf("keyword >%s<\n", keyword); */

/*0        0        20*/
      if (!strcmp(keyword,"VSCM_MAX_TRIGGER_NUM")) {
        sscanf(str,"%30s %9s", keyword, charval[0]);
        nval = 1;        
        VAL_DECODER;
        vscmSetMaxTriggerLimit(val[0]);
      }
      else if (!strcmp(keyword,"FSSR_ADDR_REG_DISC_THR")) {
        sscanf(str,"%30s %1s %1s %3s", keyword, \
                charval[0], charval[1], charval[2]);
        nval = 3;        
        VAL_DECODER;
        fssrSetThreshold(id, (int)val[0],val[1],val[2]);
      }
      else if (!strcmp(keyword,"FSSR_ADDR_REG_KILL")) {
/*0        0x00000000        0x00000000        0x00000000        0x00000000*/
        sscanf(str,"%30s %1s %10s %10s %10s %10s", \
                keyword, charval[0], charval[1], charval[2], \
                charval[3], charval[4]);
        nval = 5;        
        VAL_DECODER;
        if (fssrSetMask(id, val[0], FSSR_ADDR_REG_KILL, (uint32_t *)&val[1]))
#ifdef DEBUG
          logMsg("ERROR: %s: %d/%d Mask Reg# %d not set correctly\n", \
                  __func__, id, val[0], FSSR_ADDR_REG_KILL); 
#else
          continue;
#endif
      }
/*0        0x00000000        0x00000000        0x00000000        0x00000000*/
      else if (!strcmp(keyword,"FSSR_ADDR_REG_INJECT")) {
        sscanf(str,"%30s %1s %10s %10s %10s %10s", keyword, \
                charval[0], charval[1], charval[2], charval[3], charval[4]);
        nval = 5;        
        VAL_DECODER;
        if (fssrSetMask(id, val[0], FSSR_ADDR_REG_INJECT, (uint32_t *)&val[1]))
#ifdef DEBUG
          logMsg("ERROR: %s: %d/%d Mask Reg# %d not set correctly\n", \
                  __func__, id, val[0], FSSR_ADDR_REG_INJECT); 
#else
          continue;
#endif
      }
/*0        0x1F*/
      else if (!strcmp(keyword,"FSSR_ADDR_REG_DCR")) {
        sscanf(str,"%30s %1s %4s", keyword, charval[0], charval[1]);
        nval = 2;        
        VAL_DECODER;
        fssrSetControl(id, val[0], val[1]);
      }
/*32*/
      else if(!strcmp(keyword,"VSCM_BCO_FREQ")) {
        sscanf(str,"%30s %3s", keyword, charval[0]);
        nval = 1;        
        VAL_DECODER;
        vscmSetBCOFreq(id, val[0]);
	  }
      else if(!strcmp(keyword,"VSCM_TRIG_WINDOW"))
	  { /*256        512        32*/
        sscanf(str,"%30s %3s %3s %3s", \
                keyword, charval[0], charval[1], charval[2]);
        nval = 3;
        VAL_DECODER;
        vscmSetTriggerWindow(id, val[0],val[1],val[2]);
	  }
      else
	  {
        printf("VSCM_ConfigDownload ERROR: unknown keyword >%s< (%d 0x%02x)\n",keyword,ch[0],ch[0]);
        fclose(fd);
        return(-3);
	  }
	}
  }

  fclose(fd);

  printf("8\n");

  for (ii = 0; ii < 8; ii++)
  {
    fssrSetActiveLines(id, ii, FSSR_ALINES_6);
    fssrRejectHits(id, ii, 0);
    fssrSCR(id, ii);
    fssrSendData(id, ii, 1);
  }
  printf("9\n");

  return 0;
}

/*******************/

static uint32_t
rand32()
{
  uint32_t x;

  x = rand() & 0xff;
  x |= (rand() & 0xff) << 8;
  x |= (rand() & 0xff) << 16;
  x |= (rand() & 0xff) << 24;

  return x;
}

void
fssrInternalPulserEnable(int id, int chip)
{
  if (vscmIsNotInit(&id, __func__))
    return;

  fssrTransfer(id, chip, 2, FSSR_CMD_DEFAULT, 1, NULL);
}

void
fssrSetInternalPulserAmp(int id, int chip, uint8_t mask)
{
  uint32_t val = mask;

  if (vscmIsNotInit(&id, __func__))
    return;

  fssrTransfer(id, chip, 1, FSSR_CMD_WRITE, 8, &val);
}

uint8_t
fssrGetInternalPulserAmp(int id, int chip)
{
  uint32_t rsp;

  if (vscmIsNotInit(&id, __func__))
    return EXIT_FAILURE;

  fssrTransfer(id, chip, 1, FSSR_CMD_READ, 9, &rsp);
  return (rsp & 0xff);
}

void
fssrSetControl(int id, int chip, uint8_t mask)
{
  uint32_t val = mask;

  if (vscmIsNotInit(&id, __func__))
    return;

  fssrTransfer(id, chip, FSSR_ADDR_REG_DCR, FSSR_CMD_WRITE, 8, &val);
}

uint8_t
fssrGetControl(int id, int chip)
{
  uint32_t rsp;

  if (vscmIsNotInit(&id, __func__))
    return EXIT_FAILURE;

  fssrTransfer(id, chip, FSSR_ADDR_REG_DCR, FSSR_CMD_READ, 9, &rsp);
#if DEBUG
  printf("Control = 0x%02X\n", rsp & 0xFF);
#endif

  return (rsp & 0xFF);
}

int
fssrParseControl(int id, int chip, char *s)
{
  uint8_t val;
  char str[20];
  char numstr[4];

/*  if (strlen(s) < strlen(str))
 *    return EXIT_FAILURE;*/

  val = fssrGetControl(id, chip);

  switch (val & 3) {
    case 0:
      strcpy(str, "65, ");
      break;
    case 1:
      strcpy(str, "85, ");
      break;
    case 2:
      strcpy(str, "100, ");
      break;
    case 3:
      strcpy(str, "125, ");
      break;
  }

  switch ((val >> 2) & 1) {
    case 0:
      strcat(str, "High, ");
      break;
    case 1:
      strcat(str, "Low, ");
      break;
  }

  switch ((val >> 3) & 1) {
    case 0:
      strcat(str, "On, ");
      break;
    case 1:
      strcat(str, "Off, ");
      break;
  }
  snprintf(numstr, sizeof(numstr), "%d", \
            vmeRead32(&VSCMpr[id]->FssrClkCfg) * 8);
  strcat(str, numstr);

  strcpy(s, str);
  return EXIT_SUCCESS;
}

void
fssrSetThreshold(int id, int chip, int idx, uint8_t thr)
{
  uint32_t val;
  uint8_t reg;

  if (vscmIsNotInit(&id, __func__))
    return;

  if (idx > 7 || idx < 0)
    return;

  val = thr;
  reg = (FSSR_ADDR_REG_DISC_THR0 + idx);
	fssrTransfer(id, chip, reg, FSSR_CMD_WRITE, 8, &val);
}

uint8_t
fssrGetThreshold(int id, int chip, uint8_t idx)
{
  uint32_t rsp = 0;
  uint8_t reg;

  if (vscmIsNotInit(&id, __func__))
    return EXIT_FAILURE;

  if (idx > 7)
    return rsp;

  reg = (FSSR_ADDR_REG_DISC_THR0 + idx);
  fssrTransfer(id, chip, reg, FSSR_CMD_READ, 9, &rsp);
#if DEBUG
  logMsg("Threshold %d = %d\n", idx, rsp & 0xFF);
#endif

  return (rsp & 0xFF);
}

void
fssrSetVtn(int id, int chip, uint8_t thr)
{
  uint32_t val;

  if (vscmIsNotInit(&id, __func__))
    return;

  val = thr;
	fssrTransfer(id, chip, FSSR_ADDR_REG_DISC_VTN, FSSR_CMD_WRITE, 8, &val);
}

uint8_t
fssrGetVtn(int id, int chip)
{
  uint32_t rsp = 0;

  if (vscmIsNotInit(&id, __func__))
    return EXIT_FAILURE;

  fssrTransfer(id, chip, FSSR_ADDR_REG_DISC_VTN, FSSR_CMD_READ, 9, &rsp);
#if DEBUG
  logMsg("Vtn = %d\n", rsp & 0xFF);
#endif

  return (rsp & 0xFF);
}

int
fssrWaitReady(int id) {
  int i;
  for (i = 0; i < 10; i++) {
    if(vmeRead32(&VSCMpr[id]->FssrSerCfg) & (1<<14))
      return 1;
    myDelay(1);
  }
#ifdef DEBUG
  logMsg("ERROR: %s: interface timeout\n", __func__);
#endif
  return 0;
}

void
fssrTransfer(int id, uint8_t chip, uint8_t reg, uint8_t cmd, \
              uint8_t nBits, uint32_t *pData)
{
  uint32_t SerCfgReg = 0;
	
  SerCfgReg |= (chip & 0xF)<<24;
  SerCfgReg |= (reg & 0x1F)<<0;
  SerCfgReg |= (cmd & 0x7)<<8;
  SerCfgReg |= (nBits & 0xFF)<<16;
  SerCfgReg |= (1<<15);

  if (pData && nBits > 0) vmeWrite32(&VSCMpr[id]->FssrSerData[0], pData[0]);
  if (pData && nBits > 32) vmeWrite32(&VSCMpr[id]->FssrSerData[1], pData[1]);
  if (pData && nBits > 64) vmeWrite32(&VSCMpr[id]->FssrSerData[2], pData[2]);
  if (pData && nBits > 96) vmeWrite32(&VSCMpr[id]->FssrSerData[3], pData[3]);

  if (!fssrWaitReady(id))
    logMsg("ERROR: %s not ready to start\n", __func__);
	
  vmeWrite32(&VSCMpr[id]->FssrSerCfg, SerCfgReg);
	
  if (!fssrWaitReady(id))
    logMsg("ERROR: %s did not end\n", __func__);

  if (pData && (cmd == FSSR_CMD_READ)) {
    int i;
    uint32_t rsp[4];
    rsp[0] = vmeRead32(&VSCMpr[id]->FssrSerData[0]);
    rsp[1] = vmeRead32(&VSCMpr[id]->FssrSerData[1]);
    rsp[2] = vmeRead32(&VSCMpr[id]->FssrSerData[2]);
    rsp[3] = vmeRead32(&VSCMpr[id]->FssrSerData[3]);
    for (i = 0; i < nBits; i++) {
      if (i >= 96) {
        if (i == 96) pData[3] = 0;
        if (rsp[0] & (1 << (127 - i)))
          pData[3] |= 1 << (i - 96);
      }
      else if (i >= 64) {
        if (i == 64) pData[2] = 0;
        if (rsp[1] & (1 << (95 - i)))
          pData[2] |= 1 << (i - 64);
      }
      else if (i >= 32) {
        if (i == 32) pData[1] = 0;
        if (rsp[2] & (1 << (63 - i)))
          pData[1] |= 1 << (i - 32);
      }
      else {
        if (i == 0) pData[0] = 0;
        if (rsp[3] & (1 << (31 - i)))
          pData[0] |= 1 << i;
      }
    }
  }
	
#if DEBUG
  if (cmd == FSSR_CMD_READ) {
    logMsg("Data response: 0x%08X 0x%08X 0x%08X 0x%08X\n", \
            vmeRead32(&VSCMpr[id]->FssrSerData[3]), \
            vmeRead32(&VSCMpr[id]->FssrSerData[2]), \
            vmeRead32(&VSCMpr[id]->FssrSerData[1]), \
            vmeRead32(&VSCMpr[id]->FssrSerData[0]));
  }
#endif
}

void
fssrMasterReset(int id)
{
  vmeWrite32(&VSCMpr[id]->FssrSerCfg, 0xF<<28);
  myDelay(1);
  vmeWrite32(&VSCMpr[id]->FssrSerCfg, 0);
  myDelay(1);
}

int
fssrMaskCompare(uint32_t *mask, uint32_t *readmask)
{
  int i, status = 0;

  for (i = 0; i < 4; i++) {
    uint32_t v = readmask[i];
    /* Reverse Bit Sequence
    http://graphics.stanford.edu/~seander/bithacks.html#ReverseParallel*/
    v = ((v >> 1) & 0x55555555) | ((v & 0x55555555) << 1);
    v = ((v >> 2) & 0x33333333) | ((v & 0x33333333) << 2);
    v = ((v >> 4) & 0x0F0F0F0F) | ((v & 0x0F0F0F0F) << 4);
    v = ((v >> 8) & 0x00FF00FF) | ((v & 0x00FF00FF) << 8);
    v = ( v >> 16             ) | ( v               << 16);

    if (mask[3 - i] != v)
      status |= 1;
  }

  return status;
}

int
fssrSetMask(int id, int chip, int reg, uint32_t *mask)
{
  uint32_t readmask[4] = {0, 0, 0, 0};
  /* Per FSSR procedures disable/enable core
   * when doing a kill/inject operation*/
  fssrRejectHits(id, chip, 1);
  fssrTransfer(id, chip, reg, FSSR_CMD_WRITE, 128, mask);
  fssrRejectHits(id, chip, 0);

  fssrGetMask(id, chip, reg, readmask);
  if (fssrMaskCompare(mask, readmask))
    return EXIT_FAILURE;

  return EXIT_SUCCESS;
}

/* Disable all channels on a chip */
void
fssrKillMaskDisableAll(int id, int chip)
{
  uint32_t mask[4];

  if (vscmIsNotInit(&id, __func__))
    return;

  mask[0] = 0xFFFFFFFF;
  mask[1] = 0xFFFFFFFF;
  mask[2] = 0xFFFFFFFF;
  mask[3] = 0xFFFFFFFF;
  if (fssrSetMask(id, chip, FSSR_ADDR_REG_KILL, mask))
    logMsg("ERROR: %s: Mask Reg# %d not set correctly\n", \
            __func__, FSSR_ADDR_REG_KILL);
}

/* Enable all channels on a chip */
void
fssrKillMaskEnableAll(int id, int chip)
{
  uint32_t mask[4] = {0, 0, 0, 0};

  if (vscmIsNotInit(&id, __func__))
    return;

  if (fssrSetMask(id, chip, FSSR_ADDR_REG_KILL, mask))
    logMsg("ERROR: %s: Mask Reg# %d not set correctly\n", \
            __func__, FSSR_ADDR_REG_KILL);
}

/* Toggle a single channel (disabled) on a chip */
void
fssrKillMaskDisableSingle(int id, int chip, int chan)
{
  if (vscmIsNotInit(&id, __func__))
    return;

  fssrMaskSingle(id, chip, FSSR_ADDR_REG_KILL, chan, 1);
}

/* Toggle a single channel (enable) on a chip */
void
fssrKillMaskEnableSingle(int id, int chip, int chan)
{
  if (vscmIsNotInit(&id, __func__))
    return;

  fssrMaskSingle(id, chip, FSSR_ADDR_REG_KILL, chan, 0);
}

/* Toggle a single channel inject mask on a chip */
void
fssrInjectMaskEnableSingle(int id, int chip, int chan)
{
  if (vscmIsNotInit(&id, __func__))
    return;

  fssrMaskSingle(id, chip, FSSR_ADDR_REG_INJECT, chan, 1);
}

/*
 * Toggle a single mask channel on a chip
 * boolean = what to set the mask value to
*/
void
fssrMaskSingle(int id, int chip, int reg, int chan, int boolean)
{
  uint32_t mask[4], readmask[4];

  if (chan >=0 && chan <= 127) {
    int i;
    chan = 127 - chan;
    fssrGetMask(id, chip, reg, readmask);

    for (i = 0; i < 4; i++) {
      uint32_t v = readmask[i];
      /* Reverse Bit Sequence
      http://graphics.stanford.edu/~seander/bithacks.html#ReverseParallel*/
      v = ((v >> 1) & 0x55555555) | ((v & 0x55555555) << 1);
      v = ((v >> 2) & 0x33333333) | ((v & 0x33333333) << 2);
      v = ((v >> 4) & 0x0F0F0F0F) | ((v & 0x0F0F0F0F) << 4);
      v = ((v >> 8) & 0x00FF00FF) | ((v & 0x00FF00FF) << 8);
      v = ( v >> 16             ) | ( v               << 16);

      mask[3 - i] = v;
    }

    if (boolean == 1)
      mask[chan >> 5] |= (1 << (chan & 0x1F));
    else if (boolean == 0)
      mask[chan >> 5] &= ~(1 << (chan & 0x1F));

    if (fssrSetMask(id, chip, reg, mask))
      logMsg("ERROR: %s: Mask Reg# %d not set correctly\n", __func__, reg);
  }
  else
    logMsg("ERROR: %s: Reg %d bad channel #: %d\n", __func__, reg, chan);
}

/* Disable Inject mask on all channels on a chip */
void
fssrInjectMaskDisableAll(int id, int chip)
{
  uint32_t mask[4] = {0, 0, 0, 0};

  if (vscmIsNotInit(&id, __func__))
    return;

  if (fssrSetMask(id, chip, FSSR_ADDR_REG_INJECT, mask))
    logMsg("ERROR: %s: %d/%d Mask Reg# %d not set correctly\n", \
            __func__, id, chip, FSSR_ADDR_REG_INJECT);
}

void
fssrKillMaskDisableAllChips(int id)
{
  int i;

  if (vscmIsNotInit(&id, __func__))
    return;

  for (i = 0; i < 8; i++)
    fssrKillMaskDisableAll(id, i);
}

void
fssrInjectMaskDisableAllChips(int id)
{
  int i;

  if (vscmIsNotInit(&id, __func__))
    return;

  for (i = 0; i < 8; i++)
    fssrInjectMaskDisableAll(id, i);
}

void
fssrGetMask(int id, int chip, int reg, uint32_t *mask)
{
/*
  mask[0] = 0;
  mask[1] = 0;
  mask[2] = 0;
  mask[3] = 0;
*/

  fssrTransfer(id, chip, reg, FSSR_CMD_READ, 129, mask);
}

void
fssrGetKillMask(int id, int chip, uint32_t *mask)
{
  if (vscmIsNotInit(&id, __func__))
    return;

  fssrGetMask(id, chip, FSSR_ADDR_REG_KILL, mask);

#if DEBUG
  logMsg("Kill [ch127->0] = 0x%08X 0x%08X 0x%08X 0x%08X\n", \
          mask[3], mask[2], mask[1], mask[0]);
#endif
}

void
fssrGetInjectMask(int id, int chip, uint32_t *mask)
{
  if (vscmIsNotInit(&id, __func__))
    return;

  fssrGetMask(id, chip, FSSR_ADDR_REG_INJECT, mask);

#if DEBUG
  logMsg("Inject [ch127->0] = 0x%08X 0x%08X 0x%08X 0x%08X\n", \
          mask[3], mask[2], mask[1], mask[0]);
#endif
}

uint8_t
fssrGetBCONum(int id, int chip)
{
  return fssrGetBCONumOffset(id, chip, FSSR_SCR_BCONUM_START);
}

uint8_t
fssrGetBCONumOffset(int id, int chip, uint8_t offset) {
  uint32_t rsp;
	
  vmeWrite32(&VSCMpr[id]->FssrSerClk, 0x100 | ((offset + 1) & 0xFF));
  fssrTransfer(id, chip, FSSR_ADDR_REG_AQBCO, FSSR_CMD_SET, 1, NULL);
  vmeWrite32(&VSCMpr[id]->FssrSerClk, 0);
  fssrTransfer(id, chip, FSSR_ADDR_REG_AQBCO, FSSR_CMD_READ, 9, &rsp);

#if DEBUG
  logMsg("BCO [sync @ %d] = %u\n", offset, rsp & 0xFF);
#endif

  return (rsp & 0xFF);
}

uint8_t
fssrGetBCONumNoSync(int id, int chip)
{
  uint32_t rsp;

  fssrTransfer(id, chip, FSSR_ADDR_REG_AQBCO, FSSR_CMD_SET, 1, NULL);
  fssrTransfer(id, chip, FSSR_ADDR_REG_AQBCO, FSSR_CMD_READ, 9, &rsp);
	
#if DEBUG
  logMsg("BCO [no sync] = %u\n", rsp & 0xFF);
#endif

  return (rsp & 0xFF);
}

void
fssrRejectHits(int id, int chip, int reject)
{
  if(reject)
    fssrTransfer(id, chip, FSSR_ADDR_REG_REJECTHITS, FSSR_CMD_SET, 1, NULL);
  else
    fssrTransfer(id, chip, FSSR_ADDR_REG_REJECTHITS, FSSR_CMD_RESET, 1, NULL);
}

void
fssrSendData(int id, int chip, int send)
{
  if (send)
    fssrTransfer(id, chip, FSSR_ADDR_REG_SENDDATA, FSSR_CMD_SET, 1, NULL);
  else
    fssrTransfer(id, chip, FSSR_ADDR_REG_SENDDATA, FSSR_CMD_RESET, 1, NULL);
}

void
fssrSetActiveLines(int id, int chip, unsigned int lines)
{
  uint32_t val = (lines & 0x3);

  if (vscmIsNotInit(&id, __func__))
    return;

	fssrTransfer(id, chip, FSSR_ADDR_REG_ALINES, FSSR_CMD_WRITE, 2, &val);
}

/*
 * Set the Chip ID for the chips based on first number passed
 *
 * For each ID 8 (0b01XXX) is added to the passed chip ID
 * This is due to the fact that the chip ID is really 5 bits, 
 * but only 3 are user settable via wire bonds
 * 0 = set both connectors to use the same chip IDs
 * 1 = only set for the top connector
 * 2 = only set for the bottom connector
*/
void
fssrSetChipID(int id, \
              unsigned int hfcb, \
              unsigned int u1, \
              unsigned int u2, \
              unsigned int u3, \
              unsigned int u4)
{
  if (vscmIsNotInit(&id, __func__))
    return;

  if (hfcb > 2) {
    logMsg("ERROR: %s: Invalid HFCB #\n", __func__);
    return;
  }
  if (u1 > 7 || u2 > 7 || u3 > 7 || u4 > 7) {
    logMsg("ERROR: %s: Invalid Chip ID\n", __func__);
    return;
  }

  if (hfcb == 0 || hfcb == 1) {
    vmeWrite32(&VSCMpr[id]->FssrAddrH1, \
                ((8 + u4) << 24) | ((8 + u3) << 16) | \
                ((8 + u2) << 8) | ((8 + u1) << 0));
  }
  if (hfcb == 0 || hfcb == 2) {
    vmeWrite32(&VSCMpr[id]->FssrAddrH2, \
                ((8 + u4) << 24) | ((8 + u3) << 16) | \
                ((8 + u2) << 8) | ((8 + u1) << 0));
  }
}


void
fssrSCR(int id, int chip)
{
  if (vscmIsNotInit(&id, __func__))
    return;

  vmeWrite32(&VSCMpr[id]->FssrSerClk, 0x100 | FSSR_SCR_BCONUM_START);
  fssrTransfer(id, chip, FSSR_ADDR_REG_SCR, FSSR_CMD_SET, 1, NULL);
  vmeWrite32(&VSCMpr[id]->FssrSerClk, 0);
}

void
fssrRegisterTest(int id, int chip)
{
  int c, r, i;
  uint32_t t;
  int start = 0, stop = 7;
  unsigned int mask[4] = {0, 0, 0, 0}, readmask[4];
  unsigned int res;

  if (vscmIsNotInit(&id, __func__))
    return;

  if (chip >= 0 && chip < 8) {
    start = stop = chip;
  }

  for (c = start; c <= stop; c++) {
    if ((c > 0) && (start != stop)) printf("\n");
    printf("Chip %d\n", c);
    /* can't read 28 & 30, so stop at 27 */
    for (r = 1; r < 28; r++) {
      /* skip non-existant registers */
      if (r == 21 || r == 22 || r == 23 || r == 25 || r == 26)
        continue;
      /* skip registers that always return 0 */
      else if (r == 2 || r == 19 || r == 20 || r == 24)
        continue;
      /* kill & inject masks */
      else if (r == 17 || r == 18) {
        /* Try setting mask to all zeros */
        mask[0] = mask[1] = mask[2] = mask[3] = 0;
        fssrTransfer(id, c, r, FSSR_CMD_WRITE, 128, mask);
        fssrTransfer(id, c, r, FSSR_CMD_READ, 129, readmask);
        printf("\rReg #%2d .. All Zeros", r);

        if (fssrMaskCompare(mask, readmask)) {
          printf(" FAILED\n");
          continue;
        }
        /* 5 passes of random 32-bit numbers for mask */
        for (i = 1; i <= 5; i++) {
          mask[0] = rand32();
          mask[1] = rand32();
          mask[2] = rand32();
          mask[3] = rand32();

          fssrTransfer(id, c, r, FSSR_CMD_WRITE, 128, mask);
					fssrTransfer(id, c, r, FSSR_CMD_READ, 129, readmask);
          printf("\rReg #%2d .. Random #%d", r, i);

          if (fssrMaskCompare(mask, readmask)) {
            printf(" FAILED\n");
            break;
          }
        }
        /* Check to see if all 5 passes of random numbers finished */
        if (i < 5)
          continue;
        /* Try setting mask to all ones */
        mask[0] = mask[1] = mask[2] = mask[3] = 0xffffffff;
        fssrTransfer(id, c, r, FSSR_CMD_WRITE, 128, mask);
				fssrTransfer(id, c, r, FSSR_CMD_READ, 129, readmask);
        printf("\rReg #%2d .. All Ones", r);

        if (fssrMaskCompare(mask, readmask)) {
          printf(" FAILED\n");
          continue;
        }
        /* If made it this far haven't had any errors, so report a pass */
        printf(" PASSED\n");
      }
      /* handle 8-bit registers */
      else if ((r >= 1 && r <= 15) || r == 27) {
        for (t = 0; t < 256; t++) {
          fssrTransfer(id, c, r, FSSR_CMD_WRITE, 8, &t);
          fssrTransfer(id, c, r, FSSR_CMD_READ, 9, &res);
          printf("\rReg #%2d .. %3d", r, res & 0xFF);
          if ((res & 0xFF) != t) {
            printf(" FAILED\n");
            break;
          }
        }
        if (t == 256)
          printf(" PASSED\n");
      }
      /* handle active lines register */
      else if (r == 16) {
        for (t = 0; t < 4; t++) {
          fssrTransfer(id, c, r, FSSR_CMD_WRITE, 2, &t);
          fssrTransfer(id, c, r, FSSR_CMD_READ, 3, &res);
          printf("\rReg #%2d .. %1d", r, res & 0xFF);
          if ((res & 0xFF) != t) {
            printf(" FAILED\n");
            break;
          }
        }
        if (t == 4)
          printf(" PASSED\n");
      }
    }
  }
  fssrMasterReset(id);
}

char *
readNormalizedScaler(char *buf, char *prefix, \
                          uint32_t ref, uint32_t scaler)
{
  double normalized = VSCM_SYS_CLK * (double)scaler / (double)ref;
  snprintf(buf, 80, "%s = %08u, %.1fHz\n", \
            prefix, scaler, normalized);
  return buf;
}

void
fssrStatusAll()
{
  int ii, jj;
  for(ii=0; ii<nvscm; ii++)
  {
    for(jj=0; jj<8; jj++)
    {
      fssrStatus(vscmID[ii], jj);
	}
  }

}


void 
fssrStatus(int id, int chip)
{
  uint32_t ref;
  uint32_t mask[4];
  char buf[80];

  if (vscmIsNotInit(&id, __func__))
    return;

  vscmDisableScalers(id, chip);	

	ref = vmeRead32(&VSCMpr[id]->Fssr[chip].ScalerRef);

  printf("SLOT: %d ", id);
	
	switch (chip) {
    case 0: printf("HFCB 1 U1:\n"); break;
    case 1: printf("HFCB 1 U2:\n"); break;
    case 2: printf("HFCB 1 U3:\n"); break;
    case 3: printf("HFCB 1 U4:\n"); break;
    case 4: printf("HFCB 2 U1:\n"); break;
    case 5: printf("HFCB 2 U2:\n"); break;
    case 6: printf("HFCB 2 U3:\n"); break;
    case 7: printf("HFCB 2 U4:\n"); break;
  }

  printf("----------- Status ------------\n");
  printf("Last Status Word   = 0x%08X\n", \
          vmeRead32(&VSCMpr[id]->Fssr[chip].LastStatusWord));
  printf(readNormalizedScaler(buf, "StatusWordCount   ", ref, \
          vmeRead32(&VSCMpr[id]->Fssr[chip].ScalerStatusWord)));
  printf(readNormalizedScaler(buf, "EventWordCount    ", ref, \
          vmeRead32(&VSCMpr[id]->Fssr[chip].ScalerEvent)));
  printf(readNormalizedScaler(buf, "TotalWordCount    ", ref, \
          vmeRead32(&VSCMpr[id]->Fssr[chip].ScalerWords)));
  printf(readNormalizedScaler(buf, "IdleWordCount     ", ref, \
          vmeRead32(&VSCMpr[id]->Fssr[chip].ScalerIdle)));
  printf(readNormalizedScaler(buf, "AcqBcoCount       ", ref, \
          vmeRead32(&VSCMpr[id]->Fssr[chip].ScalerAqBco)));
  printf(readNormalizedScaler(buf, "MarkErrors        ", ref, \
          vmeRead32(&VSCMpr[id]->Fssr[chip].ScalerMarkErr)));
  printf(readNormalizedScaler(buf, "StripEncodeErrors ", ref, \
          vmeRead32(&VSCMpr[id]->Fssr[chip].ScalerEncErr)));
  printf(readNormalizedScaler(buf, "ChipIdErrors      ", ref, \
          vmeRead32(&VSCMpr[id]->Fssr[chip].ScalerChipIdErr)));
  printf(readNormalizedScaler(buf, "GotHit            ", ref, \
          vmeRead32(&VSCMpr[id]->Fssr[chip].ScalerGotHit)));
  printf(readNormalizedScaler(buf, "Coretalking       ", ref, \
          vmeRead32(&VSCMpr[id]->Fssr[chip].ScalerCoreTalking)));
  printf("FSSR Max Latency   = %u (BCO ticks)\n", \
          vmeRead32(&VSCMpr[id]->Fssr[chip].LatencyMax));
	
  vmeWrite32(&VSCMpr[id]->ScalerLatch, 0xFF);

  printf("----------- Config ------------\n");	
  printf("FSSR BCO Clock Period: %dns\n", \
          vmeRead32(&VSCMpr[id]->FssrClkCfg) * 8);
  printf("FSSR Control: 0x%02X\n", fssrGetControl(id, chip));
  printf("FSSR Thresholds: %d %d %d %d %d %d %d %d\n", \
          fssrGetThreshold(id, chip, 0), fssrGetThreshold(id, chip, 1), \
          fssrGetThreshold(id, chip, 2), fssrGetThreshold(id, chip, 3), \
          fssrGetThreshold(id, chip, 4), fssrGetThreshold(id, chip, 5), \
          fssrGetThreshold(id, chip, 6), fssrGetThreshold(id, chip, 7));
		
  fssrGetKillMask(id, chip, mask);
  printf("FSSR Kill[ch127->0]: 0x%08X 0x%08X 0x%08X 0x%08X\n", mask[3], mask[2], mask[1], mask[0]);
	
  fssrGetInjectMask(id, chip, mask);
  printf("FSSR Inject[ch127->0]: 0x%08X 0x%08X 0x%08X 0x%08X\n", mask[3], mask[2], mask[1], mask[0]);

  printf("FSSR BCO[@0 @128, @255]: %d %d %d\n", \
          fssrGetBCONumOffset(id, chip, FSSR_SCR_BCONUM_START), \
          fssrGetBCONumOffset(id, chip, FSSR_SCR_BCONUM_START-128), \
          fssrGetBCONumOffset(id, chip, FSSR_SCR_BCONUM_START-1));
  printf("\n");	
}

void
vscmSWSync(int id)
{
  unsigned int reg = vmeRead32(&VSCMpr[id]->Sync);

  vmeWrite32(&VSCMpr[id]->Sync, (reg & 0xFFFFFFF0) | IO_MUX_0); 
  vmeWrite32(&VSCMpr[id]->Sync, (reg & 0xFFFFFFF0) | IO_MUX_1); 
  vmeWrite32(&VSCMpr[id]->Sync, reg); 
}

void
vscmSetTriggerWindow(int id, \
                      uint32_t windowSize, \
                      uint32_t windowLookback, \
                      uint32_t bcoFreq)
{
  uint32_t bcoStop_i, bcoStop_r;
  uint32_t bcoStart_i, bcoStart_r;
  uint32_t stop = (windowLookback - windowSize - bcoFreq);
  uint32_t pulser_period;

  if (vscmIsNotInit(&id, __func__))
    return;

  bcoStart_i = (0x100 - windowLookback / bcoFreq) & 0xFF;
  bcoStart_r = (0x100 - windowLookback % bcoFreq) & 0xFF;

  bcoStop_i = (0x100 - stop / bcoFreq) & 0xFF;
  bcoStop_r = (0x100 - stop % bcoFreq) & 0xFF;
	
  vmeWrite32(&VSCMpr[id]->TriggerWindow, \
              (bcoStart_r << 0) | (bcoStart_i << 8) | \
              (bcoStop_r << 16) | (bcoStop_i << 24));

  /* Check the maximum pulser rate only if its already set */
  if ((pulser_period = vmeRead32(&VSCMpr[id]->PulserPeriod)))
  {
    /* Formula from: https://clasweb.jlab.org/elog-svt/daq/5 */
    uint32_t trig_rate_limit = 50000000 / (16 * (1 + windowSize / bcoFreq));
    /* Convert pulser period to frequency in Hz before comparing to limit */
    if ((int)(1.0 / (pulser_period * 8.0e-9)) > trig_rate_limit)
    {
      /* Add 0.5 to naively force rounding up */
      vmeWrite32(&VSCMpr[id]->PulserPeriod, \
                ((1.0 / trig_rate_limit) / 8.0e-9) + 0.5);
      logMsg("INFO: %s: Raised Pulser Period from %d ns to %d ns\n", \
              __func__, pulser_period, vmeRead32(&VSCMpr[id]->PulserPeriod));
    }
  }

#ifdef DEBUG
  logMsg("DEBUG: %s: bcoStart(%u,%u) bcoStop(%u,%u)\n", \
          __func__, bcoStart_i, bcoStart_r, bcoStop_i, bcoStop_r);
#endif
}



/*print fifo*/
void
vscmPrintFIFO(unsigned int *buf, int n)
{
  int i, type, slot;
  unsigned int word, word1;

  for(i=0; i<n; i++)
  {
#ifndef VXWORKS
    word = LSWAP(buf[i]);		
#else
    word = buf[i];		
#endif
    if(word & 0x80000000)
    {
      printf("0x%08X", word);
      type = (word>>27)&0xF;
      switch(type)
      {
        case DATA_TYPE_BLKHDR:
          slot = (word>>22)&0x1f;
          printf(" {BLKHDR} SLOTID: %d", (word>>22)&0x1f);
          printf(" NEVENTS: %d", (word>>11)&0x7ff);
          printf(" BLOCK: %d\n", (word>>0)&0x7ff);	
          break;
        case DATA_TYPE_BLKTLR:
          printf(" {BLKTLR} SLOTID: %d", (word>>22)&0x1f);
          printf(" NWORDS: %d\n", (word>>0)&0x3fffff);
          break;
        case DATA_TYPE_EVTHDR:
          printf(" {EVTHDR} EVENT: %d\n", (word>>0)&0x7ffffff);
          break;
        case DATA_TYPE_TRGTIME:
#ifndef VXWORKS
          word1 = LSWAP(buf[i+1]);
#else
          word1 = buf[i+1];
#endif
          printf(" {TRGTIME} high 24 bits: 0x%06x low 24 bits: 0x%06x",word&0xffffff,word1&0xffffff);
          break;
        case DATA_TYPE_BCOTIME:
          printf(" {BCOTIME} START: %u STOP: %u\n", (word>>0) & 0xFF, (word>>16) & 0xFF);
          break;
        case DATA_TYPE_FSSREVT:
		  if( slot==3 /*(((word>>12)&0x7F)==127)*/ )
		  {
          printf(" {FSSREVT}");
          printf(" HFCBID: %1u", (word>>20)&0x1);
          printf(" CHIPID: %1u", (word>>19)&0x7);
          printf(" CH: %3u", (word>>12)&0x7F);
          printf(" BCO: %3u", (word>>4)&0xFF);
          printf(" ADC: %1u\n", (word>>0)&0x7);
	      }
          else printf(" {FSSREVT}\n");
		  /*
          printf(" {FSSREVT}");
          printf(" HFCBID: %1u", (word>>20)&0x1);
          printf(" CHIPID: %1u", (word>>19)&0x7);
          printf(" CH: %3u", (word>>12)&0x7F);
          printf(" BCO: %3u", (word>>4)&0xFF);
          printf(" ADC: %1u\n", (word>>0)&0x7);
		  */
          break;
        case DATA_TYPE_DNV:
          printf(" {***DNV***}\n");
          return;
        case DATA_TYPE_FILLER:
          printf(" {FILLER}\n");
          break;
        default:
         printf(" {***DATATYPE ERROR***}\n");
         return;
      }
    }
    else
    {
      printf("\n");
    }
  }
  printf("\n");

  return;
}



/*******************************************************************************
 *
 * vscmReadBlock - General Data readout routine
 *
 *    id    - VSCM to read from
 *    data  - local memory address to place data
 *    nwrds - Max number of words to transfer
 *    rflag - Readout Flag
 *              0 - programmed I/O from the specified board
 *              1 - DMA transfer using Universe/Tempe DMA Engine 
 *                    (DMA VME transfer Mode must be setup prior)
 *              2 - Multiblock DMA transfer (Multiblock must be enabled
 *                     and daisychain in place or SD being used)
 *
 * RETURNS: Number of words transferred to data if successful, ERROR otherwise
 *
 */
int
vscmReadBlock(int id, volatile uintptr_t *data, int nwrds, int rflag)
{
  unsigned int vmeAdr;
  int retVal;
  volatile uintptr_t *laddr;

  if(id==0) id=vscmID[0];

  if (vscmIsNotInit(&id, __func__)) return EXIT_FAILURE;

  if (VSCMpf[id] == NULL)
  {
    logMsg("ERROR: %s: VSCM A32 not initialized\n", __func__);
    return EXIT_FAILURE;
  }

  if (data == NULL)
  {
    logMsg("ERROR: %s: Invalid Destination address\n", __func__);
    return EXIT_FAILURE;
  }

  VSCMLOCK;

  if(id==vscmID[0])
    vscmIncTriggerCount();

  /* Block transfer */
  if (rflag >= 1)
  {
    /* Assume that the DMA programming is already setup. 
    Don't Bother checking if there is valid data - that should be done prior
    to calling the read routine */


    laddr = data;
    if(rflag == 2)
    { /* Multiblock Mode */
	  if((vmeRead32(&(VSCMpr[id]->Adr32M))&(1<<26))==0) 
	  {
	    logMsg("ERROR: %s: VSCM in slot %d is not First Board\n",(long)__func__,id, 3, 4, 5, 6);
	    VSCMUNLOCK;
	    return(EXIT_FAILURE);
	  }
	  vmeAdr = (unsigned int)(VSCMpmb) - vscmA32Offset;
	}
    else
	{
  	  vmeAdr = (unsigned int)(VSCMpf[id]) - vscmA32Offset;
	}


#ifdef CODA3DMA
#ifdef VXWORKS
    retVal = sysVmeDmaSend(laddr, VSCM_A32_BASE, (nwrds<<2), 0);
#else
    retVal = vmeDmaSend((unsigned long)laddr, VSCM_A32_BASE, (nwrds<<2));
#endif
#else
    retVal = usrVme2MemDmaStart(vmeAdr, (unsigned int *)laddr, (nwrds<<2));
#endif
    if (retVal |= 0) {
      logMsg("ERROR: %s: DMA transfer Init @ 0x%x Failed\n", __func__, retVal);
      VSCMUNLOCK;
      return retVal;
    }

    /* Wait until Done or Error */
#ifdef CODA3DMA
#ifdef VXWORKS
    retVal = sysVmeDmaDone(10000,1);
#else
    retVal = vmeDmaDone();
#endif
#else
    retVal = usrVme2MemDmaDone();
#endif

    if (retVal > 0) {
#ifdef CODA3DMA
#ifdef VXWORKS
      int xferCount = (nwrds - (retVal>>2));
#else
      int xferCount = ((retVal>>2));
#endif
#else
      int xferCount = ((retVal>>2));
#endif
      VSCMUNLOCK;
      return(xferCount);
    }
    else if(retVal == 0)
    {
      logMsg("WARN: %s: DMA transfer returned zero word count 0x%x\n",__func__,retVal,3,4,5,6);
      VSCMUNLOCK;
      return(0);
    }
    else /* Error in DMA */
    {
      logMsg("ERROR: %s: vmeDmaDone returned an Error %d\n",__func__,2,3,4,5,6);
      VSCMUNLOCK;
      return(0);
    }
  }
  else  /* Programmed IO */
  {
    int dCnt = 0;
    int ii = 0;

    while (ii < nwrds)
    {
      uint32_t val = *VSCMpf[id];
	  /*do not swap - will be done in ROL2
#ifndef VXWORKS
      val = LSWAP(val);
#endif
	  */
/*
      if (val == TI_EMPTY_FIFO)
        break;
#ifndef VXWORKS
      val = LSWAP(val);
#endif
*/
      data[ii] = val;
      ii++;
    }
    ii++;
    dCnt += ii;

    VSCMUNLOCK;
    return dCnt;
  }

  VSCMUNLOCK;
  return EXIT_SUCCESS;
}

void
vscmDisableScalers(int id, int chip)
{
  if (vscmIsNotInit(&id, __func__))
    return;

  vmeWrite32(&VSCMpr[id]->ScalerLatch, 0xFF & ~(1 << chip));
}

void
vscmEnableScalers(int id)
{
  if (vscmIsNotInit(&id, __func__))
    return;

  vmeWrite32(&VSCMpr[id]->ScalerLatch, 0xFF);
}

void
vscmClearScalers(int id, int chip)
{
  int i;

  if (vscmIsNotInit(&id, __func__))
    return;

  for (i = 0; i < 128; i++) {
    vmeRead32(&VSCMpr[id]->Fssr[chip].ScalerStrip);
  }
}

int
vscmReadScalers(int id, int chip, uint32_t *arr)
{
  int i;

  if (vscmIsNotInit(&id, __func__))
    return;

  for (i = 0; i < 128; i++)
    arr[i] = vmeRead32(&VSCMpr[id]->Fssr[chip].ScalerStrip);

  return EXIT_SUCCESS;
}

void
vscmResetToken(int id)
{
  if (vscmIsNotInit(&id, __func__))
    return;
  
  VSCMLOCK;
  vmeWrite32((volatile unsigned int *)&(VSCMpr[id]->Adr32M),
             vmeRead32((volatile unsigned int *)&(VSCMpr[id]->Adr32M)) | (1<<28));
  VSCMUNLOCK;
}

void
vscmFifoClear(int id)
{
	vmeWrite32(&VSCMpr[id]->Reset, 1);
}

void
vscmStat(int id)
{
  if (vscmIsNotInit(&id, __func__))
    return;

  logMsg("VME slot: %d\n", vmeRead32(&VSCMpr[id]->Geo) & 0x1f);
  logMsg("FIFO Word Count: %u\n", vmeRead32(&VSCMpr[id]->FifoWordCnt));
  logMsg("FIFO Event Count: %u\n", vmeRead32(&VSCMpr[id]->FifoEventCnt));
  logMsg("FIFO Block Count: %u\n", vmeRead32(&VSCMpr[id]->FifoBlockCnt));

  /* do OR !!! and clear scalers at Go */
  vmeWrite32(&VSCMpr[id]->ScalerLatch, 0x80000000);

  logMsg("Triggers total/accepted: %u %u\n",
		 vmeRead32(&VSCMpr[id]->ScalerTrigger),
		 vmeRead32(&VSCMpr[id]->ScalerTriggerAccepted));
}

unsigned int
vscmGBready()
{
  unsigned int mask;
  int id, ii;

  mask = 0;
  for(ii=0; ii<nvscm; ii++)
  {
    id = vscmID[ii];

    if(vmeRead32(&VSCMpr[id]->FifoBlockCnt)>0) mask |= (1<<id);
	/*if(vmeRead32(&VSCMpr[id]->FifoEventCnt)>0) mask |= (1<<id);*/
  }

  return(mask);
}


int
vscmGetSerial(int id)
{
  char buf[3];
  int i;

  if (vscmIsNotInit(&id, __func__))
    return -1;

  vscmSelectSpi(id, 0);
  vscmSelectSpi(id, 1);

  vscmTransferSpi(id, 0x03); /* Read Continuous */
  vscmTransferSpi(id, 0x7F);
  vscmTransferSpi(id, 0xF0);
  vscmTransferSpi(id, 0x00);

  memset(buf, 0, sizeof(buf));
  for (i = 0; i < sizeof(buf); i++) {
    buf[i] = vscmTransferSpi(id, 0xFF);
    if (buf[i] == 0x0)
      break;
    if (buf[i] == 0xFF) {
      buf[0] = 0x0;
      break;
    }
  }
  vscmSelectSpi(id, 0);
  return atoi(buf);
}

void
vscmSetBCOFreq(int id, uint32_t freq)
{
  if (vscmIsNotInit(&id, __func__))
    return;

  vmeWrite32(&VSCMpr[id]->FssrClkCfg, freq);
}

uint8_t
vscmSetDacCalibration(int id)
{
  uint8_t result;

  vscmSelectSpi(id, 0);
  vscmSelectSpi(id, 1);

  vscmTransferSpi(id, 0x03); /* Read Continuous */
  vscmTransferSpi(id, 0x7F);
  vscmTransferSpi(id, 0xF0);
  vscmTransferSpi(id, 0x80);
  result = vscmTransferSpi(id, 0xFF);
  vscmSelectSpi(id, 0);

  /* Do this to load calibration value into DAC */
  vmeWrite32(&VSCMpr[id]->DacCfg, (0<<16) | 0x80000D00 | (result & 0x3F));
  /*  vmeWrite32(&VSCMpr[id]->DacCfg, (8192<<16)); */
  myDelay(1);

#ifdef DEBUG
  logMsg("INFO: %s: DAC Calibration = %d\n", __func__, result);
#endif

  return result;
}

/* freq = rate in Hz for calibration pulser */
void
vscmSetPulserRate(int id, uint32_t freq)
{
  uint32_t periodCycles;
  uint32_t dutyCycles;
  uint32_t window;
  uint32_t trig_rate_limit;
  uint32_t bcoFreq;

  if (vscmIsNotInit(&id, __func__))
    return;

  if (!freq) {
    periodCycles = VSCM_SYS_CLK;
    dutyCycles = VSCM_SYS_CLK + 1;
  }
  else {
    /* subtract 1 since index is from 0 */
    periodCycles = (VSCM_SYS_CLK / freq) - 1;
    /* Always run at 50% duty cycle */
    dutyCycles = periodCycles >> 1;

    if (!dutyCycles) dutyCycles = 1;
    if (!periodCycles) periodCycles = 2;
  }

  /* Check to see if need to limit rate only if window is already set */
  if ((window = vmeRead32(&VSCMpr[id]->TriggerWindow))) {
    bcoFreq = vmeRead32(&VSCMpr[id]->FssrClkCfg);
    window = ((window >> 24 & 0xFF) - (window >> 8 & 0xFF) - 1) * bcoFreq;
    trig_rate_limit = 50000000 / (16 * (1 + window / bcoFreq));
    if (freq > trig_rate_limit) {
      logMsg("INFO: %s: Raised Pulser Period from %d ns ", \
              __func__, periodCycles); 
      periodCycles = (int)((1.0 / trig_rate_limit) / 8.0e-9) + 0.5;
      logMsg("to %d ns\n", periodCycles);
    }
  }

  vmeWrite32(&VSCMpr[id]->PulserHigh, dutyCycles);
  vmeWrite32(&VSCMpr[id]->PulserPeriod, periodCycles);

#ifdef DEBUG
  logMsg("%s: Pulser setup (%u, %u)\n", __func__, periodCycles, dutyCycles);
#endif
}

/* Get the rate (in Hz) for the calibration pulser */
uint32_t
vscmGetPulserRate(int id)
{
  if (vscmIsNotInit(&id, __func__))
    return;

  return (VSCM_SYS_CLK / (vmeRead32(&VSCMpr[id]->PulserPeriod) + 1));
}

/* ch = which pulser connector to manipulate: 0 = both, 1 = top, 2 = bottom
 * amp = amplitude in millivolts (mV)
 * num_pulses = number of pulses to deliver - default to freerunning */
void
vscmPulser(int id, int ch, uint32_t amp, uint32_t num_pulses)
{
  const int length = 128;
  uint32_t i, val, unum;

  if (vscmIsNotInit(&id, __func__))
    return;

	if (ch < 0 || ch > 2) {
    logMsg("ERROR: %s Invalid channel, must be 0, 1 or 2\n", __func__);
    return;
  }

  /* Convert amplitude to DAC units
   * 1e3 factor is to convert into mV
   * Factor of 2 is from 50ohm termination */
  amp /= ((1.0 / (8192 * 2)) * 1e3);

  if (!num_pulses)
    unum = 0xFFFFFFFF;
  else
    unum = num_pulses;

  vmeWrite32(&VSCMpr[id]->DacCfg, (8192 << 16));
  vmeWrite32(&VSCMpr[id]->PulserN, unum);

  for (i = 0; i < length; i++) {
    /* set first and last entries to "0" */
    if (i == 0 || i == (length - 1)) {
      val = 8192;
    }
    else {
      val = 8192 + amp;
    }

    if (ch == 0 || ch == 1) {
			vmeWrite32(&VSCMpr[id]->DacCh0,	(i << 23) | ((length - 1) << 14) | val);
		}
		if (ch == 0 || ch == 2) {	
      vmeWrite32(&VSCMpr[id]->DacCh1,	(i << 23) | ((length - 1) << 14) | val);
    }
  }
}

void
vscmPulserStart(int id)
{
  if (vscmIsNotInit(&id, __func__))
    return;

  vmeWrite32(&VSCMpr[id]->PulserStart, 1);
}

void
vscmSetHitMask(int id, uint32_t mask)
{
	vmeWrite32(&VSCMpr[id]->FssrHitReg, mask);
}

int
vscmIsNotInit(int *id, const char *func)
{
  if (*id == 0) *id = vscmID[0];

  if ((*id <= 0) || (*id > 21) || (VSCMpr[*id] == NULL))
  {
    logMsg("ERROR: %s: VSCM in slot %d is not initialized\n", func, *id);
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}

void
vscmSetBlockLevel(int id, int block_level)
{
  vmeWrite32(&VSCMpr[id]->BlockCfg, block_level);
}

void
vscmPrestart(char *fname)
{
  int ii;

vscmSetMaxTriggerLimit(0);
vscmClearTriggerCount();

  for(ii=0; ii<nvscm; ii++)
  {
    vscmFifoClear(vscmID[ii]); /* clear FIFO */
	
    /*vscmSWSync(vscmID[ii]);*/ /* Issue S/W sync: comment out if hardware sync is expected*/

    fssrMasterReset(vscmID[ii]);
  }

  myDelay(10);
  
	/* Initialize FSSR2 chips*/
  for(ii=0; ii<nvscm; ii++)
  {
    /* change ID for PP modules */
     fssrSetChipID(vscmID[ii], 0, 1, 2, 6, 4);
	 vscmConfigDownload(vscmID[ii], fname);
  }
}

int
vscmInit(uintptr_t addr, uint32_t addr_inc, int numvscm)
{
  uintptr_t rdata;
  uintptr_t laddr, laddr2;
  volatile struct VSCM_regs *vreg;
  int i, ii, res;
  int boardID = 0;
  uint32_t firmware;
  uintptr_t a32addr;

  /* Check for valid address */
  if (addr == 0)
  {
    logMsg("ERROR: %s: Must specify VME-based A24 address for VSCM 0\n", \
            __func__);
    return(EXIT_FAILURE);
  }
  else if (addr > 0xFFFFFF)\
  {
    logMsg("ERROR: %s: A32 addressing not allowed\n", __func__);
    return(EXIT_FAILURE);
  }
  else
  {
    if ((addr_inc == 0) || (numvscm == 0)) numvscm = 1;

#ifdef VXWORKS
    res = sysBusToLocalAdrs(0x39, (char *)addr, (char **)&laddr);
#else
    res = vmeBusToLocalAdrs(0x39, (char *)addr, (char **)&laddr);
#endif
    if (res != 0)
    {
#ifdef VXWORKS
      logMsg("ERROR: %s: sysBusToLocalAdrs(0x39, 0x%x, &laddr)\n", \
              __func__, addr);
#else
      logMsg("ERROR: %s: vmeBusToLocalAdrs(0x39, %p, &laddr)\n", \
              __func__, (void *)addr);
#endif
      return(EXIT_FAILURE);
    }
    /*else
	{
      logMsg("INFO: addr=0x%08x, AM=0x39, laddr=0x%08x\n",addr,laddr,0,0,0,0);
	}*/

    vscmA24Offset = laddr - addr;

    vscmInited = nvscm = 0;
    bzero((char *)vscmID, sizeof(vscmID));

    for(i=0; i<numvscm; i++)
    {
      vreg = (struct VSCM_regs *)(laddr + (i * addr_inc));
      /*logMsg("INFO: probing vreg->BoardID=0x%08x (vreg=0x%08x)\n",&(vreg->BoardID),vreg,0,0,0,0);*/
#ifdef VXWORKS
      res = vxMemProbe((char *)&(vreg->BoardID), VX_READ, 4, (char *)&rdata);
#else
      res = vmeMemProbe((char *)&(vreg->BoardID), 4, (char *)&rdata);
#endif
      if(res < 0)
      {
#ifdef VXWORKS
        logMsg("ERROR: %s: No addressable board at addr=0x%x\n", __func__, (uint32_t)vreg);
#else
        logMsg("ERROR: %s: No addressable board at VME addr=%p (%p)\n", __func__, (void *)(addr + (i * addr_inc)), (void *)vreg);
#endif
      }
      else
      {
        if (rdata != VSCM_BOARD_ID)
        {
          logMsg("ERROR: %s: For board at %p, Invalid Board ID: %p\n",
                  __func__, (void *)vreg, (void *)rdata);
          break;
        }
        boardID = vmeRead32(&vreg->Geo) & 0x1F;
        if ((boardID <= 0) || (boardID > 21))
        {
          logMsg("ERROR: %s: Board Slot ID %d is not in range.\n", \
                  __func__, boardID);
          return(EXIT_FAILURE);
        }

        VSCMpr[boardID] = (struct VSCM_regs *)(laddr + (i * addr_inc));
        vscmID[nvscm] = boardID; /* slot number */
        if (boardID >= maxSlot) maxSlot = boardID;
        if (boardID <= minSlot) minSlot = boardID;

        nvscm++;

#ifdef VXWORKS
        firmware = VSCMpr[boardID]->FirmwareRev;
#else
        firmware = LSWAP(VSCMpr[boardID]->FirmwareRev);
#endif

        logMsg("INFO: found VSCM board at slot %2d (firmware 0x%08x)\n",boardID,firmware,0,0,0,0);
      }
    }




    /* Setup FIFO pointers */
    for(i=0; i<nvscm; i++)
    {
      a32addr = VSCM_A32_BASE + (i * VSCM_MAX_FIFO);
      /* Event readout setup */
      vmeWrite32(&VSCMpr[vscmID[i]]->AD32, ((a32addr >> 16) & 0xFF80) | 0x0001);
#ifdef VXWORKS
      res = sysBusToLocalAdrs(0x09, (char *)a32addr, (char **)&laddr2);
      if (res != 0) {
        logMsg("ERROR: %s: sysBusToLocalAdrs(0x09, 0x%x, &laddr2)\n", \
                __func__, a32addr);
        return(EXIT_FAILURE);
      }
#else
      res = vmeBusToLocalAdrs(0x09, (char *)a32addr, (char **)&laddr2);
      if (res != 0) {
        logMsg("ERROR: %s: vmeBusToLocalAdrs(0x09, %p, &laddr2)\n", \
                __func__, (void *)a32addr);
        return(EXIT_FAILURE);
      }
#endif
      VSCMpf[vscmID[i]] = (uintptr_t *)laddr2;

      vscmA32Offset = laddr2 - a32addr; /* will be the same for every iteration, do not care ... */
	  /*printf("i=%d vscmA32Offset=0x%08x\n",i,vscmA32Offset);*/
    }



    /* If there are more than 1 VSCM in the crate then setup the Muliblock Address
       window. This must be the same on each board in the crate */
    if(nvscm > 1) 
    {
      a32addr = VSCM_A32_BASE + nvscm*VSCM_MAX_FIFO; /* set MB base above individual board base */
#ifdef VXWORKS
      res = sysBusToLocalAdrs(0x09,(char *)a32addr,(char **)&laddr);
      if (res != 0) 
      {
        printf("ERROR: %s: in sysBusToLocalAdrs(0x09,0x%x,&laddr) \n", (long)__func__, a32addr);
        return(EXIT_FAILURE);
      }
#else
      res = vmeBusToLocalAdrs(0x09,(char *)a32addr,(char **)&laddr);
      if (res != 0) 
      {
	    printf("ERROR: %s: in vmeBusToLocalAdrs(0x09,0x%x,&laddr) \n", (long)__func__, a32addr);
	    return(EXIT_FAILURE);
      }
#endif
      VSCMpmb = (unsigned int *)(laddr);  /* Set a pointer to the FIFO */
      if(1/*!noBoardInit*/)
      {
	    for (ii=0;ii<nvscm;ii++) 
	    {
	      /* Write the register and enable */
          vmeWrite32((volatile unsigned int *)&(VSCMpr[vscmID[ii]]->Adr32M), ((a32addr+VSCM_MAX_A32MB_SIZE)>>7) | (a32addr>>23) | (1<<25));
	    }
      }    
      /* Set First Board and Last Board */
      vscmMaxSlot = maxSlot;
      vscmMinSlot = minSlot;
      if(1/*!noBoardInit*/)
      {
        vmeWrite32((volatile unsigned int *)&(VSCMpr[minSlot]->Adr32M),
                   vmeRead32((volatile unsigned int *)&(VSCMpr[minSlot]->Adr32M)) | (1<<26));
        vmeWrite32((volatile unsigned int *)&(VSCMpr[maxSlot]->Adr32M),
                   vmeRead32((volatile unsigned int *)&(VSCMpr[maxSlot]->Adr32M)) | (1<<27));
      }
    }




    /* Setup VSCM */
    for(i=0; i<nvscm; i++)
    {
      boardID = vscmID[i]; /* slot number */

      /* get clock from switch slot B (2,0-int, 3,1-ext)*/
      vmeWrite32(&VSCMpr[boardID]->ClockCfg, /*2*/3); /* sets clock */
      vmeWrite32(&VSCMpr[boardID]->ClockCfg, /*0*/1); /* release reset */

      /* Setup Front Panel and Trigger */
      vmeWrite32(&VSCMpr[boardID]->FpOutput[0], IO_MUX_FPINPUT1 | (384<<16));
      vmeWrite32(&VSCMpr[boardID]->FpOutput[1], IO_MUX_FPINPUT1 | (384<<16));
      vmeWrite32(&VSCMpr[boardID]->FpOutput[2], IO_MUX_DACTRIGGERED);
      vmeWrite32(&VSCMpr[boardID]->FpOutput[3], IO_MUX_DACTRIGGERED_DLY);

      /* get trigger from switch slot B */
      vmeWrite32(&VSCMpr[boardID]->Trigger, IO_MUX_SWB_TRIG1/*IO_MUX_FPINPUT0*/);

      /* get sync from switch slot B */
      vmeWrite32(&VSCMpr[boardID]->Sync, IO_MUX_SWB_SYNC);

      /* busy to switch slot B */
      vmeWrite32(&VSCMpr[boardID]->SwBGpio, IO_MUX_BUSY | (1<<24));

      /* for token only - will always do it */
      vmeWrite32(&VSCMpr[boardID]->TokenInCfg, IO_MUX_TOKENIN);
      vmeWrite32(&VSCMpr[boardID]->TokenOutCfg, IO_MUX_TOKENOUT);


      /* the number of events per block */
      vmeWrite32(&VSCMpr[boardID]->BlockCfg, 1);

      /* */
      /*vmeWrite32(&VSCMpr[boardID]->DACTrigger, IO_MUX_PULSER | 0x80000000 | (0<<16));*/





      /* Enable Bus Error */
      vmeWrite32(&VSCMpr[boardID]->ReadoutCfg, 1);

      /* Disable Bus Error 
      vmeWrite32(&VSCMpr[boardID]->ReadoutCfg, 0);
*/


      /* Setup VSCM Pulser */
      vscmSetDacCalibration(boardID);
      vscmSetPulserRate(boardID, 200000);



      vmeWrite32(&VSCMpr[boardID]->DACTrigger, IO_MUX_FPINPUT3);
      vscmPulser(boardID, 0, 100, 1);
      vscmPulser(boardID, 1, 100, 1);


      /* FSSR Clock & Triggering setup */
      /*vmeWrite32(&VSCMpr[boardID]->ClockCfg, 0);*/
      vscmSetBCOFreq(boardID, 16);
      vscmSetTriggerWindow(boardID, 128, 512, 16);

      /* delay for trigger processing in a board - must be more then 4us for 70MHz readout clock;
      if clock changed, it must be changes as well; ex. 35MHz -> 1024 etc */
      vmeWrite32(&VSCMpr[boardID]->TrigLatency, 0);

      /* Clear event buffers */
      vscmFifoClear(boardID);
	
      vscmSWSync(boardID);
      fssrMasterReset(boardID);
    }

#ifdef CODA3DMA

    /* VME DMA setup */
#ifdef VXWORKS
    VME_DMAInit();
    VME_DMAMode(DMA_MODE_BLK32);
#else
    vmeDmaConfig(2, 5, 1);

    dmaPFreeAll();
    vmeIN = dmaPCreate("vmeIN", 2048, 10, 0);
    vmeOUT = dmaPCreate("vmeOUT", 0, 0, 0);
#ifdef DEBUG
    dmaPStatsAll();
#endif
    dmaPReInitAll();
#endif

#endif

  }

  logMsg("INFO: found %d VSCM boards\n",nvscm,0,0,0,0,0);
  if (nvscm > 16) {
    logMsg("WARNING: There are only 16 payload slots in a VXS Crate\n");
  }
  return(nvscm);
}




/* vscmFirmware.c */

#define FLASH_CMD_WRPAGE      0x02
#define FLASH_CMD_RD          0x03
#define FLASH_CMD_GETSTATUS   0x05
#define FLASH_CMD_WREN        0x06
#define FLASH_CMD_GETID       0x9F
#define FLASH_CMD_ERASE64K    0xD8

#define FLASH_BYTE_LENGTH     8*1024*1024
#define FLASH_MFG_WINBOND     0xEF
#define FLASH_DEVID_W25Q64    0x4017

void
vscmSelectSpi(int id, int sel)
{
  if(sel)
    vmeWrite32(&VSCMpr[id]->SpiFlash, 0x0);
  else
    vmeWrite32(&VSCMpr[id]->SpiFlash, 0x4);
}

uint8_t
vscmTransferSpi(int id, uint8_t data)
{
  int i;
  uint8_t rsp = 0;

  for(i = 0; i < 8; i++) {
    vmeWrite32(&VSCMpr[id]->SpiFlash, ((data >> 7) & 0x1));
    rsp = (rsp << 1) | (vmeRead32(&VSCMpr[id]->SpiFlash) & 0x1);
    vmeWrite32(&VSCMpr[id]->SpiFlash, 0x2 | ((data >> 7) & 0x1));
    data <<= 1;
  }
  return rsp;
}

void
vscmFlashGetID(int id, uint8_t *rsp)
{
	vscmSelectSpi(id, 1);
	vscmTransferSpi(id, FLASH_CMD_GETID);
	rsp[0] = vscmTransferSpi(id, 0xFF);
	rsp[1] = vscmTransferSpi(id, 0xFF);
	rsp[2] = vscmTransferSpi(id, 0xFF);
	vscmSelectSpi(id, 0);
}

uint8_t
vscmFlashGetStatus(int id)
{
  uint8_t rsp;

  vscmSelectSpi(id, 1);
  vscmTransferSpi(id, FLASH_CMD_GETSTATUS);
  rsp = vscmTransferSpi(id, 0xFF);
  vscmSelectSpi(id, 0);

  return rsp;
}

void
vscmReloadFirmware(int id)
{
  int i;
  uint16_t reloadSequence[] = {
    0xFFFF, 0xAA99, 0x5566, 0x3261,
    0x0000, 0x3281, 0x0B00, 0x32A1,
    0x0000, 0x32C1, 0x0B00, 0x30A1,
    0x000E, 0x2000
  };

  if (vscmIsNotInit(&id, __func__))
    return;

  vmeWrite32(&VSCMpr[id]->ICap, 0x40000 | 0x00000);
  vmeWrite32(&VSCMpr[id]->ICap, 0x40000 | 0x20000);
  for (i = 0; i < (sizeof(reloadSequence) / sizeof(reloadSequence[0])); i++) {
    vmeWrite32(&VSCMpr[id]->ICap, 0x00000 | reloadSequence[i]);
    vmeWrite32(&VSCMpr[id]->ICap, 0x20000 | reloadSequence[i]);
  }
  for (i = 0; i < 10; i++) {
    vmeWrite32(&VSCMpr[id]->ICap, 0x40000 | 0x00000);
    vmeWrite32(&VSCMpr[id]->ICap, 0x40000 | 0x20000);
  }
  printf("Firmware .. ");fflush(stdout);
  sleep(2); /* 2 sec delay to finish reloading */
  printf("is ready !!!\n");fflush(stdout);
}

int
vscmFirmwareUpdate(int id, const char *filename)
{
  uint8_t rspId[3];

  if (vscmIsNotInit(&id, __func__))
    return EXIT_FAILURE;

  vscmSelectSpi(id, 0);
  vscmFlashGetID(id, rspId);
#ifdef DEBUG
  printf("Flash: Mfg=0x%02X, Type=0x%02X, Capacity=0x%02X\n", \
          rspId[0], rspId[1], rspId[2]);
#endif
  if ((rspId[0] == FLASH_MFG_WINBOND) && \
      (rspId[1] == (FLASH_DEVID_W25Q64>>8)) && \
      (rspId[2] == (FLASH_DEVID_W25Q64&0xFF))) {

    FILE *f;
    int i;
    unsigned int addr = 0;
    uint8_t buf[256];

    f = fopen(filename, "rb");
    if (!f) {
      printf("%s: ERROR: vscmFirmwareUpdate invalid file %s\n", __func__, filename);
      return EXIT_FAILURE;
    }


    memset(buf, 0xff, 256);
    while (fread(buf, 1, 256, f) > 0) {

      /* sector erase */
      if (!(addr % 65536)) {
        vscmSelectSpi(id, 1);
        vscmTransferSpi(id, FLASH_CMD_WREN); /* write enable */
        vscmSelectSpi(id, 0);

        vscmSelectSpi(id, 1);
        vscmTransferSpi(id, FLASH_CMD_ERASE64K); /* 64k sector erase */
        vscmTransferSpi(id, (addr>>16)&0xFF);
        vscmTransferSpi(id, (addr>>8)&0xFF);
        vscmTransferSpi(id, (addr)&0xFF);
        vscmSelectSpi(id, 0);

        printf(".");
        fflush(stdout);
        i = 0;
        while (1) {
          if (!(vscmFlashGetStatus(id) & 0x1))
            break;

          taskDelay(1);

          /* 1000ms maximum sector erase time */
          if (i == (60 + 6)) {
            fclose(f);
            printf("%s: ERROR: Failed to erase flash\n", __func__);
            return EXIT_FAILURE;
          }
          i++;
        }
      }

      vscmSelectSpi(id, 1);
      vscmTransferSpi(id, FLASH_CMD_WREN); /* write enable */
      vscmSelectSpi(id, 0);

      vscmSelectSpi(id, 1);
      vscmTransferSpi(id, FLASH_CMD_WRPAGE); /* write page */
      vscmTransferSpi(id, ((addr >> 16) & 0xFF));
      vscmTransferSpi(id, ((addr >> 8) & 0xFF));
      vscmTransferSpi(id, (addr & 0xFF));

      for (i = 0; i < 256; i++)
        vscmTransferSpi(id, buf[i]);
      vscmSelectSpi(id, 0);

      i = 0;
      while (1) {
        if (!(vscmFlashGetStatus(id) & 0x1)) /* no faster than 1us per call */
          break;
        /* 3ms maximum page program time */
        if (i == 3000) {
          fclose(f);
          printf("%s: ERROR: Failed to program flash\n", __func__);
          return EXIT_FAILURE;
        }
        i++;
      }
      memset(buf, 0xff, 256);
      addr += 256;
    }
    fclose(f);
  }
  else {
    printf("%s: ERROR: Failed to identify flash id (or device not supported)\n", __func__);
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}

int
vscmFirmwareVerify(int id, const char *filename)
{
  uint8_t rspId[3];

  if (vscmIsNotInit(&id, __func__))
    return EXIT_FAILURE;

  vscmSelectSpi(id, 0);
  vscmFlashGetID(id, rspId);
#ifdef DEBUG
  printf("Flash: Mfg=0x%02X, Type=0x%02X, Capacity=0x%02X\n", \
          rspId[0], rspId[1], rspId[2]);
#endif

  if ((rspId[0] == FLASH_MFG_WINBOND) && \
      (rspId[1] == (FLASH_DEVID_W25Q64>>8)) && \
      (rspId[2] == (FLASH_DEVID_W25Q64&0xFF))) {

    FILE *f;
    int i, len;
    unsigned int addr = 0;
    uint8_t buf[256], val;

    f = fopen(filename, "rb");
    if (!f) {
      printf("%s: ERROR: Invalid file %s\n", __func__, filename);
      return EXIT_FAILURE;
    }

    vscmSelectSpi(id, 1);
    vscmTransferSpi(id, FLASH_CMD_RD); /* continuous array read */
    vscmTransferSpi(id, ((addr >> 16) & 0xFF));
    vscmTransferSpi(id, ((addr >> 8) & 0xFF));
    vscmTransferSpi(id, (addr & 0xFF));

    while ((len = fread(buf, 1, 256, f)) > 0) {
      for (i = 0; i < len; i++) {
        val = vscmTransferSpi(id, 0xFF);
        if (buf[i] != val) {
          vscmSelectSpi(id, 0);
          fclose(f);
          printf("ERROR: %s: Failed verify at addr 0x%08X[%02X,%02X]\n", \
                  __func__, addr+i, buf[i], val);
          return EXIT_FAILURE;
        }
      }
      addr += 256;
      if ((addr % 65536) == 0) {
        printf(".");
        fflush(stdout);

        taskDelay(1);
      }
    }
    vscmSelectSpi(id, 0);
    fclose(f);
  }
  else {
    printf("ERROR: %s: Failed to identify flash\n", __func__);
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}

int
vscmFirmwareUpdateVerify(int id, const char *filename)
{
  int result;

  if (vscmIsNotInit(&id, __func__))
    return EXIT_FAILURE;

  printf("Updating firmware for VSCM board at slot %d using file >%s< ...\n",id,filename);
  result = vscmFirmwareUpdate(id, filename);
  if (result != 0) {
    printf("failed.\n");
    return result;
  }
  else
    printf("succeeded.\n");

  printf(" Verifying...\n");
  result = vscmFirmwareVerify(id, filename);
  if (result != 0)
  {
    printf("failed.\n");
    return result;
  }
  else
  {
    printf("ok.\n");
  }

  vscmReloadFirmware(id);
		
  return EXIT_SUCCESS;
}

int
vscmFirmwareRead(int id, const char *filename)
{
  uint8_t rspId[3];

  if (vscmIsNotInit(&id, __func__))
    return EXIT_FAILURE;

  vscmSelectSpi(id, 0);
  vscmFlashGetID(id, rspId);
#ifdef DEBUG
  printf("Flash: Mfg=0x%02X, Type=0x%02X, Capacity=0x%02X\n", \
          rspId[0], rspId[1], rspId[2]);
#endif
  if ((rspId[0] == FLASH_MFG_WINBOND) && \
      (rspId[1] == (FLASH_DEVID_W25Q64>>8)) && \
      (rspId[2] == (FLASH_DEVID_W25Q64&0xFF))) {

    FILE *f;
    int i;
    unsigned int addr = 0;

    f = fopen(filename, "wb");
    if (!f) {
      printf("%s: ERROR: Invalid file %s\n", __func__, filename);
      return EXIT_FAILURE;
    } 

    vscmSelectSpi(id, 1);
    vscmTransferSpi(id, FLASH_CMD_RD); /* continuous array read */
    vscmTransferSpi(id, ((addr >> 16) & 0xFF));
    vscmTransferSpi(id, ((addr >> 8) & 0xFF));
    vscmTransferSpi(id, (addr & 0xFF));
		
    for (i = 0; i < FLASH_BYTE_LENGTH; i++) {
      fputc(vscmTransferSpi(id, 0xFF), f);
      if (!(i% 65536)) {
        printf(".");

        taskDelay(1);
      }
    }
    vscmSelectSpi(id, 0);
    fclose(f);
  }
  else {
    printf("ERROR: %s: Failed to identify flash\n", __func__);
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}


void
vscmFirmware(char *filename, int slot)
{
  int nvscm;
  int ii, id;

  nvscm = vscmInit((unsigned int)(3<<19),(1<<19),20);

  if(slot<0) /* do nothing */
  {
    ;
  }
  else if(slot==0) /* do all boards */
  {
    for(ii=0; ii<nvscm; ii++)
    {
      id = vscmID[ii];
      vscmFirmwareUpdateVerify(id, filename);
    }
  }
  else /* do one board */
  {
    vscmFirmwareUpdateVerify(slot, filename);
  }

  return;
}





#else /* dummy version*/

void
vscmLib_dummy()
{
  return;
}

#endif
