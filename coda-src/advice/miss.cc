/* BLURB gpl

                           Coda File System
                              Release 5

          Copyright (c) 1987-1999 Carnegie Mellon University
                  Additional copyrights listed below

This  code  is  distributed "AS IS" without warranty of any kind under
the terms of the GNU General Public Licence Version 2, as shown in the
file  LICENSE.  The  technical and financial  contributors to Coda are
listed in the file CREDITS.

                        Additional copyrights
                           none currently

#*/


#ifdef __cplusplus
extern "C" {
#endif __cplusplus

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
#include "coda_assert.h" 
#include <struct.h>
#include "coda_string.h"
#include <unistd.h>
#include <stdlib.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>

#ifdef __cplusplus
}
#endif __cplusplus


#include <util.h>
#include "mybstree.h"
#include "advice_srv.h"
#include "miss.h"


/* Data structures for holding the ordered list of disconnected cache misses */
bstree *current;            /* This version is the one in which to insert new elements */
bstree *previous;           /* This version is presently being processed by the user */



/**************
 * CLASS MISS *
 **************/

miss::miss(char *Path, char *Program) {
    bsnode *existing_entry; 
    
    path = new char[strlen(Path)+1];
    program = new char[strlen(Program)+1];

    LogMsg(0,LogLevel,LogFile, "E miss(%s,%s)", Path, Program);

    strcpy(path, Path);
    strcpy(program, Program);

    CODA_ASSERT(current != NULL);
    existing_entry = current->get(&queue_handle);
    if (existing_entry != NULL) {
        miss *existing_miss = strbase(miss, existing_entry, queue_handle);
	CODA_ASSERT(existing_miss != NULL);
	existing_miss->num_instances++;
	delete this;
    } else {
        num_instances = 1;
	current->insert(&queue_handle);
    }
}

miss::miss(miss& m) {
    abort();
}

int miss::operator=(miss& m) {
    abort();
    return(0);
}

miss::~miss() {
    CODA_ASSERT(current != NULL);
    current->remove(&queue_handle);
    delete[] path;
    delete[] program;
}

void miss::print(FILE *outfile) {
    fprintf(outfile, "%s & %s (%d)\n", path, program, num_instances);
}

// Friend of CLASS MISS
void PrintMissList(char *filename) {
    FILE *outfile;
    bsnode *b;
    bstree_iterator next(*previous, BstDescending);
 
    outfile = fopen(filename, "w+");
    if (outfile == NULL) {
	LogMsg(0,LogLevel,LogFile,"Failed to open %s for printing the miss list",filename);
	return;
    }

    while (b = next()) {
        miss *m = strbase(miss, b, queue_handle);
	CODA_ASSERT(m != NULL);
	m->print(outfile);
    }

    fflush(outfile);
    fclose(outfile);
}

/* 
 * Friend of CLASS MISS
 * 
 * Compare first based upon object's priority, then alphabetically by pathname.
 */
int PathnamePriorityFN(bsnode *b1, bsnode *b2) {
    int pathcmp;
    int programcmp;
    int rc;
    miss *m1; 
    miss *m2;

    CODA_ASSERT(b1 != NULL);
    CODA_ASSERT(b2 != NULL);

    m1 = strbase(miss, b1, queue_handle);
    m2 = strbase(miss, b2, queue_handle);

    CODA_ASSERT(m1 != NULL);
    CODA_ASSERT(m2 != NULL);

    pathcmp = strcmp(m1->path, m2->path);
    programcmp = strcmp(m1->program, m2->program);
    rc = pathcmp?pathcmp:programcmp;
    return(rc);
}

/**************************************
 * Helper routines for the advice_srv *
 **************************************/

void InitMissQueue() {
    current = new bstree(PathnamePriorityFN);
    previous = NULL;
}

void ClearPreviousMissQueue() {
    CODA_ASSERT(previous != NULL);
    previous->clear();
    delete previous;
    previous = NULL;
}

void ReinstatePreviousMissQueue() {
    int i, num;

    CODA_ASSERT(previous != NULL);
    num = previous->count();
    for (i = 0; i < num; i++){
	bsnode *b = previous->get(BstGetMin);
	CODA_ASSERT(b != NULL);
	miss *m = strbase(miss, b, queue_handle);
	CODA_ASSERT(m != NULL);
	CODA_ASSERT(current != NULL);
	current->insert(&(m->queue_handle));
    }
    CODA_ASSERT(previous != NULL);
    num = previous->count();
    CODA_ASSERT(num == 0);

    delete previous;
    previous = NULL;
}

void OutputMissStatistics() {

    // First move current to previous and create a new current
    CODA_ASSERT(previous == NULL);
    previous = current;
    current = new bstree(PathnamePriorityFN);

    // Generate the input to the tcl script
    PrintMissList(TMPMISSLIST);

    ClearPreviousMissQueue();
}
