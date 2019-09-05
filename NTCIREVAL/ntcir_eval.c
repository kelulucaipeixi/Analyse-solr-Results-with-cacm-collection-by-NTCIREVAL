/******************************************************************************
  ntcir_eval.c

 for Information Access evaluation  
                                                      April 2011 Tetsuya Sakai
                                              updated June 2019
******************************************************************************/
#include "ntcir_eval.h"

static int ignore_unjudged; /* flag for ignoring unjudged docs */
static int ec_mode; /* equivalence class based evaluation */
static char *outstr = NULL; /* used as output prefix for (g)compute/irec.
			       This can be a topicID, for example. */
static char *sep = NULL; /* separator for input and output files */

static int eval_dinlabel( int, char** );
static int eval_glabel( int, char** );
static int eval_gcompute( int, char** );
static int eval_irec( int, char** );
static int eval_label( int, char** );
static int eval_compute( int, char** );
static int eval_1click( int, char** );

static struct {
  char *cmd;
  int (*func)();
} table[] = {
  { "dinlabel", eval_dinlabel },
  { "glabel", eval_glabel },
  { "gcompute", eval_gcompute },
  { "irec", eval_irec },
  { "label", eval_label },
  { "compute", eval_compute },
  { "1click", eval_1click },
  {(char *) 0, (int (*)()) 0}
};

int main( int ac, char **av )
{
  int i;
    
  if( ac < 2 ){

#ifdef OUTERR      
    fprintf(stderr, "Usage: %s command args...\n", av[0]);
    fprintf(stderr, " command= glabel|dinlabel|gcompute|irec|label|compute|1click\n\n" );
    fprintf(stderr, "   *glabel* reads an ideal ranked list with gain values\n" );
    fprintf(stderr, "    and a system ranked list WITHOUT gain values;\n" );
    fprintf(stderr, "    outputs a system ranked list WITH gain values.\n\n" );
    fprintf(stderr, "   *dinlabel* reads a din (diversify for inf and nav) file\n" );
    fprintf(stderr, "    and a system ranked list WITHOUT gain values;\n" );
    fprintf(stderr, "    outputs a system ranked list WITH gain values.\n" );
    fprintf(stderr, "    dup docs for nav intents are ignored.\n\n" );
    fprintf(stderr, "   *gcompute* reads an ideal ranked list with gain values\n" );
    fprintf(stderr, "    and a system ranked list WITH gain values;\n" );
    fprintf(stderr, "    outputs evaluation metric values.\n\n" );
    fprintf(stderr, "   *irec reads one or more reldoclist (possibly with other info)\n" );
    fprintf(stderr, "    for each intent, and a system ranked list;\n" );
    fprintf(stderr, "    outputs I-recall.\n\n" );
    fprintf(stderr, "   *label* reads a list of judged docs with rel levels\n" );
    fprintf(stderr, "    and a system ranked list;\n" );
    fprintf(stderr, "    outputs a system ranked list WITH rel levels.\n\n" );
    fprintf(stderr, "   *compute* reads a list of judged docs with rel levels\n" );
    fprintf(stderr, "    and a system ranked list WITH rel levels;\n" );
    fprintf(stderr, "    outputs evaluation metric values.\n\n" );
    fprintf(stderr, "   *1click* reads a gold-standard nugget file and a\n" );
    fprintf(stderr, "    matched nugget file and outputs evaluation metric values\n\n" );
#endif
	
    exit(1);
  }

  for( i = 0; table[i].cmd; i++ )
    if( strcmp( table[i].cmd, av[1] ) == 0 ){
      table[i].func(ac, av);
      exit( 0 );
    }

#ifdef OUTERR
  fprintf( stderr, "Unknown command.\n");
#endif

  return( 0 );

}/* of main */

/******************************************************************************
  eval_dinlabel

similar to glabel but is for computing 
DIN (diversification for informational and navigational intents) metrics.
Thus, redundant docs for navigational intents are not labelled.

Instead of a global ideal list,
reads a din file, which is of the form:
<intnum> <inf/nav> <docID> <P(i|q)*gi>

Also reads a systemfile and labels it.

systemfile format:
<docID>
<docID>
  :

output format (default: judged and unjudged docs):
<docID> <gv>
<docID> 0
<docID> <gv>
  :

where gv = SUM P(i|q)*gi + SUM isnew(j)P(j|q)*gj
            i               j

i: informational
j: nagivational
isnew(j): flag indicating whether the nav intent is new or not

    -j option will remove documents not listed in ideal file.
  
return value: 0 (OK)
             -1 (NG)
******************************************************************************/
static int eval_dinlabel( int ac, char **av )
{
  FILE *fa = NULL;
  FILE *fs = stdin;

  int argc = 2;
  int isjudged;
  int found[ INTENT_NUMMAX ]; /* is intent already seen? */

  long truncaterank = 0;
  /* by default, do not truncate */
  long i;
  long highintnum; /* highest intent number */

  double ggain; /* global gain of doc */

  char line[ BUFSIZ + 1 ];
  char *pos;

  struct strdoublong2list *din = NULL;
  struct strdoublong2list *p;

  int freestrdoublong2list();
  long store_din();

  ignore_unjudged = 0;
  /* by default, output all docs in system list (=judged + unjudged) */

  while( argc < ac ){

    if( strcmp( av[ argc ], OPTSTR_IGNORE_UNJUDGED ) == 0 ){
      ignore_unjudged = 1;
      argc++;
    }
    else if( strcmp( av[ argc ], OPTSTR_SEP ) == 0 && ( argc + 1 < ac ) ){
      sep = strdup( av[ argc + 1 ] );
      argc += 2;
    }
    else if( strcmp( av[ argc ], OPTSTR_TRUNCATE ) == 0 && ( argc + 1 < ac ) ){
      truncaterank = atol( av[ argc + 1 ] );
      if( truncaterank < 1 ){

#ifdef OUTERR
        fprintf( stderr, "Bad %s value\n", OPTSTR_TRUNCATE );
#endif
        return( -1 );
      }
      argc += 2;
    }
    else if( strcmp( av[ argc ], OPTSTR_DINFILE ) == 0 &&
	     ( argc + 1 < ac ) ){
      if( ( fa = fopen( av[ argc + 1 ], "r" ) ) == NULL ){

#ifdef OUTERR
	fprintf( stderr, "Cannot open %s\n", av[ argc + 1 ] );
#endif
	return( -1 );
      }
      argc += 2;
    }
      /* ranked list file */
    else{
      if( ( fs = fopen( av[ argc ], "r" ) ) == NULL ){

#ifdef OUTERR
	fprintf( stderr, "Cannot open %s\n", av[ argc ] );
#endif
	return( -1 );
      }
      argc++;
    }
  }

  if( fa == NULL ){

#ifdef OUTERR      
    fprintf( stderr,
	     "Usage: %s %s [%s] [%s <separator>] [%s <rank>] %s <din_file> [system ranked list]\n",
	     av[ 0 ], av[ 1 ],
	     OPTSTR_IGNORE_UNJUDGED,
	     OPTSTR_SEP, OPTSTR_TRUNCATE, OPTSTR_DINFILE );
#endif
    return( 0 );
  }

  if( sep == NULL ){
    sep = strdup( DEFAULT_SEP );
  }

  /* store din file */
  if( ( highintnum = store_din( fa, &din ) ) == 0 ){

#ifdef OUTERR
    fprintf( stderr, "store_din failed\n" );
#endif
    return( -1 );
  }
  fclose( fa );

  for( i = 0; i < highintnum; i++ ){
    found[ i ] = 0; /* intent 1 is stored in found[ 0 ] */
  }

  /* output system ranked list with gain values */    

  i = 1; /* rank */
  while( fgets( line, sizeof( line ), fs ) ){

    if( ( pos = strchr( line, '\n' ) ) == NULL ){
#ifdef OUTERR   
      fprintf( stderr, "Line too long: %s\n", line );
#endif  
      return( -1 );
    }
    *pos = '\0';

    isjudged = 0; /* is this a judged doc (i.e. one in ideal list?) */
    ggain = 0.0;
    p = din;
    while( p ){

      if( strcmp( p->string, line ) == 0 ){

	isjudged = 1;
	if( p->val3 == NAVIGATIONAL ){

	  if( found[ p->val2 - 1 ] ){
	    /* nav intent already seen -> do not add to ggain */
	  }
	  else{ /* nav intent seen for the 1st time */
	    ggain += p->val1;
	  }
	}
	else{ /* INFORMATIONAL: store_din guarantees it */

	  ggain += p->val1; /* add to ggain unconditionally */
	}
	found[ p->val2 - 1 ] = 1; /* this intent has already been covered */

      }
      p = p->next;
    }

    if( isjudged ){

      printf( "%s%s%.4f\n", line, sep, ggain );
    }
    else{ /* unjudged doc */

      if( ignore_unjudged ){
	/* do not print unjudged document */
      }
      else{
	/* print this unjuded document WITHOUT a gain value */
	printf( "%s\n", line );
      }

    }

    if( i == truncaterank ){ /* if truncaterank is positive,
                                truncate system output at this rank */
      break;
    }
    i++;
  }
  fclose( fs );

  free( sep );
  freestrdoublong2list( &din );
  return( 0 );

}/* of eval_dinlabel */


/******************************************************************************
  eval_glabel

reads an ideal ranked list with decreasing gain values
      (possibly with judged nonrel docs at bottom)
      and a system ranked list WITHOUT gain values;
outputs a system ranked list WITH gain values.

ideal ranked list file format:
<docID> <gv>
<docID> <gv>
  :
[Sorted by gv.]

systemfile format:
<docID>
<docID>
  :

output format (default: judged and unjudged docs):
<docID> <gv>
<docID> 0
<docID> <gv>
  :

    -j option will remove documents not listed in ideal file.
  
return value: 0 (OK)
             -1 (NG)
******************************************************************************/
static int eval_glabel( int ac, char **av )
{
  FILE *fa = NULL;
  FILE *fs = stdin;

  int argc = 2;
  int isjudged;

  long jnonrelnum; /* N: number of judged nonrelevant docs */
  long jrelnum;    /* R: number of judged relevant docs */
  long truncaterank = 0;
  /* by default, do not truncate */
  long i;

  char line[ BUFSIZ + 1 ];
  char *pos;

  struct strdoublist *ideal = NULL; /* ideal list with decreasing gains */
  struct strdoublist *p;

  int freestrdoublist();
  double store_ideal();

  ignore_unjudged = 0;
  /* by default, output all docs in system list (=judged + unjudged) */

  while( argc < ac ){

    if( strcmp( av[ argc ], OPTSTR_IGNORE_UNJUDGED ) == 0 ){
      ignore_unjudged = 1;
      argc++;
    }
    else if( strcmp( av[ argc ], OPTSTR_SEP ) == 0 && ( argc + 1 < ac ) ){
      sep = strdup( av[ argc + 1 ] );
      argc += 2;
    }
    else if( strcmp( av[ argc ], OPTSTR_TRUNCATE ) == 0 && ( argc + 1 < ac ) ){
      truncaterank = atol( av[ argc + 1 ] );
      if( truncaterank < 1 ){

#ifdef OUTERR
        fprintf( stderr, "Bad %s value\n", OPTSTR_TRUNCATE );
#endif
        return( -1 );
      }
      argc += 2;
    }
    else if( strcmp( av[ argc ], OPTSTR_IDEALFILE ) == 0 &&
	     ( argc + 1 < ac ) ){
      if( ( fa = fopen( av[ argc + 1 ], "r" ) ) == NULL ){

#ifdef OUTERR
	fprintf( stderr, "Cannot open %s\n", av[ argc + 1 ] );
#endif
	return( -1 );
      }
      argc += 2;
    }
      /* ranked list file */
    else{
      if( ( fs = fopen( av[ argc ], "r" ) ) == NULL ){

#ifdef OUTERR
	fprintf( stderr, "Cannot open %s\n", av[ argc ] );
#endif
	return( -1 );
      }
      argc++;
    }
  }

  if( fa == NULL ){

#ifdef OUTERR      
    fprintf( stderr,
	     "Usage: %s %s [%s] [%s <separator>] [%s <rank>] %s <ideal_file> [system ranked list]\n",
	     av[ 0 ], av[ 1 ],
	     OPTSTR_IGNORE_UNJUDGED,
	     OPTSTR_SEP, OPTSTR_TRUNCATE, OPTSTR_IDEALFILE );
#endif
    return( 0 );
  }

  if( sep == NULL ){
    sep = strdup( DEFAULT_SEP );
  }

  /* store ideal list with decreasing gain values */
  if( store_ideal( &ideal, &jrelnum, &jnonrelnum, fa ) <= 0.0 ){

#ifdef OUTERR
    fprintf( stderr, "store_ideal failed\n" );
#endif
    return( -1 );
  }
  fclose( fa );

  /* output system ranked list with gain values */    

  i = 1; /* rank */
  while( fgets( line, sizeof( line ), fs ) ){

    if( ( pos = strchr( line, '\n' ) ) == NULL ){
#ifdef OUTERR   
      fprintf( stderr, "Line too long: %s\n", line );
#endif  
      return( -1 );
    }
    *pos = '\0';

    isjudged = 0; /* is this a judged doc (i.e. one in ideal list?) */
    p = ideal;
    while( p ){

      if( strcmp( p->string, line ) == 0 ){

	printf( "%s%s%.4f\n", line, sep, p->val );
	isjudged = 1;
	break;
      }
      p = p->next;
    }

    if( isjudged == 0 ){ /* unjudged doc */

      if( ignore_unjudged ){
	/* do not print unjudged document */
      }
      else{
	/* print this unjuded document WITHOUT a gain value */
	printf( "%s\n", line );
      }

    }

    if( i == truncaterank ){ /* if truncaterank is positive,
                                truncate system output at this rank */
      break;
    }
    i++;
  }
  fclose( fs );

  free( sep );
  freestrdoublist( &ideal );
  return( 0 );

}/* of eval_glabel */


