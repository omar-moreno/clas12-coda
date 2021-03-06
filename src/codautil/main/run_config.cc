// run_config

//  gets run and config

//  ejw, 20-may-97


#include <stdlib.h>
#include <string.h>

#include <iostream>

using namespace std;



char *msql_database = getenv("EXPID");
char *session       = getenv("SESSION");

extern "C"{
  void get_run_config(char *msql_database, char *session, int *run, char **config, char **, char **);
}
void decode_command_line(int argc, char **argv);


//----------------------------------------------------------------

int
main(int argc, char **argv)
{
  int run;
  char *config;
  char *conffile;
  char *datafile;

  decode_command_line(argc,argv);
  if(session==NULL) session=(char *)"clasprod";


  // get run status
  get_run_config(msql_database,session,&run,&config,&conffile,&datafile);
  cout << config << endl;

}


//----------------------------------------------------------------


void decode_command_line(int argc, char **argv)
{

  int i=1;
  char *help=(char *)"\nusage:\n\n  run_config [-s session] [-m msql_database] \n\n\n";


  while(i<argc) {
    
    if(strncasecmp(argv[i],"-h",2)==0){
      cout << help;
      exit(EXIT_SUCCESS);
    }
    else if (strncasecmp(argv[i],"-s",2)==0){
      session=strdup(argv[i+1]);
      i=i+2;
    }
    else if (strncasecmp(argv[i],"-m",2)==0){
      msql_database=strdup(argv[i+1]);
      i=i+2;
    }
  }
}


/*---------------------------------------------------------------------*/

