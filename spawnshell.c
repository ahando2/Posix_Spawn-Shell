/* $begin shellmain */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <spawn.h>

#include <sys/stat.h>
#include <fcntl.h>

#define MAXARGS 128
#define MAXLINE 8192 /* Max text line length */

extern char **environ; /* Defined by libc */
static volatile sig_atomic_t runStat = 1 ; // do i need it?

/* Function prototypes */
void eval(char *cmdline,posix_spawn_file_actions_t  actions);
int parseline(char *buf, char **argv,posix_spawn_file_actions_t  actions, int *argc);
int builtin_command(char **argv);

int input_output_parse (char **argv,int *argc, int pos);
int output_redirection(char **argv,posix_spawn_file_actions_t actions, pid_t pid,int *argc, int pos);
int input_redirection(char **argv,posix_spawn_file_actions_t actions, pid_t pid,int *argc, int pos);
int input_output_redirection(char **argv,posix_spawn_file_actions_t actions, pid_t pid,int *argc, int in_pos,int out_pos);
int piping (char **argv, posix_spawn_file_actions_t  actions, pid_t pid,int pos, int *argc);
int wait_command(char **argv, posix_spawn_file_actions_t  actions, pid_t pid,int pos, int *argc);
int specialValueParse (int *argc, char **argv);
int input_output_check (int *argc, char **argv, char* sym);
int input_output_command(char **argv, posix_spawn_file_actions_t  actions, pid_t pid,int *argc, int *waitVal);


void unix_error(char *msg) /* Unix-style error */
{
  fprintf(stderr, "%s: %s\n", msg, strerror(errno));
  exit(EXIT_FAILURE);
}

/* signal override function */
static void sigHandler(int thisSignal){
  if ( thisSignal == SIGINT){
    write(STDOUT_FILENO,"\ncaught sigint\n",15 );
  }
   if ( thisSignal == SIGTSTP){
    write(STDOUT_FILENO,"\ncaught sigtstp\n",17);
  }
  main();
}

int main() {
  char cmdline[MAXLINE]; /* Command line */

  /* override SIGINT and SIGSTP */
  signal(SIGINT,sigHandler);
  signal(SIGTSTP,sigHandler);

  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);


  while (runStat) { // do i need run stat ?

    char *result;
    /* Read */
    printf("CS361 > ");
    result = fgets(cmdline, MAXLINE, stdin);
    if (result == NULL && ferror(stdin)) {
      fprintf(stderr, "fatal fgets error\n");
      exit(EXIT_FAILURE);
    }

    if (feof(stdin)) exit(0);

    /* Evaluate */
    eval(cmdline,actions);
  }
}
/* $end shellmain */

/* $begin eval */
/* eval - Evaluate a command line */
void eval(char *cmdline, posix_spawn_file_actions_t  actions) {
  char *argv[MAXARGS]; /* Argument list execve() */
  char buf[MAXLINE];   /* Holds modified command line */
  int bg;              /* Should the job run in bg or fg? */
  pid_t pid;           /* Process id */

  
  int argc = 0;
  int specialVal = -1;
   

  strcpy(buf, cmdline);
  bg = parseline(buf, argv, actions, &argc);
  if (argv[0] == NULL) return; /* Ignore empty lines */

  if (!builtin_command(argv)) {
    if (!input_output_command(argv,actions,pid,&argc, &specialVal)) {
      return;
    }
    
    else if (argv[0] != NULL && 0 != posix_spawnp(&pid, argv[0], NULL, NULL, argv, environ) ) {
      perror("spawn failed");
      exit(1);
    }

    
    /* Parent waits for foreground job to terminate */
    if (!bg) {
      int status;
      if ( specialVal != -1) { /* if input is ? */
        printf("\npid:%d status:%d\n", pid, status);
        return;
      }
      if (waitpid(pid, &status, 0) < 0) unix_error("waitfg: waitpid error");
    } else {
      int status;
      waitpid(pid, &status, 0);
      printf("%d %s", pid, cmdline);
    }
  }

  /* destroy action */
  posix_spawn_file_actions_destroy(&actions);

  return;
}

