/*
 * vim: ai ts=4 sts=4 sw=4 cinoptions=>4 expandtab
 */
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>
#include <string.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <sched.h>

#define PROCMAP_SIZE 4096

/* how many runs to average by default */
#define DEFAULT_NR_LOOPS 10

/* we have 3 tests at the moment */
#define MAX_TESTS 3

/* default block size for test 2, in bytes */
#define DEFAULT_BLOCK_SIZE 262144

/* test types */
#define TEST_MEMCPY 0
#define TEST_DUMB 1
#define TEST_MCBLOCK 2

/* version number */
#define VERSION "1.4"

#define MYMEMSET(a, x, sz)                          \
    do                                              \
    {                                               \
        for (int __memi = 0; __memi < sz; __memi++) \
        {                                           \
            ((char *)a)[__memi] = x;                \
        }                                           \
    } while (0)

/*
 * MBW memory bandwidth benchmark
 *
 * 2006, 2012 Andras.Horvath@gmail.com
 * 2013 j.m.slocum@gmail.com
 * (Special thanks to Stephen Pasich)
 *
 * http://github.com/raas/mbw
 *
 * compile with:
 *			gcc -O -o mbw mbw.c
 *
 * run with eg.:
 *
 *			./mbw 300
 *
 * or './mbw -h' for help
 *
 * watch out for swap usage (or turn off swap)
 */

void usage()
{
    printf("mbw memory benchmark v%s, https://github.com/raas/mbw\n", VERSION);
    printf("Usage: mbw [options] array_size_in_MiB\n");
    printf("Options:\n");
    printf("	-n: number of runs per test (0 to run forever)\n");
    printf("	-a: Don't display average\n");
    printf("	-t%d: memcpy test\n", TEST_MEMCPY);
    printf("	-t%d: dumb (b[i]=a[i] style) test\n", TEST_DUMB);
    printf("	-t%d: memcpy test with fixed block size\n", TEST_MCBLOCK);
    printf("	-b <size>: block size in bytes for -t2 (default: %d)\n", DEFAULT_BLOCK_SIZE);
    printf("	-q: quiet (print statistics only)\n");
    printf("	-p: number of worker processes (default to 1)\n");
    printf("	-r: number of inner repeats on each test round (default to 3)\n");
    printf("	-f: speecify how each process is pinned in format of 0:3,6,7,8:2:16\n");
    printf("(will then use two arrays, watch out for swapping)\n");
    printf("'Bandwidth' is amount of data copied over the time this operation took.\n");
    printf("\nThe default is to run all tests available.\n");
}

/* ------------------------------------------------------ */

/* allocate a test array and fill it with data
 * so as to force Linux to _really_ allocate it */
long *make_array(unsigned long long asize)
{
    unsigned long long t;
    unsigned int long_size = sizeof(long);
    long *a;

    a = calloc(asize, long_size);

    if (NULL == a)
    {
        perror("Error allocating memory");
        exit(1);
    }

    /* make sure both arrays are allocated, fill with pattern */
    for (t = 0; t < asize; t++)
    {
        a[t] = 0xaa;
    }
    return a;
}

/* actual benchmark */
/* asize: number of type 'long' elements in test arrays
 * long_size: sizeof(long) cached
 * type: 0=use memcpy, 1=use dumb copy loop (whatever GCC thinks best)
 *
 * return value: elapsed time in seconds
 */
