#include <rpc/rpc.h>
#include <arpa/inet.h>
#include <rpc/rpc_router_ioctl.h>
#include <debug.h>
#include <pthread.h>
#include <sys/select.h>

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>

#include <hardware_legacy/power.h>

#define ANDROID_WAKE_LOCK_NAME "rpc-interface"

void
grabPartialWakeLock() {
    acquire_wake_lock(PARTIAL_WAKE_LOCK, ANDROID_WAKE_LOCK_NAME);
}

void
releaseWakeLock() {
    release_wake_lock(ANDROID_WAKE_LOCK_NAME);
}

struct CLIENT {
    xdr_s_type *xdr;
    struct CLIENT *next;
    /* common attribute struct for setting up recursive mutexes */
    pthread_mutexattr_t lock_attr;

    /* We insist that there is only one outstanding call for a client at any
       time, and we use this mutex to enforce the rule.  When we start
       supporting multiple outstanding RPCs on a client, we will have to
       maintain a queue of them, and match incoming replies (by the XID of the
       incoming packet).  For now, we just block until we get that reply.
    */
    pthread_mutex_t lock;

    pthread_mutex_t wait_reply_lock;
    pthread_cond_t wait_reply;

    pthread_mutex_t input_xdr_lock;
    pthread_cond_t input_xdr_wait;
    volatile int input_xdr_busy;

    pthread_mutex_t wait_cb_lock;
    pthread_cond_t wait_cb;
    pthread_t cb_thread;
    volatile int got_cb;
    volatile int cb_stop;
};

extern void* svc_find(void *xprt, rpcprog_t prog, rpcvers_t vers);
extern void svc_dispatch(void *svc, void *xprt);
extern int  r_open();
extern void r_close();
extern xdr_s_type *xdr_init_common(const char *name, int is_client);
extern xdr_s_type *xdr_clone(xdr_s_type *);
extern void xdr_destroy_common(xdr_s_type *xdr);
extern bool_t xdr_recv_reply_header (xdr_s_type *xdr, rpc_reply_header *reply);
extern void *the_xprt;


static pthread_mutex_t rx_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_t rx_thread;
static volatile unsigned int num_clients;
static volatile fd_set rx_fdset;
static volatile int max_rxfd;
static volatile struct CLIENT *clients;