/******************************************************************************
  eval_gcompute

reads an ideal list with decreasing gain values
and a system list with gain values;
outputs several different evaluation metric values.

If the labelled ranked list is a condensed list,
use the -j option to compute bpref.

Note that, unlike compute,
gcompute relies on the highest gain value observed in the ideal list
(whereas compute uses the highest gain value explictly specified 
in the command line).

- Does not support graded-uniform NCU

releavance assessment file format:
<docID> <gv>
<docID> <gv>
  :
(may contain empty lines or comments beginning with #. They will be ignored.)

labelled systemfile format:
<docID> <gv>
<docID>
<docID> <gv>
 :    

******************************************************************************/
static int eval_gcompute( int ac, char **av )
{
  FILE *fa = NULL;
  FILE *fs = stdin;

  int argc = 2;
  int verbose = 0;

  int compute_gap = 0; /* computing GAP is more expensive
			than others so omit by default */

  long i, j;
  long cutoff[ CUTOFF_NUMMAX ]; /* cutoff for prec, hit, nDCG... */
  long cutoff_num = 0; /* 0 means cutoff was not specified by the user */
  long r1 = 0; /* rank of the first correct doc */
  long rp = 0; /* preferred rank for P-measure and P-plus */
  long syslen; /* length of system ranked list */
  long maxlen; /* max( syslen, jrelnum )
		    where jrelnum is the length of a minimal
		    ideal ranked output */
  long minlen; /* min(cutoff, syslen) */
  long jnonrelnum; /* N: number of judged nonrelevant docs */
  long jrelnum;    /* R: number of judged relevant docs */

  double *CGi, *DCGi, *msDCGi;
/* (cumulative) gain, discounted (cumulative) gain, 
   Microsoft's discounted (cumulative) gain for the ideal ranked list */
  double *Gs, *CGs, *DCGs, *msDCGs;
/* ditto for system ranked list */

  double *count; /* treat is as a double rather than an integer */
  double *BR;    /* blended ratio */
  double RBPp = 0.0; /* RBP persistence parameter p: should be positive */
  double *RBPpower;          /* p^(r-1) */
  double *RBPsum;            /* SUM g(r)p^(r-1) */
  double *ERR; /* expected reciprocal rank by Chapelle et al CIKM09 */
  double *ERRdsat; /* dissatisfaction prob at rank r for ERR */
  double *ERRi; /* ideal ERR for normalisation */
  double *ERRidsat;
  double *EBR; /* expected blended ratio, 2018 */
  double *GAPsum; /* for computing Robertson GAP */
  double *GAPisum; /* for GAP@l */

  double ratio, ratio2, ratio3;
  double sum, sum2, sum3;
  double mini;
  double penalty;
  double denom;
  double hgain; /* highest gain value in ideal ranked list */
  double qbeta = DEFAULT_BETA;
  double gamma = DEFAULT_GAMMA;
  double logbase = DEFAULT_LOGB;
  double sysgainmax; /* max gain value in system list
			      for obtaining preferred rank */

  char *buf, *p;

  struct strdoublist *ideal = NULL; /* ideal list with decreasing gains */
  struct strdoublist *syslist = NULL; /* ranked list with gains */
  struct strdoublist *doc;

  int gcompute_usage();
  double store_ideal();
  int store_syslist();
  int freestrdoublist();
  double orig_dcglog();

  ignore_unjudged = 0;
  /* by default, assume that system list contains unjudged docs.
     if ignore_unjudged == 1, assume that system list is a condensed list,
     so output bpref. */

  while( argc < ac ){

    if( strcmp( av[ argc ], OPTSTR_HELP ) == 0 ){
      return( gcompute_usage( ac, av ) );
    }
    else if( strcmp( av[ argc ], OPTSTR_VERBOSE ) == 0 ){
      verbose = 1;
      argc++;
    }
    else if( strcmp( av[ argc ], OPTSTR_GAP ) == 0 ){
      compute_gap = 1;
      argc++;
    }
    else if( strcmp( av[ argc ], OPTSTR_IGNORE_UNJUDGED ) == 0 ){
      ignore_unjudged = 1; /* input contains judged docs only
			      so output bpref etc. */
      argc++;
    }
    else if( strcmp( av[ argc ], OPTSTR_OUTSTR ) == 0 && ( argc + 1 < ac ) ){
      /* user specified string for each output line, e.g. topicID */
      outstr = strdup( av[ argc + 1 ] );
      argc += 2;
    }
    else if( strcmp( av[ argc ], OPTSTR_SEP ) == 0 && ( argc + 1 < ac ) ){
      sep = strdup( av[ argc + 1 ] );
      argc += 2;
    }
    else if( strcmp( av[ argc ], OPTSTR_IDEALFILE ) == 0 &&
	     ( argc + 1 < ac ) ){
      if( ( fa = fopen( av[ argc + 1 ], "r" ) ) == NULL ){

#ifdef OUTERR
	fprintf( stderr, "Cannot open %s\n", av[ argc + 1 ] );
#endif
	return( -1 );
      }
      argc += 2;
    }
    else if( strcmp( av[ argc ], OPTSTR_BETA ) == 0 && ( argc + 1 < ac ) ){
      /* for blended ratio */
      qbeta = atof( av[ argc + 1 ] );
      argc += 2;
    }
    else if( strcmp( av[ argc ], OPTSTR_GAMMA ) == 0 && ( argc + 1 < ac ) ){
      /* for rank-biased NCU */
      gamma = atof( av[ argc + 1 ] );
      if( gamma > 1 ){

#ifdef OUTERR
	fprintf( stderr, "Bad %s value\n", OPTSTR_GAMMA );
#endif
	return( -1 );
      }
      argc += 2;
    }
    else if( strcmp( av[ argc ], OPTSTR_LOGB ) == 0 && ( argc + 1 < ac ) ){
      /* for original nDCG */
      logbase = atof( av[ argc + 1 ] );
      argc += 2;
    }
    else if( strcmp( av[ argc ], OPTSTR_RBP ) == 0 && ( argc + 1 < ac ) ){
      RBPp = atof( av[ argc + 1 ] );
      if( RBPp > 1 ){

#ifdef OUTERR
	fprintf( stderr, "Bad %s value\n", OPTSTR_RBP );
#endif
	return( -1 );
      }
      argc += 2;
    }
    else if( strcmp( av[ argc ], OPTSTR_CUTOFF ) == 0 && ( argc + 1 < ac ) ){

      buf = strdup( av[ argc + 1 ] );
      cutoff_num = 0;
      if( ( p = strtok( buf, "," ) ) == NULL ){

#ifdef OUTERR
	fprintf( stderr, "strtok failed\n" );
#endif
	return( -1 );
      }
      do{
	cutoff[ cutoff_num ] = atof( p );
	cutoff_num++;
	if( cutoff_num >= CUTOFF_NUMMAX ){

#ifdef OUTERR
	  fprintf( stderr, "Too many cutoffs\n" );
#endif
	  return( -1 );
	}
      }while( ( p = strtok( NULL, "," ) ) != NULL );
	
      free( buf );
      argc += 2;
    }
    /* system file with gain values*/
    else{
      if( ( fs = fopen( av[ argc ], "r" ) ) == NULL ){

#ifdef OUTERR
	fprintf( stderr, "Cannot open %s\n", av[ argc ] );
#endif
	return( -1 );
      }
      argc++;
    }
  }

  if( fa == NULL ){
    return( gcompute_usage( ac, av ) );
  }

  if( RBPp == 0.0 ){ /* RBPp not specified */
    RBPp = DEFAULT_RBP;
  }

  if( outstr == NULL ){
    /* output prefix not specified */
    outstr = strdup( "" );
  }
  if( sep == NULL ){
    sep = strdup( DEFAULT_SEP );
  }

  /* if cutoff is not specified, use default */
  if( cutoff_num == 0 ){
    cutoff_num = 1;
    cutoff[ 0 ] = DEFAULT_CUTOFF;
  }

  /* set ideal gains, jrelnum and jnonrelnum */
  if( ( hgain = store_ideal( &ideal, &jrelnum, &jnonrelnum, fa ) ) <= 0.0 ){

#ifdef OUTERR
    fprintf( stderr, "store_ideal_gains\n" );
#endif
    return( -1 );

    }
  fclose( fa );

  if( jrelnum == 0 ){

#ifdef OUTERR
    fprintf( stderr, "no rel doc\n" );
#endif
    return( -1 );
  }

  if( ( ignore_unjudged == 1 ) && ( jnonrelnum == 0 ) ){

#ifdef OUTERR
    fprintf( stderr,
	     "no judged nonrel: bpref etc. not computable\n" );
#endif
    return( -1 );
  }

  /* store system list with gain values */
  if( store_syslist( &syslist, &syslen, &sysgainmax, fs ) < 0 ){

#ifdef OUTERR
    fprintf( stderr, "store_syslist failed\n" );
#endif
    return( -1 );
  }
  fclose( fs );

  printf( "%s # syslen=%ld jrel=%ld jnonrel=%ld\n",
	  outstr, syslen, jrelnum, jnonrelnum );

  /* maxlen = max( jrelnum, syslen ) */
  maxlen = jrelnum;
  if( maxlen < syslen ){
    maxlen = syslen;
  }

  CGi = ( double *)calloc( maxlen + 1, sizeof( double ) );
  DCGi = ( double *)calloc( maxlen + 1, sizeof( double ) );
  msDCGi = ( double *)calloc( maxlen + 1, sizeof( double ) );

  /* it is convenient to store raw system gain values for each rank. */
  Gs = ( double *)calloc( maxlen + 1, sizeof( double ) );
  CGs = ( double *)calloc( maxlen + 1, sizeof( double ) );
  DCGs = ( double *)calloc( maxlen + 1, sizeof( double ) );
  msDCGs = ( double *)calloc( maxlen + 1, sizeof( double ) );

  count = ( double *)calloc( maxlen + 1, sizeof( double ) );
  BR = ( double *)calloc( maxlen + 1, sizeof( double ) );

  RBPpower = ( double *)calloc( maxlen + 1, sizeof( double ) );
  RBPsum = ( double *)calloc( maxlen + 1, sizeof( double ) );

  ERR = ( double *)calloc( maxlen + 1, sizeof( double ) );
  ERRdsat = ( double *)calloc( maxlen + 1, sizeof( double ) );
  ERRi = ( double *)calloc( maxlen + 1, sizeof( double ) );
  ERRidsat = ( double *)calloc( maxlen + 1, sizeof( double ) );

  /* 2018 EBR */
  EBR = ( double *)calloc( maxlen + 1, sizeof( double ) );

  GAPsum = ( double *)calloc( maxlen + 1, sizeof( double ) );
  GAPisum = ( double *)calloc( maxlen + 1, sizeof( double ) );

  /* store ideal cumulative gain etc. */

  ERRi[ 0 ] = 0;
  ERRidsat[ 0 ] = 1;

  doc = ideal;
  for( i = 1; i <= jrelnum; i++ ){

    CGi[ i ] = CGi[ i - 1 ] + doc->val;
    DCGi[ i ] = DCGi[ i - 1 ] + doc->val/orig_dcglog( logbase, i );
    /* For MSnDCG, the logbase is irrelevant */
    msDCGi[ i ] = msDCGi[ i - 1 ] + doc->val/log( i + 1 );

    if( compute_gap ){
      GAPisum[ i ] = GAPisum[ i - 1 ] + doc->val * ( doc->val + 1 );
    }

    ERRi[ i ] = ERRi[ i - 1 ] + ( ERRidsat[ i - 1 ]/i ) * ( doc->val/( hgain+1 ) );
    ERRidsat[ i ] = ERRidsat[ i - 1 ] * ( 1 - doc->val/( hgain+1 ) );
    
    doc = doc->next;
  }

  freestrdoublist( &ideal );

  /* exhausted reldocs, so copy the cumulative vals at rank jrelnum
     for the remaining ranks (if any) */
  for( i = jrelnum + 1; i <= maxlen; i++ ){

    CGi[ i ] = CGi[ jrelnum ];
    DCGi[ i ] = DCGi[ jrelnum ];
    msDCGi[ i ] = msDCGi[ jrelnum ];

    if( compute_gap ){
      GAPisum[ i ] = GAPisum[ jrelnum ];
    }

    ERRi[ i ] = ERRi[ jrelnum ];

  }

  if( verbose ){
    printf( "\n" );
    for( i = 1; i <= maxlen; i++ ){
      printf( "#%ld CGi=%.4f DCGi=%.4f msDCGi=%.4f\n",
	      i, CGi[ i ], DCGi[ i ], msDCGi[ i ] );
    }
  }

  /* store system cumulative gain, etc. */

  RBPpower[ 1 ] = 1;
  RBPsum[ 0 ] = 0;
  ERR[ 0 ] = 0;
  ERRdsat[ 0 ] = 1;
  EBR[ 0 ] = 0; /* 2018 */

  doc = syslist;

  for( i = 1; i <= syslen; i++ ){

    if( doc->val > 0.0 ){ /* relevant */

      count[ i ] = count[ i - 1 ] + 1;
      Gs[ i ] = doc->val;
      CGs[ i ] = CGs[ i -1 ] + doc->val;
      DCGs[ i ] = DCGs[ i - 1 ] + doc->val/orig_dcglog( logbase, i );
      /* For MSnDCG, the logbase is irrelevant */
      msDCGs[ i ] = msDCGs[ i - 1 ] + doc->val/log( i + 1 );

      if( compute_gap ){ /* do rather expensive GAP computation */

	/* NOTE: GAP in compute relies on relevance levels, not
	   gain values. In contrast, GAP in gcompute relies on global
	   gain values. */

	GAPsum[ i ] = 0.0;
	for( j = 1; j <= i; j++ ){

	  /* m(i,j)=min(Gs[i], Gs[j]), add m(i,j)*(m(i,j)+1) */
	  if( Gs[ i ] < Gs[ j ] ){
	    GAPsum[ i ] += Gs[ i ] * ( Gs[ i ] + 1 );
	  }
	  else{
	    GAPsum[ i ] += Gs[ j ] * ( Gs[ j ] + 1);
	  }
	}

      }

      if( count[ i ] == 1 ){ /* first relevant document */
	r1 = i; /* for RR and O-measure */
      }

      if( ( rp == 0 ) && ( doc->val == sysgainmax ) ){
	/* preferred rank not yet set AND current gain is highest in list */
	rp = i; /* for P-measure and P++ */
      }
      
    }
    else{ /* nonrelevant */

      count[ i ] = count[ i - 1 ];
      CGs[ i ] = CGs[ i - 1 ];
      DCGs[ i ] = DCGs[ i - 1 ];
      msDCGs[ i ] = msDCGs[ i - 1 ];
    }

    /* blended ratio */
    BR[ i ] = ( qbeta * CGs[ i ] + count[ i ] )/( qbeta * CGi[ i ] + i );
    
    if( i > 1 ){
      RBPpower[ i ] = RBPp * RBPpower[ i - 1 ];
    }
    RBPsum[ i ] = RBPsum[ i - 1 ] + Gs[ i ] * RBPpower[ i ];

    /* divide gain by maxgain+1 rather than maxgain to make sure
       rel prob < 1 even for a highly rel doc.
       Otherwise, all docs after the first highly rel doc
       will be ignored.
       Note that, because of this, ERR in as defined here
       and in CIKM09 paper Eq.4 does NOT reduce to RR even in the
       binary relevance case.
    */
    ERR[ i ] = ERR[ i - 1 ] + ( ERRdsat[ i - 1 ]/i ) * ( Gs[ i ]/( hgain+1 ) );
    ERRdsat[ i ] = ERRdsat[ i - 1 ] * ( 1 - Gs[ i ]/( hgain+1 ) );

    /* 2018 */
    EBR[ i ] = EBR[ i - 1 ] + BR[ i ]*ERRdsat[ i - 1 ]*Gs[ i ]/( hgain+1 );

    doc = doc->next;
  }

  freestrdoublist( &syslist );

  for( i = syslen + 1; i <= maxlen; i++ ){ /* if syslen < jrelnum
					    no more relevant */

    count[ i ] = count[ syslen ];
    CGs[ i ] = CGs[ syslen ];
    DCGs[ i ] = DCGs[ syslen ];
    msDCGs[ i ] = msDCGs[ syslen ];
    ERR[ i ] = ERR[ syslen ];
    /* 2018 */
    EBR[ i ] = EBR[ syslen ];
    
    /* 2018 */
    RBPsum[ i ] = RBPsum[ syslen ]; /* RBP@l 2018 */

    /* no need to compute BR beyond syslen */

  }

  if( verbose ){
    printf( "\n" );
    for( i = 1; i <= syslen; i++ ){
      if( Gs[ i ] > 0 ){
	printf( "#%ld count=%ld Gs=%.4f CGs=%.4f DCGs=%.4f msDCGs=%.4f BR=%.4f\n",
		i, (long)count[ i ], Gs[ i ], CGs[ i ], DCGs[ i ], msDCGs[ i ], BR[ i ] );
      }
    }
    printf( "\n" );
  }
    
  printf( "%s # r1=%ld rp=%ld\n", outstr, r1, rp );

    
  /***** output evaluation metric values *****/

  /* RR, O-measure, P-measure, P-plus */
  if( r1 == 0 ){ /* no relevant doc in system output */
    printf( "%s RR=                  %.4f\n", outstr, (double)0 );
    printf( "%s O-measure=           %.4f\n", outstr, (double)0 );
    printf( "%s P-measure=           %.4f\n", outstr, (double)0 );
    printf( "%s P-plus=              %.4f\n", outstr, (double)0 );
  }
  else{ /* at least one rel doc in system output */
    printf( "%s RR=                  %.4f\n", outstr, (double)1/r1 );
    printf( "%s O-measure=           %.4f\n", outstr, BR[ r1 ] );
    printf( "%s P-measure=           %.4f\n", outstr, BR[ rp ] );
    sum = 0.0;
    for( i = 1; i <= rp; i++ ){
      if( Gs[ i ] > 0 ){
	sum += BR[ i ];
      }
    }
    printf( "%s P-plus=              %.4f\n", outstr, sum/count[ rp ] );
  }

  /* AP and Q */ 
  sum = 0.0; sum2 = 0.0;
  for( i = 1; i <= syslen; i++ ){
    if( Gs[ i ] > 0 ){
      sum += count[ i ]/i;
      sum2 += BR[ i ];
    }
  }
  printf( "%s AP=                  %.4f\n", outstr, sum/jrelnum );
  printf( "%s Q-measure=           %.4f\n", outstr, sum2/jrelnum );

  /* NCU measures */

  /* rank-biased NCU */
  denom = 0.0;
  for( i = 1; i <= jrelnum; i++ ){
    denom += pow( gamma, i-1 );
  }

  sum = 0.0; sum2 = 0.0;
  for( i = 1; i <= syslen; i++ ){
    if( Gs[ i ] > 0 ){
      sum += ( count[ i ]/i ) * pow( gamma, count[ i ]-1 );
      sum2 += BR[ i ] * pow( gamma, count[ i ]-1 );
    }
  }
  printf( "%s NCUrb,P=             %.4f\n", outstr, sum/denom );
  printf( "%s NCUrb,BR=            %.4f\n", outstr, sum2/denom );

  /* GAP */
  if( compute_gap ){

    sum = 0.0;
    for( i = 1; i <= syslen; i++ ){
      sum += GAPsum[ i ]/i;
    }
    printf( "%s GAP=                 %.4f\n",
	    outstr, sum/GAPisum[ jrelnum ] );

  }

  /* RBP */
  printf( "%s RBP=                 %.4f\n",
	  outstr,(1-RBPp)*RBPsum[ syslen ]/hgain );
  /* ERR */
  printf( "%s ERR=                 %.4f\n", outstr, ERR[ syslen ] );
  /* 2018 EBR */
  printf( "%s EBR=                 %.4f\n", outstr, EBR[ syslen ] );

  /* cutoff-based metrics */
  for( i = 0; i < cutoff_num; i++ ){

    /* cutoff-based Q */ 
    minlen = cutoff[ i ];
    if( minlen > syslen ){
      minlen = syslen;
    }/* minlen = min(cutoff, syslen) */

    sum = 0.0; sum2 = 0.0; 
    for( j = 1; j <= minlen; j++ ){
      if( Gs[ j ] > 0 ){
	sum += count[ j ]/j;
	sum2 += BR[ j ];
      }
    }

    /* divide by min(cutoff, jrelnum ) */
    if( cutoff[ i ] < jrelnum ){
      printf( "%s AP@%04ld=             %.4f\n",
	      outstr, cutoff[ i ], sum/cutoff[ i ] );
      printf( "%s Q@%04ld=              %.4f\n",
	      outstr, cutoff[ i ], sum2/cutoff[ i ] );
    }
    else{ 
      printf( "%s AP@%04ld=             %.4f\n",
	      outstr, cutoff[ i ], sum/jrelnum );
      printf( "%s Q@%04ld=              %.4f\n",
	      outstr, cutoff[ i ], sum2/jrelnum );
    }

    if( compute_gap ){

      sum = 0.0;
      for( j = 1; j <= minlen; j++ ){
        sum += GAPsum[ j ]/j;
      }

      if( cutoff[ i ] < jrelnum ){
        printf( "%s GAP@%04ld=            %.4f\n",
                outstr, cutoff[ i ], sum/GAPisum[ cutoff[ i ] ] );
      }
      else{
        printf( "%s GAP@%04ld=            %.4f\n",
                outstr, cutoff[ i ], sum/GAPisum[ jrelnum ] );
      }
      
    }


    if( cutoff[ i ] <= maxlen ){

      printf( "%s nDCG@%04ld=           %.4f\n",
	      outstr, cutoff[ i ], DCGs[ cutoff[ i ] ]/DCGi[ cutoff[ i ] ]);
      printf( "%s MSnDCG@%04ld=         %.4f\n",
	      outstr, cutoff[ i ], msDCGs[ cutoff[ i ] ]/msDCGi[ cutoff[ i ] ]);
      printf( "%s P@%04ld=              %.4f\n",
	      outstr, cutoff[ i ], count[ cutoff[ i ] ]/cutoff[ i ]);
      printf( "%s RBP@%04ld=            %.4f\n",
              outstr, cutoff[ i ], (1-RBPp)*RBPsum[ cutoff[ i ] ]/hgain );
      /* 20181003 note that hgain is obtained PER TOPIC */
      printf( "%s ERR@%04ld=            %.4f\n",
              outstr, cutoff[ i ], ERR[ cutoff[ i ] ]);
      printf( "%s nERR@%04ld=           %.4f\n",
              outstr, cutoff[ i ], ERR[ cutoff[ i ] ]/ERRi[ cutoff[ i ] ]);
      /* 2018 */
      printf( "%s EBR@%04ld=            %.4f\n",
              outstr, cutoff[ i ], EBR[ cutoff[ i ] ] );

      /* In version 180312, ERR@l was included in gcompute but NOT in compute. */
      /* 20180909 RBP@l */      

      if( (long)count[ cutoff[ i ] ] > 0 ){
	printf( "%s Hit@%04ld=            %.4f\n",
		outstr, cutoff[ i ], (double)1 );
      }
      else{
	printf( "%s Hit@%04ld=            %.4f\n",
		outstr, cutoff[ i ], (double)0 );
      }
	
    }
    else{ /* cutoff exceeds maxlen */

      printf( "%s nDCG@%04ld=           %.4f\n",
	      outstr, cutoff[ i ], DCGs[ maxlen ]/DCGi[ maxlen ]);
      printf( "%s MSnDCG@%04ld=         %.4f\n",
	      outstr, cutoff[ i ], msDCGs[ maxlen ]/msDCGi[ maxlen ]);
      printf( "%s P@%04ld=              %.4f\n",
	      outstr, cutoff[ i ], count[ maxlen ]/cutoff[ i ]);
      /* note that the above definition of precision is correct. */
      printf( "%s RBP@%04ld=            %.4f\n",
              outstr, cutoff[ i ], (1-RBPp)*RBPsum[ maxlen ]/hgain );
      /* 20181003 note that hgain is obtained PER TOPIC */
      printf( "%s ERR@%04ld=            %.4f\n",
              outstr, cutoff[ i ], ERR[ maxlen ]);
      printf( "%s nERR@%04ld=           %.4f\n",
              outstr, cutoff[ i ], ERR[ maxlen ]/ERRi[ maxlen ]);
      /* 2018 */
      printf( "%s EBR@%04ld=            %.4f\n",
              outstr, cutoff[ i ], EBR[ maxlen ] );

      /* In version 180312, ERR@l was included in gcompute but NOT in compute. */
      /* 20180909 RBP@l */

      if( (long)count[ maxlen ] > 0 ){
	printf( "%s Hit@%04ld=            %.4f\n",
		outstr, cutoff[ i ], (double)1 );
      }
      else{
	printf( "%s Hit@%04ld=            %.4f\n",
		outstr, cutoff[ i ], (double)0 );
      }
    }

  }

  if( ignore_unjudged ){ /* compute bpref etc */
    
    /* bpref and bpref_R */
    sum = 0.0; sum2 = 0.0;
    for( i = 1; i <= syslen; i++ ){

      if( Gs[ i ] > 0 ){

	mini = i - count[ i ];
	if( jrelnum < mini ){ mini = jrelnum; };
	/* min(R, r'-count(r')) */

	/* bpref */
	if( jrelnum > jnonrelnum ){
	  ratio = 1 - mini/jnonrelnum;
	}
	else{ /* jrelnum <= jnonrelnum */
	  ratio = 1 - mini/jrelnum;
	}
	/* 1- min(R, r'-count(r')/min(R,N) */
	sum += ratio;

	/* bpref_R */
	ratio2 = 1 - mini/jrelnum;
	/* 1- min(R, r'-count(r')/R */
	sum2 += ratio2;
      }

    }
    printf( "%s bpref=               %.4f\n", outstr, sum/jrelnum );
    printf( "%s bpref_R=             %.4f\n", outstr, sum2/jrelnum );

    /* bpref_N and bpref_relative */
    sum = 0.0; sum2 = 0.0;
    for( i = 1; i <= syslen; i++ ){

      if( Gs[ i ] > 0 ){

	/* bpref_N */
	ratio = 1 - (double)(i-count[ i ])/jnonrelnum;
	sum += ratio;
	
	/* bpref_relative */
	if( i > 1 ){
	  ratio2 = 1 - (double)(i-count[ i ])/(i-1);
	}
	else{ /* ignore relevant doc at Rank 1 */
	  ratio2 = 0.0;
	}
	sum2 += ratio2;
      }
    }
    printf( "%s bpref_N=             %.4f\n", outstr, sum/jrelnum );
    printf( "%s bpref_relative=      %.4f\n", outstr, sum2/jrelnum );

    /* rpref_N, rpref_relative and rpref_relative2 */
    sum = 0.0; sum2 = 0.0; sum3 = 0.0;
    for( i = 1; i <= syslen; i++ ){

      if( Gs[ i ] > 0 ){

	/* rpref_N */
	penalty = 0.0; /* penalty for rank i: examine Ranks 1-(i-1) */
	for( j = 1; j < i; j++ ){
	  if( Gs[ i ] - Gs[ j ] > 0 ){
	    penalty += (Gs[ i ]-Gs[ j ])/Gs[ i ];
	  }
	}
	ratio = 1 - penalty/( jrelnum + jnonrelnum - CGi[ jrelnum ]/hgain );
	sum += Gs[ i ] * ratio;

	/* rpref_relative */
	if( i > 1 ){
	  ratio2 = 1 - penalty/(i-1);
	}
	else{  /* ignore relevant doc at Rank 1 */
	  ratio2 = 0.0;
	}
	sum2 += Gs[ i ] * ratio2;

	/* rpref_relative2 */
	ratio3 = 1 - penalty/i;
	sum3 += Gs[ i ] * ratio3;

      }
    }

    /* note: CGi can be used instead of cgi here */
    printf( "%s rpref_N=             %.4f\n", outstr, sum/CGi[ jrelnum ] );
    printf( "%s rpref_relative=      %.4f\n", outstr, sum2/CGi[ jrelnum ] );
    printf( "%s rpref_relative2=     %.4f\n", outstr, sum3/CGi[ jrelnum ] );
  }

/* end */

  free( outstr );
  free( sep );

  free( CGi );
  free( DCGi );
  free( msDCGi );

  free( Gs );
  free( CGs );
  free( DCGs );
  free( msDCGs );

  free( count );
  free( BR );

  free( RBPpower );
  free( RBPsum );

  free( ERR );
  free( ERRdsat );
  free( ERRi );
  free( ERRidsat );

  free( EBR );

  free( GAPsum );
  free( GAPisum );

  return( 0 );

}/* of eval_gcompute */

     
/******************************************************************************
  eval_irec

reads reldoc lists for each intent (possibly with other values like gains)
and a system list;
outputs I-recall

each reldoc list may contain just docIDs or something extra (these are ignored)
list may or may not be sorted (makes no difference)

******************************************************************************/
static int eval_irec( int ac, char **av )
{
  FILE *fa[ INTENT_NUMMAX ];
  FILE *fs = NULL;

  int argc = 2;
  int verbose = 0;
  int found[ INTENT_NUMMAX ];
  /* flag indicating whether a reldoc for a particular intent has been found */

  long cutoff[ CUTOFF_NUMMAX ];
  long cutoff_num = 0;
  long intent_num = 0; /* actual number of intents == number of reldoc lists */
  /*  long covered;  intents covered <= intent_num */
  long covered = 0; /* fixed 2016/10/17 to handle empty system files */
  long i, j;

  char line[ BUFSIZ + 1 ];
  char *pos;
  char *buf, *p;

  struct strlist *rellist[ INTENT_NUMMAX ];
  /* list of reldocs for each intent */

  int strlistmatch();
  int fprintstrlist();
  int freestrlist();
  long firstfield2strlist();

  for( i = 0; i < INTENT_NUMMAX; i++ ){
    fa[ i ] = NULL;
    found[ i ] = 0;
    rellist[ i ] = NULL;
  }

  while( argc < ac ){
    
    if( strcmp( av[ argc ], OPTSTR_OUTSTR ) == 0 && ( argc + 1 < ac ) ){
      outstr = strdup( av[ argc + 1 ] );
      argc += 2;
    }
    else if( strcmp( av[ argc ], OPTSTR_SEP ) == 0 && ( argc + 1 < ac ) ){
      sep = strdup( av[ argc + 1 ] );
      argc += 2;
    }
    else if( strcmp( av[ argc ], OPTSTR_VERBOSE ) == 0 ){
      verbose = 1;
      argc++;
    }
    else if( strcmp( av[ argc ], OPTSTR_CUTOFF ) == 0 && ( argc + 1 < ac ) ){

      buf = strdup( av[ argc + 1 ] );
      cutoff_num = 0;
      if( ( p = strtok( buf, "," ) ) == NULL ){

#ifdef OUTERR
        fprintf( stderr, "strtok failed\n" );
#endif
        return( -1 );
      }
      do{
	cutoff[ cutoff_num ] = atof( p );
	cutoff_num++;
	if( cutoff_num >=CUTOFF_NUMMAX ){

#ifdef OUTERR
          fprintf( stderr, "Too many cutoffs\n" );
#endif
          return( -1 );
	}
      }while( ( p = strtok( NULL, "," ) ) != NULL );

      free( buf );
      argc += 2;
    }
    else{ /* system list */
      if( ( fs = fopen( av[ argc ], "r" ) ) == NULL ){

#ifdef OUTERR
        fprintf( stderr, "Cannot open %s\n", av[ 2 ] );
#endif
        return( -1 );
      }
      argc++;
      break; /* the rest are rellists */
    }
  }

  if( outstr == NULL ){
    outstr = strdup( "" );
  }
  if( sep == NULL ){
    sep = strdup( DEFAULT_SEP );
  }

  if( cutoff_num == 0 ){
    cutoff_num = 1;
    cutoff[ 0 ] = DEFAULT_CUTOFF;
  }

  /* open rel files for each intent */
  j = argc;
  for( argc = j; argc < ac; argc++ ){
    i = argc - j;
    if( i >= INTENT_NUMMAX ){
#ifdef OUTERR
      fprintf( stderr, "too many rel files\n" );
#endif
        return( -1 );
    }

    if( ( fa[ i ] = fopen( av[ argc ], "r" ) ) == NULL ){

#ifdef OUTERR
      fprintf( stderr, "Cannot open %s\n", av[ argc ] );
#endif
      return( -1 );
    }
    intent_num++;
  }

  if( fs == NULL || intent_num == 0 ){
#ifdef OUTERR
    fprintf( stderr,
	     "Usage: %s %s [%s <cutoff,...>] [%s <separator>] [%s <outstr>] [%s] <system ranked list> <rellist for intent1>...\n",
	     av[ 0 ], av[ 1 ], OPTSTR_CUTOFF, OPTSTR_SEP, OPTSTR_OUTSTR, OPTSTR_VERBOSE );
#endif
    return( -1 );
  }

  printf( "%s #intent_num=%ld\n", outstr, intent_num );

  /* store reldocs for each intent (file may contain extra fields but ignore)*/
  for( i = 0; i < intent_num; i++ ){
    if( firstfield2strlist( fa[ i ], &rellist[ i ] ) < 0 ){
      
#ifdef OUTERR
      fprintf( stderr, "firstfield2strlist failed\n" );
#endif
      return( -1 );
    }

    /*    printf( "#rellist %ld\n", i );
	  fprintstrlist( stdout, rellist[ i ] ); */
    fclose( fa[ i ] );
  }

  /* read system file and compute I-recall */
  i = 1; /* rank */
  while( fgets( line, sizeof( line ), fs ) ){

    if( ( pos = strchr( line, '\n' ) ) == NULL ){
#ifdef OUTERR   
      fprintf( stderr, "Line too long: %s\n", line );
#endif  
      return( -1 );
    }
    *pos = '\0';

    if( verbose ){
      printf( "#%s", line );
    }
      
    for( j = 0; j < intent_num; j++ ){

      if( strlistmatch( rellist[ j ], line ) ){
	/* this doc covers intent j */
	found[ j ] = 1;
	if( verbose ){
	  printf( " %ld", j + 1 ); /* intent number */
	}
      }
    }

    if( verbose ){
      printf( "\n" );
    }

    /*    covered = 0; 2016/10/17 now handles empty system files */

    covered = 0; /* 2018/09/22 recovered this to properly compute covered */
    for( j = 0; j < intent_num; j++ ){
      if( found[ j ] == 1 ){
	covered++;
      }
    }


    for( j = 0; j < cutoff_num; j++ ){
      if( i == cutoff[ j ] ){

	printf( "%s I-rec@%04ld=          %.4f\n", outstr, cutoff[ j ],
		(double)covered/intent_num );
      }
    }

    if( i == intent_num ){

      printf( "%s I-rec@n=             %.4f\n", outstr,
	      (double)covered/intent_num );
    }

    i++;
  }


  for( j = 0; j < cutoff_num; j++ ){
    if( cutoff[ j ] >= i ){ /* cutoff bigger than system size */

      printf( "%s I-rec@%04ld=          %.4f\n", outstr, cutoff[ j ],
	      (double)covered/intent_num );
    }
  }

  if( i <= intent_num ){
    /* I-recall is defined at rank n even if system size is smaller than n */

    printf( "%s I-rec@n=             %.4f\n", outstr,
	    (double)covered/intent_num );
  }

  fclose( fs );

  for( i = 0; i < intent_num; i++ ){
    freestrlist( &rellist[ i ] );
  }

  free( outstr );
  free( sep );
  
  return( 0 );

}/* of eval_irec */

