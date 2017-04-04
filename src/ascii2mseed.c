/***************************************************************************
 * ascii2mseed.c
 *
 * Simple waveform data conversion from ASCII timeseries to Mini-SEED.
 *
 * Written by Chad Trabant, IRIS Data Management Center
 *
 * modified 2015.148
 ***************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <ctype.h>

#include <libmseed.h>

#define VERSION "1.5"
#define PACKAGE "ascii2mseed"

struct listnode {
  char *key;
  char *data;
  struct listnode *next;
};

static void packtraces (MSTraceGroup *mstg, flag flush);
static void freetraces (MSTraceGroup *mstg);
static int packascii (char *infile);
static int setheadervalues (char *flags, MSRecord *msr);
static int readslist (FILE *ifp, void *data, char datatype, int32_t datacnt);
static int readtspair (FILE *ifp, void *data, char datatype, int32_t datacnt, double samprate);
static int parameter_proc (int argcount, char **argvec);
static char *getoptval (int argcount, char **argvec, int argopt);
static int readlistfile (char *listfile);
static void addnode (struct listnode **listroot, char *key, char *data);
static void record_handler (char *record, int reclen, void *handlerdata);
static void usage (void);

static inline void setbit (uint8_t *byte, int bit) { *byte |= (1 << bit); }
static inline void clearbit (uint8_t *byte, int bit) { *byte &= ~(1 << bit); }

static int   verbose     = 0;
static int   packreclen  = -1;
static int   encoding    = 11;
static int   byteorder   = -1;
static char  srateblkt   = 0;
static char *outputfile  = 0;
static FILE *ofp         = 0;

/* A list of input files */
struct listnode *filelist = 0;

static int64_t packedtraces  = 0;
static int64_t packedsamples = 0;
static int64_t packedrecords = 0;

int
main (int argc, char **argv)
{
  struct listnode *flp;
  
  /* Process given parameters (command line and parameter file) */
  if (parameter_proc (argc, argv) < 0)
    return -1;
  
  /* Open the output file if specified */
  if ( outputfile )
    {
      if ( strcmp (outputfile, "-") == 0 )
        {
          ofp = stdout;
        }
      else if ( (ofp = fopen (outputfile, "wb")) == NULL )
        {
          fprintf (stderr, "Cannot open output file: %s (%s)\n",
                   outputfile, strerror(errno));
          return -1;
        }
    }
  
  /* Read and convert input files */
  flp = filelist;
  while ( flp != 0 )
    {
      if ( verbose )
	fprintf (stderr, "Reading %s\n", flp->data);
      
      packascii (flp->data);
      
      flp = flp->next;
    }
  
  fprintf (stderr, "Packed %lld trace(s) of %lld samples into %lld records\n",
	   (long long int)packedtraces,
           (long long int)packedsamples,
           (long long int)packedrecords);
  
  /* Make sure everything is cleaned up */
  if ( ofp )
    fclose (ofp);
  
  return 0;
}  /* End of main() */


/***************************************************************************
 * packtraces:
 *
 * Pack all traces in a group using per-MSTrace templates.
 *
 * Returns 0 on success, and -1 on failure
 ***************************************************************************/
static void
packtraces (MSTraceGroup *mstg, flag flush)
{
  MSTrace *mst;
  int64_t trpackedsamples = 0;
  int64_t trpackedrecords = 0;

  mst = mstg->traces;
  while ( mst )
    {
      if ( mst->numsamples <= 0 )
	{
	  mst = mst->next;
	  continue;
	}
      
      trpackedrecords = mst_pack (mst, &record_handler, 0, packreclen, encoding, byteorder,
				  &trpackedsamples, flush, verbose-2, (MSRecord *) mst->prvtptr);
      
      if ( trpackedrecords < 0 )
	{
	  fprintf (stderr, "Error packing data\n");
	}
      else
	{
	  packedrecords += trpackedrecords;
	  packedsamples += trpackedsamples;
	}
      
      mst = mst->next;
    }
}  /* End of packtraces() */