/* If first arg is a builtin command, run it and return true */
int builtin_command(char **argv) {
  if (!strcmp(argv[0], "exit")) /* exit command */
    exit(0);
  if (!strcmp(argv[0], "&")) /* Ignore singleton & */
    return 1;
  return 0; /* Not a builtin command */
}
/* $end eval */







/* $begin parseline */
/* parseline - Parse the command line and build the argv array */
int parseline(char *buf, char **argv, posix_spawn_file_actions_t actions, int *argc) {
  char *delim; /* Points to first space delimiter */
  // int argc;    /* Number of args */
  int bg;      /* Background job? */


  buf[strlen(buf) - 1] = ' ';   /* Replace trailing '\n' with space */
  while (*buf && (*buf == ' ')) /* Ignore leading spaces */
    buf++;

  /* Build the argv list */
  *argc = 0;
  while ((delim = strchr(buf, ' '))) {
   
    argv[(*argc)++] = buf;
    *delim = '\0';
    buf = delim + 1;
    while (*buf && (*buf == ' ')) /* Ignore spaces */
      buf++;
  }
  argv[*argc] = NULL;

  if (*argc == 0) /* Ignore blank line */
    return 1;
 
  /* Should the job run in the background? */
  if ((bg = (*argv[*argc - 1] == '&')) != 0) argv[--*argc] = NULL;

  return bg;
}
/* $end parseline */

/* $begin output input handlers */
/* parse input output commands */
int input_output_parse (char **argv,int *argc, int pos){
  for( int i = pos;i < *argc -1; i++)argv[i] = argv[i+1];
  argv[*argc-1] = NULL;
  return 0;
}


/* output_redirection - redirect output to a file */
int output_redirection(char **argv,posix_spawn_file_actions_t actions, pid_t pid,int *argc, int pos){

  if ( argv[pos+1] == NULL) return 1; /* check if command is invalid */

  /* open stdout file and direct it to filename
    if filename doesn't exist it will create one,
    if it exist it will truncate the existing file
  */
 
  if (0 !=posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, argv[pos + 1], O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR)){
    perror("open file failed");
    return 1;
  }
  if (0 !=posix_spawn_file_actions_adddup2(&actions,  STDOUT_FILENO, 1)){
    perror("adddup file failed");
    return 1;
  }//echo hi > t.txt

  /* parse the command to make it posix_spawn friendly :) */
  input_output_parse (argv,argc,pos);
  argv[pos] = NULL;
  printf("%s\n",argv[2]);

  /* run the posix_spawn */
  if (0 != posix_spawnp(&pid,  argv[0], &actions, NULL, argv, environ)) {
    perror("spawn file failed");exit(1);
    return 1;
  }

  /* close file */
  if ( 0 != posix_spawn_file_actions_addclose(&actions,  STDOUT_FILENO)){
    perror("close file failed");
    return 1;
  }

  /* wait child to die*/
  int status;
  waitpid(pid, &status, 0);
  return 0;
}

int input_redirection(char **argv,posix_spawn_file_actions_t actions, pid_t pid,int *argc, int pos){
 
  if ( argv[pos+1] == NULL) return 1; /* check if command is invalid */

  /* open file and direct it to stdin */
  if (0 != posix_spawn_file_actions_addopen(&actions,  STDIN_FILENO, argv[pos+1], O_RDONLY , S_IRUSR)){
    perror("open file failed");
    return 1;
  }
  if (0 != posix_spawn_file_actions_adddup2(&actions,   STDIN_FILENO, 0)){
    perror("adddup file failed");
    return 1;
  }

  /* close file */
  if (0 != posix_spawn_file_actions_addclose(&actions,  STDIN_FILENO)){
    perror("close file failed");
    return 1;
  }

  /* parse the command to make it posix_spawn friendly :) */
  input_output_parse (argv,argc,pos);

  /* run the posix_spawn */
  if (0 != posix_spawnp(&pid,  argv[0], &actions, NULL, argv, environ)) {
      perror("spawn failed");
      exit(1);
  }

  /* wait for child to die*/
  int status;
  waitpid(pid, &status, 0);
  return 0;
}