/******************************************************************************
  store_ideal

read ideal ranked output (possibly with judged nonrel docs at bottom) and
store docIDS with decreasing gain values, preserving the exact order.
Also set jrel (#judged relevant) and jnonrel (#judged nonrelevant).

returns the highest gain value in ranked output
for computing RBP, ERR and rpref_N.

each docID MUST be accompanied by a gain value.

return value: positive highest gain value (OK)
              nonpositive value (ERROR)
******************************************************************************/
double store_ideal( jd, jrel, jnonrel, f )
     struct strdoublist **jd; /* o */
     long *jrel;              /* o: R > 0 */
     long *jnonrel;           /* o: N >= 0 */
     FILE *f;                 /* i */
{
  int i;

  double gv; /* gain value read from ideal file */
  double hgv = 0.0; /* highest gain value in ideal file to be returned */

  char *docid, *p;
  char line[ BUFSIZ + 1 ];
  char *pos;

  struct strdoublist **pp1, **pp2;

  struct strdoublist **addtostrdoublist();

  *jrel = 0;
  *jnonrel = 0;

  pp1 = jd;
  while( fgets( line, sizeof( line ), f ) ){
    if( ( pos = strchr( line, '\n' ) ) == NULL ){
#ifdef OUTERR   
      fprintf( stderr, "Line too long: %s\n", line );
#endif  
      return( -1 );
    }
    *pos = '\0';
	
    if( ( p = strtok( line, sep ) ) == NULL ){

#ifdef OUTERR
      fprintf( stderr, "strtok failed: %s\n", line );
#endif
      return( -1 );
    }
    docid = strdup( p );
    if( ( p = strtok( NULL, sep ) ) == NULL ){

#ifdef OUTERR
      fprintf( stderr, "strtok failed: %s\n", p );
#endif
      return( -1 );
    }

    gv = atof( p );
    if( gv < 0 ){

#ifdef OUTERR
      fprintf( stderr, "invalid gain in ideal list\n" );
#endif
      return( -1 );
    }
    else if( gv == 0.0 ){ /* judged nonrelevant */
      (*jnonrel)++;
    }
    else{ /* judged relevant */
      (*jrel)++;

      if( hgv < gv ){
	hgv = gv; /* hgv is kept maximum */
      }
    }

    if( ( pp2 = addtostrdoublist( pp1, docid, gv ) ) == NULL ){
	  
#ifdef OUTERR	  
      fprintf( stderr, "addtostrdoublist failed\n" );
#endif
      return( -1 );
    }
    pp1 = pp2;

  }

  return( hgv );
    
}/* of store_ideal */

/******************************************************************************
  store_syslist

read system output and
store docIDs and gain values, preserving the exact order
(but skip unjudged docs if ignore_unjudged==1)

Also return system length, and
the maximum gain in system list (for preferred rank)

If there are unjudged docs (i.e. ones without gain values),
zeroes are stored (if ignore_unjudged==0) or
they are skipped (if ignore_unjudged==1).

return value: 0 (OK)
             -1 (ERROR)
******************************************************************************/
int store_syslist( sd, sysl, sysgmax, f )
struct strdoublist **sd;
long *sysl;
double *sysgmax; 
FILE *f;
{
  long len;

  double gv;

  char *docid, *p;
  char line[ BUFSIZ + 1 ];
  char *pos;

  struct strdoublist **pp1, **pp2;

  struct strdoublist **addtostrdoublist();

  *sysl = 0;
  *sysgmax = 0.0;

  pp1 = sd;
  while( fgets( line, sizeof( line ), f ) ){
    if( ( pos = strchr( line, '\n' ) ) == NULL ){
#ifdef OUTERR   
      fprintf( stderr, "Line too long: %s\n", line );
#endif  
      return( -1 );
    }
    *pos = '\0';

    if( ( p = strtok( line, sep ) ) == NULL ){

#ifdef OUTERR
      fprintf( stderr, "strtok failed: %s\n", line );
#endif

      return( -1 );
    }
    docid = strdup( p );
    if( ( p = strtok( NULL, sep ) ) == NULL ){
      /* no gv -> unjudged doc */
      gv = 0.0;
    }
    else{ /* judged doc */
      gv = atof( p );
    }

    if( ( ignore_unjudged == 1 && p != NULL ) || /* judged doc */
	( ignore_unjudged == 0 ) ){

      /* Thus, if ignore_unjudged == 1 and the doc is unjudged,
	 it is ignored. */
	  
      /* add docID and gv to the bottom of the list */
      if( ( pp2 = addtostrdoublist( pp1, docid, gv ) ) == NULL ){

#ifdef OUTERR
	fprintf( stderr, "addtostrdoublist failed: %s\n", docid );
#endif
	return( -1 );
      }
      (*sysl)++;
      if( gv > *sysgmax ){
	*sysgmax = gv; /* max gain value within system list */
      }
      
      pp1 = pp2;
    }
    
  }

  return( 0 );

}/* of store_syslist */


