/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil -*- */
/*
 * Copyright (c) 2004-2005 The Trustees of Indiana University and Indiana
 *                         University Research and Technology
 *                         Corporation.  All rights reserved.
 * Copyright (c) 2004-2016 The University of Tennessee and The University
 *                         of Tennessee Research Foundation.  All rights
 *                         reserved.
 * Copyright (c) 2004-2005 High Performance Computing Center Stuttgart,
 *                         University of Stuttgart.  All rights reserved.
 * Copyright (c) 2004-2005 The Regents of the University of California.
 *                         All rights reserved.
 * Copyright (c) 2006-2017 Cisco Systems, Inc.  All rights reserved
 * Copyright (c) 2009-2012 Oracle and/or its affiliates.  All rights reserved.
 * Copyright (c) 2012      Oak Ridge National Labs.  All rights reserved.
 * Copyright (c) 2015-2017 Los Alamos National Security, LLC.  All rights
 *                         reserved.
 * Copyright (c) 2018      FUJITSU LIMITED.  All rights reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */
/**
 * @file
 *
 * Top-level description of requests
 */

#ifndef OMPI_REQUEST_H
#define OMPI_REQUEST_H

#include "ompi_config.h"
#include "mpi.h"
#include "opal/class/opal_free_list.h"
#include "opal/class/opal_pointer_array.h"
#include "opal/threads/condition.h"
#include "opal/threads/wait_sync.h"
#include "ompi/constants.h"

#include "ompi/runtime/ompi_spc.h"

BEGIN_C_DECLS

#include "request_types.h"


/**
 * Initialize a request.  This is a macro to avoid function call
 * overhead, since this is typically invoked in the critical
 * performance path (since requests may be re-used, it is possible
 * that we will have to initialize a request multiple times).
 */
#define OMPI_REQUEST_INIT(request, persistent)                  \
    do {                                                        \
        (request)->req_complete =                               \
            (persistent) ? REQUEST_COMPLETED : REQUEST_PENDING; \
        (request)->req_state = OMPI_REQUEST_INACTIVE;           \
        (request)->req_persistent = (persistent);               \
        (request)->req_complete_cb  = NULL;                     \
        (request)->req_complete_cb_data = NULL;                 \
    } while (0);


#define REQUEST_COMPLETE(req)        (REQUEST_COMPLETED == (req)->req_complete)
/**
 * Finalize a request.  This is a macro to avoid function call
 * overhead, since this is typically invoked in the critical
 * performance path (since requests may be re-used, it is possible
 * that we will have to finalize a request multiple times).
 *
 * When finalizing a request, if MPI_Request_f2c() was previously
 * invoked on that request, then this request was added to the f2c
 * table, and we need to remove it
 *
 * This function should be called only from the MPI layer. It should
 * never be called from the PML. It take care of the upper level clean-up.
 * When the user call MPI_Request_free we should release all MPI level
 * ressources, so we have to call this function too.
 */
#define OMPI_REQUEST_FINI(request)                                      \
do {                                                                    \
    (request)->req_state = OMPI_REQUEST_INVALID;                        \
    if (MPI_UNDEFINED != (request)->req_f_to_c_index) {                 \
        opal_pointer_array_set_item(&ompi_request_f_to_c_table,         \
                                    (request)->req_f_to_c_index, NULL); \
        (request)->req_f_to_c_index = MPI_UNDEFINED;                    \
    }                                                                   \
} while (0);


/**
 * Globals used for tracking requests and request completion.
 */
OMPI_DECLSPEC extern opal_pointer_array_t   ompi_request_f_to_c_table;
OMPI_DECLSPEC extern ompi_predefined_request_t        ompi_request_null;
OMPI_DECLSPEC extern ompi_predefined_request_t        *ompi_request_null_addr;
OMPI_DECLSPEC extern ompi_request_t         ompi_request_empty;
OMPI_DECLSPEC extern ompi_status_public_t   ompi_status_empty;
OMPI_DECLSPEC extern ompi_request_fns_t     ompi_request_functions;

/**
 * Initialize the MPI_Request subsystem; invoked during MPI_INIT.
 */
int ompi_request_init(void);

/**
 * Shut down the MPI_Request subsystem; invoked during MPI_FINALIZE.
 */
