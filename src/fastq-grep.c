
/*
 * This file is part of fastq-tools.
 *
 * Copyright (c) 2011 by Daniel C. Jones <dcjones@cs.washington.edu>
 *
 * fastq-grep :
 * Regular expression searches of the sequences within a FASTQ file.
 *
 */


#include "common.h"
#include "parse.h"
#include <stdio.h>
#include <string.h>
#include <getopt.h>
#include <zlib.h>
#include <pcre.h>


#if defined(MSDOS) || defined(OS2) || defined(WIN32) || defined(__CYGWIN__)
#  include <fcntl.h>
#  include <io.h>
#  define SET_BINARY_MODE(file) setmode(fileno(file), O_BINARY)
#else
#  define SET_BINARY_MODE(file)
#endif


static const char* prog_name = "fastq-grep";


void print_help()
{
    fprintf(stdout,
"fastq-grep [OPTION]... PATTERN [FILE]...\n"
"Search for PATTERN in the read sequences in each FILE or standard input.\n"
"PATTERN, by default, is a perl compatible regular expression.\n\n"
"Options:\n"
"  -i, --id                match the read id (by default, sequence is matched)\n"
"  -v, --invert-match      select nonmatching entries\n"
"  -m, --mismatches=FILE   output mismatching entries to the given file\n"
"  -c, --count             output only the number of matching sequences\n"
"  -a, --trim_after        trim output after the match end\n"
"  -b, --trim_before       trim output before the match start\n"
"  -t, --trim_match        trim the match itself, regardless of trimming mode\n"
"  -h, --help              print this message\n"
"  -V, --version           output version information and exit\n"
    );
}

static int invert_flag;
static int count_flag;
static int id_flag;
static int trim_before_flag;
static int trim_after_flag;
static int trim_match_flag;


void fastq_print_maybe_trim(FILE* fout, seq_t* seq, int* ovector) 
{
    if (!trim_before_flag && !trim_after_flag) {
        fastq_print(fout, seq);
        return;
    }

    // trimming
    seq_t* trimmed = seq_create();
    int trimmed_start = 0;
    int trimmed_end   = 0;
    int match_start   = ovector[0];
    int match_end     = ovector[1];
    if (trim_before_flag) {
        trimmed_end = seq->seq.n;
        trimmed_start = trim_match_flag ? match_end : match_start;
    } else if (trim_after_flag) {
        trimmed_start = 0;
        trimmed_end = trim_match_flag ? match_start : match_end;
    }
    seq_trim(seq, trimmed, trimmed_start, trimmed_end);
    fastq_print(fout, trimmed);
    seq_free(trimmed);
}

void fastq_grep(FILE* fin, FILE* fout, FILE* mismatch_file, pcre* re)
{
    int rc;
    int ovector[3];
    size_t count = 0;

    fastq_t* fqf = fastq_create(fin);
    seq_t* seq = seq_create();

    while (fastq_read(fqf, seq)) {
        rc = pcre_exec(re,          /* pattern */
                       NULL,        /* extra data */
                       id_flag ? seq->id1.s : seq->seq.s,
                       id_flag ? seq->id1.n : seq->seq.n,
                       0,           /* subject offset */
                       0,           /* options */
                       ovector,     /* output vector */
                       3         ); /* output vector length */

        if ((invert_flag && rc == PCRE_ERROR_NOMATCH) || (!invert_flag && rc >= 0)) {
            if (count_flag) count++;
            else            fastq_print_maybe_trim(fout, seq, ovector);
        }
        else if (mismatch_file) {
            fastq_print(mismatch_file, seq);
        }
    }

    seq_free(seq);
    fastq_free(fqf);

    if (count_flag) fprintf(fout, "%zu\n", count);
}



int main(int argc, char* argv[])
{
    SET_BINARY_MODE(stdin);
    SET_BINARY_MODE(stdout);

    const char* pat;
    pcre* re;
    const char* pat_error;
    int pat_error_offset;

    FILE*  fin;


    invert_flag      = 0;
    count_flag       = 0;
    id_flag          = 0;
    trim_before_flag = 0;
    trim_after_flag  = 0;
    trim_match_flag  = 0;

    int opt;
    int opt_idx;

    FILE* mismatch_file = NULL;

    static struct option long_options[] =
        {
          {"id",           no_argument, &id_flag,     1},
          {"invert-match", no_argument, &invert_flag, 1},
          {"mismatches",   required_argument, NULL, 'm'},
          {"count",        no_argument, &count_flag,  1},
          {"trim-before",  no_argument, &trim_before_flag,  1},
          {"trim-after",   no_argument, &trim_after_flag,  1},
          {"trim-match",   no_argument, &trim_match_flag,  1},
          {"help",         no_argument, NULL, 'h'},
          {"version",      no_argument, NULL, 'V'},
          {0, 0, 0, 0}
        };

    while (1) {
        opt = getopt_long(argc, argv, "ivmcabthV", long_options, &opt_idx);

        if (opt == -1) break;

        switch (opt) {
            case 0:
                if (long_options[opt_idx].flag != 0) break;
                if (optarg) {
                }
                break;

            case 'i':
                id_flag = 1;
                break;

            case 'v':
                invert_flag = 1;
                break;

            case 'a':
                trim_after_flag = 1;
                break;

            case 'b':
                trim_before_flag = 1;
                break;

            case 't':
                trim_match_flag = 1;
                break;

            case 'm':
                mismatch_file = fopen(optarg, "w");
                if (mismatch_file == NULL) {
                    fprintf(stderr, "No such file '%s'.\n", optarg);
                    return 1;
                }
                break;

            case 'c':
                count_flag = 1;
                break;

            case 'h':
                print_help();
                return 0;

            case 'V':
                print_version(stdout, prog_name);
                return 0;

            case '?':
                return 1;

            default:
                abort();
        }

    }

    if (trim_before_flag && trim_after_flag) {
        fprintf(stderr, "Specify -b or -a, not both.\n");
        return 1;
    }

    int trim = trim_before_flag || trim_after_flag;
    if (trim && id_flag) {
        fprintf(stderr, "Makes no sense to trim IDs.\n");
        return 1;
    }

    if (optind >= argc) {
        fprintf(stderr, "A pattern must be specified.\n");
        return 1;
    }

    pat = argv[optind++];
    re = pcre_compile( pat, PCRE_CASELESS, &pat_error, &pat_error_offset, NULL );

    if (re == NULL) {
        fprintf(stderr, "Syntax error in PCRE pattern at offset: %d: %s\n",
                pat_error_offset, pat_error );
        return 1;
    }

    if (optind >= argc || (argc - optind == 1 && strcmp(argv[optind],"-") == 0)) {
        fastq_grep(stdin, stdout, mismatch_file, re);
    }
    else {
        for (; optind < argc; optind++) {
            fin = fopen(argv[optind], "rb");
            if (fin == NULL) {
                fprintf(stderr, "No such file '%s'.\n", argv[optind]);
                continue;
            }

            fastq_grep(fin, stdout, mismatch_file, re);

            fclose(fin);
        }
    }

    pcre_free(re);
    if (mismatch_file) fclose(mismatch_file);

    return 0;
}