/* There's one of these for each RPC client which has received an RPC call. */
static void *cb_context(void *__u)
{
    CLIENT *client = (CLIENT *)__u;
    D("RPC-callback thread for %08x:%08x starting.\n",
      (client->xdr->x_prog | 0x01000000),
      client->xdr->x_vers);
    pthread_mutex_lock(&client->wait_cb_lock);
    while (client->cb_stop == 0) {
        if (!client->got_cb)
            pthread_cond_wait(&client->wait_cb,
                              &client->wait_cb_lock);
        /* We tell the thread it's time to exit by setting cb_stop to nonzero
           and signalling the conditional variable.  When there's no data, we
           skip to the top of the loop and exit. 
        */
        if (!client->got_cb) {
            D("RPC-callback thread for %08x:%08x: signalled but no data.\n",
              (client->xdr->x_prog | 0x01000000),
              client->xdr->x_vers);
            continue;
        }
        client->got_cb = 0;

        /* We dispatch the message to the server representing the callback
         * client.
         */
        if (the_xprt) {
            void *svc;
            rpcprog_t prog =
                ntohl(((uint32 *)(client->xdr->in_msg))[RPC_OFFSET+3]);
            rpcvers_t vers =
                ntohl(((uint32 *)(client->xdr->in_msg))[RPC_OFFSET+4]);
            
            svc = svc_find(the_xprt, prog, vers);
            if (svc) {
                XDR **svc_xdr = (XDR **)svc;
                D("%08x:%08x dispatching RPC call (XID %d, xdr %p) for "
                  "callback client %08x:%08x.\n",
                  client->xdr->x_prog,
                  client->xdr->x_vers,
                  ntohl(((uint32 *)(client->xdr->in_msg))[RPC_OFFSET]),
                  client->xdr,
                  (uint32_t)prog, (int)vers);
                /* We transplant the xdr of the client into the entry 
                   representing the callback client in the list of servers.
                   Note that since we hold the wait_cb_lock for this client,
                   if another call for this callback client arrives before
                   we've finished processing this call, that will block until
                   we're done with this one.  If this happens, it would be
                   most likely a bug in the arm9 rpc router.
                */
                if (*svc_xdr) {
                    D("%08x:%08x expecting XDR == NULL"
                      "callback client %08x:%08x!\n",
                      client->xdr->x_prog,
                      client->xdr->x_vers,
                      (uint32_t)prog, (int)vers);
                    xdr_destroy_common(*svc_xdr);
                }
                
                D("%08x:%08x cloning XDR for "
                  "callback client %08x:%08x.\n",
                  client->xdr->x_prog,
                  client->xdr->x_vers,
                  (uint32_t)prog, (int)vers);
                *svc_xdr = xdr_clone(client->xdr);
                
                (*svc_xdr)->x_prog = prog;
                (*svc_xdr)->x_vers = vers;
                memcpy((*svc_xdr)->in_msg,
                       client->xdr->in_msg, client->xdr->in_len);
                memcpy((*svc_xdr)->out_msg,
                       client->xdr->out_msg, client->xdr->out_next);
                (*svc_xdr)->in_len = client->xdr->in_len;
                (*svc_xdr)->out_next = client->xdr->out_next;

                pthread_mutex_lock(&client->input_xdr_lock);
                D("%08x:%08x marking input buffer as free.\n",
                  client->xdr->x_prog, client->xdr->x_vers);
                client->input_xdr_busy = 0;
                pthread_cond_signal(&client->input_xdr_wait);
                pthread_mutex_unlock(&client->input_xdr_lock);

                svc_dispatch(svc, the_xprt);
                xdr_destroy_common(*svc_xdr);
                *svc_xdr = NULL;
            }
            else E("%08x:%08x call packet arrived, but there's no "
                   "RPC server registered for %08x:%08x.\n",
                   client->xdr->x_prog,
                   client->xdr->x_vers,                   
                   (uint32_t)prog, (int)vers);                           
        }
        else E("%08x:%08x call packet arrived, but there's "
               "no RPC transport!\n",
               client->xdr->x_prog,
               client->xdr->x_vers);

        releaseWakeLock();
    }
    pthread_mutex_unlock(&client->wait_cb_lock);


    D("RPC-callback thread for %08x:%08x terminating.\n",
      (client->xdr->x_prog | 0x01000000),
      client->xdr->x_vers);
    return NULL;
}

