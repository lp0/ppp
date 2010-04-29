/*
 * print PPP statistics:
 * 	pppstats [-a|-d] [-v|-r|-s|-z] [-c count] [-w wait] <interface> [interface...]
 *
 *   -a Show absolute values rather than deltas
 *   -d Show data rate (kB/s) rather than bytes
 *   -v Show more stats for VJ TCP header compression
 *   -r Show compression ratio
 *   -s Show no VJ stats
 *   -z Show compression statistics instead of default display
 *
 * History:
 *      perkins@cps.msu.edu: Added compression statistics and alternate 
 *                display. 11/94
 *	Brad Parker (brad@cayman.com) 6/92
 *
 * from the original "slstats" by Van Jacobson
 *
 * Copyright (c) 1989 Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that the above copyright notice and this paragraph are
 * duplicated in all such forms and that any documentation,
 * advertising materials, and other materials related to such
 * distribution and use acknowledge that the software was developed
 * by the University of California, Berkeley.  The name of the
 * University may not be used to endorse or promote products derived
 * from this software without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#ifndef __STDC__
#define const
#endif

#ifndef lint
static const char rcsid[] = "$Id: pppstats.c,v 1.29 2002/10/27 12:56:26 fcusack Exp $";
#endif

#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <time.h>

#ifndef STREAMS
#if defined(__linux__) && defined(__powerpc__) \
    && (__GLIBC__ == 2 && __GLIBC_MINOR__ == 0)
/* kludge alert! */
#undef __GLIBC__
#endif
#include <sys/socket.h>		/* *BSD, Linux, NeXT, Ultrix etc. */
#ifndef __linux__
#include <net/if.h>
#include <net/ppp_defs.h>
#include <net/if_ppp.h>
#else
/* Linux */
#if __GLIBC__ >= 2
#include <asm/types.h>		/* glibc 2 conflicts with linux/types.h */
#include <net/if.h>
#else
#include <linux/types.h>
#include <linux/if.h>
#endif
#include <linux/ppp_defs.h>
#include <linux/if_ppp.h>
#endif /* __linux__ */

#else	/* STREAMS */
#include <sys/stropts.h>	/* SVR4, Solaris 2, SunOS 4, OSF/1, etc. */
#include <net/ppp_defs.h>
#include <net/pppio.h>

#endif	/* STREAMS */

int	vflag, rflag, sflag, zflag;	/* select type of display */
int	aflag;			/* print absolute values, not deltas */
int	dflag;			/* print data rates, not bytes */
int	interval, count;
int	infinite;
int	unit;
int	s;			/* socket or /dev/ppp file descriptor */
int	signalled;		/* set if alarm goes off "early" */
char	*progname;
char	**interfaces;
int	numintf;
#define MAXINTF 16

#if defined(SUNOS4) || defined(ULTRIX) || defined(NeXT)
extern int optind;
extern char *optarg;
#endif

/*
 * If PPP_DRV_NAME is not defined, use the legacy "ppp" as the
 * device name.
 */
#if !defined(PPP_DRV_NAME)
#define PPP_DRV_NAME    "ppp"
#endif /* !defined(PPP_DRV_NAME) */

static void usage __P((void));
static void catchalarm __P((int));
static int get_ppp_stats __P((struct ppp_stats *, int));
static int get_ppp_cstats __P((struct ppp_comp_stats *, int));
static void intpr __P((void));

int main __P((int, char *argv[]));

static void
usage()
{
    fprintf(stderr, "Usage: %s [-a|-d] [-v|-r|-s|-z] [-c count] [-w wait] <interface> [interface...]\n",
	    progname);
    exit(1);
}

/*
 * Called if an interval expires before intpr has completed a loop.
 * Sets a flag to not wait for the alarm.
 */
static void
catchalarm(arg)
    int arg;
{
    signalled = 1;
}


