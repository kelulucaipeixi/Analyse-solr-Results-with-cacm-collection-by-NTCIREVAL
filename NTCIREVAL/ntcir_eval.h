/*******************************************************************************
 ntcir_eval.h
                                                      April 2011 Tetsuya Sakai
                                                revised June 2011
                                                revised September 2018
                                                 for RBU (SIGIR2018)
*******************************************************************************/
#ifndef _NTCIR_EVAL_H_
#define _NTCIR_EVAL_H_

#include <sys/types.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __APPLE__
  #include <stdlib.h>
#else
  #include <malloc.h>
#endif

#define MAXRL_MAX 15 /* max number of relevance levels */

#define INTENT_NUMMAX 64      /* number of intents for diversified output */
#define EC_NUMMAX 1024 /* number of equivalence classes
			   for subtopic mining.
			  Set to a high value for handling
			  unclustered erel files where
			  each string is regarded as an equiv class */

#define DEFAULT_BETA 1.0       /* for Q-measure */
#define DEFAULT_GAMMA 0.95     /* for rank-biased NCU */
#define DEFAULT_CUTOFF 1000    /* cutoff for prec, hit, nDCG */
#define CUTOFF_NUMMAX 10       /* how many cutoffs? */
#define DEFAULT_LOGB 2.0       /* for original nDCG */
#define DEFAULT_RBP 0.95       /* for RBP */
#define DEFAULT_FFLATBETA 1.0  /* for 1click F-flat */

#define INFORMATIONAL 0
#define NAVIGATIONAL 1

#define DEFAULT_SEP " " /* default field separator for input/output files
			   do not use this default if fields contain
			   white spaces etc. */

#define RLEVEL_RLEVEL_SEP ":" /* separator between
                                 gain/stop values for different
                                 rel levels */

#define INFSTR "inf"
#define NAVSTR "nav"

#define OPTSTR_HELP    "-h"
#define OPTSTR_VERBOSE "-v"
#define OPTSTR_IGNORE_UNJUDGED  "-j" /* condensed list mode */
#define OPTSTR_EC "-ec" /* equivalence class mode */
#define OPTSTR_IDEALFILE "-I" /* ideal ranked list with gain values */
#define OPTSTR_DINFILE "-din" /* diversify for inf/nav file */
#define OPTSTR_BETA    "-beta"  /* for Q-measure and F-flat */
#define OPTSTR_GAMMA   "-gamma" /* for rank-biased NCU */
#define OPTSTR_LOGB    "-logb"  /* for original DCG */
#define OPTSTR_RBP     "-rbp"   /* RBP persistence */
#define OPTSTR_RBUP     "-rbup" /* RBU p: compute iRBP only if this val is specified */
#define OPTSTR_GAP     "-gap"   /* compute computationally
				   expensive Robertson GAP */
#define OPTSTR_CUTOFF  "-cutoffs" /* cutoffs for prec, hit, nDCG... */
#define OPTSTR_TRUNCATE "-truncate" /* truncate ranked list
				       when labelling (label and glabel) */
#define OPTSTR_OUTSTR  "-out"   /* user specified string as a prefix for
				   each output line
				   This should be useful for printing
				   topicIDs etc. */
#define OPTSTR_SEP     "-sep" /* field separator for input and output files */

#define OPTSTR_RELFILE "-r" /* judged rel/nonrel docs with rel labels */
#define OPTSTR_GAIN "-g" /* gain values for q_eval_compute */
#define OPTSTR_STOP "-s" /* stop values for graded-uniform NCU (compute) */

#define OPTSTR_1CLICK_L "-L" /* L param for computing S-measure */
#define OPTSTR_1CLICK_N "-N" /* gold starndard Nugget file */

struct strlist
{
  char *string;
  struct strlist *next;
};

struct strdoublist
{
  char *string;
  double val;
  struct strdoublist *next;
};

struct strdoublonglist
{
  char *string;
  double val1;
  long val2;
  struct strdoublonglist *next;
};

struct strdoublong2list
{
  char *string;
  double val1;
  long val2;
  long val3;
  struct strdoublong2list *next;
};

struct strstrlist
{
  char *string1;
  char *string2;
  struct strstrlist *next;
};

struct strstrlonglist
{
  char *string1;
  char *string2;
  long val;
  struct strstrlonglist *next;
};

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __cplusplus
}
#endif

#endif