static void *rx_context(void *__u __attribute__((unused)))
{
    int n;
    struct timeval tv;
    fd_set rfds;
    while(num_clients) {
        pthread_mutex_lock(&rx_mutex);
        rfds = rx_fdset;
        pthread_mutex_unlock(&rx_mutex);
        tv.tv_sec = 0; tv.tv_usec = 500 * 1000;
        n = select(max_rxfd + 1, (fd_set *)&rfds, NULL, NULL, &tv);
        if (n < 0) {
            E("select() error %s (%d)\n", strerror(errno), errno);
            continue;
        }

        if (n) {
            pthread_mutex_lock(&rx_mutex); /* sync access to the client list */
            CLIENT *client = (CLIENT *)clients;
            for (; client; client = client->next) {
                if (FD_ISSET(client->xdr->fd, &rfds)) {

                    /* We need to make sure that the XDR's in_buf is not in
                       use before we read into it.  The in_buf may be in use
                       in a race between processing an incoming call and
                       receiving a reply to an outstanding call, or processing
                       an incoming reply and receiving a call.
                    */

                    pthread_mutex_lock(&client->input_xdr_lock);
                    while (client->input_xdr_busy) {
                        D("%08x:%08x waiting for XDR input buffer "
                          "to be consumed.\n",
                          client->xdr->x_prog, client->xdr->x_vers);
                        pthread_cond_wait(
                            &client->input_xdr_wait,
                            &client->input_xdr_lock);                        
                    }
                    D("%08x:%08x reading data.\n",
                      client->xdr->x_prog, client->xdr->x_vers);
                    grabPartialWakeLock();
                    if (client->xdr->xops->read(client->xdr) == 0) {
                        E("%08x:%08x ONCRPC read error: aborting!\n",
                          client->xdr->x_prog, client->xdr->x_vers);
                        abort();
                    }
                    client->input_xdr_busy = 1;
                    pthread_mutex_unlock(&client->input_xdr_lock);

                    if (((uint32 *)(client->xdr->in_msg))[RPC_OFFSET+1] == 
                        htonl(RPC_MSG_REPLY)) {
                        /* Wake up the RPC client to receive its data. */
                        D("%08x:%08x received REPLY (XID %d), "
                          "grabbing mutex to wake up client.\n",
                          client->xdr->x_prog,
                          client->xdr->x_vers,
                          ntohl(((uint32 *)client->xdr->in_msg)[RPC_OFFSET]));
                        pthread_mutex_lock(&client->wait_reply_lock);
                        D("%08x:%08x got mutex, waking up client.\n",
                          client->xdr->x_prog,
                          client->xdr->x_vers);
                        pthread_cond_signal(&client->wait_reply);
                        pthread_mutex_unlock(&client->wait_reply_lock);

                        releaseWakeLock();
                    }
                    else {
                        pthread_mutex_lock(&client->wait_cb_lock);
                        D("%08x:%08x received CALL.\n",
                          client->xdr->x_prog,
                          client->xdr->x_vers);
                        client->got_cb = 1;
                        if (client->cb_stop < 0) {
                            D("%08x:%08x starting callback thread.\n",
                              client->xdr->x_prog,
                              client->xdr->x_vers);                            
                            client->cb_stop = 0;
                            pthread_create(&client->cb_thread,
                                           NULL,
                                           cb_context, client);
                        }
                        D("%08x:%08x waking up callback thread.\n",
                          client->xdr->x_prog,
                          client->xdr->x_vers);                            
                        pthread_cond_signal(&client->wait_cb);
                        pthread_mutex_unlock(&client->wait_cb_lock);
                    }
                }
            }
            pthread_mutex_unlock(&rx_mutex);
        }
        else {
            V("rx thread timeout (%d clients):\n", num_clients);
#if 0
            {
                CLIENT *trav = (CLIENT *)clients;
                for(; trav; trav = trav->next) {
                    if (trav->xdr)
                        V("\t%08x:%08x fd %02d\n",
                          trav->xdr->x_prog,
                          trav->xdr->x_vers,
                          trav->xdr->fd);
                    else V("\t(unknown)\n");
                }
            }
#endif
        }
    }
    D("RPC-client RX thread exiting!\n");
    return NULL;
}

enum clnt_stat
clnt_call(
    CLIENT       * client,
    u_long         proc,
    xdrproc_t      xdr_args,
    caddr_t        args_ptr,
    xdrproc_t      xdr_results,
    caddr_t        rets_ptr,
    struct timeval timeout)
{
    opaque_auth cred;
    opaque_auth verf;
    rpc_reply_header reply_header;
    enum clnt_stat ret = RPC_SUCCESS;

    xdr_s_type *xdr = client->xdr;

    pthread_mutex_lock(&client->lock);


    cred.oa_flavor = AUTH_NONE;
    cred.oa_length = 0;
    verf.oa_flavor = AUTH_NONE;
    verf.oa_length = 0;

    xdr->x_op = XDR_ENCODE;

    /* Send message header */

    if (!xdr_call_msg_start (xdr, xdr->x_prog, xdr->x_vers,
                             proc, &cred, &verf)) {
        XDR_MSG_ABORT (xdr);
        ret = RPC_CANTENCODEARGS; 
        E("%08x:%08x error in xdr_call_msg_start()\n",
          client->xdr->x_prog,
          client->xdr->x_vers);
        goto out;
    }

    /* Send arguments */