/***************************************************************************
 * freetraces:
 *
 * Free all traces in a group including per-MSTrace templates.
 *
 * Returns 0 on success, and -1 on failure
 ***************************************************************************/
static void
freetraces (MSTraceGroup *mstg)
{
  MSTrace *mst;
  MSRecord *msr;
  
  /* Free MSRecord structures at mst->prvtptr */
  mst = mstg->traces;
  while ( mst )
    {
      if ( mst->prvtptr )
	{
	  msr = (MSRecord *)mst->prvtptr;
	  msr_free (&msr);
	  mst->prvtptr = 0;
	}
      
      mst = mst->next;
    }
  
  mst_freegroup (&mstg);
}  /* End of freetraces() */


/***************************************************************************
 * packascii:
 *
 * Read an ASCII file and pack Mini-SEED.
 *
 * Returns 0 on success, and -1 on failure
 ***************************************************************************/
static int
packascii (char *infile)
{
  FILE *ifp = 0;
  MSRecord *msr = 0;
  MSTrace *mst = 0;
  MSTraceGroup *mstg = 0;
  struct blkt_1000_s Blkt1000;
  struct blkt_1001_s Blkt1001;
  struct blkt_100_s Blkt100;
  int fields;
  
  char *cp;
  char rdline[350];
  char srcname[50];
  char timestr[50];
  char listtype[20];
  char sampletype[20];
  char unitstr[20];
  char flagstr[100];
  double samplerate;
  int samplecnt;
  hptime_t hpdelta;
  
  /* Init MSTraceGroup */
  mstg = mst_initgroup (mstg);
  
  /* Open input file */
  if ( (ifp = fopen (infile, "rb")) == NULL )
    {
      fprintf (stderr, "Cannot open input file: %s (%s)\n",
	       infile, strerror(errno));
      return -1;
    }
  
  while ( fgets (rdline, sizeof(rdline), ifp) )
    {
      // TIMESERIES TA_J15A__BHZ_R, 635 samples, 40 sps, 2008-01-15T00:00:00.025000, SLIST, INTEGER[, Counts[, Flags]]
      // TIMESERIES TA_J15A__BHZ_R, 635 samples, 40 sps, 2008-01-15T00:00:00.025000, TSPAIR, INTEGER[, Counts[, Flags]]
      
      /* Terminate input string at first newline or carriage return */
      cp = rdline;
      while ( *cp && *cp != '\r' && *cp != '\n' ) cp++;
      *cp = '\0';
      
      unitstr[0] = '\0';
      flagstr[0] = '\0';
      fields = sscanf (rdline, "TIMESERIES %[^,], %d samples, %lf sps, %[^,], %[^,], %[^,], %[^,], %s",
		       srcname, &samplecnt, &samplerate, timestr, listtype, sampletype, unitstr, flagstr);
      
      //fprintf (stderr, "TIMESERIES (%d), unitstr: '%s', flagstr: '%s'\n", fields, unitstr, flagstr);
      
      if ( fields >= 6 )
	{
	  /* Initialize new MSTrace holder */
	  if ( ! (mst = mst_init(NULL)) )
	    {
	      fprintf (stderr, "Cannot initialize MSTrace strcture\n");
	      return -1;
	    }
          
	  /* Split source name into separate quantities for the MSTrace */
	  if ( ms_splitsrcname (srcname, mst->network, mst->station, mst->location, mst->channel, &(mst->dataquality)) )
	    {
	      fprintf (stderr, "Cannot parse channel source name: %s (improperly specified?)\n", srcname);
	      return -1;
	    }
	  
          mst->samplecnt = samplecnt;
	  mst->numsamples = samplecnt;
	  mst->samprate = samplerate;

	  /* Convert time string to a high-precision time value */
	  mst->starttime = ms_timestr2hptime (timestr);
	  if ( mst->starttime == HPTERROR )
	    {
	      fprintf (stderr, "Error converting start time: %s\n", timestr);
	      return -1;
	    }
          
          hpdelta = ( mst->samprate ) ? (hptime_t) (HPTMODULUS / mst->samprate) : 0;
          mst->endtime = mst->starttime + (samplecnt - 1) * hpdelta;
	  
	  /* Determine sample type */
	  if ( ! strncasecmp (sampletype, "INTEGER", 7) )
	    {
	      mst->sampletype = 'i';
	    }
	  else if ( ! strncasecmp (sampletype, "FLOAT64", 7) )
	    {
	      mst->sampletype = 'd';
	      encoding = 5;
	    }
	  else if ( ! strncasecmp (sampletype, "FLOAT", 5) )
	    {
	      mst->sampletype = 'f';
	      encoding = 4;
	    }
	  else
	    {
	      fprintf (stderr, "Unrecognized data sample type: '%s'\n", sampletype);
	      return -1;
	    }
	  
	  /* Allocate memory for the data samples */
	  if ( ! (mst->datasamples = calloc (mst->numsamples, ms_samplesize(mst->sampletype))) )
	    {
	      fprintf (stderr, "Cannot allocate memory for data samples\n");
	      return -1;
	    }
	  
	  if ( ! strncmp (listtype, "SLIST", 5) )
	    {
	      if ( readslist (ifp, mst->datasamples, mst->sampletype, mst->numsamples) )
		{
		  fprintf (stderr, "Error reading samples from file\n");
		  return -1;
		}
	    }
	  else if ( ! strncmp (listtype, "TSPAIR", 6) )
	    {
	      if ( readtspair (ifp, mst->datasamples, mst->sampletype, mst->numsamples, mst->samprate) )
		{
		  fprintf (stderr, "Error reading samples from file\n");
		  return -1;
		}
	    }
	  else
	    {
	      fprintf (stderr, "Unrecognized sample list type: '%s'\n", listtype);
	      return -1;
	    }
          
	  if ( verbose >= 1 )
	    {
	      fprintf (stderr, "[%s] %lld samps @ %.6f Hz for N: '%s', S: '%s', L: '%s', C: '%s'\n",
		       infile, (long long int)mst->numsamples, mst->samprate,
		       mst->network, mst->station,  mst->location, mst->channel);
	    }
	  
          if ( ! mst_addtracetogroup (mstg, mst) )
	    {
	      fprintf (stderr, "[%s] Error adding trace to MSTraceGroup\n", infile);
              return -1;
	    }
	  
	  /* Create an MSRecord template for the MSTrace */
          if ( ! (msr = msr_init(NULL)) )
            {
              fprintf (stderr, "[%s] Cannot initialize MSRecord strcture\n", infile);
              return -1;
            }
          
          mst->prvtptr = msr;
          
	  /* Split source name into separate quantities for the template MSRecord */
	  if ( ms_splitsrcname (srcname, msr->network, msr->station, msr->location, msr->channel, &(msr->dataquality)) )
            {
              fprintf (stderr, "Cannot parse channel source name: %s (improperly specified?)\n", srcname);
              return -1;
            }
          
          /* Add blockettes 1000 & 1001 to template */
          memset (&Blkt1000, 0, sizeof(struct blkt_1000_s));
          msr_addblockette (msr, (char *) &Blkt1000,
                            sizeof(struct blkt_1001_s), 1000, 0);
          memset (&Blkt1001, 0, sizeof(struct blkt_1001_s));
          msr_addblockette (msr, (char *) &Blkt1001,
                            sizeof(struct blkt_1001_s), 1001, 0);
          
          /* Add blockette 100 to template if requested */
          if ( srateblkt )
            {
              memset (&Blkt100, 0, sizeof(struct blkt_100_s));
              Blkt100.samprate = (float) mst->samprate;
              msr_addblockette (msr, (char *) &Blkt100,
                                sizeof(struct blkt_100_s), 100, 0);
            }
          
          /* Set flags in header if present in TIMESERIES declaration */
          if ( flagstr[0] )
            {
              if ( setheadervalues (flagstr, msr) )
                {
                  return -1;
                }
            }
        } /* End of TIMESERIES line detection loop */
    } /* End of reading lines from input file */
  
  /* Sort MSTraceGroup before packing */
  if ( mst_groupsort (mstg, 1) )
    {
      fprintf (stderr, "[%s] Error sorting traces\n", infile);
      return -1;
    }
  
  /* Pack MSTraceGroup into miniSEED */
  packtraces (mstg, 1);
  
  packedtraces += mstg->numtraces;
  
  fclose (ifp);
  
  if ( mstg )
    freetraces (mstg);
  
  return 0;
}  /* End of packascii() */