#ifndef STREAMS
static int
get_ppp_stats(curp, num)
    struct ppp_stats *curp;
    int num;
{
    struct ifpppstatsreq req;

    memset (&req, 0, sizeof (req));

#ifdef __linux__
    req.stats_ptr = (caddr_t) &req.stats;
#undef ifr_name
#define ifr_name ifr__name
#endif

    strncpy(req.ifr_name, interfaces[num], sizeof(req.ifr_name));
    if (ioctl(s, SIOCGPPPSTATS, &req) < 0) {
	return 0;
    }

    *curp = req.stats;
    return 1;
}

static int
get_ppp_cstats(csp, num)
    struct ppp_comp_stats *csp;
    int num;
{
    struct ifpppcstatsreq creq;

    memset (&creq, 0, sizeof (creq));

#ifdef __linux__
    creq.stats_ptr = (caddr_t) &creq.stats;
#undef  ifr_name
#define ifr_name ifr__name
#endif

    strncpy(creq.ifr_name, interfaces[num], sizeof(creq.ifr_name));
    if (ioctl(s, SIOCGPPPCSTATS, &creq) < 0) {
	return 0;
    }

#ifdef __linux__
    if (creq.stats.c.bytes_out == 0) {
	creq.stats.c.bytes_out = creq.stats.c.comp_bytes + creq.stats.c.inc_bytes;
	creq.stats.c.in_count = creq.stats.c.unc_bytes;
    }
    if (creq.stats.c.bytes_out == 0)
	creq.stats.c.ratio = 0.0;
    else
	creq.stats.c.ratio = 256.0 * creq.stats.c.in_count /
			     creq.stats.c.bytes_out;

    if (creq.stats.d.bytes_out == 0) {
	creq.stats.d.bytes_out = creq.stats.d.comp_bytes + creq.stats.d.inc_bytes;
	creq.stats.d.in_count = creq.stats.d.unc_bytes;
    }
    if (creq.stats.d.bytes_out == 0)
	creq.stats.d.ratio = 0.0;
    else
	creq.stats.d.ratio = 256.0 * creq.stats.d.in_count /
			     creq.stats.d.bytes_out;
#endif

    *csp = creq.stats;
    return 1;
}

#else	/* STREAMS */

int
strioctl(fd, cmd, ptr, ilen, olen)
    int fd, cmd, ilen, olen;
    char *ptr;
{
    struct strioctl str;

    str.ic_cmd = cmd;
    str.ic_timout = 0;
    str.ic_len = ilen;
    str.ic_dp = ptr;
    if (ioctl(fd, I_STR, &str) == -1)
	return -1;
    if (str.ic_len != olen)
	fprintf(stderr, "strioctl: expected %d bytes, got %d for cmd %x\n",
	       olen, str.ic_len, cmd);
    return 0;
}

static void
get_ppp_stats(curp)
    struct ppp_stats *curp;
{
    if (strioctl(s, PPPIO_GETSTAT, curp, 0, sizeof(*curp)) < 0) {
	fprintf(stderr, "%s: ", progname);
	if (errno == EINVAL)
	    fprintf(stderr, "kernel support missing\n");
	else
	    perror("couldn't get PPP statistics");
	exit(1);
    }
}

static void
get_ppp_cstats(csp)
    struct ppp_comp_stats *csp;
{
    if (strioctl(s, PPPIO_GETCSTAT, csp, 0, sizeof(*csp)) < 0) {
	fprintf(stderr, "%s: ", progname);
	if (errno == ENOTTY) {
	    fprintf(stderr, "no kernel compression support\n");
	    if (zflag)
		exit(1);
	    rflag = 0;
	} else {
	    perror("couldn't get PPP compression statistics");
	    exit(1);
	}
    }
}

#endif /* STREAMS */

#define MAX0(a)		((int)(a) > 0? (a): 0)
#define V(offset)	MAX0(cur[num].offset - old[num].offset)
#define W(offset)	MAX0(ccs[num].offset - ocs[num].offset)

#define RATIO(c, i, u)	((c) == 0? 1.0: (u) / ((double)(c) + (i)))
#define CRATE(x)	RATIO(W(x.comp_bytes), W(x.inc_bytes), W(x.unc_bytes))

