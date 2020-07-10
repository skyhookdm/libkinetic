
static int
gl_validate_req(kgetlog_t *glog)
{
	int i;

	errno = EINVAL;  /* assume we will find a problem */

	/* Check the the requested types */
	if (!glog->kgl_type || !glog->kgl_typecnt)
		return(-1);
	
	for (i=0; i<glog->kgl_typecnt; i++) {
		switch (glog->kgl_type[i]) {
		case KGLT_UTILIZATIONS:
		case KGLT_TEMPERATURES:
		case KGLT_CAPACITIES:		
		case KGLT_CONFIGURATION:	
		case KGLT_STATISTICS:		
		case KGLT_MESSAGES:		
		case KGLT_LIMITS:		
		case KGLT_LOG:
			/* a good type */
			break;
		default:
			return(-1);
		}
	}

	errno = 0;
	return(0);
}
	    
kstatus_t
ki_getlog(int ktd, kgetlog_t *glog)
{
	int rc;
	kstatus_t krc;
	
	/* Validate the passed in glog */
	rc = gl_validate_req(glog);
	if (rc < 0) {
		return(-1);
	}
	
	/* pack the message */
	kresult_message = create_getlog_message(glog);
	
	kiovec = (void *)pack_getlog_request(kresult_message);

	/* creat the kio structure */
	
	/* Send the request */
	ktli_send(ktd, &kio);
	printf ("Sent Kio: %p\n", &kio);

	/* Wait for the response */
	ktli_poll(ktd, 0);
	      
	/* Receive the response */
	/* PAK: need error handling */
	rc = ktli_receive(ktd, &kio);

	kresult_message = unpack_getlog_resp(void *), len);
	if ( kresult_message.result_code == FAILURE) {
		/* cleanup and return error */
	}

	kstatus = extract_getlog(kresult_message, &glog);


	
	kpc = kresult_message.result_message;

	krc.ks_code = kpc->status.code; 
	krc.ks_message = strdup(kpc->status.statusMessage); 
	krc.ks_detail = strdup(kpc->status.detailedMessage); 
	
	glog->kgl_limits.kl_keylen = kpc->body->getlog->limits->maxkeysize;	
	glog->kgl_limits.kl_keylen = kpc->body->getlog->messages.len
	glog->kgl_limits.kl_keylen = kpc->body->getlog->messages.data
	
	/* Validate and fixup the caller's passed in glog */

	return(0);
}
