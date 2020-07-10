#ifndef _KTLI_SESSION_H
#define _KTLI_SESSION_H


extern void kts_init();
extern int  kts_alloc_slot();
extern void kts_free_slot(int kts);

extern void kts_clear(int);
extern void kts_set(int kts,
		    struct ktli_driver *driver,
		    void *dhandle,
		    struct ktli_helpers *helpers,
		    struct ktli_queue *sendq,
		    struct ktli_queue *recvq,
		    struct ktli_queue *compq,
		    pthread_t sender,
		    pthread_t receiver,
		    enum ktli_sstate state,
		    int64_t sequence);

extern void kts_set_driver(int kts, struct ktli_driver *driver);
extern void kts_set_dhandle(int kts, void *dhandle);
extern void kts_set_helpers(int kts, struct ktli_helpers *kh);
extern void kts_set_sendq(int kts, struct ktli_queue *q);
extern void kts_set_recvq(int kts, struct ktli_queue *q);
extern void kts_set_compq(int kts, struct ktli_queue *q);
extern void kts_set_sender(int kts, pthread_t tid);
extern void kts_set_receiver(int kts, pthread_t tid);
extern void kts_set_state(int kts, enum ktli_sstate state);
extern void kts_set_sequence(int kts, int64_t sequence);

extern struct ktli_driver * kts_driver(int kts);
extern void * kts_dhandle(int kts);
extern struct ktli_helpers * kts_helpers(int kts);
extern struct ktli_queue * kts_sendq(int kts);
extern struct ktli_queue * kts_recvq(int kts);
extern struct ktli_queue * kts_compq(int kts);
extern pthread_t kts_sender(int kts);
extern pthread_t kts_receiver(int kts);
extern enum ktli_sstate kts_state(int kts);
extern int64_t kts_sequence(int kts);
extern int kts_max_sessions();
extern int kts_isvalid(int kts);

#endif /* _KTLI_SESSION_H */