/******************************************************************************
  eval_label

reads a relevance assessment file and a ranked list of documents;
outputs a labelled ranked list - one with relevance level tags.

Swallows the rel assessment file AS IS - 
whether the labels are illegal or not is not of its concern.

relevance assessment file format:
<docID> L0
<docID> L3
  :
(May contain empty lines or comments beginning with #. They will be ignored.)  

systemfile format:
<docID>
<docID>
  :

output format (default: judged and unjudged docs):
<docID> L3
<docID>
<docID> L0
  :

    or (-j judged docs only)
<docID> L3
<docID> L0
  :


April 11: if -e option is used,
looks for the third field in the rel file, which should 
represent an Equivalence Class (EC) ID.
  
relevance assessment file format:
<docID> L0 0
<docID> L3 1
<docID> L2 1

If -e is used, only one item per EC is marked as relevant.


NOTE: if -e and -j are to be used simultaneously,
each L0 doc in rel file should have a distinct EC ID.


return value: 0 (OK)
             -1 (NG)
******************************************************************************/
static int eval_label( int ac, char **av )
{
  FILE *fa = NULL;
  FILE *fs = stdin;

  int argc = 2;
  int match;

  long truncaterank = 0;
  /* by default, do not truncate */
  long i;

  char line[ BUFSIZ + 1 ];
  char *pos;
  int seen[ EC_NUMMAX ]; /* for ignoring redundant items from same EC */

  struct strstrlonglist *jdoclabEC = NULL; /* judged doc, label and EC ID */
  struct strstrlonglist *p;

  long file2strstrlonglist();
  long file2strstrcountlist();
  int freestrstrlonglist();

  ignore_unjudged = 0;
  /* by default, output all docs in system list (=judged + unjudged) */

  ec_mode = 0;
  /* by default, non-EC mode. Each relevant item consititues one EC. */

  while( argc < ac ){
    if( strcmp( av[ argc ], OPTSTR_IGNORE_UNJUDGED ) == 0 ){
      /* output judged docs only */
      ignore_unjudged = 1;
      argc++;
    }
    else if( strcmp( av[ argc ], OPTSTR_EC ) == 0 ){
      /* equivalence class mode */
      ec_mode = 1;
      argc++;
    }
    else if( strcmp( av[ argc ], OPTSTR_TRUNCATE ) == 0 && ( argc + 1 < ac ) ){
      truncaterank = atol( av[ argc + 1 ] );
      if( truncaterank < 1 ){

#ifdef OUTERR
        fprintf( stderr, "Bad %s value\n", OPTSTR_TRUNCATE );
#endif
        return( -1 );
      }
      argc += 2;
    }
    /* relevance assessment file */
    else if( strcmp( av[ argc ], OPTSTR_RELFILE ) == 0 && ( argc + 1 < ac ) ){
      if( ( fa = fopen( av[ argc + 1 ], "r" ) ) == NULL ){

#ifdef OUTERR
	fprintf( stderr, "Cannot open %s\n", av[ argc + 1 ] );
#endif
	return( -1 );
      }
      argc += 2;
    }
    else if( strcmp( av[ argc ], OPTSTR_SEP ) == 0 && ( argc + 1 < ac ) ){
      sep = strdup( av[ argc + 1 ] );
      argc += 2;
    }
    /* ranked list file */
    else{
      if( ( fs = fopen( av[ argc ], "r" ) ) == NULL ){
#ifdef OUTERR
	fprintf( stderr, "Cannot open %s\n", av[ argc ] );
#endif
	  return( -1 );
      }
      argc++;
    }
  }
    
  if( fa == NULL ){

#ifdef OUTERR      
    fprintf( stderr,
	     "Usage: %s %s [%s] [%s] [%s <separator>] [%s <rank>] %s <rel_assessment_file> [ranked list file]\n",
	     av[ 0 ], av[ 1 ],
	     OPTSTR_IGNORE_UNJUDGED, OPTSTR_EC, OPTSTR_SEP,
	     OPTSTR_TRUNCATE, OPTSTR_RELFILE );
#endif
    return( 0 );
  }

  if( sep == NULL ){
    sep = strdup( DEFAULT_SEP );
  }

  if( ec_mode ){ /* store relevant items with the EC IDs */

    if( file2strstrlonglist( fa, &jdoclabEC ) < 0 ){
#ifdef OUTERR
      fprintf( stderr, "file2strstrlonglist failed\n" );
#endif
      return( -1 );
    }
  }
  else{ /* non-EC mode: store relevant items, ignore EC info */

    if( file2strstrcountlist( fa, &jdoclabEC ) < 0 ){
#ifdef OUTERR
      fprintf( stderr, "file2strstrcountlist failed\n" );
#endif
      return( -1 );
    }
  }

  fclose( fa );

  /* initialise equivalence classes seen so far */
  if( ec_mode ){
    for( i = 0; i < EC_NUMMAX; i++ ){
      seen[ i ] = 0;
    }
  }

  /* output labelled ranked list */    

  i = 1; /* rank */
  while( fgets( line, sizeof( line ), fs ) ){
    if( ( pos = strchr( line, '\n' ) ) == NULL ){
#ifdef OUTERR   
      fprintf( stderr, "Line too long: %s\n", line );
#endif  
      return( -1 );
    }
    *pos = '\0';

    match = -1; /* -1: no match; 0: match but redundant; 1: match and new */
    p = jdoclabEC;
    while( p ){

      if( strcmp( p->string1, line ) == 0 ){ /* match */

	if( ec_mode ){

	  if( seen[ p->val - 1 ] == 0 ){ /* EC not previously seen */
	    printf( "%s%s%s%s%ld\n",
		    line, sep, p->string2, sep, p->val );
	    /* with rel label and EC ID */
	    match = 1;
	    seen[ p->val - 1 ] = 1; /* this class is now already seen */
	  }
	  else{ /* EC already seen */
	    printf( "%s\n", line ); /* regard as nonrelevant */
	    match = 0;
	  }
	  
	}
	else{ /* not ec_mode */
	  printf( "%s%s%s\n", line, sep, p->string2 ); /* with rel label */
	  match = 1;
	}

	break;

      }
      p = p->next;
    }

    if( match == -1 ){ /* unjudged */

      if( ignore_unjudged == 0 ){ /* not condensed-list mode */

	printf( "%s\n", line );
      }
    }

    if( i == truncaterank ){ /* if truncaterank is positive,
				truncate system output at this rank */
      break;
    }
    i++;
  }
  fclose( fs );

  free( sep );
  freestrstrlonglist( &jdoclabEC );
  return( 0 );

}/* of eval_label */