/***************************************************************************
 * setheadervalues:
 *
 * Read a string of encoded, bar-separated miniSEED header values
 * and set them appropriately in the specified MSRecord.
 *
 * Supported header flags:
 *   FDSH:ACTFLAGS:[bit]=[value]
 *   FDSH:IOFLAGS:[bit]=[value]
 *   FDSH:DQFLAGS:[bit]=[value]
 *   B1001:TIMINGQUALITY=[value]
 *
 * Example: "FSDH:IOFLAGS:5=1|B1001:TIMINGQUALITY=100"
 *
 * Returns 0 on sucess or non-zero on error.
 ***************************************************************************/
static int
setheadervalues (char *flags, MSRecord *msr)
{
  char *cp;
  int fields;
  int bit;
  int value;
  
  if ( ! flags || ! msr )
    return -1;

  /* Allocate FSDH struct if needed and not already present */
  if ( strstr (flags, "FSDH") )
    {
      if ( ! msr->fsdh )
        {
          if ( ! (msr->fsdh = calloc (1, sizeof(struct fsdh_s))) )
            {
              fprintf (stderr, "Cannot initialize FSDH strcture\n");
              return -1;
            }
        }
    }
  
  cp = flags;
  do
    {
      if ( ! strncmp (cp, "FSDH:ACTFLAGS", 13) )
        {
          fields = sscanf (cp, "FSDH:ACTFLAGS:%d=%d", &bit, &value);
          if ( fields == 2 )
            {
              if ( value )
                setbit (&msr->fsdh->act_flags, bit);
              else
                clearbit (&msr->fsdh->act_flags, bit);
            }
          else
            {
              fprintf (stderr, "Error parsing ACTFLAG starting at: '%s'\n", cp);
              return -1;
            }
        }
      else if ( ! strncmp (cp, "FSDH:IOFLAGS", 12) )
        {
          fields = sscanf (cp, "FSDH:IOFLAGS:%d=%d", &bit, &value);
          if ( fields == 2 )
            {
              if ( value )
                setbit (&msr->fsdh->io_flags, bit);
              else
                clearbit (&msr->fsdh->io_flags, bit);
            }
          else
            {
              fprintf (stderr, "Error parsing IOFLAG starting at: '%s'\n", cp);
              return -1;
            }
        }
      else if ( ! strncmp (cp, "FSDH:DQFLAGS", 12) )
        {
          fields = sscanf (cp, "FSDH:DQFLAGS:%d=%d", &bit, &value);
          if ( fields == 2 )
            {
              if ( value )
                setbit (&msr->fsdh->dq_flags, bit);
              else
                clearbit (&msr->fsdh->dq_flags, bit);
            }
          else
            {
              fprintf (stderr, "Error parsing DQFLAG starting at: '%s'\n", cp);
              return -1;
            }
        }
      else if ( ! strncmp (cp, "B1001:TIMINGQUALITY", 19) )
        {
          fields = sscanf (cp, "B1001:TIMINGQUALITY=%d", &value);
          if ( fields == 1 )
            {
              if ( msr->Blkt1001 )
                msr->Blkt1001->timing_qual = value;
            }
          else
            {
              fprintf (stderr, "Error parsing B1001:TIMINGQUALITY starting at: '%s'\n", cp);
              return -1;
            }
        }
      else
        {
          fprintf (stderr, "Unrecognized header value starting at: '%s'\n", cp);
          return -1;
        }
      /* WTF: the loop condition finds the next bar, advances one more character
       * and tests that it is not the terminator.  Neato and obtuse. */
    } while ( (cp = strchr(cp, '|')) && *(cp++) );
  
  return 0;
}  /* End of setheadervalues() */