    if (!xdr_args (xdr, args_ptr)) {
        XDR_MSG_ABORT(xdr);
        ret = RPC_CANTENCODEARGS; 
        E("%08x:%08x error in xdr_args()\n",
          client->xdr->x_prog,
          client->xdr->x_vers);
        goto out;
    }

    /* Finish message - blocking */
    pthread_mutex_lock(&client->wait_reply_lock);
    D("%08x:%08x sending call (XID %d).\n",
      client->xdr->x_prog, client->xdr->x_vers, client->xdr->xid);
    if (!XDR_MSG_SEND(xdr)) {
        ret = RPC_CANTSEND;
        E("error in XDR_MSG_SEND\n");
        goto out_unlock;
    }

    D("%08x:%08x waiting for reply.\n",
      client->xdr->x_prog, client->xdr->x_vers);
    pthread_cond_wait(&client->wait_reply, &client->wait_reply_lock);
    D("%08x:%08x received reply.\n", client->xdr->x_prog, client->xdr->x_vers);

    if (((uint32 *)xdr->out_msg)[RPC_OFFSET] != 
        ((uint32 *)xdr->in_msg)[RPC_OFFSET]) {
        E("%08x:%08x XID mismatch: got %d, expecting %d.\n",
          client->xdr->x_prog, client->xdr->x_vers,
          ntohl(((uint32 *)xdr->in_msg)[RPC_OFFSET]),
          ntohl(((uint32 *)xdr->out_msg)[RPC_OFFSET]));
        ret = RPC_CANTRECV;
        goto out_unlock;
    }

    D("%08x:%08x decoding reply header.\n",
      client->xdr->x_prog, client->xdr->x_vers);
    if (!xdr_recv_reply_header (client->xdr, &reply_header)) {
        E("%08x:%08x error reading reply header.\n",
          client->xdr->x_prog, client->xdr->x_vers);
        ret = RPC_CANTRECV;
        goto out_unlock;
    }

    /* Check that other side accepted and responded */
    if (reply_header.stat != RPC_MSG_ACCEPTED) {
        /* Offset to map returned error into clnt_stat */
        ret = reply_header.u.dr.stat + RPC_VERSMISMATCH;
        E("%08x:%08x call was not accepted.\n",
          (uint32_t)client->xdr->x_prog, client->xdr->x_vers);
        goto out_unlock;
    } else if (reply_header.u.ar.stat != RPC_ACCEPT_SUCCESS) {
        /* Offset to map returned error into clnt_stat */
        ret = reply_header.u.ar.stat + RPC_AUTHERROR;
        E("%08x:%08x call failed with an authentication error.\n",
          (uint32_t)client->xdr->x_prog, client->xdr->x_vers);
        goto out_unlock;
    }

    xdr->x_op = XDR_DECODE;
    /* Decode results */
    if (!xdr_results(xdr, rets_ptr) || ! XDR_MSG_DONE(xdr)) {
        ret = RPC_CANTDECODERES;
        E("%08x:%08x error decoding results.\n",
          client->xdr->x_prog, client->xdr->x_vers);
        goto out_unlock;
    }

    D("%08x:%08x call success.\n",
      client->xdr->x_prog, client->xdr->x_vers);

  out_unlock:
    pthread_mutex_lock(&client->input_xdr_lock);
    D("%08x:%08x marking input buffer as free.\n",
      client->xdr->x_prog, client->xdr->x_vers);
    client->input_xdr_busy = 0;
    pthread_cond_signal(&client->input_xdr_wait);
    pthread_mutex_unlock(&client->input_xdr_lock);

    pthread_mutex_unlock(&client->wait_reply_lock);
  out:
    pthread_mutex_unlock(&client->lock);
    return ret;
} /* clnt_call */

