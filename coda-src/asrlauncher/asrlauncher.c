/* BLURB gpl

                           Coda File System
                              Release 6

          Copyright (c) 1987-2006 Carnegie Mellon University
                  Additional copyrights listed below

This  code  is  distributed "AS IS" without warranty of any kind under
the terms of the GNU General Public Licence Version 2, as shown in the
file  LICENSE.  The  technical and financial  contributors to Coda are
listed in the file CREDITS.

                        Additional copyrights
                           none currently

*/

/*						
 * asrlauncher.c
 *
 * This file contains all of the code that makes up the ASRLauncher.
 *
 */

/* System includes */

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* Venus includes */

#include <coda_config.h>

/* Definitions */

/* ASR timeout period (milliseconds). Currently this is identical to RPC2's. */
#define ASR_TIMEOUT 60000

/* Character array lengths */

#define MAXPATHLEN     256
#define MAXCOMMANDLEN  4096

/* Environment variable names */

#define CONFLICT_BASENAME             '>' /* Puneet's original 3 vars */
#define CONFLICT_WILDCARD             '*'
#define CONFLICT_PARENT               '<'
#define SYSTYPE_VAR                   '@'

#define CONFLICT_PATH                 '=' /* Additional variables */
#define CONFLICT_VOLUME               ':'
#define CONFLICT_TYPE                 '?'

/* Conflict type enumerations.
 * Note: these values are standard also in Venus. */

#define SERVER_SERVER       1
#define SERVER_SERVER_STR  "1"
#define LOCAL_GLOBAL        2
#define LOCAL_GLOBAL_STR   "2"
#define MIXED_CONFLICT      3
#define MIXED_CONFLICT_STR "3"

struct rule_info {
  long dependencies;
  long commands;
};

/* ASRLauncher-related Globals */

int My_Pid;                 /* getpid() result. Only for logging purposes. */

/* Conflict Information Globals */

int Conflict_Type;                     
char Conflict_Path[MAXPATHLEN];        /* Full path to the conflict. */
char Conflict_Parent[MAXPATHLEN];      /* Parent directory of conflict. */
char *Conflict_Basename;               /* Just the filename to the conflict. */
char Conflict_Volume_Path[MAXPATHLEN]; /* Path to conflict's volume root. */
char Conflict_Wildcard[MAXPATHLEN];    /* Holds the part of the pathname that 
									    * matched an asterisk in a rule, for 
									    * convenience.
										* Example: *.dvi matching foo.dvi puts 
										* "foo" in this variable. */


/* Lexical Scoping/Rule Parsing Globals */

FILE *Rules_File;
long Rules_File_Offset;
char Rules_File_Path[MAXPATHLEN];

/*
 * openRulesFile
 *
 * Takes in a directory pathname (with or without garbage at the end), strips
 * it down to the final '/', and concatenates the universal rules filename. It
 * then updates Rules_File_Path with the new correct path.
 *
 * Returns the FILE handle or NULL on failure.
 */

FILE* openRulesFile(char *conflict) {
  FILE *rules;
  char parentPath[MAXPATHLEN], *end;
  
  rules = NULL;

  strncpy(parentPath, conflict, MAXPATHLEN);

  end = strrchr(parentPath, '/');
  if(end == NULL)
    return NULL;

  *(end + 1)='\0';            /* Terminate string just after final '/'. */

  strcat(parentPath, ".asr"); /* Concatenate filename of rules file. */

  rules = fopen(parentPath, "r"); /* Could fail with ENOENT or EACCES
								   * at which point we loop to a
								   * higher directory to try again. */

  strncpy(Rules_File_Path, parentPath, MAXPATHLEN);

  return rules;
}

/*
 * Perform wildcard matching between strings.
 */
