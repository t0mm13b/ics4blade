/*
 * Sun RPC is a product of Sun Microsystems, Inc. and is provided for
 * unrestricted use provided that this legend is included on all tape
 * media and as a part of the software program in whole or part.  Users
 * may copy or modify Sun RPC without charge, but are not authorized
 * to license or distribute it to anyone else except as part of a product or
 * program developed by the user.
 *
 * SUN RPC IS PROVIDED AS IS WITH NO WARRANTIES OF ANY KIND INCLUDING THE
 * WARRANTIES OF DESIGN, MERCHANTIBILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE, OR ARISING FROM A COURSE OF DEALING, USAGE OR TRADE PRACTICE.
 *
 * Sun RPC is provided with no support and without any obligation on the
 * part of Sun Microsystems, Inc. to assist in its use, correction,
 * modification or enhancement.
 *
 * SUN MICROSYSTEMS, INC. SHALL HAVE NO LIABILITY WITH RESPECT TO THE
 * INFRINGEMENT OF COPYRIGHTS, TRADE SECRETS OR ANY PATENTS BY SUN RPC
 * OR ANY PART THEREOF.
 *
 * In no event will Sun Microsystems, Inc. be liable for any lost revenue
 * or profits or other special, indirect and consequential damages, even if
 * Sun has been advised of the possibility of such damages.
 *
 * Sun Microsystems, Inc.
 * 2550 Garcia Avenue
 * Mountain View, California  94043
 */

/*
 * clnt.h - Client side remote procedure call interface.
 *
 * Copyright (C) 1984, Sun Microsystems, Inc.
 */

#ifndef _RPC_CLNT_H
#define _RPC_CLNT_H 1

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/time.h>

/*
 * Rpc calls return an enum clnt_stat.  This should be looked at more,
 * since each implementation is required to live with this (implementation
 * independent) list of errors.
 */
enum clnt_stat {
  RPC_SUCCESS=0,      /* call succeeded */
  /*
   * local errors
   */
  RPC_CANTENCODEARGS=1,    /* can't encode arguments */
  RPC_CANTDECODERES=2,    /* can't decode results */
  RPC_CANTSEND=3,      /* failure in sending call */
  RPC_CANTRECV=4,      /* failure in receiving result */
  RPC_TIMEDOUT=5,      /* call timed out */
  /*
   * remote errors
   */
  RPC_VERSMISMATCH=6,    /* rpc versions not compatible */
  RPC_AUTHERROR=7,    /* authentication error */
  RPC_PROGUNAVAIL=8,    /* program not available */
  RPC_PROGVERSMISMATCH=9,    /* program version mismatched */
  RPC_PROCUNAVAIL=10,    /* procedure unavailable */
  RPC_CANTDECODEARGS=11,    /* decode arguments error */
  RPC_SYSTEMERROR=12,    /* generic "other problem" */
  RPC_NOBROADCAST = 21,    /* Broadcasting not supported */
  /*
   * callrpc & clnt_create errors
   */
  RPC_UNKNOWNHOST=13,    /* unknown host name */
  RPC_UNKNOWNPROTO=17,    /* unknown protocol */
  RPC_UNKNOWNADDR = 19,    /* Remote address unknown */

  /*
   * rpcbind errors
   */
  RPC_RPCBFAILURE=14,    /* portmapper failed in its call */
#define RPC_PMAPFAILURE RPC_RPCBFAILURE
  RPC_PROGNOTREGISTERED=15,  /* remote program is not registered */
  RPC_N2AXLATEFAILURE = 22,  /* Name to addr translation failed */
  /*
   * unspecified error
   */
  RPC_FAILED=16,
  RPC_INTR=18,
  RPC_TLIERROR=20,
  RPC_UDERROR=23,
  /*
   * asynchronous errors
   */
  RPC_INPROGRESS = 24,
  RPC_STALERACHANDLE = 25
};

struct CLIENT;
typedef struct CLIENT CLIENT;
/* client call callback. 
 * Callback called when the reply is recieved or there is an error in
 * getting reply.
 */
typedef void (*clnt_call_cb)
( 
  CLIENT * clnt, 
  void * cookie, 
  caddr_t results, 
  rpc_reply_header error
);

typedef void (*clnt_call_non_blocking_cb)
( 
  CLIENT * clnt, 
  void * cookie, 
  caddr_t results, 
  rpc_reply_header error
);

/*
 * By convention, procedure 0 takes null arguments and returns them
 */
#define NULLPROC ((rpcproc_t)0)

/*===========================================================================
FUNCTION CLNT_CALL

DESCRIPTION
  RPCGEN support routine. This routine is called by client routines generated
  by RPCGEN. It generates and sends an RPC message to a server.

  This is a blocking call.   

DEPENDENCIES
  None.

ARGUMENTS
  xdr - the XDR to use to send the RPC message
  proc - the server procedure to call
  xdr_args - function pointer for encoding the RPC message args
  args_ptr - pointer to args data structure
  xdr_results - function pointer for decoding the RPC response
  rets_ptr - pointer to results data structure
  timeout - return after timeout (ignored)

RETURN VALUE
  RPC_SUCCESS - if successful
  error code otherwise

SIDE EFFECTS
  None.
===========================================================================*/
extern enum clnt_stat 
clnt_call
( 
  CLIENT *h, 
  u_long proc,
  xdrproc_t xdr_args,
  caddr_t args_ptr,
  xdrproc_t xdr_results, 
  caddr_t rets_ptr,
  struct timeval timeout
);

/*===========================================================================
FUNCTION CLNT_CALL_NON_BLOCKING

DESCRIPTION
  RPCGEN support routine. This routine is called by client routines generated
  by RPCGEN. It generates and sends an RPC message to a server.

  This is a non-blocking call. It registers clnt_call_callback to be called
  when the RPC response is received.
  
DEPENDENCIES
  None.

ARGUMENTS
  xdr - the XDR to use to send the RPC message
  proc - the server procedure to call
  xdr_args - function pointer for encoding the RPC message args
  args_ptr - pointer to args data structure
  xdr_results - function pointer for decoding the RPC response
  results_size - size of the results data structure
  result_cb - function pointer to be called with the results
  cb_data - cookie for results call back function

RETURN VALUE
  RPC_SUCCESS - if successful
  error code otherwise

SIDE EFFECTS
  None.
===========================================================================*/
extern enum clnt_stat 
clnt_call_non_blocking
( 
  CLIENT *h,
  u_long proc,
  xdrproc_t xdr_args,
  caddr_t args_ptr,
  xdrproc_t xdr_results,
  int results_size,
  clnt_call_cb result_cb,
  void * cb_data
);

extern bool_t clnt_freeres( CLIENT *xdr, xdrproc_t xdr_res, caddr_t res_ptr );
extern void clnt_destroy( CLIENT *xdr );
extern CLIENT * clnt_create ( char * host, uint32 prog, uint32 vers,
                              char * proto);

#ifdef __cplusplus
}
#endif

#endif /* rpc/clnt.h */