double worker(unsigned long long asize, long *a, long *b, int type, unsigned long long block_size, int repeats)
{
    unsigned long long t;
    struct timeval starttime, endtime;
    double te;
    unsigned int long_size = sizeof(long);
    /* array size in bytes */
    unsigned long long array_bytes = asize * long_size;

    gettimeofday(&starttime, NULL);
    for (int rep = 0; rep < repeats; rep++)
    {
        if (type == TEST_MEMCPY)
        { /* memcpy test */
            memcpy(b, a, array_bytes);
        }
        else if (type == TEST_MCBLOCK)
        { /* memcpy block test */
            char *aa = (char *)a;
            char *bb = (char *)b;
            for (t = array_bytes; t >= block_size; t -= block_size, aa += block_size)
            {
                bb = mempcpy(bb, aa, block_size);
            }
            if (t)
            {
                bb = mempcpy(bb, aa, t);
            }
        }
        else if (type == TEST_DUMB)
        { /* dumb test */
            volatile long *va = a, *vb = b;
            for (t = 0; t < asize; t++)
            {
                vb[t] = va[t];
            }
        }
    }
    gettimeofday(&endtime, NULL);

    te = ((double)(endtime.tv_sec * 1000000 - starttime.tv_sec * 1000000 + endtime.tv_usec - starttime.tv_usec)) / 1000000;

    return te;
}

/* ------------------------------------------------------ */

/* pretty print worker's output in human-readable terms */
/* te: elapsed time in seconds
 * mt: amount of transferred data in MiB
 * type: see 'worker' above
 *
 * return value: -
 */
void printout(double te, double mt, int type)
{
    switch (type)
    {
    case TEST_MEMCPY:
        printf("Method: MEMCPY\t");
        break;
    case TEST_DUMB:
        printf("Method: DUMB\t");
        break;
    case TEST_MCBLOCK:
        printf("Method: MCBLOCK\t");
        break;
    }
    printf("Elapsed: %.5f\t", te);
    printf("MiB: %.5f\t", mt);
    printf("Copy: %.3f MiB/s\n", mt / te);
    return;
}

double gettimedelta(struct timeval starttime, struct timeval endtime)
{
    return ((double)(endtime.tv_sec * 1000000 - starttime.tv_sec * 1000000 + endtime.tv_usec - starttime.tv_usec)) / 1000000;
}

int parse_cpu_affinity_str(int *cpu_pinno, const char *cpu_pinstr)
{
    int *pcpu_pinno = cpu_pinno;
    const char *p = cpu_pinstr;
    int start = -1, end = -1, step = -1;
    while (*p)
    {
        int t = 0;
        while (isdigit(*p))
        {
            t = t * 10 + (*p - '0');
            p++;
        }
        if (*p == ':' || *p == ',' || *p == '\0')
        {
            if (start == -1)
            {
                start = t;
            }
            else if (end == -1)
            {
                end = t;
            }
            else if (step == -1)
            {
                step = end;
                end = t;
            }
            else
                return -1;
            if (*p != ':')
            {
                for (int i = start; i <= end; i += (step >= 1 ? step : 1))
                    *(++pcpu_pinno) = i;
                start = end = step = -1;
            }
            if (!*p)
                break;
            p++;
        }
        else
        {
            return -1;
        }
    }
    return pcpu_pinno - cpu_pinno;
}

/* ------------------------------------------------------ */