/***************************************************************************
 * readslist:
 *
 * Read a alphanumeric data from a file and add to an array, the array
 * must already be allocated with datacnt floats.
 *
 * The data must be organized in 1-8 columns.  32-bit integers, floats
 * and 64-bit doubles are parsed according to the 'datatype' argument
 * ('i', 'f' or 'd').
 *
 * Returns 0 on sucess or a positive number indicating line number of
 * parsing failure.
 ***************************************************************************/
static int
readslist (FILE *ifp, void *data, char datatype, int32_t datacnt)
{
  char line[1025];
  int linecnt = 1;
  int samplesread = 0;
  int count = 0;
  int dataidx = 0;
  
  if ( ! ifp || ! data || ! datacnt )
    return -1;
  
  /* Each data line should contain 1-8 samples */
  for (;;)
    {
      if ( ! fgets(line, sizeof(line), ifp) )
	return linecnt;
      
      if ( datatype == 'i' )
	count = sscanf (line, " %d %d %d %d %d %d %d %d ", (int32_t *) data + dataidx,
			(int32_t *) data + dataidx + 1, (int32_t *) data + dataidx + 2,
			(int32_t *) data + dataidx + 3, (int32_t *) data + dataidx + 4,
			(int32_t *) data + dataidx + 5, (int32_t *) data + dataidx + 6,
			(int32_t *) data + dataidx + 7);
      else if ( datatype == 'f' )
	count = sscanf (line, " %f %f %f %f %f %f %f %f ", (float *) data + dataidx,
			(float *) data + dataidx + 1, (float *) data + dataidx + 2,
			(float *) data + dataidx + 3, (float *) data + dataidx + 4,
			(float *) data + dataidx + 5, (float *) data + dataidx + 6,
			(float *) data + dataidx + 7);
      else if ( datatype == 'd' )
	count = sscanf (line, " %lf %lf %lf %lf %lf %lf %lf %lf ", (double *) data + dataidx,
			(double *) data + dataidx + 1, (double *) data + dataidx + 2,
			(double *) data + dataidx + 3, (double *) data + dataidx + 4,
			(double *) data + dataidx + 5, (double *) data + dataidx + 6,
			(double *) data + dataidx + 7);
      
      samplesread += count;
      
      if ( samplesread >= datacnt )
	break;
      else if ( count < 1 || count > 8 )
	return linecnt;
      
      dataidx += count;
      linecnt++;
    }
  
  return 0;
}  /* End of readslist() */


