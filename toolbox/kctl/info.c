#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <inttypes.h>
#include <sys/types.h>

#include <kinetic/kinetic.h>
#include "kctl.h"

/* Cheating with globals */
static char	config, capacity, limits, msgs, ops, temps, utils, all, most;

/* used to dump getlog op stats */
extern char *ki_msgtype_label[];

void kctl_dump(kgetlog_t *glog);

#define CMD_USAGE(_ka) kctl_info_usage(_ka)
int
kctl_info_usage(struct kargs *ka)
{
        fprintf(stderr, "Usage: %s [..] %s [CMD OPTIONS]\n",
		ka->ka_progname, ka->ka_cmdstr);
	fprintf(stderr, "\nWhere, CMD OPTIONS are [default]:\n");
	fprintf(stderr, "\t-a           Show All [default]\n");
	fprintf(stderr, "\t-c           Show Configuartion\n");
	fprintf(stderr, "\t-C           Show Capacities\n");
	fprintf(stderr, "\t-L           Show Limits\n");
	fprintf(stderr, "\t-m           Show Most [-cCLOTU] (no messages)\n");
	fprintf(stderr, "\t-M           Show Messages (can be large)\n");
	fprintf(stderr, "\t-O           Show Operation Stats\n");
	fprintf(stderr, "\t-T           Show Tempratures\n");
	fprintf(stderr, "\t-U           Show Utilizations\n");
	fprintf(stderr, "\t-?           Help\n");
	fprintf(stderr, "\nTo see available COMMON OPTIONS: ./kctl -?\n");
}

/**
 *  Issue the GetLog command and dump its contents. 
 */
int
kctl_info(int argc, char *argv[], int kts, struct kargs *ka)
{
	extern char     *optarg;
        extern int	optind, opterr, optopt;
        char		c;
	kgetlog_t 	glog;
	kstatus_t 	kstatus;
	kgltype_t	glt[10];
	
	/* clear global flag vars */
	config = capacity = limits = msgs = 0;
	ops = temps = utils = all = most = 0;

	/* setup the glog and types vector */
	memset((void *)&glog, 0, sizeof(kgetlog_t));
	glog.kgl_type = glt;
	glog.kgl_typecnt = 0;
	
        while ((c = getopt(argc, argv, "acCLMmOTUh?")) != EOF) {
                switch (c) {
		case 'a':
			/* Everything */
			all = 1;
			break;
		case 'm':
			/* everything but messages */
			most = 1;
			break;
		case 'c':
			config = 1;
			glog.kgl_type[glog.kgl_typecnt++] = KGLT_CONFIGURATION;
			break;
		case 'C':
			capacity = 1;
			glog.kgl_type[glog.kgl_typecnt++] = KGLT_CAPACITIES;
			break;
		case 'L':
			limits = 1;
			glog.kgl_type[glog.kgl_typecnt++] = KGLT_LIMITS;
			break;
		case 'M':
			msgs = 1;
			glog.kgl_type[glog.kgl_typecnt++] = KGLT_MESSAGES;
			break;
		case 'O':
			ops = 1;
			glog.kgl_type[glog.kgl_typecnt++] = KGLT_STATISTICS;
			break;
		case 'T':
			temps = 1;
			glog.kgl_type[glog.kgl_typecnt++] = KGLT_TEMPERATURES;
			break;
		case 'U':
			utils = 1;
			glog.kgl_type[glog.kgl_typecnt++] = KGLT_UTILIZATIONS;
			break;
		case 'h':
                case '?':
                default:
                        CMD_USAGE(ka);
			return(-1);
		}
        }
	
	/* if no flags, set all, i.e. default to all */
	if (!all    && !most && !config && !capacity &&
	    !limits && !msgs && !ops    && !temps    && !utils)
		all = 1;
	
	/*
	 * Because the combo flags, all and most, can be combined with
	 * other flags, they take precedence over the individual types
	 * requested. So clear the types and push all the types represented
	 * by all and most
	 *
	 * (Keep in the following order, check 'most' then 'all'
	 * 'all' takes precedence over 'most'
	 */
	if (most) {
		config = capacity = limits = ops = temps = utils = 1;
		glog.kgl_typecnt = 0;
		glog.kgl_type[glog.kgl_typecnt++] = KGLT_CONFIGURATION;
		glog.kgl_type[glog.kgl_typecnt++] = KGLT_CAPACITIES;
		glog.kgl_type[glog.kgl_typecnt++] = KGLT_LIMITS;
		glog.kgl_type[glog.kgl_typecnt++] = KGLT_STATISTICS;
		glog.kgl_type[glog.kgl_typecnt++] = KGLT_TEMPERATURES;
		glog.kgl_type[glog.kgl_typecnt++] = KGLT_UTILIZATIONS;
	}

	/* Set all types */ 
	if (all) {
		config = capacity = limits = msgs = ops = temps = utils = 1;
		glog.kgl_typecnt = 0;
		glog.kgl_type[glog.kgl_typecnt++] = KGLT_CONFIGURATION;
		glog.kgl_type[glog.kgl_typecnt++] = KGLT_CAPACITIES;
		glog.kgl_type[glog.kgl_typecnt++] = KGLT_LIMITS;
		glog.kgl_type[glog.kgl_typecnt++] = KGLT_MESSAGES;
		glog.kgl_type[glog.kgl_typecnt++] = KGLT_STATISTICS;
		glog.kgl_type[glog.kgl_typecnt++] = KGLT_TEMPERATURES;
		glog.kgl_type[glog.kgl_typecnt++] = KGLT_UTILIZATIONS;
	}
	
	/* Shouldn't be any other args */
	if (argc - optind) {
		fprintf(stderr, "*** Too many args\n");
		CMD_USAGE(ka);
		return(-1);
	}

	/* Get the log */
	kstatus = ki_getlog(kts, &glog);
	
	if(!kstatus.ks_code) {
		printf("GetLog failed: %s\n", kstatus.ks_message);
		return(-1);
	}

	kctl_dump(&glog);
	
	return(0);
}