int main(int argc, char **argv)
{
    int nr_procs = 1;
    int nr_repeats = 3;
    unsigned int long_size = 0;
    double te, te_sum;            /* time elapsed */
    unsigned long long asize = 0; /* array size (elements in array) */
    int i;
    long *a, *b; /* the two arrays to be copied from/to */
    int o;       /* getopt options */
    unsigned long testno;
    char *cpu_pinstr = NULL;
    int cpu_pinno[PROCMAP_SIZE];

    /* options */

    /* how many runs to average? */
    int nr_loops = DEFAULT_NR_LOOPS;
    /* fixed memcpy block size for -t2 */
    unsigned long long block_size = DEFAULT_BLOCK_SIZE;
    /* show average, -a */
    int showavg = 1;
    /* what tests to run (-t x) */
    int tests[MAX_TESTS];
    double mt = 0; /* MiBytes transferred == array size in MiB */
    int quiet = 0; /* suppress extra messages */

    tests[0] = 0;
    tests[1] = 0;
    tests[2] = 0;

    memset(cpu_pinno, 0, sizeof(cpu_pinno));

    while ((o = getopt(argc, argv, "haqn:t:b:p:r:f:")) != EOF)
    {
        switch (o)
        {
        case 'h':
            usage();
            exit(1);
            break;
        case 'a': /* suppress printing average */
            showavg = 0;
            break;
        case 'n': /* no. loops */
            nr_loops = strtoul(optarg, (char **)NULL, 10);
            break;
        case 't': /* test to run */
            testno = strtoul(optarg, (char **)NULL, 10);
            if (testno > MAX_TESTS - 1)
            {
                printf("Error: test number must be between 0 and %d\n", MAX_TESTS - 1);
                exit(1);
            }
            tests[testno] = 1;
            break;
        case 'b': /* block size in bytes*/
            block_size = strtoull(optarg, (char **)NULL, 10);
            if (0 >= block_size)
            {
                printf("Error: what block size do you mean?\n");
                exit(1);
            }
            break;
        case 'q': /* quiet */
            quiet = 1;
            break;
        case 'p': /* no. procs */
            nr_procs = strtoul(optarg, (char **)NULL, 10);
            break;
        case 'r': /* no. repeats */
            nr_repeats = strtoul(optarg, (char **)NULL, 10);
            break;
        case 'f': /* cpu affinity string */
            cpu_pinstr = (char *)malloc(strlen(optarg) + 2);
            strcpy(cpu_pinstr, optarg);
            break;
        default:
            break;
        }
    }

    {
        if (!cpu_pinstr)
        {
            for (int i = 1; i <= nr_procs; i++)
            {
                cpu_pinno[i] = i - 1;
            }
        }
        else
        {
            int ret = parse_cpu_affinity_str(cpu_pinno, cpu_pinstr);
            if (ret != nr_procs)
            {
                printf("CPU affinity settings refers to %d CPUs, rather than %d.\n", ret, nr_procs);
                return 1;
            }
        }
        printf("The workers would be pinned to these cpus:\n");
        for (int i = 1; i <= nr_procs; i++)
            printf("%4d", cpu_pinno[i]);
        printf("\n");
    }

    /* default is to run all tests if no specific tests were requested */
    if ((tests[0] + tests[1] + tests[2]) == 0)
    {
        tests[0] = 1;
        tests[1] = 1;
        tests[2] = 1;
    }

    if (nr_loops == 0 && ((tests[0] + tests[1] + tests[2]) != 1))
    {
        printf("Error: nr_loops can be zero if only one test selected!\n");
        exit(1);
    }

    if (optind < argc)
    {
        mt = strtoul(argv[optind++], (char **)NULL, 10);
    }
    else
    {
        printf("Error: no array size given!\n");
        exit(1);
    }

    if (0 >= mt)
    {
        printf("Error: array size wrong!\n");
        exit(1);
    }

    /* ------------------------------------------------------ */

    long_size = sizeof(long);             /* the size of long on this platform */
    asize = 1024 * 1024 / long_size * mt; /* how many longs then in one array? */

    if (asize * long_size < block_size)
    {
        printf("Error: array size larger than block size (%llu bytes)!\n", block_size);
        exit(1);
    }

    if (!quiet)
    {
        printf("Long uses %d bytes. ", long_size);
        printf("Allocating 2*%lld elements = %lld bytes of memory.\n", asize, 2 * asize * long_size);
        if (tests[2])
        {
            printf("Using %lld bytes as blocks for memcpy block copy test.\n", block_size);
        }
    }

    /* ------------------------------------------------------ */
    if (!quiet)
    {
        printf("Getting down to business... Doing %d runs per test.\n", nr_loops);
    }

    volatile char *procmap = mmap(NULL, PROCMAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    MYMEMSET(procmap, 0, PROCMAP_SIZE);

    volatile double(**mpresults)[MAX_TESTS] = NULL;
    if (nr_loops)
    {
        mpresults = malloc(sizeof(void *) * (nr_procs + 1));
        for (int i = 1; i <= nr_procs; i++)
        {
            mpresults[i] = (double(*)[MAX_TESTS])mmap(NULL, sizeof(double) * MAX_TESTS * (nr_loops + 1),
                                                      PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
        }
    }

    int procno = 0;
    for (int i = 1; i <= nr_procs; i++)
    {
        int forkret = fork();
        if (forkret == 0)
        {
            procno = i;
            break;
        }
        else if (forkret < 0)
        {
            MYMEMSET(procmap, -1, PROCMAP_SIZE);
            perror("error forking workers");
            exit(1);
            return 1;
        }
    }
    if (procno <= 0)
    { // Controller process
        struct timeval starttime, endtime;

        printf("Ensure all process can respond simultaneously after 1s.\n");
        usleep(1000000);
        gettimeofday(&starttime, NULL);
        for (int i = 1; i <= nr_procs; i++)
            procmap[i] = 1;
        for (int i = 1; i <= nr_procs; i++)
            while (procmap[i] == 1)
                ;
        gettimeofday(&endtime, NULL);
        printf("Syncing all workers cost %4.3lf seconds.\nIf that's too long, the result should be considered unreliable.\n", gettimedelta(starttime, endtime));

        printf("Pre-allocate memory after 1s.\n");
        usleep(1000000);
        gettimeofday(&starttime, NULL);
        for (int i = 1; i <= nr_procs; i++)
            procmap[i] = 3;
        for (int i = 1; i <= nr_procs; i++)
        {
            while (procmap[i] == 3)
                ;
            if (procmap[i] < 0)
            {
                MYMEMSET(procmap, -1, PROCMAP_SIZE);
                printf("Worker %d failed to allocate memory. Exiting...\n", i);
                exit(1);
            }
        }
        gettimeofday(&endtime, NULL);
        printf("Pre-allocating memory cost %4.3lf seconds.\n", gettimedelta(starttime, endtime));

        printf("Run tests after 2s.\n");
        usleep(2000000);
        gettimeofday(&starttime, NULL);
        for (int i = 1; i <= nr_procs; i++)
            procmap[i] = 5;
        if (!nr_loops)
            return 0;
        for (int i = 1; i <= nr_procs; i++)
            while (procmap[i] == 5)
                ;
        gettimeofday(&endtime, NULL);
        double total_run_time = gettimedelta(starttime, endtime);

        double(*speedsum)[MAX_TESTS] = malloc(sizeof(double) * nr_procs * MAX_TESTS);
        double(*speedsqsum)[MAX_TESTS] = malloc(sizeof(double) * nr_procs * MAX_TESTS);
        double *idletime = malloc(sizeof(double) * nr_procs);
        for (int i = 1; i <= nr_procs; i++)
        {
            double worker_run_time = 0;
            printf("Worker #%d\n", i);
            for (int testno = 0; testno < MAX_TESTS; testno++)
            {
                for (int j = 0; j <= nr_loops; j++)
                {
                    printf("%8.3lf\t", mpresults[i][j][testno]);
                }
                worker_run_time += mpresults[i][0][testno] * nr_loops;
                printf("\n");
            }
            for (int testno = 0; testno < MAX_TESTS; testno++)
            {
                speedsum[i - 1][testno] = 0;
                speedsqsum[i - 1][testno] = 0;
                for (int j = 0; j <= nr_loops; j++)
                {
                    double speed = mt * nr_repeats / mpresults[i][j][testno];
                    printf("%8.3lf\t", speed);
                    if (j)
                    {
                        speedsum[i - 1][testno] += speed;
                        speedsqsum[i - 1][testno] += speed * speed;
                    }
                }
                printf("\n");
            }
            idletime[i - 1] = total_run_time - worker_run_time;
            printf("Worker idle time: %8.3lf\n", idletime[i - 1]);
        }

        {
            printf("\nSpeed, std-dev and idletime:\n");
            for (int testno = 0; testno < MAX_TESTS; testno++)
            {
                for (int i = 0; i < nr_procs; i++)
                {
                    printf(" %7.2lf", speedsum[i][testno] / nr_loops);
                }
                printf(" |");
                for (int i = 0; i < nr_procs; i++)
                {
                    double std_dev = nr_procs > 1 ? sqrt((speedsqsum[i][testno] - speedsum[i][testno] * speedsum[i][testno] / nr_loops) / (nr_procs - 1)) : 0;
                    printf(" %7.2lf", std_dev);
                }
                printf("\n");
            }
            /*
            printf("debug:\n");
            for (int testno = 0; testno < MAX_TESTS; testno++)
            {
                for (int i = 0; i < nr_procs; i++)
                {
                    printf("  speedsum[%d][%d]=%7.2lf\n", i, testno, speedsum[i][testno]);
                    printf("  speedssq[%d][%d]=%7.2lf\n", i, testno, speedsqsum[i][testno]);
                }
            }
            */
            printf("\nTotal speed:\n");
            for (int testno = 0; testno < MAX_TESTS; testno++)
            {
                double sum = 0, sumsq = 0;
                for (int i = 0; i < nr_procs; i++)
                {
                    sum += speedsum[i][testno];
                    sumsq += speedsqsum[i][testno];
                }
                double std_dev = nr_procs > 1 ? sqrt((sumsq - sum * sum / (nr_loops * nr_procs)) / (nr_procs - 1)) : 0;
                printf("%7.2lf %7.2lf   ", sum, std_dev);
            }
            printf("\n");
            for (int i = 0; i < nr_procs; i++)
            {
                printf(" %7.2lf", idletime[i]);
            }
            printf("\n");
        }
        printf("All tests done in %10.3lf seconds\n\n", total_run_time);
    }
    else
    { // Worker process
        cpu_set_t cur_proc_cpu_set;
        CPU_ZERO(&cur_proc_cpu_set);
        CPU_SET(cpu_pinno[procno], &cur_proc_cpu_set);
        sched_setaffinity(0, sizeof(cur_proc_cpu_set), &cur_proc_cpu_set);
        while (procmap[procno] == 0)
            ;
        if (procmap[procno] < 0)
            exit(1);
        //We expect procmap[procno]==1 here.
        procmap[procno] = 2;
        while (procmap[procno] == 2)
            ;
        if (procmap[procno] < 0)
            exit(1);
        a = make_array(asize);
        b = make_array(asize);
        procmap[procno] = 4;
        while (procmap[procno] == 4)
            ;
        if (procmap[procno] < 0)
            exit(1);

        double(*results)[MAX_TESTS] = NULL;
        if (nr_loops)
            results = (double(*))mpresults[procno];
        /* run all tests requested, the proper number of times */
        for (testno = 0; testno < MAX_TESTS; testno++)
        {
            te_sum = 0;
            if (tests[testno])
            {
                for (i = 0; nr_loops == 0 || i < nr_loops; i++)
                {
                    te = worker(asize, a, b, testno, block_size, nr_repeats);
                    te_sum += te;
                    if (!quiet)
                    {
                        printf("worker %d\t%d\t", procno, i);
                        printout(te, mt * nr_repeats, testno);
                    }
                    //putchar('.');
                    if (nr_loops)
                        results[i + 1][testno] = te;
                }
                if (showavg && !quiet)
                {
                    printf("worker %d\tAVG\t", procno);
                    printout(te_sum / nr_loops, mt * nr_repeats, testno);
                }
                results[0][testno] = te_sum / nr_loops;
            }
        }
        procmap[procno] = 6;
        exit(0);
    }

    return 0;
}