int ompi_request_finalize(void);

/**
 * Create a persistent request that does nothing (e.g., to MPI_PROC_NULL).
 */
int ompi_request_persistent_noop_create(ompi_request_t **request);

/**
 * Cancel a pending request.
 */
static inline int ompi_request_cancel(ompi_request_t* request)
{
    if (request->req_cancel != NULL) {
        return request->req_cancel(request, true);
    }
    return OMPI_SUCCESS;
}

/**
 * Free a request.
 *
 * @param request (INOUT)   Pointer to request.
 */
static inline int ompi_request_free(ompi_request_t** request)
{
    return (*request)->req_free(request);
}

#define ompi_request_test       (ompi_request_functions.req_test)
#define ompi_request_test_any   (ompi_request_functions.req_test_any)
#define ompi_request_test_all   (ompi_request_functions.req_test_all)
#define ompi_request_test_some  (ompi_request_functions.req_test_some)
#define ompi_request_wait       (ompi_request_functions.req_wait)
#define ompi_request_wait_any   (ompi_request_functions.req_wait_any)
#define ompi_request_wait_all   (ompi_request_functions.req_wait_all)
#define ompi_request_wait_some  (ompi_request_functions.req_wait_some)

/**
 * Wait a particular request for completion
 */

static inline void ompi_request_wait_completion(ompi_request_t *req)
{
    if (opal_using_threads () && !REQUEST_COMPLETE(req)) {
        SPC_RECORD(OMPI_SPC_REQUEST_WAIT_COMPLETION_THREADS, 1);
        void *_tmp_ptr = REQUEST_PENDING;
        ompi_wait_sync_t sync;

        WAIT_SYNC_INIT(&sync, 1);

        if (OPAL_ATOMIC_COMPARE_EXCHANGE_STRONG_PTR(&req->req_complete, &_tmp_ptr, &sync)) {
            SYNC_WAIT(&sync);
        } else {
            /* completed before we had a chance to swap in the sync object */
            WAIT_SYNC_SIGNALLED(&sync);
        }

        assert(REQUEST_COMPLETE(req));
        WAIT_SYNC_RELEASE(&sync);
    } else {
        while(!REQUEST_COMPLETE(req)) {
            SPC_RECORD(OMPI_SPC_OPAL_PROGRESS, 1);
            double start_time = MPI_Wtime();

            opal_progress();

            SPC_RECORD(OMPI_SPC_OPAL_PROGRESS_TIME, (int) ((MPI_Wtime() - start_time) * 1e9));
        }
    }
}

/**
 *  Signal or mark a request as complete. If with_signal is true this will
 *  wake any thread pending on the request. If with_signal is false, the
 *  opposite will be true, the request will simply be marked as completed
 *  and no effort will be made to correctly (atomically) handle the associated
 *  synchronization primitive. This is a special case when the function
 *  is called from the critical path for small messages, where we know
 *  the current execution flow created the request, and no synchronized wait
 *  has been set.
 *  BEWARE: The error code should be set on the request prior to calling
 *  this function, or the synchronization primitive might not be correctly
 *  triggered.
 */
static inline int ompi_request_complete(ompi_request_t* request, bool with_signal)
{
    int rc = 0;

    if( NULL != request->req_complete_cb) {
        rc = request->req_complete_cb( request );
        request->req_complete_cb = NULL;
    }

    if (0 == rc) {
        if( OPAL_LIKELY(with_signal) ) {
            void *_tmp_ptr = REQUEST_PENDING;

            if(!OPAL_ATOMIC_COMPARE_EXCHANGE_STRONG_PTR(&request->req_complete, &_tmp_ptr, REQUEST_COMPLETED)) {
                ompi_wait_sync_t *tmp_sync = (ompi_wait_sync_t *) OPAL_ATOMIC_SWAP_PTR(&request->req_complete,
                                                                                       REQUEST_COMPLETED);
                /* In the case where another thread concurrently changed the request to REQUEST_PENDING */
                if( REQUEST_PENDING != tmp_sync )
                    wait_sync_update(tmp_sync, 1, request->req_status.MPI_ERROR);
            }
        } else
            request->req_complete = REQUEST_COMPLETED;
    }

    return OMPI_SUCCESS;
}

END_C_DECLS

#endif
