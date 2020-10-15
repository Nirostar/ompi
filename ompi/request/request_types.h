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

#ifndef OMPI_REQUEST_TYPES_H
#define OMPI_REQUEST_TYPES_H

#include "ompi_config.h"
#include "mpi.h"
#include "opal/class/opal_free_list.h"
#include "opal/class/opal_pointer_array.h"
#include "opal/threads/condition.h"
#include "opal/threads/wait_sync.h"
#include "ompi/constants.h"

#include "ompi/runtime/ompi_spc.h"

BEGIN_C_DECLS

/**
 * Request class
 */
OMPI_DECLSPEC OBJ_CLASS_DECLARATION(ompi_request_t);

/*
 * The following include pulls in shared typedefs with debugger plugins.
 * For more information on why we do this see the Notice to developers
 * comment at the top of the ompi_msgq_dll.c file.
 */

#include "request_dbg.h"

struct ompi_request_t;

/**
 * Initiate one or more persistent requests.
 *
 * This function is called by MPI_START and MPI_STARTALL.
 *
 * When called by MPI_START, count is 1.
 *
 * When called by MPI_STARTALL, multiple requests which have the same
 * req_start value are passed. This may help scheduling optimization
 * of multiple communications.
 *
 * @param count (IN)        Number of requests
 * @param requests (IN/OUT) Array of persistent requests
 * @return                  OMPI_SUCCESS or failure status.
 */
typedef int (*ompi_request_start_fn_t)(
    size_t count,
    struct ompi_request_t ** requests
);

/*
 * Required function to free the request and any associated resources.
 */
typedef int (*ompi_request_free_fn_t)(struct ompi_request_t** rptr);

/*
 * Optional function to cancel a pending request.
 */
typedef int (*ompi_request_cancel_fn_t)(struct ompi_request_t* request, int flag);

/*
 * Optional function called when the request is completed from the MPI
 * library perspective. This function is allowed to release the request if
 * the request will not be used with ompi_request_wait* or ompi_request_test.
 * If the function reposts (using start) a request or calls ompi_request_free()
 * on the request it *MUST* return 1. It should return 0 otherwise.
 */
typedef int (*ompi_request_complete_fn_t)(struct ompi_request_t* request);

/**
 * Forward declaration
 */
struct ompi_communicator_t;

/**
 * Forward declaration
 */
struct ompi_win_t;

/**
 * Forward declaration
 */
struct ompi_file_t;

/**
 * Union for holding several different MPI pointer types on the request
 */
typedef union ompi_mpi_object_t {
    struct ompi_communicator_t *comm;
    struct ompi_file_t *file;
    struct ompi_win_t *win;
} ompi_mpi_object_t;

/**
 * Main top-level request struct definition
 */
struct ompi_request_t {
    opal_free_list_item_t super;                /**< Base type */
    ompi_request_type_t req_type;               /**< Enum indicating the type of the request */
    ompi_status_public_t req_status;            /**< Completion status */
    volatile void *req_complete;                /**< Flag indicating wether request has completed */
    volatile ompi_request_state_t req_state;    /**< enum indicate state of the request */
    bool req_persistent;                        /**< flag indicating if the this is a persistent request */
    int req_f_to_c_index;                       /**< Index in Fortran <-> C translation array */
    ompi_request_start_fn_t req_start;          /**< Called by MPI_START and MPI_STARTALL */
    ompi_request_free_fn_t req_free;            /**< Called by free */
    ompi_request_cancel_fn_t req_cancel;        /**< Optional function to cancel the request */
    ompi_request_complete_fn_t req_complete_cb; /**< Called when the request is MPI completed */
    void *req_complete_cb_data;
    ompi_mpi_object_t req_mpi_object;           /**< Pointer to MPI object that created this request */
};

/**
 * Convenience typedef
 */
typedef struct ompi_request_t ompi_request_t;


/**
 * Padded struct to maintain back compatibiltiy.
 * See ompi/communicator/communicator.h comments with struct ompi_communicator_t
 * for full explanation why we chose the following padding construct for predefines.
 */
#define PREDEFINED_REQUEST_PAD 256

struct ompi_predefined_request_t {
    struct ompi_request_t request;
    char padding[PREDEFINED_REQUEST_PAD - sizeof(ompi_request_t)];
};

typedef struct ompi_predefined_request_t ompi_predefined_request_t;

/**
 * Non-blocking test for request completion.
 *
 * @param request (IN)   Array of requests
 * @param complete (OUT) Flag indicating if index is valid (a request completed).
 * @param status (OUT)   Status of completed request.
 * @return               OMPI_SUCCESS or failure status.
 *
 * Note that upon completion, the request is freed, and the
 * request handle at index set to NULL.
 */