void
kctl_dump(kgetlog_t *glog)
{
	if (config) {
		int n;
		kconfiguration_t *cf = &glog->kgl_conf;
		kinterface_t *i = cf->kcf_interfaces;
		
		printf("Configuration:\n");
		printf("  %-18s: %s\n", "Vendor", cf->kcf_vendor);
		printf("  %-18s: %s\n", "Model", cf->kcf_model);
		printf("  %-18s: %s\n", "Serial",  cf->kcf_serial);
		printf("  %-18s: %s\n", "WWN", cf->kcf_wwn);

		printf("  %-18s:\n", "Interface(s)");
		for(n=0;n<cf->kcf_interfacescnt; n++)  {
			printf("    %-16s: %s\n", "Name", i[n].ki_name);
			printf("      %-14s: %s\n", "MAC",  i[n].ki_mac);
			printf("      %-14s: %s\n", "IPv4", i[n].ki_ipv4);
			printf("      %-14s: %s\n", "IPv6", i[n].ki_ipv6);
		}
		printf("  %-18s: %d\n", "Port", cf->kcf_port);
		printf("  %-18s: %d\n", "TLS Port", cf->kcf_tlsport);
		
		printf("  %-18s: %s\n", "Firmware Version", cf->kcf_version);
		printf("  %-18s: %s\n", "Compile Date", cf->kcf_compdate);
		printf("  %-18s: %s\n", "FW SRC Hash", cf->kcf_srchash);
		printf("  %-18s: %s\n", "Proto Ver", cf->kcf_proto);
		printf("  %-18s: %s\n", "Proto Compile Date",cf->kcf_protocompdate);
		printf("  %-18s: %s\n", "Proto SRC Hash", cf->kcf_protosrchash);
		printf("  %-18s: %d\n", "Power", cf->kcf_power);
		printf("\n");
	}

	if (capacity) {
		kcapacity_t *c = &glog->kgl_cap;
		printf("Capacity:\n");

		/* Note GB and division by multiples of 1000, not GiB(1024) */
		printf("  %-18s: %-7" PRIu64 "GB\n", "Total", c->kc_total/1000000000);
		printf("  %-18s: %f%% full\n", "Used", c->kc_used);
		printf("\n");
	}

	if (limits) {
		klimits_t *l = &glog->kgl_limits;
		printf("Limits: \n");
		printf("  %-18s: %d\n", "Max Key Len", l->kl_keylen);
		printf("  %-18s: %d\n", "Max Value Len", l->kl_vallen);
		printf("  %-18s: %d\n", "Max Version Len", l->kl_verlen);
		printf("  %-18s: %d\n", "Max DI ChkSum Len", l->kl_disumlen);
		printf("  %-18s: %d\n", "Max Msg Len", l->kl_msglen);
		printf("  %-18s: %d\n", "Max PIN Len", l->kl_pinlen);
		printf("  %-18s: %d\n", "Max Batch Len", l->kl_batlen);

		printf("  %-18s: %d\n", "Max Read QD", l->kl_pendrdcnt);
		printf("  %-18s: %d\n", "Max Write QD", l->kl_pendwrcnt);
		printf("  %-18s: %d\n", "Max Connections", l->kl_conncnt);
		printf("  %-18s: %d\n", "Max User IDs", l->kl_idcnt);
		printf("  %-18s: %d\n", "Max Keys/Range", l->kl_rangekeycnt);
		printf("  %-18s: %d\n", "Max Ops/Batch", l->kl_batopscnt);
		printf("  %-18s: %d\n", "Max Dels/Batch", l->kl_batdelcnt);
		printf("  %-18s: %d\n", "Max Active Batches", l->kl_devbatcnt);
		printf("\n");
	}

	if (utils) {
		kutilization_t *u = glog->kgl_util;
		int i;
		printf("Utilizations:\n");
		for (i=0; i<glog->kgl_utilcnt; i++) {
			printf("  %-18s: %.02f%%\n",
			       u[i].ku_name, u[i].ku_value);
		}
		printf("\n");
	}


	if (temps) {
		ktemperature_t *t = glog->kgl_temp;
		int i;
		printf("Temperatures:\n");
		for (i=0; i<glog->kgl_tempcnt; i++) {
			printf("  %-18s: %.0f\u00B0C\n",
			       t[i].kt_name, t[i].kt_cur);
			printf("  %-18s: %.0f/%.0f/%.0f\u00B0C\n",
			       "  Min/Max/Target", t[i].kt_min,
			       t[i].kt_max,t[i].kt_target);
		}
		printf("\n");
	}

	if (ops) {
		kstatistics_t *s = glog->kgl_stat;
		int i;
		printf(" Operation Statistics:\n");
		printf("  %24s  %8s %8s %8s\n",
		       "Message Type:", "Issued", "Bytes", "Latency");
		for (i=0; i<glog->kgl_statcnt; i++) {
			printf("  %24s: %8" PRId64 " %8" PRId64 " %8" PRId64 "\n",
			       ki_msgtype_label[s[i].ks_mtype],
			       s[i].ks_cnt, s[i].ks_bytes, s[i].ks_maxlatency);
		}
		printf("\n");
	}

	if (msgs) {
		printf("Messages:\n");
		printf("%s\n", glog->kgl_msgs);
		printf("\n");
	}
}

