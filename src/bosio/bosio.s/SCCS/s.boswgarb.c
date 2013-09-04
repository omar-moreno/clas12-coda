h46426
s 00000/00000/00000
d R 1.2 70/01/01 00:00:02 Codemgr 2 1
c SunPro Code Manager data about conflicts, renames, etc...
c Name history : 1 0 clas/bosio/bosio.s/boswgarb.c
e
s 00086/00000/00000
d D 1.1 00/08/10 11:10:07 boiarino 1 0
c date and time created 00/08/10 11:10:07 by boiarino
e
u
U
f e 0
t
T
I 1
/*DECK ID>, BOSWGARB. */
/*
*/
 
#include "bos.h"
 
#ifndef VAX
 
int boswgarbage_(int *jw)
{
  return(bosWgarbage(jw));
}
 
#endif
 
int bosWgarbage(int *jw)
{
  BOSptr b;
  int *w, i, j, id, icop, istp, nstp, n, indn[200];
 
  w = jw-1;
  b = (BOSptr)jw;
 
  if(b->ick != MAGIC) return(ARRAY_NOT_INITIALIZED);
  b->s->jw1 = 0;
  if(b->ndw == 0) return(0);
 
  icop = b->igw;
  istp = b->igw;
 
  do
  {
    id = b->iwk;	/* first index of work bank area */
    nstp = 0;
    n = 0;
    do
    {
      if(w[id+(NHW-1)] >= 0)
      {
        if(n > 0 && id == indn[n-1]+indn[n])
        {
          indn[n] = indn[n] + w[id+(NHW-1)-INR];
        }
        else
        {
          if(n >= 200)
          {
            nstp = indn[n-1] + 1;
            n = 0;
          }
          n = n + 2;
          indn[n-1] = id;
          indn[n] = w[id+(NHW-1)-INR];
        }
      }
      id = id + w[id+(NHW-1)-INR];
    }
    while(id < istp);
 
    /* copy banks, to remove gaps */
 
    for(j=0; j<n; j=j+2)
    {
      i = n - j;
      icop = icop - indn[i+1];
      bcopy((char *) &w[indn[i]], (char *) &w[icop], indn[i+1]<<2);
    }
    istp = nstp;
  }
  while(istp != 0);
 
  /* update indices */
 
  b->iwk = icop;	/* first index of work bank area */
  b->ndw = 0;
  id = icop + (NHW-1);
  while(id < b->igw)
  {
    w[w[id-INXT]] = id;
    id = id + w[id-INR];
  }
  b->igw = 0;
 
  return(0);
}
 
E 1