/******************************************************************************
  eval_compute

reads a relevance assessment file and a labelled ranked list;
outputs several different evaluation metric values.

The labels in the system output may be illegal so check.

If the labelled ranked list is a condensed list,
use the -j option to compute bpref.

relevance assessment file format:
<docID> L0
<docID> L3
  :

labelled systemfile format:
<docID> L0
<docID>
<docID> L2
 :

If -e option is used, the rel file must contain an EC ID in the third field:
<docID> L3 1
<docID> L1 1
<docID> L0 2
 :
If -e is used,
the number of ECs is used as the number of relevant items,
and the highest rel level within each EC is treated as the 
rel level of that EC.


NOTE: if -e and -j are to be used simultaneously,
each L0 doc in rel file should have a distinct EC ID.
Otherwise jnonrel would be 1.

20180909: now computes intentwise RBU (but gcompute does not 
since RBU is designed to be an IA-measure, not a D-measure).

******************************************************************************/
static int eval_compute( int ac, char **av )
{

  FILE *fa = NULL;
  FILE *fs = stdin;

  int argc = 2;
  int verbose = 0;

  int compute_gap = 0; /* computing GAP is more expensive
			than others so omit by default */

  long i, j;
  long rlevel;
  long rlevel2; /* for GAP */

  long jrelnum;
  long Xrelnum[ MAXRL_MAX + 1 ]; /* number of X-rel docs */
  long maxrl_system = 0; /* max rel level in system output: for rp */
  long maxrl = 0; /* max rel level obtained from gain values */
  long maxrl_stop = 0; /* max rel level obtained from stop values */

  long r1 = 0; /* rank of the first correct doc */
  long rp = 0; /* preferred rank for P-measure and P-plus */
  long syslen; /* length of system ranked list */
  long maxlen; /* max( syslen, jrelnum )
		  where jrelnum is the length of a
		  minimal ideal ranked output */
  long minlen; /* min(cutoff, syslen) */
  long cutoff_num = 0; /* 0 means cutoff was not specified by the user */
  long cutoff[ CUTOFF_NUMMAX ]; /* cutoff for prec, hit, nDCG... */

  double gv[ MAXRL_MAX + 1 ]; /* gain value for an X-relevant doc */
  double sv[ MAXRL_MAX + 1 ]; /* stop value for an X-relevant doc */
  double qbeta = DEFAULT_BETA;
  double gamma = DEFAULT_GAMMA;
  double logbase = DEFAULT_LOGB;

  double *CGi, *DCGi, *msDCGi;
/* (cumulative) gain, discounted (cumulative) gain, 
   Microsoft's discounted (cumulative) gain for the ideal ranked list */
  double *Gs, *CGs, *DCGs, *msDCGs;
/* ditto for system ranked list */

  double *count; /* treat is as a double rather than an integer */
  double *BR;    /* blended ratio */
  double *Ss;    /* stop value at a given rank for graded-uniform NCU */
  double RBPp = DEFAULT_RBP; /* RBP persistence parameter p */
  double *RBPpower;          /* p^(r-1) */
  double *RBPsum;            /* SUM g(r)p^(r-1) */
  double *ERR; /* expected reciprocal rank by Chapelle et al CIKM09 */
  double *ERRdsat; /* dissatisfaction prob at rank r for ERR */
  double *ERRi; /* ideal ERR for normalisation */
  double *ERRidsat;
  double *EBR; /* expected blended ratio, 2018 */
  double RBUp = 0; /* RBU p parameter: compute iRBU only if this is specified */
  double *iRBU;/* intentwise RBU, where RBU is from SIGIR18 */
  double *GAPsum; /* for computing Robertson GAP */
  double *GAPisum; /* for GAP@l */

  double ratio, ratio2, ratio3;
  double sum, sum2, sum3;
  double mini;
  double penalty;
  double denom;

  char *buf, *p;

  struct strstrlist *sysdoclab = NULL; /* labelled ranked list */
  struct strstrlist *doc;
  struct strstrlist *doc2; /* for GAP */

  int compute_usage();
  int freestrstrlist();
  long count_judged();
  long count_ECjudged();
  long file2strstrlist2();
  long lab2level();
  double orig_dcglog();

  ignore_unjudged = 0;
  /* by default, assume that system list contains unjudged docs.
     if ignore_unjudged == 1, assume that system list is a condensed list,
     so output bpref. */

  ec_mode = 0;
  /* by default, every relevant item is treated as one EC. */

  while( argc < ac ){

    if( strcmp( av[ argc ], OPTSTR_HELP ) == 0 ){
      return( compute_usage( ac, av ) );
    }
    else if( strcmp( av[ argc ], OPTSTR_VERBOSE ) == 0 ){
      verbose = 1;
      argc++;
    }
    else if( strcmp( av[ argc ], OPTSTR_GAP ) == 0 ){
      compute_gap = 1;
      argc++;
    }
    else if( strcmp( av[ argc ], OPTSTR_IGNORE_UNJUDGED ) == 0 ){
      ignore_unjudged = 1; /* input contains judged docs only
			      so output bref etc. */
      argc++;
    }
    else if( strcmp( av[ argc ], OPTSTR_EC ) == 0 ){
      ec_mode = 1; /* equivalence class mode */
      argc++;
    }
    else if( strcmp( av[ argc ], OPTSTR_OUTSTR ) == 0 && ( argc + 1 < ac ) ){
      outstr = strdup( av[ argc + 1 ] );
      argc += 2;
    }
    else if( strcmp( av[ argc ], OPTSTR_SEP ) == 0 && ( argc + 1 < ac ) ){
      sep = strdup( av[ argc + 1 ] );
      argc += 2;
    }
    else if( strcmp( av[ argc ], OPTSTR_RELFILE ) == 0 && ( argc + 1 < ac ) ){
      if( ( fa = fopen( av[ argc + 1 ], "r" ) ) == NULL ){

#ifdef OUTERR
	fprintf( stderr, "Cannot open %s\n", av[ argc + 1 ] );
#endif
	return( -1 );
      }

      argc += 2;
    }
    else if( strcmp( av[ argc ], OPTSTR_BETA ) == 0 && ( argc + 1 < ac ) ){
      qbeta = atof( av[ argc + 1 ] );
      argc += 2;
    }
    else if( strcmp( av[ argc ], OPTSTR_GAMMA ) == 0 && ( argc + 1 < ac ) ){
      gamma = atof( av[ argc + 1 ] );
      if( gamma > 1 ){

#ifdef OUTERR
	fprintf( stderr, "Bad %s value\n", OPTSTR_GAMMA );
#endif
	return( -1 );
      }

      argc += 2;
    }
    else if( strcmp( av[ argc ], OPTSTR_LOGB ) == 0 && ( argc + 1 < ac ) ){
      logbase = atof( av[ argc + 1 ] );
      argc += 2;
    }
    else if( strcmp( av[ argc ], OPTSTR_RBP ) == 0 && ( argc + 1 < ac ) ){
      RBPp = atof( av[ argc + 1 ] );
      if( RBPp > 1 ){

#ifdef OUTERR
	fprintf( stderr, "Bad %s value\n", OPTSTR_RBP );
#endif
	return( -1 );
      }

      argc += 2;
    }
    else if( strcmp( av[ argc ], OPTSTR_RBUP ) == 0 && ( argc + 1 < ac ) ){
      RBUp = atof( av[ argc + 1 ] );
      if( RBUp > 1 ){

#ifdef OUTERR
	fprintf( stderr, "Bad %s value\n", OPTSTR_RBUP );
#endif
	return( -1 );
      }

      /* compute iRBU only if RBUp (> 0) is specified */

      argc += 2;
    }
    else if( strcmp( av[ argc ], OPTSTR_CUTOFF ) == 0 && ( argc + 1 < ac ) ){

      buf = strdup( av[ argc + 1 ] );
      cutoff_num = 0;
      if( ( p = strtok( buf, "," ) ) == NULL ){

#ifdef OUTERR
	fprintf( stderr, "strtok failed\n" );
#endif
	return( -1 );
      }
      do{
	cutoff[ cutoff_num ] = atof( p );
	cutoff_num++;
	if( cutoff_num >= CUTOFF_NUMMAX ){

#ifdef OUTERR
	  fprintf( stderr, "Too many cutoffs\n" );
#endif
	  return( -1 );
	}

      }while( ( p = strtok( NULL, "," ) ) != NULL );
	
      free( buf );
      argc += 2;
    }
    /* user specified gain values (REQUIRED):
       input format: -g 1:2:3 least relevant first
       (maxrl is obtained from this) */
    else if( strcmp( av[ argc ], OPTSTR_GAIN ) == 0 && ( argc + 1 < ac ) ){

      buf = strdup( av[ argc + 1 ] );
      maxrl = 0;
      if( ( p = strtok( buf, RLEVEL_RLEVEL_SEP ) ) == NULL ){

#ifdef OUTERR
	fprintf( stderr, "strtok failed\n" );
#endif
	return( -1 );

      }
      do{
	maxrl++;
	if( maxrl > MAXRL_MAX ){

#ifdef OUTERR
	  fprintf( stderr, "maxrl overflow\n" );
#endif
	  return( -1 );

	}

	gv[ maxrl ] = atof( p );
		
      }while( ( p = strtok( NULL, RLEVEL_RLEVEL_SEP ) ) != NULL );

      free( buf );
      argc += 2;
	  
    }
    /* user specified stop values (NOT REQUIRED):
       input format: -s 1:2:3 least relevant first */
    else if( strcmp( av[ argc ], OPTSTR_STOP ) == 0 && ( argc + 1 < ac ) ){
      
      buf = strdup( av[ argc + 1 ] );
      maxrl_stop = 0;
      if( ( p = strtok( buf, RLEVEL_RLEVEL_SEP ) ) == NULL ){

#ifdef OUTERR	  
	fprintf( stderr, "strtok failed\n" );
#endif
	return( -1 );

      }

      do{
	maxrl_stop++;
	if( maxrl_stop > MAXRL_MAX ){

#ifdef OUTERR	    
	  fprintf( stderr, "maxrl_stop overflow\n" );
#endif
	  return( -1 );

	}
	sv[ maxrl_stop ] = atof( p );

	if( sv[ maxrl_stop ] == 0.0 ){

#ifdef OUTERR
	  fprintf( stderr, "stop value must be positive\n" );
#endif
	  return( -1 );
	}
	  
      }while( ( p = strtok( NULL, RLEVEL_RLEVEL_SEP ) ) != NULL );

      free( buf );
      argc += 2;
      
    }
    /* labelled system file */
    else{
      if( ( fs = fopen( av[ argc ], "r" ) ) == NULL ){

#ifdef OUTERR
	fprintf( stderr, "Cannot open %s\n", av[ argc ] );
#endif
	return( -1 );

      }
      argc++;
    }
  }

  if( fa == NULL || maxrl == 0 ){ /* rel file and gain values are required */
    return( compute_usage( ac, av ) );
  }

  if( outstr == NULL ){
    outstr = strdup( "" );
  }
  if( sep == NULL ){
    sep = strdup( DEFAULT_SEP );
  }


  /* L0-relevant (judged nonrel) docs */
  gv[ 0 ] = 0;
  sv[ 0 ] = 0;

  if( maxrl_stop == 0 ){ /* stop values not specified -> copy gain values */
    
    for( i = 1; i <= maxrl; i++ ){
      sv[ i ] = gv[ i ];
    }

  }
  else if( maxrl_stop != maxrl ){ /* number of rel levels do not match */

#ifdef OUTERR
    fprintf( stderr, "gain/stop rel levels mismatch\n" );
#endif
    return( -1 );

  }

  /* so now maxrl_stop == maxrl */

  /* check that gain/stop values for lower relevance levels are lower */
  for( i = 1; i <= maxrl; i++ ){
    for( j = i + 1; j <= maxrl; j++ ){
      if( gv[ i ] > gv[ j ] || sv[ i ] > sv[ j ] ){

#ifdef OUTERR
	fprintf( stderr, "Bad gain/stop values\n" );
#endif
	return( -1 );

      }
    }
  }

  if( verbose ){
    for( i = 0; i <= maxrl; i++ ){
      printf( "# gain[ %ld ]=%f stop[ %ld ]=%f\n", i, gv[ i ], i, sv[ i ] );
    }
  }

  /* if cutoff is not specified, use default */
  if( cutoff_num == 0 ){
    cutoff_num = 1;
    cutoff[ 0 ] = DEFAULT_CUTOFF;
  }

  /* set Xrelnum[], jrelnum and jnonrelnum */
  if( ec_mode ){

    if( ( jrelnum = count_ECjudged( maxrl, Xrelnum, fa ) ) < 0 ){

#ifdef OUTERR
      fprintf( stderr, "count_ECjudged failed\n" );
#endif
      return( -1 );
    }

  }
  else{ /* not ec_mode */

    if( ( jrelnum = count_judged( maxrl, Xrelnum, fa ) ) < 0 ){

#ifdef OUTERR
      fprintf( stderr, "count_judged failed\n" );
#endif
      return( -1 );
    }
  }

  fclose( fa );

  if( jrelnum == 0 ){

#ifdef OUTERR
    fprintf( stderr, "no rel doc\n" );
#endif
    return( -1 );
  }

  if( ( ignore_unjudged == 1 ) && ( Xrelnum[ 0 ] == 0 ) ){

#ifdef OUTERR
    fprintf( stderr,
             "no judged nonrel: bpref etc. not computable\n" );
#endif
    return( -1 );
  }

  /* store system output with labels */
  if( ( syslen = file2strstrlist2( fs, &sysdoclab ) ) < 0 ){

#ifdef OUTERR
    fprintf( stderr, "file2strstrlist2 failed\n" );
#endif
    return( -1 );
  }

  fclose( fs );

  printf( "%s # syslen=%ld jrel=%ld jnonrel=%ld\n",
	  outstr, syslen, jrelnum, Xrelnum[ 0 ] );

  /* maxlen = max( jrelnum, syslen ) */
  maxlen = jrelnum;
  if( maxlen < syslen ){
    maxlen = syslen;
  }

  CGi = ( double *)calloc( maxlen + 1, sizeof( double ) );
  DCGi = ( double *)calloc( maxlen + 1, sizeof( double ) );
  msDCGi = ( double *)calloc( maxlen + 1, sizeof( double ) );

  Gs = ( double *)calloc( maxlen + 1, sizeof( double ) );
  CGs = ( double *)calloc( maxlen + 1, sizeof( double ) );
  DCGs = ( double *)calloc( maxlen + 1, sizeof( double ) );
  msDCGs = ( double *)calloc( maxlen + 1, sizeof( double ) );

  count = ( double *)calloc( maxlen + 1, sizeof( double ) );
  BR = ( double *)calloc( maxlen + 1, sizeof( double ) );
  Ss = ( double *)calloc( maxlen + 1, sizeof( double ) );

  RBPpower = ( double *)calloc( maxlen + 1, sizeof( double ) );
  RBPsum = ( double *)calloc( maxlen + 1, sizeof( double ) );

  ERR = ( double *)calloc( maxlen + 1, sizeof( double ) );
  ERRdsat = ( double *)calloc( maxlen + 1, sizeof( double ) );
  ERRi = ( double *)calloc( maxlen + 1, sizeof( double ) );
  ERRidsat = ( double *)calloc( maxlen + 1, sizeof( double ) );

  /* 2018 EBR */
  EBR = ( double *)calloc( maxlen + 1, sizeof( double ) );

  /* intentwise RBU */
  if( RBUp > 0 ){ /* RBUp has been specified */
    iRBU = ( double *)calloc( maxlen + 1, sizeof( double ) );
  }

  if( compute_gap ){
    GAPsum = ( double *)calloc( maxlen + 1, sizeof( double ) );
    GAPisum = ( double *)calloc( maxlen + 1, sizeof( double ) );
  }

  /* Store ideal cumulative gain etc. */

  ERRi[ 0 ] = 0;
  ERRidsat[ 0 ] = 1;

  /* koko */
  
  i = 1;
  for( rlevel = maxrl; rlevel >= 1; rlevel-- ){
    
    for( j = 1;  j <= Xrelnum[ rlevel ]; j++ ){

      CGi[ i ] = CGi[ i - 1 ] + gv[ rlevel ];
      DCGi[ i ] = DCGi[ i - 1 ] + gv[ rlevel ]/orig_dcglog( logbase, i );
      /* For MSnDCG, the logbase is irrelevant */
      msDCGi[ i ] = msDCGi[ i - 1 ] + gv[ rlevel ]/log( i + 1 );

      if( compute_gap ){
	GAPisum[ i ] = GAPisum[ i - 1 ] + rlevel * ( rlevel + 1 );
      }

      ERRi[ i ] = ERRi[ i - 1 ] + ( ERRidsat[ i - 1 ]/i ) * ( gv[ rlevel ]/( gv[ maxrl ]+1 ) );
      ERRidsat[ i ] = ERRidsat[ i - 1 ] * ( 1 - gv[ rlevel ]/( gv[ maxrl ]+1 ) );

      i++;
    }
  }
  for( i = jrelnum + 1; i <= maxlen; i++ ){ /* if ideal size < system size */
    CGi[ i ] = CGi[ jrelnum ];
    DCGi[ i ] = DCGi[ jrelnum ];
    msDCGi[ i ] = msDCGi[ jrelnum ];

    if( compute_gap ){
      GAPisum[ i ] = GAPisum[ jrelnum ];
    }

    ERRi[ i ] = ERRi[ jrelnum ];

  }

  if( verbose ){
    printf( "\n" );
    for( i = 1; i <= maxlen; i++ ){
      printf( "#%ld CGi=%.4f DCGi=%.4f msDCGi=%.4f\n",
	      i, CGi[ i ], DCGi[ i ], msDCGi[ i ] );
    }
  }

  /* store system cumulative gain, etc. */

  RBPpower[ 1 ] = 1;
  RBPsum[ 0 ] = 0;
  ERR[ 0 ] = 0;
  ERRdsat[ 0 ] = 1;
  EBR[ 0 ] = 0; /* 2018 */
  
  if( RBUp > 0 )
    iRBU[ 0 ] = 0; /* intentwise RBU */

  doc = sysdoclab;
  
  for( i = 1; i<= syslen; i++ ){

    if( strcmp( doc->string2, "" ) != 0 ){ /* judged doc */

      if( ( rlevel = lab2level( doc->string2 ) ) < 0 ){
#ifdef OUTERR
	fprintf( stderr, "lab2level failed for %s\n", doc->string2 );
#endif
	return( -1 );
	
      }

      if( rlevel > 0 ){ /* relevant doc */

	count[ i ] = count[ i - 1 ] + 1;
	Gs[ i ] = gv[ rlevel ];
	Ss[ i ] = sv[ rlevel ];
	CGs[ i ] = CGs[ i - 1 ] + gv[ rlevel ];
	DCGs[ i ] = DCGs[ i - 1 ] + gv[ rlevel ]/orig_dcglog( logbase, i );
	/* For MSnDCG, the logbase is irrelevant */
	msDCGs[ i ] = msDCGs[ i - 1 ] + gv[ rlevel ]/log( i + 1 );

	if( compute_gap ){ /* do rather expensive GAP computation */

	  doc2 = sysdoclab;
	  GAPsum[ i ] = 0.0;
	  for( j = 1; j <= i; j++ ){

	    if( strcmp( doc2->string2, "" ) != 0 ){ /* judged doc */

	      if( ( rlevel2 = lab2level( doc2->string2 ) ) < 0 ){
#ifdef OUTERR
		fprintf( stderr, "lab2level failed for %s\n", doc2->string2 );
#endif
		return( -1 );
	      }
	    }
	    else{ /* doc at rank j is an unjudged doc */
	      rlevel2 = 0;
	    }

	    /* min(rl,rl2) * (min(rl,rl2)+1) */
	    /* note: compare relevance levels, not the gain values */
	    if( rlevel < rlevel2 ){
	      GAPsum[ i ] += rlevel * (rlevel + 1);
	    }
	    else{
	      GAPsum[ i ] += rlevel2 * (rlevel2 + 1);
	    }

	    doc2 = doc2->next;
	  }
	  
	}

	if( count[ i ] == 1 ){ /* first relevant document */
	  r1 = i; /* for RR and O-measure */
	}

	if( maxrl_system < rlevel ){ /* highest relevance level so far? */
	  maxrl_system = rlevel;
	  rp = i; /* preferred rank for P-measure and P-plus */
	}
      }
      else{ /* judged nonrel doc */

	count[ i ] = count[ i - 1 ];
	CGs[ i ] = CGs[ i - 1 ];
	DCGs[ i ] = DCGs[ i - 1 ];
	msDCGs[ i ] = msDCGs[ i - 1 ];
	
      }

    }
    else{ /* unjudged doc */

      count[ i ] = count[ i - 1 ];
      CGs[ i ] = CGs[ i - 1 ];
      DCGs[ i ] = DCGs[ i - 1 ];
      msDCGs[ i ] = msDCGs[ i - 1 ];
    }

    /* blended ratio */
    BR[ i ] = ( qbeta * CGs[ i ] + count[ i ] )/( qbeta * CGi[ i ] + i );

    if( i > 1 ){
      RBPpower[ i ] = RBPp * RBPpower[ i - 1 ];
    }
    RBPsum[ i ] = RBPsum[ i - 1 ] + Gs[ i ] * RBPpower[ i ];

    /* divide gain by maxgain+1 rather than maxgain to make sure
       rel prob < 1 even for a highly rel doc.
       Otherwise, all docs after the first highly rel doc
       will be ignored.
       Note that, because of this, ERR in as defined here
       and in CIKM09 paper Eq.4 does NOT reduce to RR even in the
       binary relevance case.
    */
    ERR[ i ] = ERR[ i - 1 ] + ( ERRdsat[ i - 1 ]/i ) * ( Gs[ i ]/( gv[ maxrl ]+1 ) );
    ERRdsat[ i ] = ERRdsat[ i - 1 ] * ( 1 - Gs[ i ]/( gv[ maxrl ]+1 ) );

    /* 2018 */
    EBR[ i ] = EBR[ i - 1 ] + BR[ i ]*ERRdsat[ i - 1 ]*Gs[ i ]/( gv[ maxrl ]+1 );

    if( RBUp > 0 ){ /* if RBUp has been specified */
      iRBU[ i ] = iRBU[ i - 1 ] +
	pow( RBUp, i ) * ERRdsat[ i - 1 ]*Gs[ i ]/( gv[ maxrl ]+1 );
    }
	
    doc = doc->next;
  }

  freestrstrlist( &sysdoclab );

  for( i = syslen + 1; i <= maxlen; i++ ){ /* if system size < ideal size */

    count[ i ] = count[ syslen ];
    CGs[ i ] = CGs[ syslen ];
    DCGs[ i ] = DCGs[ syslen ];
    msDCGs[ i ] = msDCGs[ syslen ];
    ERR[ i ] = ERR[ syslen ];
    /* 2018 */
    EBR[ i ] = EBR[ syslen ];
    if( RBUp > 0 ){
      iRBU[ i ] = iRBU[ syslen ];
    }
    RBPsum[ i ] = RBPsum[ syslen ]; /* RBP@l 2018 */

    /* no need to compute BR beyond syslen */

  }

  if( verbose ){
    printf( "\n" );
    for( i = 1; i <= syslen; i++ ){
      if( Gs[ i ] > 0 ){
	printf( "#%ld count=%ld Gs=%.4f CGs=%.4f DCGs=%.4f msDCGs=%.4f BR=%.4f\n",
		i, (long)count[ i ], Gs[ i ], CGs[ i ], DCGs[ i ], msDCGs[ i ], BR[ i ] );
      }
    }
    printf( "\n" );
  }

  printf( "%s # r1=%ld rp=%ld\n", outstr, r1, rp );


  /***** output evaluation metric values *****/

  /* RR, O-measure, P-measure, P-plus */
  if( r1 == 0 ){ /* no relevant doc in system output */
    printf( "%s RR=                  %.4f\n", outstr, (double)0 );
    printf( "%s O-measure=           %.4f\n", outstr, (double)0 );
    printf( "%s P-measure=           %.4f\n", outstr, (double)0 );
    printf( "%s P-plus=              %.4f\n", outstr, (double)0 );
  }
  else{ /* at least one rel doc in system output */
    printf( "%s RR=                  %.4f\n", outstr, (double)1/r1 );
    printf( "%s O-measure=           %.4f\n", outstr, BR[ r1 ] );
    printf( "%s P-measure=           %.4f\n", outstr, BR[ rp ] );
    sum = 0.0;
    for( i = 1; i <= rp; i++ ){
      if( Gs[ i ] > 0 ){
	sum += BR[ i ];
      }
    }
    printf( "%s P-plus=              %.4f\n", outstr, sum/count[ rp ] );
  }

  /* AP and Q */ 
  sum = 0.0; sum2 = 0.0; 
  for( i = 1; i <= syslen; i++ ){
    if( Gs[ i ] > 0 ){
      sum += count[ i ]/i;
      sum2 += BR[ i ];
    }
  }
  printf( "%s AP=                  %.4f\n", outstr, sum/jrelnum );
  printf( "%s Q-measure=           %.4f\n", outstr, sum2/jrelnum );
    
  /* NCU measures */

  /* graded-uniform NCU */

  denom = 0.0;
  for( i = 1; i <= maxrl; i++ ){
    denom += Xrelnum[ i ] * sv[ i ];
  }

  sum = 0.0; sum2 = 0.0;
  for( i = 1; i <= syslen; i++ ){
    if( Gs[ i ] > 0 ){
      sum += ( count[ i ]/i ) * Ss[ i ];
      sum2 += BR[ i ] * Ss[ i ];
    }
  }
  printf( "%s NCUgu,P=             %.4f\n", outstr, sum/denom );
  printf( "%s NCUgu,BR=            %.4f\n", outstr, sum2/denom );

  /* rank-biased NCU */

  denom = 0.0;
  for( i = 1; i <= jrelnum; i++ ){
    denom += pow( gamma, i-1 );
  }

  sum = 0.0; sum2 = 0.0;
  for( i = 1; i <= syslen; i++ ){
    if( Gs[ i ] > 0 ){
      sum += ( count[ i ]/i ) * pow( gamma, count[ i ]-1 );
      sum2 += BR[ i ] * pow( gamma, count[ i ]-1 );
    }
  }
  printf( "%s NCUrb,P=             %.4f\n", outstr, sum/denom );
  printf( "%s NCUrb,BR=            %.4f\n", outstr, sum2/denom );

  /* GAP */
  if( compute_gap ){

    sum = 0.0;
    for( i = 1; i <= syslen; i++ ){
      sum += GAPsum[ i ]/i;
    }
    printf( "%s GAP=                 %.4f\n",
	    outstr, sum/GAPisum[ jrelnum ] );
  }

  /* RBP */
  printf( "%s RBP=                 %.4f\n",
	  outstr,(1-RBPp)*RBPsum[ syslen ]/gv[ maxrl ] );
  /* ERR */
  printf( "%s ERR=                 %.4f\n", outstr, ERR[ syslen ] );
  /* 2018 EBR */
  printf( "%s EBR=                 %.4f\n", outstr, EBR[ syslen ] );

  /* cutoff-based metrics */
  for( i = 0; i < cutoff_num; i++ ){

    /* cutoff-based Q */ 
    minlen = cutoff[ i ];
    if( minlen > syslen ){
      minlen = syslen;
    }/* minlen = min(cutoff, syslen) */
    
    sum = 0.0; sum2 = 0.0;
    for( j = 1; j <= minlen; j++ ){
      if( Gs[ j ] > 0 ){
        sum += count[ j ]/j;
        sum2 += BR[ j ];
      }
    }

    /* divide by min(cutoff, jrelnum ) */
    if( cutoff[ i ] < jrelnum ){
      printf( "%s AP@%04ld=             %.4f\n",
              outstr, cutoff[ i ], sum/cutoff[ i ] );
      printf( "%s Q@%04ld=              %.4f\n",
              outstr, cutoff[ i ], sum2/cutoff[ i ] );
    }
    else{ 
      printf( "%s AP@%04ld=             %.4f\n",
              outstr, cutoff[ i ], sum/jrelnum );
      printf( "%s Q@%04ld=              %.4f\n",
              outstr, cutoff[ i ], sum2/jrelnum );
    }

    if( compute_gap ){

      sum = 0.0;
      for( j = 1; j <= minlen; j++ ){
	sum += GAPsum[ j ]/j;
      }

      if( cutoff[ i ] < jrelnum ){
	printf( "%s GAP@%04ld=            %.4f\n",
		outstr, cutoff[ i ], sum/GAPisum[ cutoff[ i ] ] );
      }
      else{
	printf( "%s GAP@%04ld=            %.4f\n",
		outstr, cutoff[ i ], sum/GAPisum[ jrelnum ] );
      }
      
    }

    if( cutoff[ i ] <= maxlen ){
      
      printf( "%s nDCG@%04ld=           %.4f\n",
	      outstr, cutoff[ i ], DCGs[ cutoff[ i ] ]/DCGi[ cutoff[ i ] ]);
      printf( "%s MSnDCG@%04ld=         %.4f\n",
	      outstr, cutoff[ i ], msDCGs[ cutoff[ i ] ]/msDCGi[ cutoff[ i ] ]);
      printf( "%s P@%04ld=              %.4f\n",
	      outstr, cutoff[ i ], count[ cutoff[ i ] ]/cutoff[ i ] );
      printf( "%s RBP@%04ld=            %.4f\n",
	      outstr, cutoff[ i ], (1-RBPp)*RBPsum[ cutoff[ i ] ]/gv[ maxrl ] );
      printf( "%s ERR@%04ld=            %.4f\n",
	      outstr, cutoff[ i ], ERR[ cutoff[ i ] ] );
      printf( "%s nERR@%04ld=           %.4f\n",
	      outstr, cutoff[ i ], ERR[ cutoff[ i ] ]/ERRi[ cutoff[ i ] ]);
      /* 2018 */
      printf( "%s EBR@%04ld=            %.4f\n",
	      outstr, cutoff[ i ], EBR[ cutoff[ i ] ] );

      /* In version 180312, ERR@l was included in gcompute but NOT in compute. */
      /* 20180909 RBP@l */

      /* 20180909 intentwise iRBP */
      if( RBUp > 0 ){
	printf( "%s iRBU@%04ld=           %.4f\n",
		outstr, cutoff[ i ], iRBU[ cutoff[ i ] ] );
      }

      if( (long)count[ cutoff[ i ] ] > 0 ){
	printf( "%s Hit@%04ld=            %.4f\n",
		outstr, cutoff[ i ], (double)1 );
      }
      else{
	printf( "%s Hit@%04ld=            %.4f\n",
		outstr, cutoff[ i ], (double)0 );
      }
      
    }
    else{ /* cutoff exceeds maxlen */
      
      printf( "%s nDCG@%04ld=           %.4f\n",
	      outstr, cutoff[ i ], DCGs[ maxlen ]/DCGi[ maxlen ]);
      printf( "%s MSnDCG@%04ld=         %.4f\n",
	      outstr, cutoff[ i ], msDCGs[ maxlen ]/msDCGi[ maxlen ]);
      printf( "%s P@%04ld=              %.4f\n",
	      outstr, cutoff[ i ], count[ maxlen ]/cutoff[ i ] );
      printf( "%s RBP@%04ld=            %.4f\n",
	      outstr, cutoff[ i ], (1-RBPp)*RBPsum[ maxlen ]/gv[ maxrl ] );
      printf( "%s ERR@%04ld=            %.4f\n",
	      outstr, cutoff[ i ], ERR[ maxlen ] );
      printf( "%s nERR@%04ld=           %.4f\n",
	      outstr, cutoff[ i ], ERR[ maxlen ]/ERRi[ maxlen ]);
      /* 2018 */
      printf( "%s EBR@%04ld=            %.4f\n",
	      outstr, cutoff[ i ], EBR[ maxlen ] );

      /* In version 180312, ERR@l was included in gcompute but NOT in compute. */
      /* 20180909 RBP@l */

      /* 20180909 intentwise iRBP */

      if( RBUp > 0 ){
	printf( "%s iRBU@%04ld=           %.4f\n",
		outstr, cutoff[ i ], iRBU[ maxlen ] );
      }

      if( (long)count[ maxlen ] > 0 ){
	printf( "%s Hit@%04ld=            %.4f\n",
		outstr, cutoff[ i ], (double)1 );
      }
      else{
	printf( "%s Hit@%04ld=            %.4f\n",
		outstr, cutoff[ i ], (double)0 );
      }
	
    }
  }

  if( ignore_unjudged ){ /* compute bpref etc */
    
    /* bpref and bpref_R */
    sum = 0.0; sum2 = 0.0;
    for( i = 1; i <= syslen; i++ ){

      if( Gs[ i ] > 0 ){

	mini = i - count[ i ];
	if( jrelnum < mini ){ mini = jrelnum; };
	/* min(R, r'-count(r')) */

	/* bpref */
	if( jrelnum > Xrelnum[ 0 ] ){
	  ratio = 1 - mini/Xrelnum[ 0 ];
	}
	else{ /* jrelnum <= jnonrelnum */
	  ratio = 1 - mini/jrelnum;
	}
	/* 1- min(R, r'-count(r')/min(R,N) */
	sum += ratio;

	/* bpref_R */
	ratio2 = 1 - mini/jrelnum;
	/* 1- min(R, r'-count(r')/R */
	sum2 += ratio2;
      }

    }
    printf( "%s bpref=               %.4f\n", outstr, sum/jrelnum );
    printf( "%s bpref_R=             %.4f\n", outstr, sum2/jrelnum );

    /* bpref_N and bpref_relative */
    sum = 0.0; sum2 = 0.0;
    for( i = 1; i <= syslen; i++ ){

      if( Gs[ i ] > 0 ){

	/* bpref_N */
	ratio = 1 - (double)(i-count[ i ])/Xrelnum[ 0 ];
	sum += ratio;

	/* bpref_relative */
	if( i > 1 ){
	  ratio2 = 1 - (double)(i-count[ i ])/(i-1);
	}
	else{ /* ignore relevant doc at Rank 1 */
	  ratio2 = 0.0;
	}
	sum2 += ratio2;
      }
    }
    printf( "%s bpref_N=             %.4f\n", outstr, sum/jrelnum );
    printf( "%s bpref_relative=      %.4f\n", outstr, sum2/jrelnum );

    /* rpref_N, rpref_relative and rpref_relative2 */
    sum = 0.0; sum2 = 0.0; sum3 = 0.0;
    for( i = 1; i <= syslen; i++ ){

      if( Gs[ i ] > 0 ){

	/* rpref_N */
	penalty = 0.0; /* penalty for rank i: examine Ranks 1-(i-1) */
	for( j = 1; j < i; j++ ){
	  if( Gs[ i ] - Gs[ j ] > 0 ){
	    penalty += (Gs[ i ]-Gs[ j ])/Gs[ i ];
	  }
	}
	ratio = 1 - penalty/( jrelnum + Xrelnum[ 0 ] - CGi[ jrelnum ]/gv[ maxrl ] );
	sum += Gs[ i ] * ratio;

	/* rpref_relative */
	if( i > 1 ){
	  ratio2 = 1 - penalty/(i-1);
	}
	else{  /* ignore relevant doc at Rank 1 */
	  ratio2 = 0.0;
	}
	sum2 += Gs[ i ] * ratio2;

	/* rpref_relative2 */
	ratio3 = 1 - penalty/i;
	sum3 += Gs[ i ] * ratio3;

      }
    }

    /* note: CGi can be used instead of cgi here */
    printf( "%s rpref_N=             %.4f\n", outstr, sum/CGi[ jrelnum ] );
    printf( "%s rpref_relative=      %.4f\n", outstr, sum2/CGi[ jrelnum ] );
    printf( "%s rpref_relative2=     %.4f\n", outstr, sum3/CGi[ jrelnum ] );
  }

  /* end */

  free( outstr );
  free( sep );

  free( CGi );
  free( DCGi );
  free( msDCGi );

  free( Gs );
  free( CGs );
  free( DCGs );
  free( msDCGs );

  free( count );
  free( BR );
  free( Ss );

  free( RBPpower );
  free( RBPsum );

  free( ERR );
  free( ERRdsat );
  free( ERRi );
  free( ERRidsat );

  free( EBR );

/* intentwise RBP*/
  if( RBUp > 0 ){
      free( iRBU);
  }

  if( compute_gap ){

    free( GAPsum );
    free( GAPisum );
    
  }

  return( 0 );

}/* of eval_compute */