int compareWithWilds(char *str, char *wildstr) {
  char *s, *w;
  int asterisk = 0;
  
 begin:
  for (s = str, w = wildstr; *s != '\0'; s++, w++) {
	switch (*w) {
	  
	case '?':
	  /* Skip one letter. */
	  break;
	  
	case '*':
	  asterisk = 1;
	  str = s;
	  wildstr = w + 1;
	  
	  /* Skip multiple *'s */
	  while (*wildstr == '*')
		wildstr++;
	  
	  /* Ending on asterisk indicates success. */
	  if (*wildstr == '\0') 
		return 0;
	  
	  goto begin;
	  break;
	  
	default:
	  
	  /* On character mismatch, check if we had a previous asterisk. */
	  if(*s != *w)
		goto mismatch;
	  
	  /* Otherwise, the characters match. */
	  break;
	}
  }
  
  while (*w == '*') w++;
  
  /* If there's no string left to match wildstr chars after the final 
   * asterisk, return failure. Otherwise, success. */
  return (*w != '\0');
  
 mismatch:
  if (asterisk == 0) return -1;
  str++;
  goto begin;
}

int parseRulesFile(struct rule_info *data) {
  int rule_not_found = 1; /* invariant */
  char name[MAXPATHLEN];

  if((Rules_File == NULL) || (data == NULL))
    return 1;

  if(fseek(Rules_File, Rules_File_Offset, SEEK_SET) < 0)
	{ perror("ftell"); exit(EXIT_FAILURE); }

  while(rule_not_found) {
    char *end;
    char fmt[10];

	/* Get first word from the current line. */
    sprintf(fmt, "%%%ds", MAXPATHLEN-1);
    if(fscanf(Rules_File, fmt, name) == EOF) {
      fprintf(stderr, "ASRLauncher(%d): No matching rule exists in this "
			  "file.\n", My_Pid);
      return 1;
    }

	if((end = strchr(name, ':')) == NULL) {
	  char c;

	  /* Ignore the rest of this line if the first word doesn't have ':' */
	  do { c = fgetc(Rules_File); } 
	  while((c != '\n') && (c != '\0'));

      continue;
	}
	else
	  end[0] = '\0';
	
    /* Match rule against conflict filename. */
    if(compareWithWilds(Conflict_Basename, name)) /* Not a match. */
      continue;
	
    fprintf(stderr, "ASRLauncher(%d): Found matching (conflict name, rule): "
			"(%s, %s)\n",  My_Pid, Conflict_Basename, name);
	
    rule_not_found = 0;

    /* Store what matched that '*' in $* (if the rule led off with one). */
    if(name[0] == '*') {
	  int diff; /* how many bytes to cut off of the end of Conflict_Path */
	  
	  diff = strlen(Conflict_Basename) - (strlen(name) - 1);
      
      strcpy(Conflict_Wildcard, Conflict_Basename);
      *(Conflict_Wildcard + diff) = '\0';
      
      fprintf(stderr, "ASRLauncher(%d): Wildcard variable $* = %s\n",
			  My_Pid, Conflict_Wildcard);
    }
	
    { /* Mark data to be used later in the launch. */

      /* Mark dependency list. */
	  
	  if((data->dependencies = ftell(Rules_File)) < 0) 
		{ perror("ftell"); exit(EXIT_FAILURE); }
	  
	  while(fgetc(Rules_File) != '\n') continue;

      /* Mark command list, and remember where we left off. */

	  if((Rules_File_Offset = data->commands = ftell(Rules_File)) < 0) 
		{ perror("ftell"); exit(EXIT_FAILURE); }
	  
    }
  } /* while(rule_not_found) */
  
  return 0;
} /* parseRulesFiles */