/***************************************************************************
 * readtspair:
 *
 * Read a alphanumeric data from a file and add to an array, the array
 * must already be allocated with datacnt floats.
 *
 * The data must be organized in 2 column, time-sample pairs.  32-bit
 * integers, floats and 64-bit doubles are parsed according to the
 * 'datatype' argument ('i', 'f' or 'd').
 *
 * Example data line:
 * "2008-01-15T00:00:08.975000  678.145"
 *
 * The data is checked to be evenly spaced and to match the supplied
 * sample rate.
 *
 * Returns 0 on sucess or a positive number indicating line number of
 * parsing failure.
 ***************************************************************************/
static int
readtspair (FILE *ifp, void *data, char datatype, int32_t datacnt, double samprate)
{
  hptime_t samptime = HPTERROR;
  hptime_t prevtime = HPTERROR;
  char line[1025];
  char stime[50];
  int linecnt = 1;
  int samplesread = 0;
  int count = 0;
  int dataidx = 0;
  
  if ( ! ifp || ! data || ! datacnt )
    return -1;
  
  /* Each data line should contain a time-sample pair */
  for (;;)
    {
      if ( ! fgets(line, sizeof(line), ifp) )
	return linecnt;
      
      if ( datatype == 'i' )
	count = sscanf (line, " %s %d ", stime, (int32_t *) data + dataidx);
      else if ( datatype == 'f' )
	count = sscanf (line, " %s %f ", stime, (float *) data + dataidx);
      else if ( datatype == 'd' )
	count = sscanf (line, " %s %lf ", stime, (double *) data + dataidx);
      
      if ( count == 2 )
	{
	  /* Convert sample time to high-precision time value */
	  if ( (samptime = ms_timestr2hptime (stime)) == HPTERROR )
	    {
	      fprintf (stderr, "Error converting sample time stamp: '%s'\n", stime);
	      return linecnt;
	    }
	  
	  /* Check sample spacing */
	  if ( prevtime != HPTERROR )
	    {
	      double srate = (double) HPTMODULUS / (samptime - prevtime);
	      
	      if ( ! MS_ISRATETOLERABLE (samprate, srate) )
		{
		  fprintf (stderr, "Data samples are not evenly sampled starting at sample %d (%g versus %g)\n",
                           linecnt, samprate, srate);
		  return linecnt;
		}
	    }
	  
	  prevtime = samptime;
	  
	  samplesread += 1;
	  
	  if ( samplesread >= datacnt )
	    break;
	}
      else
	{
	  return linecnt;
	}
      
      dataidx += 1;
      linecnt++;
    }
  
  return 0;
}  /* End of readtspair() */