bool_t xdr_recv_auth (xdr_s_type *xdr, opaque_auth *auth)
{
    switch(sizeof(auth->oa_flavor)) {
    case 1:
        if(!XDR_RECV_INT8(xdr, (int8_t *)&(auth->oa_flavor))) return FALSE;
        break;
    case 2:
        if(!XDR_RECV_INT16(xdr, (int16_t *)&(auth->oa_flavor))) return FALSE;
        break;
    case 4:
        if(!XDR_RECV_INT32(xdr, (int32_t *)&(auth->oa_flavor))) return FALSE;
        break;
    }
    if (!XDR_RECV_UINT (xdr, (unsigned *)&(auth->oa_length))) {
        return FALSE;
    }
    
    if (auth->oa_length != 0) {
        /* We throw away the auth stuff--it's always the default. */
        auth->oa_base = NULL;
        if (!XDR_RECV_BYTES (xdr, NULL, auth->oa_length))
            return FALSE;
        else
            return FALSE;
    }
    
    return TRUE;
} /* xdr_recv_auth */

static bool_t
xdr_recv_accepted_reply_header(xdr_s_type *xdr,
                               struct rpc_accepted_reply_header *accreply)
{
    if (!xdr_recv_auth(xdr, &accreply->verf)) {
        return FALSE;
    }

    if (!XDR_RECV_ENUM(xdr, &accreply->stat)) {
        return FALSE;
    }

    switch ((*accreply).stat) {
    case RPC_PROG_MISMATCH:
        if (!XDR_RECV_UINT32(xdr, &accreply->u.versions.low)) {
            return FALSE;
        }

        if (!XDR_RECV_UINT32(xdr, &accreply->u.versions.high)) {
            return FALSE;
        }
        break;

    case RPC_ACCEPT_SUCCESS:
    case RPC_PROG_UNAVAIL:
    case RPC_PROC_UNAVAIL:
    case RPC_GARBAGE_ARGS:
    case RPC_SYSTEM_ERR:
    case RPC_PROG_LOCKED:
        // case ignored
        break;

    default:
        return FALSE;
    }

    return TRUE;
} /* xdr_recv_accepted_reply_header */

static bool_t xdr_recv_denied_reply(xdr_s_type *xdr,
                                    struct rpc_denied_reply *rejreply)
{
    if (!XDR_RECV_ENUM (xdr, &rejreply->stat))
        return FALSE;

    switch ((*rejreply).stat) {
    case RPC_MISMATCH:
        if (!XDR_RECV_UINT32(xdr, &rejreply->u.versions.low))
            return FALSE;
        if (!XDR_RECV_UINT32(xdr, &rejreply->u.versions.high))
            return FALSE;
        break;
    case RPC_AUTH_ERROR:
        if (!XDR_RECV_ENUM (xdr, &rejreply->u.why))
            return FALSE;
        break;
    default:
        return FALSE;
    }

    return TRUE;
} /* xdr_recv_denied_reply */

bool_t xdr_recv_reply_header (xdr_s_type *xdr, rpc_reply_header *reply)
{
    if (!XDR_RECV_ENUM(xdr, &reply->stat)) {
        return FALSE;
    }

    switch ((*reply).stat) {
    case RPC_MSG_ACCEPTED:
        if (!xdr_recv_accepted_reply_header(xdr, &reply->u.ar))
            return FALSE;
        break;
    case RPC_MSG_DENIED:
        if (!xdr_recv_denied_reply(xdr, &reply->u.dr))
            return FALSE;
        break;
    default:
        return FALSE;
    }

    return TRUE;
} /* xdr_recv_reply_header */