int input_output_redirection(char **argv,posix_spawn_file_actions_t actions, pid_t pid,int *argc, int in_pos,int out_pos){

  /* Initialize spawn file actions object for input */
  posix_spawn_file_actions_init(&actions);

  /* open file and direct it to stdin */
  if (0 != posix_spawn_file_actions_addopen(&actions,  STDIN_FILENO, argv[in_pos+1], O_RDONLY , S_IRUSR)){
    perror("open file failed");
    return 1;
  }
  if (0 != posix_spawn_file_actions_adddup2(&actions,   STDIN_FILENO, 0)){
    perror("adddup file failed");
    return 1;
  }

  /* Initialize spawn file actions object for output */
  posix_spawn_file_actions_init(&actions);

  /* open stdin file and direct it to filename
    if filename doesn't exist it will create one,
    if it exist it will truncate the existing file
  */
  if (0 !=posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, argv[out_pos+1], O_WRONLY | O_CREAT| O_TRUNC, S_IRUSR | S_IWUSR)){
    perror("open file failed");
    return 1;
  }
  if (0 !=posix_spawn_file_actions_adddup2(&actions,  STDOUT_FILENO, 1)){
    perror("adddup file failed");
    return 1;
  }

  
  
  /* parse the command to make it posix_spawn friendly :) */
  input_output_parse (argv,&out_pos,in_pos);
  input_output_parse (argv,argc,out_pos);

  
 /* run the posix_spawn */
  if (0 != posix_spawnp(&pid,  argv[0], &actions, NULL, argv, environ)) {
    perror("spawn file failed");
    exit(1);
  }

  /*close file*/
  if (0 != posix_spawn_file_actions_addclose(&actions,  STDIN_FILENO)){
      perror("close file failed");
      return 1;
  } 
  /* wait for child to die*/
  int status;
  waitpid(pid, &status, 0);
  return 0;
}

int piping (char **argv, posix_spawn_file_actions_t  actions, pid_t pid,int pos, int *argc){
  
  int Pstatus; /* set spawn status*/
  posix_spawn_file_actions_t actions1, actions2; /* set file action for both processes*/

  char* cmd1[128];
  char* cmd2[128];
  
  if ( argv[pos+1] == NULL)return 0;  /* check if command is invalid */

  /* parse commands */
  for ( int i = 0; i < pos; i++){
    cmd1[i] = argv[i];
  }
  for ( int i = pos+1; i < *argc; i++){
    cmd2[i-pos-1] = argv[i];
  }
  
  int pipe_fds[2];
  int pid1, pid2;

  /* Initialize spawn file actions objects */
  posix_spawn_file_actions_init(&actions1);
  posix_spawn_file_actions_init(&actions2);

  /* create pipe for read and write ends*/
  pipe(pipe_fds);

   /* Add duplication action copy the write end of the pipe to stdout file */
  posix_spawn_file_actions_adddup2(&actions1, pipe_fds[1], STDOUT_FILENO);

  /*/ close action read */ 
  posix_spawn_file_actions_addclose(&actions1, pipe_fds[0]);

  /* Add duplication action copy the read end of the pipe to stdin file */
  posix_spawn_file_actions_adddup2(&actions2, pipe_fds[0], STDIN_FILENO);

  /* close action write */ 
  posix_spawn_file_actions_addclose(&actions2,  pipe_fds[1]);

  /* Create the first child */
  if (0 != posix_spawnp(&pid1, cmd1[0], &actions1, NULL, cmd1, environ)) {
    perror("spawn failed");
    exit(1);
  }

  /* Create the second child */
  if (0 != posix_spawnp(&pid2, cmd2[0], &actions2, NULL, cmd2, environ)) {
    perror("spawn failed");
    exit(1);
  }

  /* Close the read end  */
  close(pipe_fds[0]);

  /* Close the write end  */
  close(pipe_fds[1]);

  /* wait for children to die*/
  waitpid(pid1, &Pstatus, 0);
  waitpid(pid2, &Pstatus, 0);
  
  return 0;
}