/******************************************************************************
  eval_1click

- read gold standard nug file with nug weights and vital strlen
- read system output (matched nuggets with offsets)
    1st line contains "syslen= <syslen>"
- compute S-measure, weighted recall etc.
******************************************************************************/
static int eval_1click( int ac, char **av )
{
  FILE *fa = NULL;
  FILE *fs = stdin;

  int argc = 2;
  int verbose = 0;

  long Lparam = 0; /* for S-measure */
  long nugnum = 0; /* number of nuggets */
  long matchednum = 0; /* number of matched nuggets */
  long offset;
  long cumvlen = 0;
  /* vital strlen cumulated for computing S-measure denominator */
  long vlen;
  long summatchvlen = 0; /* T-measure numerator */
  long Xlen = -1; /* T-measure denominator = system output len
		      not yet read (1st line of output) */

  double nugw;
  double wrec_nume = 0.0;
  double wrec_denom = 0.0;
  double Smeasure_nume = 0.0;
  double Smeasure_denom = 0.0;
  double fflatbeta = DEFAULT_FFLATBETA;
  double Sflat, Tflat;

  char line[ BUFSIZ + 1 ];
  char *pos;
  char *p;
  char *buf;

  struct strdoublonglist *sortnug = NULL;
  /* sorted nuggets */
  struct strdoublonglist *q;

  int freestrdoublonglist();
  long file2sortnug();
  double nugget2weightvlen();

  while( argc < ac ){

    if( strcmp( av[ argc ], OPTSTR_1CLICK_L ) == 0 && ( argc + 1 < ac ) ){

      Lparam =atol( av[ argc + 1 ] );
      /* sanity check is done later */
      argc += 2;
    }
    else if( strcmp( av[ argc ], OPTSTR_VERBOSE ) == 0 ){
      verbose = 1;
      argc++;
    }
    else if( strcmp( av[ argc ], OPTSTR_OUTSTR ) == 0 && ( argc + 1 < ac ) ){
      outstr = strdup( av[ argc + 1 ] );
      argc += 2;
    }
    else if( strcmp( av[ argc ], OPTSTR_SEP ) == 0 && ( argc + 1 < ac ) ){
      sep = strdup( av[ argc + 1 ] );
      argc += 2;
    }
    else if( strcmp( av[ argc ], OPTSTR_BETA ) == 0 && ( argc + 1 < ac ) ){
      /* for F-flat */
      fflatbeta = atof( av[ argc + 1 ] );
      argc += 2;
    }
    else if( strcmp( av[ argc ], OPTSTR_1CLICK_N ) == 0 && ( argc + 1 < ac ) ){

      if( ( fa = fopen( av[ argc + 1 ], "r" ) ) == NULL ){
#ifdef OUTERR
        fprintf( stderr, "Cannot open %s\n", av[ argc + 1 ] );
#endif
        return( -1 );
      }
      argc += 2;
    }
    else{ /* system output, that is, matched nugget file */

      if( ( fs = fopen( av[ argc ], "r" ) ) == NULL ){
#ifdef OUTERR
        fprintf( stderr, "Cannot open %s\n", av[ argc ] );
#endif
          return( -1 );
      }
      
      argc++;
    }
  }

  if( Lparam < 1 || fa == NULL ){

#ifdef OUTERR
    fprintf( stderr,
	     "Usage: %s %s [%s] [%s <separator>] [%s <outstr>] [%s <outstr>] %s <Lparameter> %s <nuggetfile> [matched nugget file]\n",
	     av[ 0 ], av[ 1 ],
	     OPTSTR_VERBOSE, OPTSTR_SEP,
	     OPTSTR_OUTSTR, OPTSTR_BETA,
	     OPTSTR_1CLICK_L, OPTSTR_1CLICK_N );
    
#endif

    return( 0 );
    
  }

  if( outstr == NULL ){
    outstr = strdup( "" );
  }
  if( sep == NULL ){
    sep = strdup( DEFAULT_SEP );
  }

  /* store and sort gold standard nuggets with weights and vital lens*/


  if( ( nugnum = file2sortnug( fa, &sortnug ) ) < 1 ){

#ifdef OUTERR
    fprintf( stderr, "file2sortnug failed\n" );
#endif
    return( -1 );

  }
  fclose( fa );

  /* compute denominators for S-measure etc. */
  q = sortnug;
  while( q ){ /* scan sorted nuggets, i.e. pseudo minimal output */

    wrec_denom += q->val1; /* add nugget weights for weighted recall */

    cumvlen += q->val2; /* vital strlen cumulated */

    if( verbose ){
      printf( "#%s w= %f vl= %ld offset*= %ld\n",
	      q->string, q->val1, q->val2, cumvlen );
    }
    
    if( cumvlen < Lparam ){
      Smeasure_denom += q->val1 * (Lparam - cumvlen);
    }
    
    q = q->next;
  }

  if( verbose ){
    printf( "#wrec_denom= %f S-measure_denom= %f\n",
	    wrec_denom, Smeasure_denom );
  }

  /* read system output (matched nuggets) and compute numerators for
  S-measure etc */
  
  while( fgets( line, sizeof( line ), fs ) ){

    /* input file format: <nugID> <offset> 
       except the first line: Xlen= <Xlen> */

    if( ( pos = strchr( line, '\n' ) ) == NULL ){

#ifdef OUTERR   
      fprintf( stderr, "Line too long: %s\n", line );
#endif  
      return( -1 );
    }
    *pos = '\0';

    if( ( p = strtok( line, sep ) ) == NULL ){

#ifdef OUTERR   
      fprintf( stderr, "strtok failed\n" );
#endif  
      return( -1 );
      
    }
    buf = p; /* nuggetID */

    if( ( p = strtok( NULL, sep ) ) == NULL ){

#ifdef OUTERR   
      fprintf( stderr, "strtok failed\n" );
#endif  
      return( -1 );
    }


    if( Xlen < 0 ){ /* first line of file */

      Xlen = atol( p ); /* denominator of T-measure */
    }
    else{ /* matched nugget line */

      offset = atol( p );
      if( ( nugw = nugget2weightvlen( sortnug, buf, &vlen ) ) < 0 ){
	/* lookup failed */
      
#ifdef OUTERR   
	fprintf( stderr, "nugget2weightvlen failed: %s\n", buf );
#endif  
	return( -1 );
      }

      matchednum++; /* count nugget matches for unweighted recall */
      wrec_nume += nugw; /* add weights of nugget matches for weighted recall */
      summatchvlen += vlen; /* T-measure numerator */

      if( offset < Lparam ){ /* add to S-measure only if within L */

	Smeasure_nume += nugw * (Lparam - offset);
      }

      if( verbose ){
	printf( "#matched=%s w=%f offset=%ld\n", buf, nugw, offset );
      }
    }
    
  }/* of while fgets */

  if( Xlen < 0 ){ /* handle empty file */

    Xlen = 0;

  }

  /* output metrics */

  printf( "%s #nuggets= %ld #matched= %ld #syslen= %ld\n",
	  outstr, nugnum, matchednum, Xlen );
  printf( "%s Recall=              %.3f\n", outstr,
	  (double)matchednum/nugnum );
  printf( "%s W-recall=            %.3f\n", outstr,
	  wrec_nume/wrec_denom );
  printf( "%s S-measure=           %.3f\n", outstr,
	  Smeasure_nume/Smeasure_denom );

  
  if( Smeasure_nume > Smeasure_denom ){
    Sflat = 1.0;
  }
  else{ /* Sflat = S-measure */
    Sflat = Smeasure_nume/Smeasure_denom;
  }
  printf( "%s S-flat=              %.3f\n", outstr, Sflat );


  if( Xlen == 0 ){
    printf( "%s T-measure=           %.3f\n", outstr, (double)0 );
    Tflat = 0.0;
  }
  else{
    
    printf( "%s T-measure=           %.3f\n", outstr,
	    (double)summatchvlen/Xlen );

    if( summatchvlen > Xlen ){
      Tflat = 1.0;
    }
    else{
      Tflat = (double)summatchvlen/Xlen;
    }
  }
  printf( "%s T-flat=              %.3f\n", outstr, Tflat );


  if( Sflat > 0.0 || Tflat > 0.0 ){
    printf( "%s F-flat=              %.3f\n", outstr,
	    (1+fflatbeta*fflatbeta)*Tflat*Sflat/(fflatbeta*fflatbeta*Tflat+Sflat) );
    
  }
  else{
    printf( "%s F-flat=              %.3f\n", outstr, (double)0 );
    
  }

  free( outstr );
  free( sep );
  freestrdoublonglist( &sortnug );

  return( 0 );

  
}/* of eval_1click */

/******************************************************************************
  file2sortnug

reads nug file (nuggetID, weight, vitalstrlen)
and stores them, after sorting them by
(1) weight (bigger the better)
(2) vitalstrlen (shorter the bettter).

nug file format:
<nuggetID> <nugget weight> <vital strlen> [...]

return value: number of nuggets found >0 (OK)
              0 (empty nugget file)
             -1 (ERROR)
******************************************************************************/
long file2sortnug( f, list )
FILE *f;
struct strdoublonglist **list;
{
  long retv = 0;

  char line[ BUFSIZ + 1 ];
  char *p;
  char *pos;
  char *buf;
  double weight;
  long vlen;

  int addto_sortnug();

  while( fgets( line, sizeof( line ), f ) ){

    if( ( pos = strchr( line, '\n' ) ) == NULL ){
#ifdef OUTERR   
      fprintf( stderr, "Line too long: %s\n", line );
#endif  
      return( -1 );
    }
    *pos = '\0';

    if( ( p = strtok( line, sep ) ) == NULL ){

#ifdef OUTERR   
      fprintf( stderr, "strtok failed\n" );
#endif  
      return( -1 );
    }
    buf = strdup( p );

    if( ( p = strtok( NULL, sep ) ) == NULL ){

#ifdef OUTERR   
      fprintf( stderr, "strtok failed\n" );
#endif  
      return( -1 );
    }
    weight = atof( p );

    if( ( p = strtok( NULL, sep ) ) == NULL ){

#ifdef OUTERR   
      fprintf( stderr, "strtok failed\n" );
#endif  
      return( -1 );
    }
    vlen = atol( p );

    /* the remaining fields, if any, are igored */

    if( addto_sortnug( list, buf, weight, vlen ) < 0 ){

#ifdef OUTERR   
      fprintf( stderr, "addto_sortnug failed for %s\n", buf );
#endif  
      return( -1 );
    }

    retv++;
    
  }/* of while fgets */

  return( retv );
  
}/* of file2sortnug */

/******************************************************************************
  addto_sortnug

add a nugget record to list sorted by 
nugget weight (bigger the better) and vital strlen (shorter the better)

Note that this module does not check nuggetID duplicates.

return value: 0 (added successfully)
             -1 (ERROR)
******************************************************************************/
int addto_sortnug( p, s, weight, vlen )
     struct strdoublonglist **p;
     char *s;
     double weight;
     long vlen;
{
  struct strdoublonglist *p1, *p2;

  if( s == NULL || weight <= 0.0 || vlen <=0 )
    return( -1 );

  p1 = *p;

  if( ( p1 == NULL ) || /* start of list */
      ( p1->val1 < weight ) ||
      /* new nugget weight bigger than any existing nuggets */
      ( p1->val1 == weight && p1->val2 > vlen ) ){
    /* new nugget weight equal to the highest existing nuggets AND
       new vital strlen smaller or equal */

    if( ( p2 = ( struct strdoublonglist * )
	  malloc( sizeof( struct strdoublonglist ) ) ) == NULL ){
#ifdef OUTERR   
      fprintf( stderr, "malloc failed: %s\n", s );
#endif  
      return( -1 );
    }
    p2->string = s;
    p2->val1 = weight;
    p2->val2 = vlen;
    p2->next = p1;
    *p = p2;
    return( 0 ); /* added a new record */
  }

  while( p1 ){

    if( ( p1->next == NULL ) || /* end of list */
	( ( p1->next )->val1 < weight ) ||
	( ( p1->next )->val1 == weight && ( p1->next )->val2 > vlen ) ){
      
      if( ( p2 = ( struct strdoublonglist * )
	    malloc( sizeof( struct strdoublonglist ) ) ) == NULL ){
#ifdef OUTERR   
	fprintf( stderr, "malloc failed: %s\n", s );
#endif  
	return( -1 );
      }
      p2->string = s;
      p2->val1 = weight;
      p2->val2 = vlen;
      p2->next = p1->next;
      p1->next = p2;
      return( 1 ); /* added a new record */

    }
    
    p1 = p1->next;
  }

  return( -1 );
  
}/* of addto_sortnug */