CLIENT *clnt_create(
    char * host,
    uint32 prog,
    uint32 vers,
    char * proto)
{
    CLIENT *client = calloc(1, sizeof(CLIENT));
    if (client) {
        char name[256];

        /* for versions like 0x00010001, only compare against major version */
        if ((vers & 0xFFF00000) == 0)
            vers &= 0xFFFF0000;

        pthread_mutex_lock(&rx_mutex);

        snprintf(name, sizeof(name), "/dev/oncrpc/%08x:%08x",
                 (uint32_t)prog, (int)vers);
        client->xdr = xdr_init_common(name, 1 /* client XDR */);
        if (!client->xdr) {
            E("failed to initialize client (permissions?)!\n");
            free(client);
            pthread_mutex_unlock(&rx_mutex);
            return NULL;
        }
        client->xdr->x_prog = prog;
        client->xdr->x_vers = vers;
        client->cb_stop = -1; /* callback thread has not been started */

        if (!num_clients) {
            FD_ZERO(&rx_fdset);
            max_rxfd = 0;
        }

        FD_SET(client->xdr->fd, &rx_fdset);
        if (max_rxfd < client->xdr->fd)
            max_rxfd = client->xdr->fd;
        client->next = (CLIENT *)clients;
        clients = client;
        if (!num_clients++) {
            D("launching RX thread.\n");
            pthread_create(&rx_thread, NULL, rx_context, NULL);
        }

        pthread_mutexattr_init(&client->lock_attr);
//      pthread_mutexattr_settype(&client->lock_attr, PTHREAD_MUTEX_RECURSIVE);
        pthread_mutex_init(&client->lock, &client->lock_attr);
        pthread_mutex_init(&client->wait_reply_lock, &client->lock_attr);
        pthread_cond_init(&client->wait_reply, NULL);
        pthread_mutex_init(&client->wait_cb_lock, &client->lock_attr);
        pthread_cond_init(&client->wait_cb, NULL);
        pthread_mutex_init(&client->input_xdr_lock, &client->lock_attr);
        pthread_cond_init(&client->input_xdr_wait, NULL);

        pthread_mutex_unlock(&rx_mutex);
    }

    return client;
}

void clnt_destroy(CLIENT *client) {
    if (client) {
        pthread_mutex_lock(&client->lock);
        D("%08x:%08x destroying client\n",
          client->xdr->x_prog,
          client->xdr->x_vers);


        if (!client->cb_stop) {
            /* The callback thread is running, we need to stop it */
            client->cb_stop = 1;
            D("%08x:%08x stopping callback thread\n",
              client->xdr->x_prog,
              client->xdr->x_vers);
            pthread_mutex_lock(&client->wait_cb_lock);
            pthread_cond_signal(&client->wait_cb);
            pthread_mutex_unlock(&client->wait_cb_lock);
            D("%08x:%08x joining callback thread\n",
              client->xdr->x_prog,
              client->xdr->x_vers);
            pthread_join(client->cb_thread, NULL);
        }

        pthread_mutex_lock(&rx_mutex); /* sync access to the client list */
        {
            CLIENT *trav = (CLIENT *)clients, *prev = NULL;
            for(; trav; trav = trav->next) {
                if (trav == client) {
                     D("%08x:%08x removing from client list\n",
                      client->xdr->x_prog,
                      client->xdr->x_vers);
                    if (prev)
                        prev->next = trav->next;
                    else
                        clients = trav->next;
                    num_clients--;
                    FD_CLR(client->xdr->fd, &rx_fdset);
                    break;
                }
                prev = trav;
            }
        }
        if (!num_clients) {
            D("stopping rx thread!\n");
            pthread_join(rx_thread, NULL);
            D("stopped rx thread\n");
        }
        pthread_mutex_unlock(&rx_mutex); /* sync access to the client list */
 
        pthread_mutex_destroy(&client->input_xdr_lock);
        pthread_cond_destroy(&client->input_xdr_wait);

        pthread_mutex_destroy(&client->wait_reply_lock);
        pthread_cond_destroy(&client->wait_reply);
        xdr_destroy_common(client->xdr);

        // FIXME: what happens when we lock the client while destroying it,
        // and another thread locks the mutex in clnt_call, and then we 
        // call pthread_mutex_destroy?  Does destroy automatically unlock and
        // then cause the lock in clnt_call() to return an error?  When we
        // unlock the mutex here there can be a context switch to the other
        // thread, which will cause it to obtain the mutex on the destroyed
        // client (and probably crash), and then we get to the destroy call
        // here... will that call fail?
        pthread_mutex_unlock(&client->lock);        
        pthread_mutex_destroy(&client->lock);
        pthread_mutexattr_destroy(&client->lock_attr);
        D("client destroy done\n");
        free(client);
    }
}