/* XXX: Does this work with cygwin? */
int isConflict(char *path) {
  struct stat buf;
  char link[MAXPATHLEN];

  if(path == NULL)
    return 0;

  if(path[0] == '\0')
    return 0;

  /* Get stat() info. */
  if(lstat(path, &buf) < 0) {
    int error = errno;
    fprintf(stderr, "ASRLauncher(%d): lstat() failed on dependency %s: %s\n",
	    My_Pid, path, strerror(error));
    return 1;
  }

  /* Conflict defined as a dangling symlink. */
  if(!S_ISLNK(buf.st_mode))
    return 0;

  /* Read symlink contents. */
  if(readlink(path, link, MAXPATHLEN) < 0) {
    int error = errno;
    fprintf(stderr, "ASRLauncher(%d): readlink() failed: %s\n",
	    My_Pid, strerror(error));
    return 0;
  }
  
  /* The dangling symlink contents must begin with a '@', '#', or '$' */
  if((link[0] != '@') && (link[0] != '#') && (link[0] != '$'))
    return 0;

  return 1; /* passed all tests, considered a 'conflict' */
}

/*
 * replaceEnvVars
 *
 * Takes a string of text and replaces all instances of special
 * ASRLauncher environment variables with the appropriate filenames,
 * pathnames, or conflict types. Performed in place to avoid memory
 * allocations.
 *
 * Returns 0 on success and -1 on failure.
 */
int replaceEnvVars(char *string, int maxlen) {
  int len;
  char *trav;
  char *temp;

  if((string == NULL) || maxlen <= 0 )
    return -1;

  len = strlen(string);

  /* Allocate temporary buffer to perform copies into. */
  temp = (char *) malloc((maxlen)*sizeof(char));
  if(temp == NULL) { perror("malloc");	exit(EXIT_FAILURE); }

  strncpy(temp, string, maxlen-1);

  trav = temp;
  while(*trav != '\0') {

    switch(*trav) {
      
    case '$':                /* marks beginning of a env variable name */
      {
		char *replace;        /* points to replacement str for env variable */
		
		/* Compare against known environment variables. */
		
		/* These are the important environment variables that make the
		 * .asr files much more useful. It's possible that user-defined 
		 * variables could be set within the .asr file itself and checked 
		 * here for convenience -- however, I haven't come across a case where
		 * this would be useful yet. */
		
		replace = NULL;
		
		switch(*(trav+1)) {

		case SYSTYPE_VAR:
		  replace = SYSTYPE;
		  break;
		  
		case CONFLICT_BASENAME:
		  replace = Conflict_Basename;
		  break;
		  
		case CONFLICT_PATH:
		  replace = Conflict_Path;
		  break;
		  
		case CONFLICT_PARENT:
		  replace = Conflict_Parent;
		  break;
		  
		case CONFLICT_WILDCARD:
		  replace = Conflict_Wildcard;
		  break;
		  
		case CONFLICT_VOLUME:
		  replace = Conflict_Volume_Path;
		  break;
		  
		case CONFLICT_TYPE:
		  
		  switch(Conflict_Type) {
			
		  case SERVER_SERVER:
			replace = SERVER_SERVER_STR;
			break;
			
		  case LOCAL_GLOBAL:
			replace = LOCAL_GLOBAL_STR;
			break;
			
		  case MIXED_CONFLICT:
			replace = MIXED_CONFLICT_STR;
			break;
			
		  default: /* Unknown conflict type, leave it untouched. */
			break;
		  }
		  
		  break;
		  
		default:
		  break;
		}
		
		if(replace == NULL) { /* Unknown environment variable name. */
		  fprintf(stderr, "ASRLauncher(%d): Unknown environment variable!\n",
				  My_Pid);
		  trav++;
		  continue;
		}
		
		fprintf(stderr, "ASRLauncher(%d): Replacing environment "
				"variable with: %s\n", My_Pid, replace);
		
		{
		  char *src, *dest, *holder;
		  
		  /* See if the replacement string will fit in the buffer. */
		  if((strlen(temp) + strlen(replace)) > maxlen) {
			fprintf(stderr, "ASRLauncher(%d): Replacement does not fit!\n",
					My_Pid);
			free(temp);
			return 1;
		  }
		  
		  src = trav + 2;                     /* skip environment var name */
		  dest = trav + strlen(replace);
		  
		  holder = (char *)malloc(maxlen * sizeof(char));
		  if(holder == NULL) { 
			perror("malloc");	
			free(temp);
			exit(EXIT_FAILURE); 
		  }
		  
		  strcpy(holder, src);
		  strcpy(dest, holder);
		  strncpy(trav, replace, strlen(replace));
		  free(holder);
		}
		
		break;
      }
	  
    default:
      trav++;
      break;
    }
  }
  
  if(strcmp(string, temp))
	strncpy(string, temp, maxlen-1);

  free(temp);
  
  return 0;
}