/***************************************************************************
 * parameter_proc:
 * Process the command line parameters.
 *
 * Returns 0 on success, and -1 on failure.
 ***************************************************************************/
static int
parameter_proc (int argcount, char **argvec)
{
  int optind;

  /* Process all command line arguments */
  for (optind = 1; optind < argcount; optind++)
    {
      if (strcmp (argvec[optind], "-V") == 0)
	{
	  fprintf (stderr, "%s version: %s\n", PACKAGE, VERSION);
	  exit (0);
	}
      else if (strcmp (argvec[optind], "-h") == 0)
	{
	  usage();
	  exit (0);
	}
      else if (strncmp (argvec[optind], "-v", 2) == 0)
	{
	  verbose += strspn (&argvec[optind][1], "v");
	}
      else if (strcmp (argvec[optind], "-S") == 0)
	{
	  srateblkt = 1;
	}
      else if (strcmp (argvec[optind], "-r") == 0)
	{
	  packreclen = strtoul (getoptval(argcount, argvec, optind++), NULL, 10);
	}
      else if (strcmp (argvec[optind], "-e") == 0)
	{
	  encoding = strtoul (getoptval(argcount, argvec, optind++), NULL, 10);
	}
      else if (strcmp (argvec[optind], "-b") == 0)
	{
	  byteorder = strtoul (getoptval(argcount, argvec, optind++), NULL, 10);
	}
      else if (strcmp (argvec[optind], "-o") == 0)
	{
	  outputfile = getoptval(argcount, argvec, optind++);
	}
      else if (strncmp (argvec[optind], "-", 1) == 0 &&
	       strlen (argvec[optind]) > 1 )
	{
	  fprintf(stderr, "Unknown option: %s\n", argvec[optind]);
	  exit (1);
	}
      else
	{
	  addnode (&filelist, NULL, argvec[optind]);
	}
    }
  
  /* Make sure an input files were specified */
  if ( filelist == 0 )
    {
      fprintf (stderr, "No input files were specified\n\n");
      fprintf (stderr, "%s version %s\n\n", PACKAGE, VERSION);
      fprintf (stderr, "Try %s -h for usage\n", PACKAGE);
      exit (1);
    }

  /* Report the program version */
  if ( verbose )
    fprintf (stderr, "%s version: %s\n", PACKAGE, VERSION);
  
  /* Check for an output file */
  if ( ! outputfile )
    fprintf (stderr, "WARNING: no output file specified\n");    
  
  /* Check the input files for any list files, if any are found
   * remove them from the list and add the contained list */
  if ( filelist )
    {
      struct listnode *prevln, *ln;
      char *lfname;
      
      prevln = ln = filelist;
      while ( ln != 0 )
	{
	  lfname = ln->data;
	  
	  if ( *lfname == '@' )
	    {
	      /* Remove this node from the list */
	      if ( ln == filelist )
		filelist = ln->next;
	      else
		prevln->next = ln->next;
	      
	      /* Skip the '@' first character */
	      if ( *lfname == '@' )
		lfname++;

	      /* Read list file */
	      readlistfile (lfname);
	      
	      /* Free memory for this node */
	      if ( ln->key )
		free (ln->key);
	      free (ln->data);
	      free (ln);
	    }
	  else
	    {
	      prevln = ln;
	    }
	  
	  ln = ln->next;
	}
    }

  return 0;
}  /* End of parameter_proc() */


