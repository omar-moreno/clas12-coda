#if defined(VXWORKS) || defined(Linux_vme)

/****************************************************************************
 *
 *  tiConfig.c  -  configuration library file for ti board 
 *
 
config file format:

TI_CRATE      <tdcecal1>     <- crate name, usually IP name

TI_ADD_SLAVE 1                                 # for every slave need to be added

TI_FIBER_DELAY_OFFSET 0x80 0xcf                # fiber delay and offsets

TI_BLOCK_LEVEL 1                               # the number of events in readout block

TI_BUFFER_LEVEL 1                              # 0 - pipeline mode, 1 - ROC Lock mode, >=2 - buffered mode

*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifdef Linux_vme
#include "jvme.h"
#endif

#include "tiLib.h"
#include "xxxConfig.h"

#define FNLEN     128       /* length of config. file name */
#define STRLEN    250       /* length of str_tmp */
#define ROCLEN     80       /* length of ROC_name */

#define MAXSLAVES 100

static int active;

static int is_slave;
static nslave, slave_list[MAXSLAVES];
static unsigned int delay, offset;
static int block_level;
static int buffer_level;
static int input_prescale[6];

/* tiInit() have to be called before this function */
int  
tiConfig(char *fname)
{
  int res;
  char *string; /*dummy, will not be used*/

  if(strlen(fname) > 0) /* filename specified  - upload initial settings from the hardware */
  {
    tiUploadAll(string, 0);
  }
  else /* filename not specified  - set defaults */
  {
    tiInitGlobals();
  }

  /* read config file */
  if( (res = tiReadConfigFile(fname)) < 0 ) return(res);

  /* download to all boards */
  tiDownloadAll();

  return(0);
}


int
tiInitGlobals()
{
  int ii, jj;

  is_slave = 0;
  delay = 0x80;
  offset = 0xcf;
  block_level = 1;
  buffer_level = 1;
  nslave = 0;
  for(ii=0; ii<MAXSLAVES; ii++) slave_list[ii] = 0;
  for(ii=0; ii<6; ii++) input_prescale[ii] = 0;

  return(0);
}


/**/
int
tiReadConfigFile(char *filename)
{
  FILE   *fd;
  char   fname[FNLEN] = { "" };  /* config file name */
  int    ii, jj, ch;
  char   str_tmp[STRLEN], keyword[ROCLEN];
  char   host[ROCLEN], ROC_name[ROCLEN];
  char   str2[2];
  int    args, i1, i2;
  int    slot, chan;
  unsigned int  ui1, ui2;
  char *getenv();
  char *clonparms;
  char *expid;

  clonparms = getenv("CLON_PARMS");
  expid = getenv("EXPID");
  if(strlen(filename)!=0) /* filename specified */
  {
    if ( filename[0]=='/' || (filename[0]=='.' && filename[1]=='/') )
	{
      sprintf(fname, "%s", filename);
	}
    else
	{
      sprintf(fname, "%s/ti/%s", clonparms, filename);
	}

    if((fd=fopen(fname,"r")) == NULL)
    {
      printf("\nReadConfigFile: Can't open config file >%s<\n",fname);
      return(-1);
    }
  }
  else /* filename does not specified */
  {
    /* obtain our hostname */
    gethostname(host,ROCLEN);
    sprintf(fname, "%s/ti/%s.cnf", clonparms, host);
    if((fd=fopen(fname,"r")) == NULL)
    {
      sprintf(fname, "%s/ti/%s.cnf", clonparms, expid);
      if((fd=fopen(fname,"r")) == NULL)
      {
        printf("\nReadConfigFile: Can't open config file >%s<\n",fname);
        return(-2);
	  }
	}

  }
  printf("\nReadConfigFile: Using configuration file >%s<\n",fname);

  /* Parsing of config file */
  active = 0;
  while ((ch = getc(fd)) != EOF)
  {
    if ( ch == '#' || ch == ' ' || ch == '\t' )
    {
      while (getc(fd) != '\n') {}
    }
    else if( ch == '\n' ) {}
    else
    {
      ungetc(ch,fd);
      fgets(str_tmp, STRLEN, fd);
      sscanf (str_tmp, "%s %s", keyword, ROC_name);


      /* Start parsing real config inputs */
      if(strcmp(keyword,"TI_CRATE") == 0)
      {
	    if(strcmp(ROC_name,host) == 0)
        {
	      printf("\nReadConfigFile: crate = %s  host = %s - activated\n",ROC_name,host);
          active = 1;
        }
	    else if(strcmp(ROC_name,"all") == 0)
		{
	      printf("\nReadConfigFile: crate = %s  host = %s - activated\n",ROC_name,host);
          active = 1;
		}
        else
		{
	      printf("\nReadConfigFile: crate = %s  host = %s - disactivated\n",ROC_name,host);
          active = 0;
		}
      }

      else if(active && (strcmp(keyword,"TI_ADD_SLAVE")==0))
      {
        sscanf (str_tmp, "%*s %d", &i1);
        slave_list[nslave++] = i1;
      }

      else if(active && (strcmp(keyword,"TI_FIBER_DELAY_OFFSET")==0))
      {
        sscanf (str_tmp, "%*s %x %x", &i1, &i2);
        delay = i1;
        offset = i2;
      }

      else if(active && (strcmp(keyword,"TI_BLOCK_LEVEL")==0))
      {
        sscanf (str_tmp, "%*s %d", &i1);
        block_level = i1;
      }

      else if(active && (strcmp(keyword,"TI_BUFFER_LEVEL")==0))
      {
        sscanf (str_tmp, "%*s %d", &i1);
        buffer_level = i1;
      }
      else if(active && (strcmp(keyword,"TI_INPUT_PRESCALE")==0))
      {
        sscanf (str_tmp, "%*s %d %d", &i1, &i2);
        if((i1 < 1) || (i1 > 6))
        {
          printf("\nReadConfigFile: Invalid prescaler inputs selction, %s\n",str_tmp);
        }
        if((i2 < 0) || (i2 > 15))
        {
          printf("\nReadConfigFile: Invalid prescaler value selction, %s\n",str_tmp);
        }
        input_prescale[i1-1] = i2;
      }
      else
      {
        ; /* unknown key - do nothing */
		/*
        printf("ReadConfigFile: Unknown Field or Missed Field in\n");
        printf("   %s \n", fname);
        printf("   str_tmp=%s", str_tmp);
        printf("   keyword=%s \n\n", keyword);
        return(-10);
		*/
      }

    }
  } /* end of while */

  fclose(fd);

  return(0);
}


