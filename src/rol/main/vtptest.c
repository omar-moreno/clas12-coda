#if defined(Linux_armv7l)

#include <sys/mman.h>
#include <sys/signal.h>
#include <sys/time.h>
#include <sys/types.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "vtpLib.h"

void sig_handler(int signo);

int load_firmware()
{
  FILE *f;
  char buf[1000], host[100], hostfile[100], z7file[100], v7file[100];
  int i;

  sprintf(buf, "%s/firmwares/vtp_firmware.txt", getenv("CLON_PARMS"));
  f = fopen(buf, "rt");
  if(!f)
  {
    printf("%s: Error - failed to open: %s\n", __func__, buf);
    return -1;
  }

  gethostname(host,sizeof(host));
  for(i=0; i<strlen(host); i++)
  {
    if(host[i] == '.')
    {
      host[i] = '\0';
      break;
    }
  }

  while(!feof(f))
  {
    fgets(buf, sizeof(buf), f);

    if(sscanf(buf, "%100s %100s %100s", hostfile, z7file, v7file) >= 3)
    {
      if(!strcmp(hostfile, host))
      {
        printf("%s: Found host %s, z7file: %s, v7file: %s\n", __func__, hostfile, z7file, v7file);

        sprintf(buf, "%s/firmwares/%s", getenv("CLON_PARMS"), z7file);
        if(vtpZ7CfgLoad(buf) != OK)
        {
          printf("Z7 programming failed...\n");
          return -1;
        }

        sprintf(buf, "%s/firmwares/%s", getenv("CLON_PARMS"), v7file);
        if(vtpV7CfgLoad(buf) != OK)
        {
          printf("V7 programming failed...\n");
          return -1;
        }
        
        printf("Programming succeeded!\n");
        break;
      }
    }
  }

  fclose(f);
  return 0;
}

#define BUF_LEN  10000

int
main(int argc, char *argv[])
{
  int stat, i, len, evtNum=0;
  pthread_t gScalerThread;
  unsigned int buf[BUF_LEN];

  if(signal(SIGINT, sig_handler) == SIG_ERR)
  {
    perror("signal");
    exit(0);
  }


  stat = vtpOpen(VTP_FPGA_OPEN|VTP_I2C_OPEN|VTP_SPI_OPEN);
  if(stat != (VTP_FPGA_OPEN|VTP_I2C_OPEN|VTP_SPI_OPEN))
    goto CLOSE;
  else
    printf("vtpOpen'ed\n");

  /* read vtp firmware table and load into vtp here */
/*  if(load_firmware())
  {
    printf("Unabled to load firmware - exiting...\n");
    goto CLOSE;
  }
*/

  if(vtpInit(VTP_INIT_CLK_INT))
  {
    printf("VTP Init failed - exiting...\n");
    goto CLOSE;
  }

  vtpSetTrig1Source(VTP_SD_TRIG1SEL_0);

  vtpSetSyncSource(VTP_SD_SYNCSEL_1);
  vtpSetSyncSource(VTP_SD_SYNCSEL_0);

  vtpSetBlockLevel(1);

  for(i=0;i<32;i++)
    vtpSetGtTriggerBit(i,0,0,0,0,0,0,0.0E6);

//  vtpSetWindow(4000, 400);
  vtpSetWindow(4000,4);

  vtpEbResetFifo(1);
  vtpEbResetFifo(0);

  usleep(50);

//  for(i=0;i<40*8*10;i++)
  for(i=0;i<515;i++)
  {
    vtpSetTrig1Source(VTP_SD_TRIG1SEL_1);
    vtpSetTrig1Source(VTP_SD_TRIG1SEL_0);
    usleep(50);
  }

  
  while(1)
  {
    len = vtpEbReadEvent_test(buf, BUF_LEN);
    printf("EventNumber=%d, Len=%d\n", evtNum++, len);
    for(i=0;i<len;i++)
    {
      if(!(i%8))
        printf("\n0x%04X:", i*4);
      printf(" %08X", buf[i]);
    }
    printf("\n");

    if(!len)
    {
      vtpSetTrig1Source(VTP_SD_TRIG1SEL_1);
      vtpSetTrig1Source(VTP_SD_TRIG1SEL_0);
      usleep(1000000);
      printf(".");
    }
  }

/*
  vtpEnableTriggerFiberMask(0xF);

  vtpSetFPASel(0x00000000);
  vtpSetFPBSel(0x00000000);
  while(1)
  {
    printf("L");
    vtpSetFPAO(0x00000000);
    vtpSetFPBO(0x00000000);
    usleep(1000);
    printf("H");
    vtpSetFPAO(0xFFFFFFFF);
    vtpSetFPBO(0xFFFFFFFF);
    usleep(1000);
  }
*/

CLOSE:

  vtpClose(VTP_FPGA_OPEN|VTP_I2C_OPEN|VTP_SPI_OPEN);
  printf("vtpClose'd\n");
  
  return 0;
}

void
closeup()
{
  vtpClose(VTP_FPGA_OPEN|VTP_I2C_OPEN|VTP_SPI_OPEN);
  printf("DiagGUI server closed...\n");
}

void
sig_handler(int signo)
{
  printf("%s: signo = %d\n",__FUNCTION__,signo);
  switch(signo)
  {
    case SIGINT:
      closeup();
      exit(1);  /* exit if CRTL/C is issued */
  }
}

#else

main()
{
  return;
}

#endif