#define KBPS(n)		((n) / (interval * 1024.0))

/*
 * Print a running summary of interface statistics.
 * Repeat display every interval seconds, showing statistics
 * collected over that interval.  Assumes that interval is non-zero.
 * First line printed is cumulative.
 */
static void
intpr()
{
    register int line = 0;
    sigset_t oldmask, mask;
    char *bunit;
    int ratef = 0;
    struct ppp_stats cur[MAXINTF], old[MAXINTF];
    struct ppp_comp_stats ccs[MAXINTF], ocs[MAXINTF];
    int ok[MAXINTF], num;
	struct timespec begin;
	clock_gettime(CLOCK_REALTIME, &begin);

for (num = 0; num < numintf; num++) {
    memset(&old[num], 0, sizeof(old[num]));
    memset(&ocs[num], 0, sizeof(ocs[num]));
}

    while (1) {
	struct timespec now;
	clock_gettime(CLOCK_REALTIME, &now);

#if 0
	(void)signal(SIGALRM, catchalarm);
	signalled = 0;
	(void)alarm(interval);
#endif

for (num = 0; num < numintf; num++) {
	ok[num] = get_ppp_stats(&cur[num], num);
	if (zflag || rflag)
	    ok[num] = ok && get_ppp_cstats(&ccs[num], num);
	if (!ok[num]) {
    		memset(&old[num], 0, sizeof(old[num]));
    		memset(&ocs[num], 0, sizeof(ocs[num]));
	}
}

	if ((line % 20) == 0) {
for (num = 0; num < numintf; num++) {
	    if (num != 0) { printf(" ⏐ "); }
	    if (zflag) {
		printf("%91s", interfaces[num]);
	    } else if (!sflag && !vflag) {
		printf("%93s", interfaces[num]);
	    } else if (!sflag && vflag) {
		printf("%129s", interfaces[num]);
	    } else if (!rflag) {
		printf("%39s", interfaces[num]);
	    } else {
		printf("%75s", interfaces[num]);
	    }
}
	    putchar('\n');
	    if (zflag) {
for (num = 0; num < numintf; num++) {
		if (num != 0) { printf(" ⏐ "); }
		printf("IN:      COMPRESSED      INCOMPRESSIBLE   COMP ⎸ ");
		printf("OUT:     COMPRESSED      INCOMPRESSIBLE   COMP");
}
		putchar('\n');
for (num = 0; num < numintf; num++) {
		if (num != 0) { printf(" ⏐ "); }
		bunit = dflag? "KB/S": "BYTE";
		printf("        %s   PACK         %s   PACK  RATIO ⎸ ", bunit, bunit);
		printf("        %s   PACK         %s   PACK  RATIO", bunit, bunit);
}
		putchar('\n');
	    } else {
for (num = 0; num < numintf; num++) {
		if (num != 0) { printf(" ⏐ "); }
		if (!sflag)
		    printf("%9.9s %8.8s %8.8s",
			   "IN", "PACK", "VJCOMP");
		else
		    printf("%9.9s %8.8s",
			   "IN", "PACK");

		if (!sflag && !rflag)
		    printf(" %8.8s %8.8s", "VJUNC", "VJERR");
		if (!sflag && vflag)
		    printf(" %8.8s %8.8s", "VJTOSS", "NON-VJ");
		if (rflag)
		    printf(" %8.8s %8.8s", "RATIO", "UBYTE");
		if (!sflag)
		    printf(" ⎸ %9.9s %8.8s %8.8s",
			   "OUT", "PACK", "VJCOMP");
		else
		    printf(" ⎸ %9.9s %8.8s",
			   "OUT", "PACK");

		if (!sflag && !rflag)
		    printf(" %8.8s %8.8s", "VJUNC", "NON-VJ");
		if (!sflag && vflag)
		    printf(" %8.8s %8.8s", "VJSRCH", "VJMISS");
		if (rflag)
		    printf(" %8.8s %8.8s", "RATIO", "UBYTE");
}
		putchar('\n');
	    }
	}

for (num = 0; num < numintf; num++) {
        if (num != 0) { printf(" ⏐ "); }
	if (zflag) {
if (!ok[num]) {
	printf("%9s %8s %9s %8s %6s", "-", "-", "-", "-", "-", "-");
	printf(" ⎸ %9s %8s %9s %8s %6s", "-", "-", "-", "-", "-", "-");
} else {
	    if (ratef) {
		printf("%9.3f %8u %9.3f %8u %6.2f",
		       KBPS(W(d.comp_bytes)),
		       W(d.comp_packets),
		       KBPS(W(d.inc_bytes)),
		       W(d.inc_packets),
		       ccs[num].d.ratio / 256.0);
		printf(" ⎸ %9.3f %8u %9.3f %8u %6.2f",
		       KBPS(W(c.comp_bytes)),
		       W(c.comp_packets),
		       KBPS(W(c.inc_bytes)),
		       W(c.inc_packets),
		       ccs[num].c.ratio / 256.0);
	    } else {
		printf("%9u %8u %9u %8u %6.2f",
		       W(d.comp_bytes),
		       W(d.comp_packets),
		       W(d.inc_bytes),
		       W(d.inc_packets),
		       ccs[num].d.ratio / 256.0);
		printf(" ⎸ %9u %8u %9u %8u %6.2f",
		       W(c.comp_bytes),
		       W(c.comp_packets),
		       W(c.inc_bytes),
		       W(c.inc_packets),
		       ccs[num].c.ratio / 256.0);
	    }
}
	} else {
if (!ok[num]) {
	printf("%9s %8s", "-", "-");
	if (!sflag) printf(" %8s", "-");
	if (!sflag && !rflag) printf(" %8s %8s", "-", "-");
	if (!sflag && vflag) printf(" %8s %8s", "-", "-");
	if (rflag) printf(" %8s %8s", "-", "-");

	printf(" ⎸ %9s %8s", "-", "-");
	if (!sflag) printf(" %8s", "-");
	if (!sflag && !rflag) printf(" %8s %8s", "-", "-");
	if (!sflag && vflag) printf(" %8s %8s", "-", "-");
	if (rflag) printf(" %8s %8s", "-", "-");
} else {
	    if (ratef)
		printf("%9.3f", KBPS(V(p.ppp_ibytes)));
	    else
		printf("%9u", V(p.ppp_ibytes));
	    if (!sflag)
		printf(" %8u %8u",
		       V(p.ppp_ipackets),
		       V(vj.vjs_compressedin));
	    else
		printf(" %8u",
		       V(p.ppp_ipackets));
	    if (!sflag && !rflag)
		printf(" %8u %8u",
		       V(vj.vjs_uncompressedin),
		       V(vj.vjs_errorin));
	    if (!sflag && vflag)
		printf(" %8u %8u",
		       V(vj.vjs_tossed),
		       V(p.ppp_ipackets) - V(vj.vjs_compressedin)
		       - V(vj.vjs_uncompressedin) - V(vj.vjs_errorin));
	    if (rflag) {
		printf(" %8.2f ", CRATE(d));
		if (ratef)
		    printf("%8.2f", KBPS(W(d.unc_bytes)));
		else
		    printf("%8u", W(d.unc_bytes));
	    }
	    if (ratef)
		printf(" ⎸ %9.3f", KBPS(V(p.ppp_obytes)));
	    else
		printf(" ⎸ %9u", V(p.ppp_obytes));
	    if (!sflag)
		printf(" %8u %8u",
		       V(p.ppp_opackets),
		       V(vj.vjs_compressed));
	    else
		printf(" %8u",
		       V(p.ppp_opackets));
	    if (!sflag && !rflag)
		printf(" %8u %8u",
		       V(vj.vjs_packets) - V(vj.vjs_compressed),
		       V(p.ppp_opackets) - V(vj.vjs_packets));
	    if (!sflag && vflag)
		printf(" %8u %8u",
		       V(vj.vjs_searches),
		       V(vj.vjs_misses));
	    if (rflag) {
		printf(" %8.2f ", CRATE(c));
		if (ratef)
		    printf("%8.2f", KBPS(W(c.unc_bytes)));
		else
		    printf("%8u", W(c.unc_bytes));
	    }
}
	}
}

	putchar('\n');
	fflush(stdout);
	line++;

	count--;
	if (!infinite && !count)
	    break;
#if 0
	sigemptyset(&mask);
	sigaddset(&mask, SIGALRM);
	sigprocmask(SIG_BLOCK, &mask, &oldmask);
	if (!signalled) {
	    sigemptyset(&mask);
	    sigsuspend(&mask);
	}
	sigprocmask(SIG_SETMASK, &oldmask, NULL);
	signalled = 0;
	(void)alarm(interval);
#endif

	if (!aflag) {
for (num = 0; num < numintf; num++) {
if (ok[num]) {
	    old[num] = cur[num];
	    ocs[num] = ccs[num];
}
}
	    ratef = dflag;
	}

	now.tv_sec += interval;
	now.tv_nsec = begin.tv_nsec;
	clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &now, NULL);
    }
}