int
tiConfigGetBlockLevel()
{
  printf("block_level = %d\n",block_level);
  return(block_level);
}

int
tiDownloadAll()
{
  int ii;

  /*
  tiIntDisable();
  tiDisableVXSSignals();
  */

  /*for(ii=0; ii<nslave; ii++) tiAddSlave(slave_list[ii]); done automatically in ROL1 */
  for(ii=0; ii<6; ii++) tiSetInputPrescale(ii+1,input_prescale[ii]);

  tiSetFiberDelay(delay, offset);

  tiSetBlockLevel(block_level);

  tiSetBlockBufferLevel(buffer_level);

  return(0);
}

void
tiMon(int slot)
{
  tiStatus(1);
}


/*
static int is_slave;
static nslave, slave_list[MAXSLAVES];
static unsigned int delay, offset;
static int block_level;
static int buffer_level;
static int input_prescale[6];
*/

/* upload setting from all found DSC2s */
int
tiUploadAll(char *string, int length)
{
  int slot, i, ii, jj, kk, ifiber, len1, len2;
  char *str, sss[1024];
  unsigned int tmp;
  int connectedfibers;
  unsigned short sval;
  unsigned short bypMask;
  unsigned short channels[8];


  nslave = 0;
  connectedfibers = tiGetConnectedFiberMask();
  if(connectedfibers>0)
  {
	for(ifiber=0; ifiber<8; ifiber++)
	{
	  if( connectedfibers & (1<<ifiber) )
	  {
        slave_list[nslave++] = ifiber+1;
	  }
    }
  }
  block_level = tiGetCurrentBlockLevel();
  buffer_level = tiGetBlockBufferLevel();
  for(ii = 0; ii < 6; ii++)
  {
    input_prescale[ii] = tiGetInputPrescale(ii+1);
  }


  if(length)
  {
    str = string;
    str[0] = '\0';

	for(ii=0; ii<nslave; ii++)
	{
      sprintf(sss,"TI_ADD_SLAVE %d\n",slave_list[ii]);
      ADD_TO_STRING;
    }

    sprintf(sss,"TI_BLOCK_LEVEL %d\n",block_level);
    ADD_TO_STRING;

    sprintf(sss,"TI_BUFFER_LEVEL %d\n",buffer_level);
    ADD_TO_STRING;

    for(ii = 0; ii < 6; ii++)
    {
      sprintf(sss,"TI_INPUT_PRESCALE %d %d\n",ii+1,input_prescale[ii]);
      ADD_TO_STRING;
    }

    CLOSE_STRING;
  }

}







#else /* dummy version*/

void
tiConfig_dummy()
{
  return;
}

#endif
