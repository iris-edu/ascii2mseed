# <p >ASCII time series to miniSEED converter</p>

1. [Name](#)
1. [Synopsis](#synopsis)
1. [Description](#description)
1. [Options](#options)
1. [List Files](#list-files)
1. [Ascii Data](#ascii-data)
1. [Miniseed Header Values](#miniseed-header-values)
1. [Author](#author)

## <a id='synopsis'>Synopsis</a>

<pre >
ascii2mseed [options] file1 [file2 file3 ...]
</pre>

## <a id='description'>Description</a>

<p ><b>ascii2mseed</b> converts simple ASCII time series data to miniSEED format.  If an input file name is prefixed with an '@' character the file is assumed to contain a list of input data files, see <i>LIST FILES</i> below.  All output miniSEED records will be written to a specified output file.</p>

## <a id='options'>Options</a>

<b>-V</b>

<p style="padding-left: 30px;">Print program version and exit.</p>

<b>-h</b>

<p style="padding-left: 30px;">Print program usage and exit.</p>

<b>-v</b>

<p style="padding-left: 30px;">Be more verbose.  This flag can be used multiple times ("-v -v" or "-vv") for more verbosity.</p>

<b>-S</b>

<p style="padding-left: 30px;">Include SEED blockette 100 in each output record with the sample rate in floating point format.  The basic format for storing sample rates in SEED data records is a rational approximation (numerator/denominator).  Precision will be lost if a given sample rate cannot be well approximated.  This option should be used in those cases.</p>

<b>-r </b><i>bytes</i>

<p style="padding-left: 30px;">Specify the miniSEED record length in <i>bytes</i>, default is 4096.</p>

<b>-e </b><i>encoding</i>

<p style="padding-left: 30px;">Specify the miniSEED data encoding format, default is 11 (Steim-2 compression) for integer data and 4 for floats (stored uncompressed). Other supported encoding formats include 10 (Steim-1 compression), 1 (16-bit integers) and 3 (uncompressed 32-bit integers).</p>

<b>-b </b><i>byteorder</i>

<p style="padding-left: 30px;">Specify the miniSEED byte order, default is 1 (big-endian or most significant byte first).  The other option is 0 (little-endian or least significant byte first).  It is highly recommended to always create big-endian SEED.</p>

<b>-o </b><i>outfile</i>

<p style="padding-left: 30px;">Write all miniSEED records to <i>outfile</i>, if <i>outfile</i> is a single dash (-) then all miniSEED output will go to stdout.  All diagnostic output from the program is written to stderr and should never get mixed with data going to stdout.</p>

## <a id='list-files'>List Files</a>

<p >If an input file is prefixed with an '@' character the file is assumed to contain a list of file for input.  Multiple list files can be combined with multiple input files on the command line.  The last, space separated field on each line is assumed to be the file name to be read.</p>

<p >An example of a simple text list:</p>

<pre >
tspair.ascii
slist.ascii
</pre>

## <a id='ascii-data'>Ascii Data</a>

<p >The input ASCII data are expected to start with a simple header followed by data samples in one of two forms: a columnar sample value listing or time-sample pairs.  The columnar sample value listing may have 1 to 8 columns.  The header identifies the time series source parameters (SEED convention) along with number of samples, sample rate, time of first sample, sample list format, sample type and optionally the units of the samples.</p>

<p >Header lines are of the general form:</p>

<pre >
"TIMESERIES SourceName, # samples, # sps, Time, Format, Type, Units, Headers"
</pre>

<p >Header field descriptions:</p>

<pre >
<b>SourceName</b>: "Net_Sta_Loc_Chan_Qual", no spaces, quality code optional
<b># samples</b>:  Number of samples following header
<b># sps</b>:      Sampling rate in samples per second
<b>Time</b>:       Time of first sample in ISO YYYY-MM-DDTHH:MM:SS.FFFFFF format
<b>Format</b>:     'SLIST' (sample list) or 'TSPAIR' (time-sample pair)
<b>Type</b>:       Sample type 'INTEGER' or 'FLOAT' or 'FLOAT64'
<b>Units</b>:      Units of time-series, optional (will not be present in miniSEED)
<b>Headers</b>:    miniSEED header values and flags, optional
</pre>

<p >The header line should not be wrapped and must contain the spaces and commas as specified in the general form.  The units field of the header is optional and will not be used by ascii2mseed (there is no place for units in miniSEED).  No blanks lines should exist between the header and data samples.</p>

<p >The <b>SourceName</b> field identifies the source of the time series data using the SEED name nomenclature separated by underscores.  The data quality code is optional and defaults to 'D'.  Spaces in the source name field are not supported.</p>

<p >The <b>Type</b> field identifies the expected value data type.  The type instructs the converter to parse the data values with the following mapping: <b>INTEGER</b> => 32-bit integer, <b>FLOAT</b> => 32-bit float and <b>FLOAT64</b> => 64-bit float (double).</p>

<p >More than one data segment (header and associated data samples) may be contained in any given input file.</p>

<p ><b>Example data file using SLIST (sample list) format</b></p>

<pre >
TIMESERIES XX_TEST__BHZ, 12 samples, 40 sps, 2003-05-29T02:13:22.043400, SLIST, INTEGER, Counts
      2787        2776        2774        2780        2783        2782
      2776        2766        2759        2760        2765        2767
</pre>

<p ><b>Example data file using TSPAIR (time-sample pair) format</b></p>

<pre >
TIMESERIES XX_TEST__BHZ, 12 samples, 40 sps, 2003-05-29T02:13:22.043400, TSPAIR, INTEGER, Counts
2003-05-29T02:13:22.043400  2787
2003-05-29T02:13:22.068400  2776
2003-05-29T02:13:22.093400  2774
2003-05-29T02:13:22.118400  2780
2003-05-29T02:13:22.143400  2783
2003-05-29T02:13:22.168400  2782
2003-05-29T02:13:22.193400  2776
2003-05-29T02:13:22.218400  2766
2003-05-29T02:13:22.243400  2759
2003-05-29T02:13:22.268400  2760
2003-05-29T02:13:22.293400  2765
2003-05-29T02:13:22.318400  2767
</pre>

## <a id='miniseed-header-values'>Miniseed Header Values</a>

<p >The following miniSEED header values may be set in the TIMESERIES header line:</p>

<pre >
FSDH:ACTFLAGS:bit=value
FSDH:IOFLAGS:bit=value
FSDH:DQFLAGS:bit=value
B1001:TIMINGQUALITY=value
</pre>

<p >The Fixed Section Data Header (FSDH) flag sets are single bytes where each bit is a flag.  The Blockette 1001 (B1001) timinig quality value should be set from 0 to 100 percent.  For details see the SEED format manual.</p>

<p >Multiple values may be specified by concatinating the declarations using vertical-bar delimiters.  For example: "FSDH:IOFLAGS:5=1|B1001:TIMINGQUALITY=100". This composite value would be specified in the header like so:</p>

<pre >
TIMESERIES XX_TEST__BHZ, 12 samples, 40 sps, 2003-05-29T02:13:22.043400, SLIST, INTEGER, Counts, FSDH:IOFLAGS:5=1|B1001:TIMINGQUALITY=100
</pre>

<p >The example above sets bit 5 of the IO flags (Clock locked) and sets the timing quality value of Blockette 1001 (Timing quality) to 100%.</p>

## <a id='author'>Author</a>

<pre >
Chad Trabant
IRIS Data Management Center
</pre>


(man page 2017/04/03)