/* Tokenize dependency line. */

int evaluateDependencies(struct rule_info *data) {
  char dpnd[MAXPATHLEN];
  char fmt[10];
  long pos;

  if(data == NULL) return 1;
  if(fseek(Rules_File, data->dependencies, SEEK_SET) < 0)
	{ perror("fseek"); exit(EXIT_FAILURE); }

  sprintf(fmt, "%%%ds", MAXPATHLEN-1);
  
  pos = data->dependencies;

  while(pos < (data->commands-1)) {

	/* Grab a dependency. */
	fscanf(Rules_File, fmt, dpnd);

    /* Do environment variable expansion. */
    if(replaceEnvVars(dpnd, MAXPATHLEN) < 0) {
      fprintf(stderr, "ASRLauncher(%d): Failed replacing env vars on %s! "
			  "Skipping it!\n", My_Pid, dpnd);
      continue;
    }
	
    fprintf(stderr, "ASRLauncher(%d): Dependency: %s\n",
			My_Pid, dpnd);

    /* Check if it is in conflict. */
    if(isConflict(dpnd)) {
      fprintf(stderr, "ASRLauncher(%d): Conflict detected!\n", My_Pid);
      return 1;
    }

	/* Update current position. */
	if((pos = ftell(Rules_File)) < 0)
	  { perror("ftell"); exit(EXIT_FAILURE); }
	else
	  fprintf(stderr, "position: %ld\tend position: %ld\n", pos, data->commands);
  }
    
  return 0;
}


/*
 * findRule
 *
 * Fills in a rule struct with the data associated with a matching rule,
 * lexically scoping up the conflict's pathname if necessary.
 *
 * @param struct rule_info *data Rule info to be filled in for the caller.
 * @returns 0 on success, -1 on failure
 *
 */

int findRule(struct rule_info *data) {
  int scope;
  char *temp;

  if(data == NULL) {
	fprintf(stderr, "ASRLauncher(%d): Bad rule structure!\n", My_Pid);
	return -1;
  }

  /* Will be changed later by parseRulesFile. */
  strncpy(Rules_File_Path, Conflict_Path, MAXPATHLEN);

  while((scope = strcmp(Conflict_Volume_Path, Rules_File_Path)) != 0) {   
	int error;

	Rules_File = openRulesFile(Rules_File_Path);
	
	error = parseRulesFile(data);
	if(error) { /* Scope one directory level higher, trying new rules file. */
	  temp = strrchr(Rules_File_Path, '/');
	  *temp = '\0';               /* Remove final slash. Parent directory
								   * then looks like a filename and is
								   * dropped in opening the new rules file. */
	  fclose(Rules_File);
	  Rules_File = NULL;
	}
	else {       /* We have found a matching rule. */
	  fprintf(stderr, "ASRLauncher(%d): Opening rules file: %s\n", 
			  My_Pid, Rules_File_Path);
	  break;
	}
  }
  
  if(scope == 0) {  /* Stop after volume root. */
	fprintf(stderr, "ASRLauncher(%d): Failed lexically scoping a"
			"matching rules file!\n", My_Pid);
	return 1;
  }

  return 0;
}