typedef int (*ompi_request_test_fn_t)(ompi_request_t ** rptr,
                                      int *completed,
                                      ompi_status_public_t * status );
/**
 * Non-blocking test for request completion.
 *
 * @param count (IN)     Number of requests
 * @param request (IN)   Array of requests
 * @param index (OUT)    Index of first completed request.
 * @param complete (OUT) Flag indicating if index is valid (a request completed).
 * @param status (OUT)   Status of completed request.
 * @return               OMPI_SUCCESS or failure status.
 *
 * Note that upon completion, the request is freed, and the
 * request handle at index set to NULL.
 */
typedef int (*ompi_request_test_any_fn_t)(size_t count,
                                          ompi_request_t ** requests,
                                          int *index,
                                          int *completed,
                                          ompi_status_public_t * status);
/**
 * Non-blocking test for request completion.
 *
 * @param count (IN)      Number of requests
 * @param requests (IN)   Array of requests
 * @param completed (OUT) Flag indicating wether all requests completed.
 * @param statuses (OUT)  Array of completion statuses.
 * @return                OMPI_SUCCESS or failure status.
 *
 * This routine returns completed==true if all requests have completed.
 * The statuses parameter is only updated if all requests completed. Likewise,
 * the requests array is not modified (no requests freed), unless all requests
 * have completed.
 */
typedef int (*ompi_request_test_all_fn_t)(size_t count,
                                          ompi_request_t ** requests,
                                          int *completed,
                                          ompi_status_public_t * statuses);
/**
 * Non-blocking test for some of N requests to complete.
 *
 * @param count (IN)        Number of requests
 * @param requests (INOUT)  Array of requests
 * @param outcount (OUT)    Number of finished requests
 * @param indices (OUT)     Indices of the finished requests
 * @param statuses (OUT)    Array of completion statuses.
 * @return                  OMPI_SUCCESS, OMPI_ERR_IN_STATUS or failure status.
 *
 */
typedef int (*ompi_request_test_some_fn_t)(size_t count,
                                           ompi_request_t ** requests,
                                           int * outcount,
                                           int * indices,
                                           ompi_status_public_t * statuses);
/**
 * Wait (blocking-mode) for one requests to complete.
 *
 * @param request (IN)    Pointer to request.
 * @param status (OUT)    Status of completed request.
 * @return                OMPI_SUCCESS or failure status.
 *
 */
typedef int (*ompi_request_wait_fn_t)(ompi_request_t ** req_ptr,
                                      ompi_status_public_t * status);
/**
 * Wait (blocking-mode) for one of N requests to complete.
 *
 * @param count (IN)      Number of requests
 * @param requests (IN)   Array of requests
 * @param index (OUT)     Index into request array of completed request.
 * @param status (OUT)    Status of completed request.
 * @return                OMPI_SUCCESS or failure status.
 *
 */
typedef int (*ompi_request_wait_any_fn_t)(size_t count,
                                          ompi_request_t ** requests,
                                          int *index,
                                          ompi_status_public_t * status);
/**
 * Wait (blocking-mode) for all of N requests to complete.
 *
 * @param count (IN)      Number of requests
 * @param requests (IN)   Array of requests
 * @param statuses (OUT)  Array of completion statuses.
 * @return                OMPI_SUCCESS or failure status.
 *
 */
typedef int (*ompi_request_wait_all_fn_t)(size_t count,
                                          ompi_request_t ** requests,
                                          ompi_status_public_t * statuses);
/**
 * Wait (blocking-mode) for some of N requests to complete.
 *
 * @param count (IN)        Number of requests
 * @param requests (INOUT)  Array of requests
 * @param outcount (OUT)    Number of finished requests
 * @param indices (OUT)     Indices of the finished requests
 * @param statuses (OUT)    Array of completion statuses.
 * @return                  OMPI_SUCCESS, OMPI_ERR_IN_STATUS or failure status.
 *
 */
typedef int (*ompi_request_wait_some_fn_t)(size_t count,
                                           ompi_request_t ** requests,
                                           int * outcount,
                                           int * indices,
                                           ompi_status_public_t * statuses);

/**
 * Replaceable request functions
 */
typedef struct ompi_request_fns_t {
    ompi_request_test_fn_t      req_test;
    ompi_request_test_any_fn_t  req_test_any;
    ompi_request_test_all_fn_t  req_test_all;
    ompi_request_test_some_fn_t req_test_some;
    ompi_request_wait_fn_t      req_wait;
    ompi_request_wait_any_fn_t  req_wait_any;
    ompi_request_wait_all_fn_t  req_wait_all;
    ompi_request_wait_some_fn_t req_wait_some;
} ompi_request_fns_t;

END_C_DECLS

#endif
