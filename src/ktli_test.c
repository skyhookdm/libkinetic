#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <inttypes.h>
#include <endian.h>

#define ntohll(__x) be64toh((__x))
#define htonll(__x) htobe64((__x))

#include "ktli.h"

struct kargs {
	char		*ka_progname;
	//	kctl_command	ka_cmd;		// command, ex. info, get, put
	char		*ka_cmdstr;     // command ascii str, ex. "info", "get"
	char		*ka_key;	// key, raw unencoded buffer
	size_t		ka_keylen;	// key len, raw unencoded buffer
	char		*ka_val;	// value, raw unencoded buffer
	size_t		ka_vallen;	// value len, raw unencoded buffer
	int		ka_user;	// connection user ID 
	char		*ka_hmac;	// connection password for the user ID used
	char		*ka_host;	// connection host 
	char		*ka_port;	// connection port, ex 8123 (nonTLS), 8443 (TLS)
	int		ka_usessl;	// connection boolean to use or not use TLS
	unsigned int	ka_timeout;	// connection timeout
	int64_t		ka_clustervers;	// Client cluster version number,
					// must match server cluster version number
	int		ka_quiet;	// output ctl
	int		ka_terse;	// output ctl
	int		ka_verbose;	// output ctl
	int		ka_yes;		// answer yes to any prompts
} kargs;

void
usage()
{
	int i;
	
        fprintf(stderr, "Usage: %s [OPTIONS] <message>\n",
		kargs.ka_progname);
	fprintf(stderr, "\nWhere, CMD is any one of these:\n");


	fprintf(stderr, "\nWhere, OPTIONS are [default]:\n");
	fprintf(stderr,	"\t-h host      Hostname or IP address [%s]\n",
		kargs.ka_host);
	fprintf(stderr, "\t-p port      Port number [%s]\n", kargs.ka_port);
	fprintf(stderr, "\t-s           Use SSL [no]\n");
	fprintf(stderr, "\t-u id        User ID [%d]\n", kargs.ka_user);
	fprintf(stderr,	"\t-m hmac      HMAC Key [%s]\n", kargs.ka_hmac);
#if 0
	fprintf(stderr, "\t-c version   Client Cluster Version [0]\n");
	fprintf(stderr,	"\t-T timeout   Timeout in seconds [%d]\n",
		kargs.ka_timeout);
	fprintf(stderr, "\t-q           Be quiet [yes]\n");
	fprintf(stderr, "\t-t           Be terse [no]\n");
	fprintf(stderr, "\t-v           Be verbose [no]\n");
	fprintf(stderr, "\t-y           Automatic yes to prompts [no]\n");
	fprintf(stderr, "\t-?           Help\n");
	fprintf(stderr, "\nTo see available CMD OPTIONS: %s CMD -?\n",
		kargs.ka_progname);
#endif
        exit(2);
}

struct header {
	int32_t	h_len;
	int64_t	h_seq;
};

int64_t
msg_getseq(struct kiovec *msg, int msgcnt)
{
	struct header *h;
	h = msg[0].kiov_base;

	if (!h || (msg[0].kiov_len < sizeof(struct header))) {
		return(-1);
	}
	return(ntohll(h->h_seq));
}

void
msg_setseq(struct kiovec *msg, int msgcnt, int64_t seq)
{
	struct header *h;
	h = msg[0].kiov_base;

	if (!h || (msg[0].kiov_len < sizeof(struct header))) {
		printf("Bad Header\n");
		return;
	}
	h->h_seq = htonll(seq);
	return;
}

int32_t
msg_length(struct kiovec *msg)
{
	struct header *h;
	h = msg->kiov_base;

	if (!h || (ntohl(h->h_len) < sizeof(struct header))) {
		return(-1);
	}
	return(ntohl(h->h_len));
}