int executeCommands(long pos) {

  int status = 0, options = WNOHANG, error, pfd[2];
  pid_t pid;
  char *asr_argv[4];

  if(pos < 0) {
    fprintf(stderr, "ASRLauncher(%d): Empty command list!\n", My_Pid);
    return 1;
  }
  
  if(pipe(pfd) == -1) { perror("pipe"); exit(EXIT_FAILURE); }
  
  pid = fork();
  if(pid == -1) { perror("fork"); exit(EXIT_FAILURE); }
  
  if(pid == 0) {

	/* Make the child shell think that pfd[0] is stdin. */

	close(pfd[1]);
	dup2(pfd[0], STDIN_FILENO);

	asr_argv[0] = "/bin/sh"; 
	asr_argv[1] = NULL;
	
	if(execvp(asr_argv[0], asr_argv) < 0) {
	  perror("exec");
	  close(pfd[0]);
	  exit(EXIT_FAILURE); 
	}
  }
  else {
	int ms;
	char command[MAXCOMMANDLEN], fmt[10], firstword[MAXPATHLEN];

	close(pfd[0]);
	
	if(fseek(Rules_File, pos, SEEK_SET) < 0)   /* Go to command list. */
	  { perror("fseek"); exit(EXIT_FAILURE); }

	/* Now, pipe data to the child shell. */

	fprintf(stderr, "\nASRLauncher(%d): Commands to execute:\n\n", My_Pid);

	do {
	  long line;

	  if((line = ftell(Rules_File)) < 0) 
		{ perror("ftell"); exit(EXIT_FAILURE); }
	  
	  /* Get first word from the current line. */
	  sprintf(fmt, "%%%ds", MAXPATHLEN-1);
	  if(fscanf(Rules_File, fmt, firstword) == EOF)
		break;
	  
	  if(strchr(firstword, ':') != NULL) /* New rule. */
		break;
	  
	  if(fseek(Rules_File, line, SEEK_SET) < 0) /* Rewind to start of line. */
		{ perror("fseek"); exit(EXIT_FAILURE); }
	  
	  if(fgets(command, MAXCOMMANDLEN, Rules_File) == NULL) /* EOF */
		break;
	  
	  /* Expand any environment variables in our 
	   * command to their appropriate strings. */		
	  
	  error = replaceEnvVars(command, MAXCOMMANDLEN);
	  if(error) {
		fprintf(stderr, "ASRLauncher(%d): Failed replacing environment "
				"variables!\n", My_Pid);
		return 1;
	  }

	  fprintf(stderr, "%s", command);

	  if(write(pfd[1], (void *) command, strlen(command)) < 0) {
		perror("write");
		exit(EXIT_FAILURE);
	  }
	} while(1);
	
	close(pfd[1]);

	for(ms = 0; ms < ASR_TIMEOUT; ms += 10) {
	  int newpid;
	  struct timeval timeout;

	  timeout.tv_sec = 0;
	  timeout.tv_usec = 10000; 

	  select(0, NULL, NULL, NULL, &timeout);		/* wait 10 ms */

	  newpid = waitpid(pid, &status, options);
	  if(newpid == 0) continue;
	  else if(newpid > 0) break;
	  else if(newpid < 0) { perror("waitpid"); exit(EXIT_FAILURE); }
	}
	
	if(ms >= ASR_TIMEOUT) {
	  
	  fprintf(stderr, "ASRLauncher(%d): ASR timed out!\n", My_Pid);
	  
	  /* Destroy ASR process group. */
	  kill(pid * -1, SIGKILL);
	  
	  /* The aborting of partial changes should be done in Venus, not here.
	   * Return failure to tell Venus to abort changes. */
	  return 1;
	}
	
	/* Evaluate the status of the ASR. */
	if(WIFEXITED(status)) {
	  int ret = WEXITSTATUS(status);
	  fprintf(stderr, "ASRLauncher(%d): ASR terminated normally with "
			  "return code %d.\n", My_Pid, ret);
	}
	else {
	  fprintf(stderr, "ASRLauncher(%d): ASR terminated abnormally!\n",
			  My_Pid);
	  return 1;
	}
  }
  
  return 0;
}