/***************************************************************************
 * getoptval:
 * Return the value to a command line option; checking that the value is 
 * itself not an option (starting with '-') and is not past the end of
 * the argument list.
 *
 * argcount: total arguments in argvec
 * argvec: argument list
 * argopt: index of option to process, value is expected to be at argopt+1
 *
 * Returns value on success and exits with error message on failure
 ***************************************************************************/
static char *
getoptval (int argcount, char **argvec, int argopt)
{
  if ( argvec == NULL || argvec[argopt] == NULL ) {
    fprintf (stderr, "getoptval(): NULL option requested\n");
    exit (1);
    return 0;
  }
  
  /* Special case of '-o -' usage */
  if ( (argopt+1) < argcount && strcmp (argvec[argopt], "-o") == 0 )
    if ( strcmp (argvec[argopt+1], "-") == 0 )
      return argvec[argopt+1];
  
  if ( (argopt+1) < argcount && *argvec[argopt+1] != '-' )
    return argvec[argopt+1];
  
  fprintf (stderr, "Option %s requires a value\n", argvec[argopt]);
  exit (1);
  return 0;
}  /* End of getoptval() */


/***************************************************************************
 * readlistfile:
 *
 * Read a list of files from a file and add them to the filelist for
 * input data.  The filename is expected to be the last
 * space-separated field on the line.
 *
 * Returns the number of file names parsed from the list or -1 on error.
 ***************************************************************************/
static int
readlistfile (char *listfile)
{
  FILE *fp;
  char  line[1024];
  char *ptr;
  int   filecnt = 0;
  
  char  filename[1024];
  char *lastfield = 0;
  int   fields = 0;
  int   wspace;
  
  /* Open the list file */
  if ( (fp = fopen (listfile, "rb")) == NULL )
    {
      if (errno == ENOENT)
        {
          fprintf (stderr, "Could not find list file %s\n", listfile);
          return -1;
        }
      else
        {
          fprintf (stderr, "Error opening list file %s: %s\n",
		   listfile, strerror (errno));
          return -1;
        }
    }
  
  if ( verbose )
    fprintf (stderr, "Reading list of input files from %s\n", listfile);
  
  while ( (fgets (line, sizeof(line), fp)) !=  NULL)
    {
      /* Truncate line at first \r or \n, count space-separated fields
       * and track last field */
      fields = 0;
      wspace = 0;
      ptr = line;
      while ( *ptr )
	{
	  if ( *ptr == '\r' || *ptr == '\n' || *ptr == '\0' )
	    {
	      *ptr = '\0';
	      break;
	    }
	  else if ( *ptr != ' ' )
	    {
	      if ( wspace || ptr == line )
		{
		  fields++; lastfield = ptr;
		}
	      wspace = 0;
	    }
	  else
	    {
	      wspace = 1;
	    }
	  
	  ptr++;
	}
      
      /* Skip empty lines */
      if ( ! lastfield )
	continue;
      
      if ( fields >= 1 && fields <= 3 )
	{
	  fields = sscanf (lastfield, "%s", filename);
	  
	  if ( fields != 1 )
	    {
	      fprintf (stderr, "Error parsing file name from: %s\n", line);
	      continue;
	    }
	  
	  if ( verbose > 1 )
	    fprintf (stderr, "Adding '%s' to input file list\n", filename);
	  
	  addnode (&filelist, NULL, filename);
	  filecnt++;
	  
	  continue;
	}
    }
  
  fclose (fp);
  
  return filecnt;
}  /* End readlistfile() */