int wait_command(char **argv, posix_spawn_file_actions_t  actions, pid_t pid,int pos, int *argc){
  
  posix_spawn_file_actions_t actions1, actions2; /* set file action for both processes*/

  char cmd1[MAXLINE];
  char cmd2[MAXLINE];

  if ( argv[pos+1] == NULL)return 0;  /* check if command is invalid */
  
  /* parse commands */
  int n = 0;
  for ( int i = 0; i < pos; i++){
    char* str = argv[i];
    for( int j = 0; j< strlen(str); j++) {
      cmd1[n] = str[j];
      n++;
    }
    cmd1[n++] = ' ';
  }

  n =0;
  for ( int i = pos+1; i < *argc; i++){
    char* str = argv[i];
    for( int j = 0; j< strlen(str); j++) {
      cmd2[n] = str[j];
      n++;
    }
    cmd2[n++] = ' ';
  }


  /* Initialize spawn file actions object for both the processes */
  posix_spawn_file_actions_init(&actions1);
  posix_spawn_file_actions_init(&actions2);

  /* run the first command, eval will eventually call waitpid in the function thus it will wait for the process to die*/
  eval(cmd1, actions1);

  /* run the second command, eval will eventually call waitpid in the function thus it will wait for the process to die*/
  eval(cmd2, actions2);
  
  return 0;
}

/* parse for '?' command */
int specialValueParse (int *argc, char **argv){
  int specialVal;
  specialVal = input_output_check (argc,argv,"?");

  if ( specialVal != -1) {
      input_output_parse (argv,argc,specialVal);
  }
  return specialVal;
}

/* check symbol existence and position if exist, if not ret -1 */
int input_output_check (int *argc, char **argv, char* sym){
  int value = 0;  
  for ( int i =  0; i < *argc ; i++){
    if ( !strcmp(argv[i],sym)) {
      value++;
      return i;
    }
  } 
  return -1;
}

/* main handler for input output commands*/
int input_output_command(char **argv, posix_spawn_file_actions_t  actions, pid_t pid, int *argc, int *specialVal ){
  /* get special value '?' */
  *specialVal = input_output_check (argc,argv,"?");
  if ( *specialVal != -1) {
      input_output_parse (argv,argc,*specialVal);
      *argc -= 1;
      if ( argv[0] == NULL) return 1;
  }

  /* if command are only one word, the it's an error */
  if ( argv[1] == NULL) {
    return 1;
  }

  /* set variables*/
  int inputVal = 0;
  int outputVal = 0;
  int redirectVal = 0;
  int waitVal = 0;

  /* get variable values of each symbols */
  if ( *argc > 1){
    inputVal = input_output_check (argc,argv,"<");
    outputVal = input_output_check (argc,argv,">");
    redirectVal = input_output_check (argc,argv,"|");
    waitVal = input_output_check (argc,argv,";");
  }

  /* sent the corresponding command based on the variables values of symbols*/  
  if ( waitVal != -1)return wait_command(argv, actions, pid, waitVal,argc);
  if (redirectVal != -1) return piping (argv, actions, pid, redirectVal,argc);
  if (inputVal != -1 && outputVal != -1 )return input_output_redirection(argv,actions,pid, argc, inputVal, outputVal);
  if (inputVal != -1 && input_redirection(argv,actions,pid, argc, inputVal) == 0) return 0;
  if (outputVal != -1  && output_redirection(argv,actions,pid,  argc, outputVal) == 0) return 0;
  return 1;
}