/******************************************************************************
  nugget2weight

looks up a nugget and returns its nugget weight

return value: nugget weight >=0 (OK)
             -1 (ERROR)
******************************************************************************/
double nugget2weight( p, nugid )
     struct strdoublonglist *p;
     const char *nugid;
{
  struct strdoublonglist *q;

  q = p;
  while( q ){

    if( strcmp( q->string, nugid ) == 0 ){ /* found */

      return( q->val1 );
    }

    q = q->next;
  }

  return( -1 );
  
}/* of nugget2weight */

/******************************************************************************
  nugget2weightvlen

looks up a nugget and returns its nugget weight AND vital strlen

return value: nugget weight >=0 (OK)
             -1 (ERROR)
******************************************************************************/
double nugget2weightvlen( p, nugid, len )
     struct strdoublonglist *p;
     const char *nugid;
     long *len;
{
  struct strdoublonglist *q;

  q = p;
  while( q ){

    if( strcmp( q->string, nugid ) == 0 ){ /* found */

      *len = q->val2 ;

      return( q->val1 );
    }

    q = q->next;
  }

  return( -1 );
  
}/* of nugget2weightvlen */

/******************************************************************************
  count_ECjudged

read erel file and count
- number of judged X-rel classes (including 0-rel=judged nonrel)
- total number of judged rel classes (return value)

a class is X-relevant iff the highest relevance level
within the class is X.

e.g.
aaa L3 1
bbb L1 1

Then the relevance level for Class 1 is L3.

return value: number of judged rel classes >=0 (OK)
             -1 (ERROR)
******************************************************************************/
long count_ECjudged( maxrl, Xrel, f )
     long maxrl;
     long Xrel[];/* number of X-rel docs */
     FILE *f; /* rel assessment file */
{
  long i;
  long num; /* ec num */
  long jrel; /* return value */

  long highest[ EC_NUMMAX ];
  /* highest rel level for each equiv class.
     Class 1 <==> highest[ 0 ], Class 2 <==> highest[ 1 ]... */

  char *p;
  char *pos;
  char line[ BUFSIZ + 1 ];

  long lab2level();

  for( i = 0; i <= EC_NUMMAX; i++ ){
    highest[ i ] = 0;
  }


  while( fgets( line, sizeof( line ), f ) ){

    if( ( pos = strchr( line, '\n' ) ) == NULL ){
#ifdef OUTERR   
      fprintf( stderr, "Line too long: %s\n", line );
#endif  
      return( -1 );
    }

    *pos = '\0';

    if( ( p = strtok( line, sep ) ) == NULL ){ /* string: not used */

#ifdef OUTERR
      fprintf( stderr, "strtok failed: %s\n", line );
#endif
      return( -1 );

    }
    
    if( ( p = strtok( NULL, sep ) ) == NULL ){ /* rel label */

#ifdef OUTERR
      fprintf( stderr, "strtok failed\n" );
#endif
      return( -1 );

    }

    if( ( i = lab2level( p ) ) < 0 ){
#ifdef OUTERR
      fprintf( stderr, "lab2level failed\n" );
#endif
      return( -1 );

    }

    if( i > maxrl ){ /* relfile contains rellevel larger than declared */
#ifdef OUTERR
      fprintf( stderr, "max rel level mismatch: %s\n", p );
#endif
      return( -1 );

    }

    if( ( p = strtok( NULL, sep ) ) == NULL ){ /* equiv class num */

#ifdef OUTERR
      fprintf( stderr, "strtok failed\n" );
#endif
      return( -1 );

    }

    num = atol( p );

    if( num < 1 || num > EC_NUMMAX ){
#ifdef OUTERR
      fprintf( stderr, "bad ec number: %ld\n", num );
#endif
      return( -1 );
    }

    if( highest[ num - 1 ] < i ){
      highest[ num - 1 ] = i;
      /* highest rel level seen so far for Class num */
    }

  }

  
  for( i = 0; i <= maxrl; i++ ){
    Xrel[ i ] = 0; 
  }
  jrel = 0;

  for( i = 0; i < EC_NUMMAX; i++ ){

    if( highest[ i ] > 0 ){ /* Class i+1 exists and
			       its rel level is highest[i](>0) */
      Xrel[ highest[ i ] ]++;
      jrel++;
    }

  }

  return( jrel );
    
  
}/* count_ECjudged */

/******************************************************************************
  count_judged

read rel assessment file and count
- number of judged X-rel docs (including 0-rel=judged nonrel)
- total number of judged rel docs (return value)

return value: number of judged rel docs >=0 (OK)
             -1 (ERROR)
******************************************************************************/
long count_judged( maxrl, Xrel, f )
     long maxrl;
     long Xrel[];/* number of X-rel docs */
     FILE *f; /* rel assessment file */
{
  long i;
  long jrel; /* return value */

  char *p;
  char *pos;
  char line[ BUFSIZ + 1 ];

  long lab2level();

  for( i = 0; i <= maxrl; i++ ){
    Xrel[ i ] = 0; 
  }

  while( fgets( line, sizeof( line ), f ) ){

    if( ( pos = strchr( line, '\n' ) ) == NULL ){
#ifdef OUTERR   
      fprintf( stderr, "Line too long: %s\n", line );
#endif  
      return( -1 );
    }

    *pos = '\0';

    if( ( p = strtok( line, sep ) ) == NULL ){

#ifdef OUTERR
      fprintf( stderr, "strtok failed: %s\n", line );
#endif
      return( -1 );

    }
    
    if( ( p = strtok( NULL, sep ) ) == NULL ){

#ifdef OUTERR
      fprintf( stderr, "strtok failed\n" );
#endif
      return( -1 );

    }

    if( ( i = lab2level( p ) ) < 0 ){
#ifdef OUTERR
      fprintf( stderr, "lab2level failed\n" );
#endif
      return( -1 );

    }

    if( i > maxrl ){ /* relfile contains rellevel larger than declared */
#ifdef OUTERR
      fprintf( stderr, "max rel level mismatch: %s\n", p );
#endif
      return( -1 );

    }

    Xrel[ i ]++;

  }

  jrel = 0;
  for( i = 1; i <= maxrl; i++ ){ /* do NOT include i == 0 */

    jrel += Xrel[ i ];

  }

  return( jrel );
    
  
}/* count_judged */


/******************************************************************************
  lab2level

relevance label to relevance level
checks that maxrl is not exceeded
expects labels of the form "L<level>"

return value: relevance level 0>=(OK)
             -1 (ERROR)
******************************************************************************/
long lab2level( lab )
const char *lab;
{
  long retv;

  if( lab[ 0 ] != 'L' ){
#ifdef OUTERR
  fprintf( stderr, "Invalid rel level: %s\n", lab );
#endif
  return( -1 );
  }

  retv = atol( &lab[ 1 ] );

  if( retv > MAXRL_MAX ){
#ifdef OUTERR
  fprintf( stderr, "Invalid rel level: %s\n", lab );
#endif
  return( -1 );    
  }

  return( retv );
  
}/* lab2level */

/******************************************************************************
  orig_dcglog

This is for computing the ORIGINAL dcg [Jarvelin/Kekalainen TOIS02].

if( i == 1 ) return 1 (i.e. no discounting)  

if( i < b ) return 1 (i.e. no discounting)
else (Discounting is applied only if i >= b. )
  
 if( b>0 )  
  return either logb(i)
 else
  return either ln(i) (if b is set to zero, use natural log)

return value: logvalue (>=1)
             -1 (ERROR)
******************************************************************************/
double orig_dcglog( b, i )
     double b;  /* log base (if zero, use natural log) */
     long i;    /* rank */
{
  double retv;

  if( i < 1 || b < 0 ){ /* bad parameters */
    return( -1 );
  }

  if( i == 1 || i <= b )
    return( 1.0 ); /* discounting is not applied */

  /* i > b */

  if( b == 0 ){ /* use natural log */
    return( log( i ) );
  }

  /* b > 0 */
  return( log( i )/log( b ) );

}/* of orig_dcglog */

/******************************************************************************
  gcompute_usage

print how to use compute

return value: 0 (OK)
******************************************************************************/
int gcompute_usage( ac, av )
     int ac;
     char **av;
{

#ifdef OUTERR  
  fprintf( stderr,
	   "Usage: %s %s %s <idealfile> [OPTIONS] [labelled ranked list]\n",
	   av[ 0 ], av[ 1 ],
	   OPTSTR_IDEALFILE );
  fprintf( stderr, "OPTIONS:\n" );
  fprintf( stderr, "\t%s: show this message\n", OPTSTR_HELP );
  fprintf( stderr, "\t%s: verbose output\n", OPTSTR_VERBOSE );
  fprintf( stderr,
	   "\t%s: treat the input as a condensed list (unjudged docs removed),\n",
	   OPTSTR_IGNORE_UNJUDGED );
  fprintf( stderr, "\t    and compute condensed-list metrics including bpref\n");
  fprintf( stderr, "\t%s <separator>: input/output field separator (default: '%s').\n",
	   OPTSTR_SEP, DEFAULT_SEP );
  fprintf( stderr, "\t%s <string>: prefix string for each output line.\n",
	   OPTSTR_OUTSTR );
  fprintf( stderr, "\t    Including a topicID here would be useful.\n" );
  fprintf( stderr, "\t%s <positive value>: Q-measure's beta (default: %.2f)\n",
	   OPTSTR_BETA, DEFAULT_BETA );
  fprintf( stderr,
	   "\t%s <positive value <=1 >: gamma for rank-biased NCU (default: %.2f)\n",
	   OPTSTR_GAMMA, DEFAULT_GAMMA );
  fprintf( stderr, "\t%s <value >=0 >: log base for DCG (default: %.2f)\n",
	   OPTSTR_LOGB, DEFAULT_LOGB );
  fprintf( stderr, "\t    If you want natural log, set this value to zero.\n" );
  fprintf( stderr, "\t%s <positive value <=1 >: persistence for RBP (default: %.2f)\n",
	   OPTSTR_RBP, DEFAULT_RBP );
  fprintf( stderr,
	   "\t%s <document rank[,document rank,...]>: cutoffs for P@n, Hit@n, nDCG@n... (default: %d)\n",
	   OPTSTR_CUTOFF, DEFAULT_CUTOFF );
  fprintf( stderr, "\t%s: compute Robertson/Kanoulas/Yilmaz GAP\n", OPTSTR_GAP );
#endif

  return( 0 );
  
}/* of gcompute usage */

/******************************************************************************
  compute_usage

print how to use compute

return value: 0 (OK)
******************************************************************************/
int compute_usage( ac, av )
     int ac;
     char **av;
{

#ifdef OUTERR  
  fprintf( stderr,
	   "Usage: %s %s %s <relfile> %s <gainL1:gainL2...> [OPTIONS] [labelled ranked list]\n",
	   av[ 0 ], av[ 1 ], OPTSTR_RELFILE, OPTSTR_GAIN );
  fprintf( stderr, "OPTIONS:\n" );
  fprintf( stderr, "\t%s: show this message\n", OPTSTR_HELP );
  fprintf( stderr, "\t%s: verbose output\n", OPTSTR_VERBOSE );
  fprintf( stderr,
	   "\t%s: treat the input as a condensed list (unjudged docs removed),\n",
	   OPTSTR_IGNORE_UNJUDGED );
  fprintf( stderr, "\t    and compute condensed-list metrics including bpref\n");
  fprintf( stderr, "\t%s <separator>: input/output field separator (default: '%s').\n",
           OPTSTR_SEP, DEFAULT_SEP );
  fprintf( stderr, "\t%s <string>: prefix string for each output line.\n",
	   OPTSTR_OUTSTR );
  fprintf( stderr, "\t    Including a topicID here would be useful.\n" );
  fprintf( stderr, "\t%s <positive value>: Q-measure's beta (default: %.2f)\n",
	   OPTSTR_BETA, DEFAULT_BETA );
  fprintf( stderr,
	   "\t%s <positive value <=1 >: gamma for rank-biased NCU (default: %.2f)\n",
	   OPTSTR_GAMMA, DEFAULT_GAMMA );
  fprintf( stderr, "\t%s <value >=0 >: log base for DCG (default: %.2f)\n",
	   OPTSTR_LOGB, DEFAULT_LOGB );
  fprintf( stderr, "\t    If you want natural log, set this value to zero.\n" );
  fprintf( stderr, "\t%s <positive value <=1 >: persistence for RBP (default: %.2f)\n",
	   OPTSTR_RBP, DEFAULT_RBP );
  fprintf( stderr,
	   "\t%s <document rank[,document rank,...]>: cutoffs for P@n, Hit@n, nDCG@n... (default: %d)\n",
	   OPTSTR_CUTOFF, DEFAULT_CUTOFF );
  fprintf( stderr,
	   "\t%s <stopL1:stopL2...>: stop values for graded-uniform NCU (default: same as gain values)\n", OPTSTR_STOP );
  fprintf( stderr, "\t%s: compute Robertson/Kanoulas/Yilmaz GAP\n", OPTSTR_GAP );
  fprintf( stderr, "\t%s <positive value <=1 >: compute intentwise RBU\n", OPTSTR_RBUP );
#endif

  return( 0 );
  
}/* of compute usage */

/******************************************************************************
 freestrdoublist

return value: 0 (OK)
******************************************************************************/
int freestrdoublist( p )
     struct strdoublist **p;
{
  struct strdoublist *p1, *p2;

  p1 = *p;
  while( p1 ){

    p2 = p1;
    p1 = p1->next;
    free( p2->string );
    free( p2 );

  }

  *p = NULL;
  return( 0 );
  
}/* of freestrlintlist */

/******************************************************************************
 addtostrdoublist

add <str,double> to end of list

return value: <lastrecord>->next
              NULL (error)
******************************************************************************/
struct strdoublist **addtostrdoublist( p, s, val )
     struct strdoublist **p;
     char *s; /* must be allocated outside */
     double val;
{
  struct strdoublist *q;

  if( s == NULL )
    return( NULL );

  if( ( q = ( struct strdoublist * )
	malloc( sizeof( struct strdoublist ) ) ) == NULL ){

#ifdef OUTERR
    fprintf( stderr, "malloc failed\n" );
#endif

    return( NULL );
  }

  q->string = s;
  q->val = val;
  q->next = NULL;
  *p = q;
  return( &( q->next ) );
  
}/* of addtostrdoublist */

/******************************************************************************
 addtouniqstrdoublist

while maintaining a sorted list of unique records <str,int>,
add a new record or 
update the number of existing record.

return value: 1 (added a new record)
              0 (record already existed so updated int only
             -1 (error)
******************************************************************************/
int addtouniqstrdoublist( p, s, val )
     struct strdoublist **p;
     char *s; /* must be allocated outside */
     double val;
{
  struct strdoublist *p1, *p2;

  p1 = *p;

  if( s == NULL )
    return( -1 );

  if( ( p1 == NULL ) || /* first record */
      ( strcmp( p1->string, s ) > 0 ) ){ /* insert at top */

    if( ( p2 = ( struct strdoublist * )
	  malloc( sizeof( struct strdoublist ) ) ) == NULL ){

#ifdef OUTERR
      fprintf( stderr, "malloc failed\n" );
#endif

      return( -1 );
    }
    p2->string = s;
    p2->val = val;
    p2->next = p1;
    *p = p2;
    return( 1 ); /* added a new record */
  }

  while( p1 ){

    if( strcmp( p1->string, s ) == 0 ){ /* record already exists */
      p1->val = val;
      return( 0 ); /* update val and return successfully, for now. */
    }

    if( ( ( p1->next ) == NULL ) || /* add at bottom */
	( strcmp( ( p1->next )->string, s ) > 0 ) ){ /* insert in middle */

      if( ( p2 = ( struct strdoublist * )
	    malloc( sizeof( struct strdoublist ) ) ) == NULL ){

#ifdef OUTERR
	fprintf( stderr, "malloc failed\n" );
#endif

	return( -1 );
      }
      p2->string = s;
      p2->val = val;
      p2->next = p1->next;
      p1->next = p2;
      return( 1 ); /* added a new record */
    }

    p1 = p1->next;
  }

  return( -1 ); /* should not reach this line */
  
}/* of addtouniqstrdoublist */

/******************************************************************************
  firstfield2strlist

read first field of file and addtostrlist

return value: size of strlist (OK)
             -1 (ERROR)
******************************************************************************/
long firstfield2strlist( f, list )
     FILE *f;
     struct strlist **list;
{
  long retv = 0;

  char line[ BUFSIZ + 1 ];
  char *p, *pos, *buf;

  struct strlist **pp1, **pp2;

  struct strlist **addtostrlist();

  pp1 = list;
  while( fgets( line, sizeof( line ), f ) ){

    if( ( pos = strchr( line, '\n' ) ) == NULL ){
#ifdef OUTERR   
      fprintf( stderr, "Line too long: %s\n", line );
#endif  
      return( -1 );
    }
    *pos = '\0';

    if( ( p = strtok( line, sep ) ) == NULL ){
      /* get only the first field in file */

#ifdef OUTERR   
      fprintf( stderr, "strtok failed\n" );
#endif  
      return( -1 );
    }

    buf = strdup( p );

    if( ( pp2 = addtostrlist( pp1, buf ) ) == NULL ){
      
#ifdef OUTERR   
      fprintf( stderr, "addtostrlist failed for %s\n", buf );
#endif  
      return( -1 );
    }
    retv++;
    pp1 = pp2;

  }

  return( retv );
  
  
}/* of firstfield2strlist */

/******************************************************************************
 addtostrlist

add <str> to end of list

return value: <lastrecord>->next
              NULL (error)
******************************************************************************/
struct strlist **addtostrlist( p, s )
     struct strlist **p;
     char *s;
{
  struct strlist *q;

  if( s == NULL )
    return( NULL );

  if( ( q = ( struct strlist * ) malloc( sizeof( struct strlist ) ) ) == NULL ){

#ifdef OUTERR
    fprintf( stderr, "malloc failed: %s\n", s );
#endif

    return( NULL );
  }

  q->string = s;
  q->next = NULL;
  *p = q;
  return( &( q->next ) );
  
}/* of addtostrlist */

/******************************************************************************
  freestrdoublong2list

return value: 0 (OK)
******************************************************************************/
int freestrdoublong2list( p )
     struct strdoublong2list **p;
{
  struct strdoublong2list *p1, *p2;

  p1 = *p;
  while( p1 ){
    p2 = p1;
    p1 = p1->next;
    free( p2->string );
    free( p2 );
  }

  *p = NULL;
  return( 0 );
    
}/* of freestrdoublong2list */