int
main(int argc, char *argv[])
{
	extern char     	*optarg;
        extern int		optind, opterr, optopt;
        char			c, *cp;
	int			i, ktd, rc, br, bw, seq;
	static struct kiovec	kiovec[2];
	static struct header	header, *h;
	static struct kio	kio, *kio2;
	static struct ktli_config  kcf;
	static struct ktli_helpers kh = 
		{ sizeof(struct header), msg_getseq, msg_setseq, msg_length };
	
	memset(&kargs, 0, sizeof(kargs));
	       
	kargs.ka_progname = argv[0];
	
        while ((c = getopt(argc, argv, "+c:h:m:p:su:tqvy?")) != EOF) {
                switch (c) {
		case 'h':
			kargs.ka_host = optarg;
			break;
		case 'm':
			kargs.ka_hmac = optarg;
			break;
		case 'p':
			kargs.ka_port = optarg;
			break;
                case 's':
                        kargs.ka_usessl = 1;
                        break;
		case 'u':
			kargs.ka_user = atoi(optarg);
			break;
#if 0
		case 'c':
			kargs.ka_clustervers = strtol(optarg, &cp, 0);
			if (!cp || *cp != '\0') {
				fprintf(stderr, "Invalid Cluster Version %s\n",
				       optarg);
				 usage();
			}
			kargs.ka_clustervers = (int64_t) atoi(optarg);
			break;
		case 'q':
                        kargs.ka_quiet = 1;
                        break;
                case 't':
                        kargs.ka_terse = 1;
                        break;
		case 'T':
			kargs.ka_timeout = atoi(optarg);
			break;
                case 'v':
                        kargs.ka_verbose = 1;
                        break;
                case 'y':
                        kargs.ka_yes = 1;
                        break;
#endif 
                case '?':
                        usage();
                        break;
                default:
			usage();
                        break;
                }
        }

	// Check for the cmd [key [value]] parms
	if (argc - optind != 1) {
		fprintf(stderr, "*** Too few or too many arguments\n");
		usage();
	}

	/* grab the message */
	kiovec[1].kiov_base = argv[optind];
	kiovec[1].kiov_len = strlen(argv[optind]) + 1;
	
	if (!kargs.ka_host || !kargs.ka_port) {
		fprintf(stderr, "*** Need host and port\n");
		usage();
	}

	kcf.kcfg_host = kargs.ka_host;
	kcf.kcfg_port = kargs.ka_port;
	kcf.kcfg_flags = KCFF_NOFLAGS;
	kcf.kcfg_pconf = NULL;

	ktd = ktli_open(KTLI_DRIVER_SOCKET, &kcf, &kh);
	if (ktd < 0) {
		perror("ktli_open: failed: ");
			exit(1);
	}

	printf("KTD: %d\n", ktd);

	rc = ktli_connect(ktd);
	if (rc < 0) {
		perror("ktli_connect: failed: ");
		exit(1);
	}

	header.h_len = htonl(sizeof(header) + kiovec[1].kiov_len); 
	header.h_seq = 0;  /* ktli will set this */

	kiovec[0].kiov_base = (void *)&header;
	kiovec[0].kiov_len = sizeof(header);    

	kio.kio_cmd = 0;
	kio.kio_seq = 0;  /* ktli will set this */
	kio.kio_sendmsg.km_msg = kiovec;
	kio.kio_sendmsg.km_cnt = 2;
	
	ktli_send(ktd, &kio);
	printf ("Sent Kio: %p\n", &kio);

	ktli_poll(ktd, 0);
	      
	br = ktli_receive(ktd, &kio);
	
	if (br < 0) {
		perror("ktli_receive failed:");
		exit(1);
	}
	printf ("Received Kio: %p\n", &kio);

	h = (struct header *)kio.kio_recvmsg.km_msg[0].kiov_base;

	//	if (kio.kio_state == KIO_FAILED) {
		
	printf("Length: %d\nSequence: %ld\nPayload: %s\n",
	       ntohl(h->h_len),
	       ntohll(h->h_seq),
	       (char *)kio.kio_recvmsg.km_msg[1].kiov_base);
	
	rc = ktli_disconnect(ktd);
	if (rc < 0) {
		perror("ktli_connect: failed: ");
		exit(1);
	}

	while (ktli_drain(ktd, &kio2)) {
		printf("Draining: %p\n", kio2);
	}
	
	rc = ktli_close(ktd);
	if (rc < 0) {
		perror("ktli_close: failed: ");
		exit(1);
	}
	
	exit(0);
}