/*
 * Arguments passed by Venus:
 */

int
main(int argc, char *argv[])
{
  struct rule_info conf_rule;
  int error, len, asr_has_not_executed;

  asr_has_not_executed = 1; /* invariant */

  My_Pid = getpid();

  if(argc != 4) {
    fprintf(stderr, "ASRLauncher(%d): Invalid number of arguments!\n", My_Pid);
    return 1;
  }


  /* Conflict_Path init */

  strncpy(Conflict_Path, argv[1], MAXPATHLEN-1);


  /* Conflict_Basename init */

  strncpy(Conflict_Volume_Path, argv[2], MAXPATHLEN-1);
  len = strlen(Conflict_Volume_Path);
  if(Conflict_Volume_Path[len-1] == '/') /* remove any trailing slash */
	Conflict_Volume_Path[len-1] = '\0';


  /* Conflict_Type init */

  Conflict_Type = atoi(argv[3]); 

  if((Conflict_Type > MIXED_CONFLICT) || (Conflict_Type < SERVER_SERVER)) {
    fprintf(stderr, "ASRLauncher(%d): Unknown conflict type %d (%s)!\n", 
	    My_Pid, Conflict_Type, argv[3]);
    return 1;
  }


  /* Conflict_Basename init */

  Conflict_Basename = strrchr(Conflict_Path, '/');
  if(*(Conflict_Basename + 1) == '\0') { /* Directory conflicts end in '/' */
	char *temp = Conflict_Basename;
	Conflict_Basename[0]='\0';
	Conflict_Basename = strrchr(Conflict_Path, '/');
	temp[0] = '/';
  }
  Conflict_Basename++;


  /* Conflict_Parent init */
  {
    char *temp;
	strcpy(Conflict_Parent, Conflict_Path);
  
    temp = strrchr(Conflict_Parent, '/');
    if(temp != NULL)
      *temp = '\0';
	else {
	  fprintf(stderr, "ASRLauncher(%d): %s has no parent!\n", 
			  My_Pid, Conflict_Path);
	  return 1;
	}
  }


  fprintf(stderr, "ASRLauncher(%d): Conflict received: %s\n", 
	  My_Pid, Conflict_Path);
  fprintf(stderr, "ASRLauncher(%d): uid=%d, pid=%d, pgid=%d\n", 
	  My_Pid, getuid(), My_Pid, getpgrp());
  fprintf(stderr, "ASRLauncher(%d): Conflict's volume root: %s\n", 
	  My_Pid, Conflict_Volume_Path);

  /*
   * Steps
   * 1.) Discover associated rule.
   * 2.) Check rule dependencies for conflicts, going back to 1 if failed.
   * 3.) Fork and execute appropriate ASR commands.
   * 4.) Wait for ASR signal, timing out as appropriate
   * 5.) Die, telling Venus that the ASR is complete or has failed.
   */

  while(asr_has_not_executed) {

	/* Find a wildcard-matched rule and parse its information. */
	error = findRule(&conf_rule);
	if(error) {
	  fprintf(stderr, "ASRLauncher(%d): Failed finding valid associated "
			  "rule!\n", My_Pid);
	  break;
	}

	/* See if the dependencies on this rule are not in conflict. */
	error = evaluateDependencies(&conf_rule);
	if(error) {
	  fprintf(stderr, "ASRLauncher(%d): Failed evaluating dependencies!\n",
			  My_Pid);
	  continue;
	}

	/* Execute all commands indented below the rule name. */
	error = executeCommands(conf_rule.commands);
	if(error) {
	  fprintf(stderr, "ASRLauncher(%d): Failed executing related commands!\n",
			  My_Pid);
	  break;
	}	  

	asr_has_not_executed = 0;
  }
	
  if(Rules_File != NULL)
	fclose(Rules_File);

  return error;
}