/******************************************************************************
  freestrdoublonglist

return value: 0 (OK)
******************************************************************************/
int freestrdoublonglist( p )
     struct strdoublonglist **p;
{
  struct strdoublonglist *p1, *p2;

  p1 = *p;
  while( p1 ){
    p2 = p1;
    p1 = p1->next;
    free( p2->string );
    free( p2 );
  }

  *p = NULL;
  return( 0 );
    
}/* of freestrdoublonglist */

/******************************************************************************
  freestrlist

return value: 0 (OK)
******************************************************************************/
int freestrlist( p )
     struct strlist **p;
{
  struct strlist *p1, *p2;

  p1 = *p;
  while( p1 ){
    p2 = p1;
    p1 = p1->next;
    free( p2->string );
    free( p2 );
  }

  *p = NULL;
  return( 0 );
    
}/* of freestrlist */

/******************************************************************************
  strlistmatch

return value: 1 (match)
              0 (no match)
******************************************************************************/
int strlistmatch( list, str )
     struct strlist *list;
     const char *str;
{
  struct strlist *p;

  p = list;
  while( p ){
    if( !strcmp( p->string, str ) )
      return( 1 );
    p = p->next;
  }
    
  return( 0 );

}/* of strlistmatch */


/******************************************************************************
  fprintstrlist
return value: 0 (OK)  
******************************************************************************/
int fprintstrlist( fp, p )
     FILE *fp;
     struct strlist *p;
{
  struct strlist *p1;

  p1 = p ;

  while( p1 ){
    fprintf( fp, "%s\n", p1->string );
    p1 = p1->next;
  }
  
  return( 0 );

}/* of fprintstrlist */

/******************************************************************************
  store_din

read din file and store it in a strdoublong2list

din file format:
<intnum> <inf/nav> <docID> <P(i|q)*gi>
 val2     val3      str     val1
(long)   (long)            (double)

return value: highest intent number in din (OK)
              0 (ERROR)
******************************************************************************/
long store_din( f, list )
     FILE *f;
     struct strdoublong2list **list;
{
  long retv = 0;
  long intnum, infnav;
  double intgain;

  char line[ BUFSIZ + 1 ];
  char *p, *pos, *buf;

  struct strdoublong2list **pp1, **pp2;

  struct strdoublong2list **addtostrdoublong2list();

  pp1 = list;
  while( fgets( line, sizeof( line ), f ) ){

    if( ( pos = strchr( line, '\n' ) ) == NULL ){
#ifdef OUTERR   
      fprintf( stderr, "Line too long: %s\n", line );
#endif  
      return( -1 );
    }
    *pos = '\0';

    if( ( p = strtok( line, sep ) ) == NULL ){

#ifdef OUTERR   
      fprintf( stderr, "strtok failed\n" );
#endif  
      return( -1 );
    }
    intnum = atol( p );
    if( intnum > retv ){

      retv = intnum; /* max intent number in din */

      if( retv > INTENT_NUMMAX ){

#ifdef OUTERR   
	fprintf( stderr, "INTENT NUMMAX overflow\n" );
#endif  
      return( -1 );
      }
    }

    if( ( p = strtok( NULL, sep ) ) == NULL ){

#ifdef OUTERR   
      fprintf( stderr, "strtok failed\n" );
#endif  
      return( -1 );
    }
    if( !strcmp( p, INFSTR ) ){
      infnav = INFORMATIONAL;
    }
    else if( !strcmp( p, NAVSTR ) ){
      infnav = NAVIGATIONAL;
    }
    else{
#ifdef OUTERR   
      fprintf( stderr, "bad inf/nav: %s\n", p );
#endif  
      return( -1 );
    }

    if( ( p = strtok( NULL, sep ) ) == NULL ){

#ifdef OUTERR   
      fprintf( stderr, "strtok failed\n" );
#endif  
      return( -1 );
    }
    buf = strdup( p ); /* docno */

    if( ( p = strtok( NULL, sep ) ) == NULL ){
      
#ifdef OUTERR   
      fprintf( stderr, "strtok failed\n" );
#endif  
      return( -1 );
    }
    intgain = atof( p );

    if( ( pp2 =
	  addtostrdoublong2list( pp1, buf, intgain, intnum, infnav ) )
	== NULL ){
      
#ifdef OUTERR   
      fprintf( stderr, "addtostrdoublong2list failed for %s\n", buf );
#endif  
      return( -1 );
    }

    pp1 = pp2;

  }

  return( retv );
  
  
}/* of store_din */

/******************************************************************************
  file2strstrcountlist

same as file2strstrlong list except that
instead of reading an EC ID from the file,
it increments a counter for each new relevant item
and stores it as the EC ID.

read first TWO fields of file and addtostrstrlonglist
First two fields are REQUIRED

<string> <rlabel> [<equivnum>]

Other fields are ignored.

return value: size of strstrlonglist (OK)
             -1 (ERROR)
******************************************************************************/
long file2strstrcountlist( f, list )
     FILE *f;
     struct strstrlonglist **list;
{
  long retv = 0;

  char line[ BUFSIZ + 1 ];
  char *p, *pos, *buf, *buf2;

  struct strstrlonglist **pp1, **pp2;

  struct strstrlonglist **addtostrstrlonglist();

  pp1 = list;
  while( fgets( line, sizeof( line ), f ) ){

    if( ( pos = strchr( line, '\n' ) ) == NULL ){
#ifdef OUTERR   
      fprintf( stderr, "Line too long: %s\n", line );
#endif  
      return( -1 );
    }
    *pos = '\0';

    if( ( p = strtok( line, sep ) ) == NULL ){

#ifdef OUTERR   
      fprintf( stderr, "strtok failed\n" );
#endif  
      return( -1 );
    }
    buf = strdup( p ); /* string */

    if( ( p = strtok( NULL, sep ) ) == NULL ){

#ifdef OUTERR   
      fprintf( stderr, "strtok failed\n" );
#endif  
      return( -1 );
    }
    buf2 = strdup( p ); /* rlabel */

    retv++; /* each relevant item consitute its own EC */

    if( ( pp2 =
	  addtostrstrlonglist( pp1, buf, buf2, retv ) ) == NULL ){
      
#ifdef OUTERR   
      fprintf( stderr, "addtostrstrlonglist failed for %s,%s\n", buf, buf2 );
#endif  
      return( -1 );
    }

    pp1 = pp2;

  }

  return( retv );
  
  
}/* of file2strstrcountlist */

/******************************************************************************
  file2strstrlonglist

read first three fields of file and addtostrstrlonglist
First three fields are REQUIRED

<string> <rlabel> <ec_num>

NOTE: if ec_num > EC_NUMMAX, return ERROR

return value: size of strstrlonglist (OK)
             -1 (ERROR)
******************************************************************************/
long file2strstrlonglist( f, list )
     FILE *f;
     struct strstrlonglist **list;
{
  long retv = 0;
  long num; /* ec num */

  char line[ BUFSIZ + 1 ];
  char *p, *pos, *buf, *buf2;

  struct strstrlonglist **pp1, **pp2;

  struct strstrlonglist **addtostrstrlonglist();

  pp1 = list;
  while( fgets( line, sizeof( line ), f ) ){

    if( ( pos = strchr( line, '\n' ) ) == NULL ){
#ifdef OUTERR   
      fprintf( stderr, "Line too long: %s\n", line );
#endif  
      return( -1 );
    }
    *pos = '\0';

    if( ( p = strtok( line, sep ) ) == NULL ){

#ifdef OUTERR   
      fprintf( stderr, "strtok failed\n" );
#endif  
      return( -1 );
    }
    buf = strdup( p ); /* string */

    if( ( p = strtok( NULL, sep ) ) == NULL ){

#ifdef OUTERR   
      fprintf( stderr, "strtok failed\n" );
#endif  
      return( -1 );
    }
    buf2 = strdup( p ); /* rlabel */

    if( ( p = strtok( NULL, sep ) ) == NULL ){

#ifdef OUTERR   
      fprintf( stderr, "strtok failed\n" );
#endif  
      return( -1 );
    }

    num = atol( p );
    if( num < 1 || num >= EC_NUMMAX ){
#ifdef OUTERR
      fprintf( stderr, "bad ec number: %ld\n", num );
#endif
      return( -1 );
    }

    if( ( pp2 =
	  addtostrstrlonglist( pp1, buf, buf2, num ) ) == NULL ){
      
#ifdef OUTERR   
      fprintf( stderr, "addtostrstrlonglist failed for %s,%s\n", buf, buf2 );
#endif  
      return( -1 );
    }

    retv++;
    pp1 = pp2;

  }

  return( retv );
  
  
}/* of file2strstrlonglist */

/******************************************************************************
  file2strstrlist

read first two fields of file and addtostrstrlist
First two fields are REQUIRED

return value: size of strstrlist (OK)
             -1 (ERROR)
******************************************************************************/
long file2strstrlist( f, list )
     FILE *f;
     struct strstrlist **list;
{
  long retv = 0;

  char line[ BUFSIZ + 1 ];
  char *p, *pos, *buf, *buf2;

  struct strstrlist **pp1, **pp2;

  struct strstrlist **addtostrstrlist();

  pp1 = list;
  while( fgets( line, sizeof( line ), f ) ){

    if( ( pos = strchr( line, '\n' ) ) == NULL ){
#ifdef OUTERR   
      fprintf( stderr, "Line too long: %s\n", line );
#endif  
      return( -1 );
    }
    *pos = '\0';

    if( ( p = strtok( line, sep ) ) == NULL ){

#ifdef OUTERR   
      fprintf( stderr, "strtok failed\n" );
#endif  
      return( -1 );
    }
    buf = strdup( p );

    if( ( p = strtok( NULL, sep ) ) == NULL ){

#ifdef OUTERR   
      fprintf( stderr, "strtok failed\n" );
#endif  
      return( -1 );
    }
    buf2 = strdup( p );

    if( ( pp2 = addtostrstrlist( pp1, buf, buf2 ) ) == NULL ){
      
#ifdef OUTERR   
      fprintf( stderr, "addtostrstrlist failed for %s,%s\n", buf, buf2 );
#endif  
      return( -1 );
    }
    retv++;
    pp1 = pp2;

  }

  return( retv );
  
  
}/* of file2strstrlist */

/******************************************************************************
  file2strstrlonglist2

read first three fields of file and addtostrstrlonglist
same as file2strstrlonglist,
except that it allows empty second/third fields.
In this case, "" is stored for the second field.

return value: size of strstrlonglist (OK)
             -1 (ERROR)
******************************************************************************/
long file2strstrlonglist2( f, list )
     FILE *f;
     struct strstrlonglist **list;
{
  long retv = 0;
  long num;

  char line[ BUFSIZ + 1 ];
  char *p, *pos, *buf, *buf2;

  struct strstrlonglist **pp1, **pp2;

  struct strstrlonglist **addtostrstrlonglist();

  pp1 = list;
  while( fgets( line, sizeof( line ), f ) ){

    if( ( pos = strchr( line, '\n' ) ) == NULL ){
#ifdef OUTERR   
      fprintf( stderr, "Line too long: %s\n", line );
#endif  
      return( -1 );
    }
    *pos = '\0';

    if( ( p = strtok( line, sep ) ) == NULL ){

#ifdef OUTERR   
      fprintf( stderr, "strtok failed\n" );
#endif  
      return( -1 );
    }
    buf = strdup( p );

    if( ( p = strtok( NULL, sep ) ) == NULL ){ /* 2nd field missing */

      buf2 = strdup( "" );
      num = 0;
    }
    else{ /* found 2nd field */

      buf2 = strdup( p );

      if( ( p = strtok( NULL, sep ) ) == NULL ){ /* 3rd field missing */

	/* rel label found but equiv class not found */
#ifdef OUTERR   
	fprintf( stderr, "strtok failed\n" );
#endif  
      return( -1 );
	
      }
      else{ /* found 3rd field */
	num = atol( p );
      }

    }


    if( ( pp2 = addtostrstrlonglist( pp1, buf, buf2, num ) ) == NULL ){
      
#ifdef OUTERR   
      fprintf( stderr, "addtostrstrlonglist failed for %s,%s\n", buf, buf2 );
#endif  
      return( -1 );
    }
    retv++;
    pp1 = pp2;

  }

  return( retv );
  
  
}/* of file2strstrlonglist2 */

/******************************************************************************
  file2strstrlist2

read first two fields of file and addtostrstrlist
same as file2strstrlist,
except that it allows empty second field (e.g. rel tag).
In this case, "" is stored.

Third and other fields are ignored.

return value: size of strstrlist (OK)
             -1 (ERROR)
******************************************************************************/
long file2strstrlist2( f, list )
     FILE *f;
     struct strstrlist **list;
{
  long retv = 0;

  char line[ BUFSIZ + 1 ];
  char *p, *pos, *buf, *buf2;

  struct strstrlist **pp1, **pp2;

  struct strstrlist **addtostrstrlist();

  pp1 = list;
  while( fgets( line, sizeof( line ), f ) ){

    if( ( pos = strchr( line, '\n' ) ) == NULL ){
#ifdef OUTERR   
      fprintf( stderr, "Line too long: %s\n", line );
#endif  
      return( -1 );
    }
    *pos = '\0';

    if( ( p = strtok( line, sep ) ) == NULL ){

#ifdef OUTERR   
      fprintf( stderr, "strtok failed\n" );
#endif  
      return( -1 );
    }
    buf = strdup( p );

    if( ( p = strtok( NULL, sep ) ) == NULL ){

      buf2 = strdup( "" ); /* second field missing -> store "" */
    }
    else{

      buf2 = strdup( p );
    }

    if( ( pp2 = addtostrstrlist( pp1, buf, buf2 ) ) == NULL ){
      
#ifdef OUTERR   
      fprintf( stderr, "addtostrstrlist failed for %s,%s\n", buf, buf2 );
#endif  
      return( -1 );
    }
    retv++;
    pp1 = pp2;

  }

  return( retv );
  
  
}/* of file2strstrlist2 */

/******************************************************************************
 addtostrstrlonglist

add <str1>,<str2>,<val> to end of list

return value: <lastrecord>->next
              NULL (error)
******************************************************************************/
struct strstrlonglist **addtostrstrlonglist( p, s1, s2, val )
     struct strstrlonglist **p;
     char *s1;
     char *s2;
     long val;
{
  struct strstrlonglist *q;

  if( s1 == NULL || s2 == NULL )
    return( NULL );

  if( ( q = ( struct strstrlonglist * )
	malloc( sizeof( struct strstrlonglist ) ) ) == NULL ){

#ifdef OUTERR
    fprintf( stderr, "malloc failed: %s,%s,%ld\n", s1, s2, val );
#endif

    return( NULL );
  }

  q->string1 = s1;
  q->string2 = s2;
  q->val = val;
  q->next = NULL;
  *p = q;
  return( &( q->next ) );
  
}/* of addtostrstrlonglist */

/******************************************************************************
  freestrstrlonglist

return value: 0 (OK)
******************************************************************************/
int freestrstrlonglist( p )
     struct strstrlonglist **p;
{
  struct strstrlonglist *p1, *p2;

  p1 = *p;
  while( p1 ){
    p2 = p1;
    p1 = p1->next;
    free( p2->string1 );
    free( p2->string2 );
    free( p2 );
  }

  *p = NULL;
  return( 0 );
    
}/* of freestrstrlonglist */

/******************************************************************************
  fprintstrstrlonglist
return value: 0 (OK)  
******************************************************************************/
int fprintstrstrlonglist( fp, p )
     FILE *fp;
     struct strstrlonglist *p;
{
  struct strstrlonglist *p1;

  p1 = p ;

  while( p1 ){
    fprintf( fp, "%s %s %ld\n", p1->string1, p1->string2, p1->val );
    p1 = p1->next;
  }
  
  return( 0 );

}/* of fprintstrstrlonglist */

/******************************************************************************
 addtostrdoublong2list

add <str1>,<double>,<long>,<long> to end of list

return value: <lastrecord>->next
              NULL (error)
******************************************************************************/
struct strdoublong2list **addtostrdoublong2list( p, s1, dval, lval1, lval2 )
     struct strdoublong2list **p;
     char *s1;
     double dval;
     long lval1;
     long lval2;
{
  struct strdoublong2list *q;

  if( s1 == NULL )
    return( NULL );

  if( ( q = ( struct strdoublong2list * )
	malloc( sizeof( struct strdoublong2list ) ) ) == NULL ){

#ifdef OUTERR
    fprintf( stderr, "malloc failed: %s\n", s1 );
#endif

    return( NULL );
  }

  q->string = s1;
  q->val1 = dval;
  q->val2 = lval1;
  q->val3 = lval2;
  q->next = NULL;
  *p = q;
  return( &( q->next ) );
  
}/* of addtostrdoublong2list */

/******************************************************************************
 addtostrstrlist

add <str1>,<str2> to end of list

return value: <lastrecord>->next
              NULL (error)
******************************************************************************/
struct strstrlist **addtostrstrlist( p, s1, s2 )
     struct strstrlist **p;
     char *s1;
     char *s2;
{
  struct strstrlist *q;

  if( s1 == NULL || s2 == NULL )
    return( NULL );

  if( ( q = ( struct strstrlist * )
	malloc( sizeof( struct strstrlist ) ) ) == NULL ){

#ifdef OUTERR
    fprintf( stderr, "malloc failed: %s,%s\n", s1, s2 );
#endif

    return( NULL );
  }

  q->string1 = s1;
  q->string2 = s2;
  q->next = NULL;
  *p = q;
  return( &( q->next ) );
  
}/* of addtostrstrlist */

/******************************************************************************
  freestrstrlist

return value: 0 (OK)
******************************************************************************/
int freestrstrlist( p )
     struct strstrlist **p;
{
  struct strstrlist *p1, *p2;

  p1 = *p;
  while( p1 ){
    p2 = p1;
    p1 = p1->next;
    free( p2->string1 );
    free( p2->string2 );
    free( p2 );
  }

  *p = NULL;
  return( 0 );
    
}/* of freestrstrlist */

/******************************************************************************
  fprintstrstrlist
return value: 0 (OK)  
******************************************************************************/
int fprintstrstrlist( fp, p )
     FILE *fp;
     struct strstrlist *p;
{
  struct strstrlist *p1;

  p1 = p ;

  while( p1 ){
    fprintf( fp, "%s %s\n", p1->string1, p1->string2 );
    p1 = p1->next;
  }
  
  return( 0 );

}/* of fprintstrstrlist */