/***************************************************************************
 * addnode:
 *
 * Add node to the specified list.
 ***************************************************************************/
static void
addnode (struct listnode **listroot, char *key, char *data)
{
  struct listnode *lastlp, *newlp;
  
  if ( data == NULL )
    {
      fprintf (stderr, "addnode(): No file name specified\n");
      return;
    }
  
  lastlp = *listroot;
  while ( lastlp != 0 )
    {
      if ( lastlp->next == 0 )
        break;
      
      lastlp = lastlp->next;
    }
  
  newlp = (struct listnode *) malloc (sizeof (struct listnode));
  memset (newlp, 0, sizeof (struct listnode));
  if ( key ) newlp->key = strdup(key);
  else newlp->key = key;
  if ( data) newlp->data = strdup(data);
  else newlp->data = data;
  newlp->next = 0;
  
  if ( lastlp == 0 )
    *listroot = newlp;
  else
    lastlp->next = newlp;
  
}  /* End of addnode() */


/***************************************************************************
 * record_handler:
 * Saves passed records to the output file.
 ***************************************************************************/
static void
record_handler (char *record, int reclen, void *handlerdata)
{
  if ( ofp )
    {
      if ( fwrite(record, reclen, 1, ofp) != 1 )
	{
	  fprintf (stderr, "Error writing to output file\n");
	}
    }
}  /* End of record_handler() */


/***************************************************************************
 * usage:
 * Print the usage message and exit.
 ***************************************************************************/
static void
usage (void)
{
  fprintf (stderr, "%s version: %s\n\n", PACKAGE, VERSION);
  fprintf (stderr, "Convert ASCII time-series data to Mini-SEED.\n\n");
  fprintf (stderr, "Usage: %s [options] file1 [file2 file3 ...]\n\n", PACKAGE);
  fprintf (stderr,
	   " ## Options ##\n"
	   " -V             Report program version\n"
	   " -h             Show this usage message\n"
	   " -v             Be more verbose, multiple flags can be used\n"
	   " -S             Include SEED blockette 100 for very irrational sample rates\n"
	   " -r bytes       Specify record length in bytes for packing, default: 4096\n"
	   " -e encoding    Specify SEED encoding format for packing, default: 11 (Steim2)\n"
	   " -b byteorder   Specify byte order for packing, MSBF: 1 (default), LSBF: 0\n"
	   " -o outfile     Specify the output file, default is <inputfile>.mseed\n"
	   "\n"
	   " file(s)        File(s) of ASCII input data\n"
	   "                  If a file is prefixed with an '@' it is assumed to contain\n"
	   "                  a list of data files to be read\n"
	   "\n"
	   "Supported Mini-SEED encoding formats:\n"
           " 3  : 32-bit integers\n"
           " 4  : 32-bit floats, required for float (FLOAT) input samples\n"
           " 5  : 64-bit floats, required for double (FLOAT64) input samples\n"
           " 10 : Steim 1 compression of 32-bit integers\n"
           " 11 : Steim 2 compression of 32-bit integers\n"
	   "\n");
}  /* End of usage() */