int
main(argc, argv)
    int argc;
    char *argv[];
{
    int c;
#ifdef STREAMS
    char *dev;
#endif

/*
    interface = PPP_DRV_NAME "0";
*/
    if ((progname = strrchr(argv[0], '/')) == NULL)
	progname = argv[0];
    else
	++progname;

    while ((c = getopt(argc, argv, "advrszc:w:")) != -1) {
	switch (c) {
	case 'a':
	    ++aflag;
	    break;
	case 'd':
	    ++dflag;
	    break;
	case 'v':
	    ++vflag;
	    break;
	case 'r':
	    ++rflag;
	    break;
	case 's':
	    ++sflag;
	    break;
	case 'z':
	    ++zflag;
	    break;
	case 'c':
	    count = atoi(optarg);
	    if (count <= 0)
		usage();
	    break;
	case 'w':
	    interval = atoi(optarg);
	    if (interval <= 0)
		usage();
	    break;
	default:
	    usage();
	}
    }
    argc -= optind;
    argv += optind;

    if (!interval && count)
	interval = 5;
    if (interval && !count)
	infinite = 1;
    if (!interval && !count)
	count = 1;
    if (aflag)
	dflag = 0;

    if (argc > 1)
	usage();
    if (argc > 0)
	interface = argv[0];

    numintf = argc;
    if (numintf == 0 || numintf > MAXINTF)
	usage();
    interfaces = argv;

    if (sscanf(interface, PPP_DRV_NAME "%d", &unit) != 1) {
	fprintf(stderr, "%s: invalid interface '%s' specified\n",
		progname, interface);
    }

#ifndef STREAMS
    {
	struct ifreq ifr;

	s = socket(AF_INET, SOCK_DGRAM, 0);
	if (s < 0) {
	    fprintf(stderr, "%s: ", progname);
	    perror("couldn't create IP socket");
	    exit(1);
	}

#ifdef __linux__
#undef  ifr_name
#define ifr_name ifr_ifrn.ifrn_name
#endif
/*
	strncpy(ifr.ifr_name, interface, sizeof(ifr.ifr_name));
	if (ioctl(s, SIOCGIFFLAGS, (caddr_t)&ifr) < 0) {
	    fprintf(stderr, "%s: nonexistent interface '%s' specified\n",
		    progname, interface);
	    exit(1);
	}
*/
    }

#else	/* STREAMS */
#ifdef __osf__
    dev = "/dev/streams/ppp";
#else
    dev = "/dev/" PPP_DRV_NAME;
#endif
    if ((s = open(dev, O_RDONLY)) < 0) {
	fprintf(stderr, "%s: couldn't open ", progname);
	perror(dev);
	exit(1);
    }
    if (strioctl(s, PPPIO_ATTACH, &unit, sizeof(int), 0) < 0) {
	fprintf(stderr, "%s: ppp%d is not available\n", progname, unit);
	exit(1);
    }

#endif	/* STREAMS */

    intpr();
    exit(0);
}
