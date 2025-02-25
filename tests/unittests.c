/*
 * unittests.c
 *
 * Copyright (C) 2021 wolfSSL Inc.
 *
 * This file is part of wolfSentry.
 *
 * wolfSentry is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * wolfSentry is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1335, USA
 */

#define _GNU_SOURCE

#define WOLFSENTRY_SOURCE_ID WOLFSENTRY_SOURCE_ID_USER_BASE
#define WOLFSENTRY_ERROR_ID_UNIT_TEST_FAILURE WOLFSENTRY_ERROR_ID_USER_BASE

#include "src/wolfsentry_internal.h"

#include <stdlib.h>

#ifdef WOLFSENTRY_NO_STDIO
#define printf(...)
#endif

#ifdef WOLFSENTRY_THREADSAFE

#include <unistd.h>
#include <pthread.h>

#define WOLFSENTRY_EXIT_ON_FAILURE(...) do { wolfsentry_errcode_t _retval = (__VA_ARGS__); if (_retval < 0) { WOLFSENTRY_WARN(#__VA_ARGS__ ": " WOLFSENTRY_ERROR_FMT "\n", WOLFSENTRY_ERROR_FMT_ARGS(_retval)); exit(1); }} while(0)
#define WOLFSENTRY_EXIT_ON_SUCCESS(...) do { if ((__VA_ARGS__) == 0) { WOLFSENTRY_WARN(#__VA_ARGS__ " should have failed, but succeeded.\n"); exit(1); }} while(0)
#define WOLFSENTRY_EXIT_ON_FALSE(...) do { if (! (__VA_ARGS__)) { WOLFSENTRY_WARN(#__VA_ARGS__ " should have been true, but was false.\n"); exit(1); }} while(0)
#define WOLFSENTRY_EXIT_ON_TRUE(...) do { if (__VA_ARGS__) { WOLFSENTRY_WARN(#__VA_ARGS__ " should have been false, but was true.\n"); exit(1); }} while(0)
#define WOLFSENTRY_EXIT_ON_FAILURE_PTHREAD(...) do { int _pthread_ret; if ((_pthread_ret = (__VA_ARGS__)) != 0) { WOLFSENTRY_WARN(#__VA_ARGS__ ": %s\n", strerror(_pthread_ret)); exit(1); }} while(0)

#else /* !WOLFSENTRY_THREADSAFE */

#define WOLFSENTRY_EXIT_ON_FAILURE(...) do { wolfsentry_errcode_t _retval = (__VA_ARGS__); if (_retval < 0) { WOLFSENTRY_WARN(#__VA_ARGS__ ": " WOLFSENTRY_ERROR_FMT "\n", WOLFSENTRY_ERROR_FMT_ARGS(_retval)); return 1; }} while(0)
#define WOLFSENTRY_EXIT_ON_SUCCESS(...) do { if ((__VA_ARGS__) == 0) { WOLFSENTRY_WARN(#__VA_ARGS__ " should have failed, but succeeded.\n"); return 1; }} while(0)
#define WOLFSENTRY_EXIT_ON_FALSE(...) do { if (! (__VA_ARGS__)) { WOLFSENTRY_WARN(#__VA_ARGS__ " should have been true, but was false.\n"); return 1; }} while(0)
#define WOLFSENTRY_EXIT_ON_TRUE(...) do { if (__VA_ARGS__) { WOLFSENTRY_WARN(#__VA_ARGS__ " should have been false, but was true.\n"); return 1; }} while(0)

#endif /* WOLFSENTRY_THREADSAFE */

/* If not defined use default allocators */
#ifndef WOLFSENTRY_TEST_HPI
#  define WOLFSENTRY_TEST_HPI NULL
#else
extern struct wolfsentry_host_platform_interface* WOLFSENTRY_TEST_HPI;
#endif

#define TEST_SKIP(name) static int name (void) { printf("[  skipping " #name "  ]\n"); return 0; }


#ifdef TEST_INIT

static wolfsentry_errcode_t test_init (void) {
    struct wolfsentry_context *wolfsentry;
    struct wolfsentry_eventconfig config = { .route_private_data_size = 32, .max_connection_count = 10 };
    wolfsentry_errcode_t ret;

    ret = wolfsentry_init(WOLFSENTRY_TEST_HPI,
                          &config,
                          &wolfsentry);
    printf("wolfsentry_init() returns " WOLFSENTRY_ERROR_FMT "\n", WOLFSENTRY_ERROR_FMT_ARGS(ret));
    if (ret < 0)
        return ret;

    ret = wolfsentry_shutdown(&wolfsentry);
    printf("wolfsentry_shutdown() returns " WOLFSENTRY_ERROR_FMT "\n", WOLFSENTRY_ERROR_FMT_ARGS(ret));

    return ret;
}

#endif /* TEST_INIT */

#if defined(TEST_RWLOCKS)

#if defined(WOLFSENTRY_THREADSAFE)

#include <signal.h>

struct rwlock_args {
    struct wolfsentry_context *wolfsentry;
    volatile int *measured_sequence;
    volatile int *measured_sequence_i;
    int thread_id;
    struct wolfsentry_rwlock *lock;
    wolfsentry_time_t max_wait;
    pthread_mutex_t thread_phase_lock; /* need to wrap a mutex around thread_phase to blind the thread sanitizer to the spin locks on it. */
    volatile int thread_phase;
};

#define INCREMENT_PHASE(x) do { pthread_mutex_lock(&(x)->thread_phase_lock); ++(x)->thread_phase; pthread_mutex_unlock(&(x)->thread_phase_lock); } while(0)

static void *rd_routine(struct rwlock_args *args) {
    INCREMENT_PHASE(args);
    if (args->max_wait >= 0)
        WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_lock_shared_timed(args->wolfsentry, args->lock, args->max_wait));
    else
        WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_lock_shared(args->lock));
    INCREMENT_PHASE(args);
    int i = WOLFSENTRY_ATOMIC_POSTINCREMENT(*args->measured_sequence_i,1);
    args->measured_sequence[i] = args->thread_id;
    usleep(10000);
    i = WOLFSENTRY_ATOMIC_POSTINCREMENT(*args->measured_sequence_i,1);
    args->measured_sequence[i] = args->thread_id + 4;
    INCREMENT_PHASE(args);
    WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_lock_unlock(args->lock));
    INCREMENT_PHASE(args);
    return 0;
}

static void *wr_routine(struct rwlock_args *args) {
    INCREMENT_PHASE(args);
    if (args->max_wait >= 0)
        WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_lock_mutex_timed(args->wolfsentry, args->lock, args->max_wait));
    else
        WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_lock_mutex(args->lock));
    INCREMENT_PHASE(args);
    int i = WOLFSENTRY_ATOMIC_POSTINCREMENT(*args->measured_sequence_i,1);
    args->measured_sequence[i] = args->thread_id;
    usleep(10000);
    i = WOLFSENTRY_ATOMIC_POSTINCREMENT(*args->measured_sequence_i,1);
    args->measured_sequence[i] = args->thread_id + 4;
    INCREMENT_PHASE(args);
    WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_lock_unlock(args->lock));
    INCREMENT_PHASE(args);
    return 0;
}

static void *rd2wr_routine(struct rwlock_args *args) {
    INCREMENT_PHASE(args);
    if (args->max_wait >= 0)
        WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_lock_shared_timed(args->wolfsentry, args->lock, args->max_wait));
    else
        WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_lock_shared(args->lock));
    int i = WOLFSENTRY_ATOMIC_POSTINCREMENT(*args->measured_sequence_i,1);
    args->measured_sequence[i] = args->thread_id;
    INCREMENT_PHASE(args);
    if (args->max_wait >= 0)
        WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_lock_shared2mutex_timed(args->wolfsentry, args->lock, args->max_wait));
    else
        WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_lock_shared2mutex(args->lock));
    INCREMENT_PHASE(args);
    i = WOLFSENTRY_ATOMIC_POSTINCREMENT(*args->measured_sequence_i,1);
    args->measured_sequence[i] = args->thread_id + 4;
    WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_lock_unlock(args->lock));
    INCREMENT_PHASE(args);
    return 0;
}

static void *rd2wr_reserved_routine(struct rwlock_args *args) {
    INCREMENT_PHASE(args);
    if (args->max_wait >= 0)
        WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_lock_shared_timed(args->wolfsentry, args->lock, args->max_wait));
    else
        WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_lock_shared(args->lock)); // GCOV_EXCL_LINE
    int i = WOLFSENTRY_ATOMIC_POSTINCREMENT(*args->measured_sequence_i,1);
    args->measured_sequence[i] = args->thread_id;

    INCREMENT_PHASE(args);
    WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_lock_shared2mutex_reserve(args->lock));
    INCREMENT_PHASE(args);

    WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_lock_shared2mutex_redeem(args->lock));
    INCREMENT_PHASE(args);

    i = WOLFSENTRY_ATOMIC_POSTINCREMENT(*args->measured_sequence_i,1);
    args->measured_sequence[i] = args->thread_id + 4;
    WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_lock_unlock(args->lock));
    INCREMENT_PHASE(args);
    return 0;
}

#define MAX_WAIT 100000
#define WAIT_FOR_PHASE(x, atleast) do { int cur_phase; pthread_mutex_lock(&(x).thread_phase_lock); cur_phase = (x).thread_phase; pthread_mutex_unlock(&(x).thread_phase_lock); if (cur_phase >= (atleast)) break; usleep(1000); } while(1)

static int test_rw_locks (void) {
    struct wolfsentry_context *wolfsentry;
    struct wolfsentry_rwlock *lock;
    struct wolfsentry_eventconfig config = { .route_private_data_size = 32, .max_connection_count = 10 };

    (void)alarm(1);

    WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_init(WOLFSENTRY_TEST_HPI,
                                               &config,
                                               &wolfsentry));
    WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_lock_alloc(wolfsentry, &lock, 0 /* pshared */));

    volatile int measured_sequence[8], measured_sequence_i = 0;

    pthread_t thread1, thread2, thread3, thread4;
    struct rwlock_args thread1_args = {
        .wolfsentry = wolfsentry,
        .measured_sequence = measured_sequence,
        .measured_sequence_i = &measured_sequence_i,
        .lock = lock,
        .max_wait = -1
    }, thread2_args = thread1_args, thread3_args = thread1_args, thread4_args = thread1_args;

    thread1_args.thread_id = 1;
    thread2_args.thread_id = 2;
    thread3_args.thread_id = 3;
    thread4_args.thread_id = 4;

    WOLFSENTRY_EXIT_ON_FAILURE_PTHREAD(pthread_mutex_init(&thread1_args.thread_phase_lock, NULL));
    WOLFSENTRY_EXIT_ON_FAILURE_PTHREAD(pthread_mutex_init(&thread2_args.thread_phase_lock, NULL));
    WOLFSENTRY_EXIT_ON_FAILURE_PTHREAD(pthread_mutex_init(&thread3_args.thread_phase_lock, NULL));
    WOLFSENTRY_EXIT_ON_FAILURE_PTHREAD(pthread_mutex_init(&thread4_args.thread_phase_lock, NULL));


    WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_lock_mutex_timed(wolfsentry,lock,0));

    WOLFSENTRY_EXIT_ON_FAILURE_PTHREAD(pthread_create(&thread1, 0 /* attr */, (void *(*)(void *))rd_routine, (void *)&thread1_args));
    WOLFSENTRY_EXIT_ON_FAILURE_PTHREAD(pthread_create(&thread2, 0 /* attr */, (void *(*)(void *))rd_routine, (void *)&thread2_args));
    WOLFSENTRY_EXIT_ON_FAILURE_PTHREAD(pthread_create(&thread3, 0 /* attr */, (void *(*)(void *))wr_routine, (void *)&thread3_args));

    /* go to a lot of trouble to make sure thread 3 has entered _lock_mutex() wait. */
    WAIT_FOR_PHASE(thread3_args, 1);
    WOLFSENTRY_EXIT_ON_FAILURE_PTHREAD(pthread_kill(thread3, 0));
    usleep(10000);

    WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_lock_unlock(lock));

    WOLFSENTRY_EXIT_ON_FAILURE_PTHREAD(pthread_join(thread1, 0 /* retval */));

    WOLFSENTRY_EXIT_ON_FAILURE_PTHREAD(pthread_create(&thread4, 0 /* attr */, (void *(*)(void *))wr_routine, (void *)&thread4_args));
    WOLFSENTRY_EXIT_ON_FAILURE_PTHREAD(pthread_join(thread4, 0 /* retval */));

    WOLFSENTRY_EXIT_ON_FAILURE_PTHREAD(pthread_join(thread2, 0 /* retval */));
    WOLFSENTRY_EXIT_ON_FAILURE_PTHREAD(pthread_join(thread3, 0 /* retval */));

    /* the first write-locking thread must get the lock first after the parent unlocks,
     * because write lock requests have priority over read lock requests all else equal.
     * the second write-locking thread must get the lock last, because it is launched
     * after the first read lock thread has returned.  there is a race between the other read-locking thread
     * launching and the first write locking thread completing, but the usleep(10000) before the
     * parent unlock relaxes that race.  there is a second race between the first read-locking
     * thread returning and the other read-locking thread activating its read lock -- the second
     * write-locking thread can beat it by getting lock->sem first.  this race is relaxed with the
     * usleep(10000) in rd_routine().  the usleep(10000) in wr_routine() is just to catch lock
     * violations in the measured_sequence.
     *
     * the sequence of the two read-locking threads, sandwiched between the write-locking threads,
     * is undefined, and experimentally does vary.
     *
     */

    if ((measured_sequence[0] != 3) ||
        (measured_sequence[6] != 4) ||
        (measured_sequence[1] != 7) ||
        (measured_sequence[7] != 8)) {
    // GCOV_EXCL_START
        size_t i;
        fprintf(stderr,"wrong sequence at L%d.  should be {3,7,1,2,5,6,4,8} (the middle 4 are safely permutable), but got {", __LINE__);
        for (i = 0; i < sizeof measured_sequence / sizeof measured_sequence[0]; ++i)
            fprintf(stderr,"%d%s",measured_sequence[i], i == (sizeof measured_sequence / sizeof measured_sequence[0]) - 1 ? "}.\n" : ",");
        return 1;
    // GCOV_EXCL_STOP
    }


    /* now a scenario with shared2mutex and mutex2shared in the mix: */

    thread1_args.thread_phase = thread2_args.thread_phase = thread3_args.thread_phase = thread4_args.thread_phase = 0;

    measured_sequence_i = 0;

    WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_lock_mutex(lock));

    thread1_args.max_wait = MAX_WAIT; /* builtin wolfsentry_time_t is microseconds, same as usleep(). */
    WOLFSENTRY_EXIT_ON_FAILURE_PTHREAD(pthread_create(&thread1, 0 /* attr */, (void *(*)(void *))rd_routine, (void *)&thread1_args));

    WAIT_FOR_PHASE(thread1_args, 1);
    thread2_args.max_wait = MAX_WAIT;
    WOLFSENTRY_EXIT_ON_FAILURE_PTHREAD(pthread_create(&thread2, 0 /* attr */, (void *(*)(void *))rd2wr_routine, (void *)&thread2_args));

    WAIT_FOR_PHASE(thread2_args, 1);

    /* this transition advances thread1 and thread2 to both hold shared locks.
     * non-negligible chance that thread2 goes into shared2mutex wait before
     * thread1 can get a shared lock.
     */
    WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_lock_mutex2shared(lock));

    WAIT_FOR_PHASE(thread2_args, 2);

    /* this thread has to wait until thread2 is done with its shared2mutex sequence. */

/* constraint: thread2 must unlock (6) before thread3 locks (3) */
/* constraint: thread3 lock-unlock (3, 7) must be adjacent */
    thread3_args.max_wait = MAX_WAIT;
    WOLFSENTRY_EXIT_ON_FAILURE_PTHREAD(pthread_create(&thread3, 0 /* attr */, (void *(*)(void *))wr_routine, (void *)&thread3_args));

    WAIT_FOR_PHASE(thread3_args, 1);

    /* this one must fail, because at this point thread2 must be in shared2mutex wait. */
    WOLFSENTRY_EXIT_ON_FALSE(WOLFSENTRY_ERROR_CODE_IS(wolfsentry_lock_shared2mutex(lock), BUSY));

    /* take the opportunity to test expected failures of the _timed() variants. */
    WOLFSENTRY_EXIT_ON_FALSE(WOLFSENTRY_ERROR_CODE_IS(wolfsentry_lock_mutex_timed(wolfsentry,lock,0), BUSY));
    WOLFSENTRY_EXIT_ON_FALSE(WOLFSENTRY_ERROR_CODE_IS(wolfsentry_lock_mutex_timed(wolfsentry,lock,1000), TIMED_OUT));
    WOLFSENTRY_EXIT_ON_FALSE(WOLFSENTRY_ERROR_CODE_IS(wolfsentry_lock_shared_timed(wolfsentry,lock,0), BUSY));
    WOLFSENTRY_EXIT_ON_FALSE(WOLFSENTRY_ERROR_CODE_IS(wolfsentry_lock_shared_timed(wolfsentry,lock,1000), TIMED_OUT));

    /* this unlock allows thread2 to finally get its mutex. */
    WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_lock_unlock(lock));

    WOLFSENTRY_EXIT_ON_FAILURE_PTHREAD(pthread_join(thread1, 0 /* retval */));

/* constraint: thread2 must unlock (6) before thread4 locks (4) */
/* constraint: thread4 lock-unlock (4, 8) must be adjacent */
    WOLFSENTRY_EXIT_ON_FAILURE_PTHREAD(pthread_create(&thread4, 0 /* attr */, (void *(*)(void *))wr_routine, (void *)&thread4_args));
    WOLFSENTRY_EXIT_ON_FAILURE_PTHREAD(pthread_join(thread4, 0 /* retval */));

    WOLFSENTRY_EXIT_ON_FAILURE_PTHREAD(pthread_join(thread2, 0 /* retval */));
    WOLFSENTRY_EXIT_ON_FAILURE_PTHREAD(pthread_join(thread3, 0 /* retval */));

    int measured_sequence_transposed[8];
    {
        int i;
        for (i=0; i<8; ++i)
            measured_sequence_transposed[measured_sequence[i] - 1] = i + 1;
    }
#define SEQ(x) measured_sequence_transposed[(x)-1]
    if ((SEQ(6) > SEQ(3)) ||
        (SEQ(7) - SEQ(3) != 1) ||
        (SEQ(6) > SEQ(4)) ||
        (SEQ(8) - SEQ(4) != 1)) {
    // GCOV_EXCL_START
        size_t i;
        fprintf(stderr,"wrong sequence at L%d.  got {", __LINE__);
        for (i = 0; i < sizeof measured_sequence / sizeof measured_sequence[0]; ++i)
            fprintf(stderr,"%d%s",measured_sequence[i], i == (sizeof measured_sequence / sizeof measured_sequence[0]) - 1 ? "}.\n" : ",");
        return 1;
    // GCOV_EXCL_STOP
    }


    /* again, using shared2mutex reservation: */

    thread1_args.thread_phase = thread2_args.thread_phase = thread3_args.thread_phase = thread4_args.thread_phase = 0;

    measured_sequence_i = 0;

    WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_lock_mutex(lock));

    WOLFSENTRY_EXIT_ON_FALSE(WOLFSENTRY_ERROR_CODE_IS(wolfsentry_lock_shared2mutex(lock), ALREADY));
    WOLFSENTRY_EXIT_ON_FALSE(WOLFSENTRY_ERROR_CODE_IS(wolfsentry_lock_shared2mutex_reserve(lock), ALREADY));
    WOLFSENTRY_EXIT_ON_FALSE(WOLFSENTRY_ERROR_CODE_IS(wolfsentry_lock_shared2mutex_redeem(lock), ALREADY));
    WOLFSENTRY_EXIT_ON_FALSE(WOLFSENTRY_ERROR_CODE_IS(wolfsentry_lock_shared2mutex_abandon(lock), ALREADY));

    thread1_args.max_wait = MAX_WAIT; /* builtin wolfsentry_time_t is microseconds, same as usleep(). */
    WOLFSENTRY_EXIT_ON_FAILURE_PTHREAD(pthread_create(&thread1, 0 /* attr */, (void *(*)(void *))rd_routine, (void *)&thread1_args));

    WAIT_FOR_PHASE(thread1_args, 1);

    thread2_args.max_wait = MAX_WAIT;
    WOLFSENTRY_EXIT_ON_FAILURE_PTHREAD(pthread_create(&thread2, 0 /* attr */, (void *(*)(void *))rd2wr_reserved_routine, (void *)&thread2_args));

    WAIT_FOR_PHASE(thread2_args, 1);

    /* this transition advances thread1 and thread2 to both hold shared locks.
     * non-negligible chance that thread2 goes into shared2mutex wait before
     * thread1 can get a shared lock.
     */
    WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_lock_mutex2shared(lock));

    WAIT_FOR_PHASE(thread2_args, 3);

    /* this thread has to wait until thread2 is done with its shared2mutex sequence. */

/* constraint: thread2 must unlock (6) before thread3 locks (3) */
/* constraint: thread3 lock-unlock (3, 7) must be adjacent */
    thread3_args.max_wait = MAX_WAIT;
    WOLFSENTRY_EXIT_ON_FAILURE_PTHREAD(pthread_create(&thread3, 0 /* attr */, (void *(*)(void *))wr_routine, (void *)&thread3_args));

    WAIT_FOR_PHASE(thread3_args, 1);

    /* this one must fail, because at this point thread2 must be in shared2mutex wait. */
    WOLFSENTRY_EXIT_ON_FALSE(WOLFSENTRY_ERROR_CODE_IS(wolfsentry_lock_shared2mutex(lock), BUSY));

    /* take the opportunity to test expected failures of the _timed() variants. */
    WOLFSENTRY_EXIT_ON_FALSE(WOLFSENTRY_ERROR_CODE_IS(wolfsentry_lock_mutex_timed(wolfsentry,lock,0), BUSY));
    WOLFSENTRY_EXIT_ON_FALSE(WOLFSENTRY_ERROR_CODE_IS(wolfsentry_lock_mutex_timed(wolfsentry,lock,1000), TIMED_OUT));
    WOLFSENTRY_EXIT_ON_FALSE(WOLFSENTRY_ERROR_CODE_IS(wolfsentry_lock_shared_timed(wolfsentry,lock,0), BUSY));
    WOLFSENTRY_EXIT_ON_FALSE(WOLFSENTRY_ERROR_CODE_IS(wolfsentry_lock_shared_timed(wolfsentry,lock,1000), TIMED_OUT));

    WOLFSENTRY_EXIT_ON_FALSE(WOLFSENTRY_ERROR_CODE_IS(wolfsentry_lock_have_shared(lock), OK));
    WOLFSENTRY_EXIT_ON_FALSE(WOLFSENTRY_ERROR_CODE_IS(wolfsentry_lock_have_mutex(lock), NOT_OK));

    /* this unlock allows thread2 to finally get its mutex. */
    WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_lock_unlock(lock));

    WOLFSENTRY_EXIT_ON_FAILURE_PTHREAD(pthread_join(thread1, 0 /* retval */));

/* constraint: thread2 must unlock (6) before thread4 locks (4) */
/* constraint: thread4 lock-unlock (4, 8) must be adjacent */
    WOLFSENTRY_EXIT_ON_FAILURE_PTHREAD(pthread_create(&thread4, 0 /* attr */, (void *(*)(void *))wr_routine, (void *)&thread4_args));
    WOLFSENTRY_EXIT_ON_FAILURE_PTHREAD(pthread_join(thread4, 0 /* retval */));

    WOLFSENTRY_EXIT_ON_FAILURE_PTHREAD(pthread_join(thread2, 0 /* retval */));
    WOLFSENTRY_EXIT_ON_FAILURE_PTHREAD(pthread_join(thread3, 0 /* retval */));

    {
        int i;
        for (i=0; i<8; ++i)
            measured_sequence_transposed[measured_sequence[i] - 1] = i + 1;
    }
#define SEQ(x) measured_sequence_transposed[(x)-1]
    if ((SEQ(6) > SEQ(3)) ||
        (SEQ(7) - SEQ(3) != 1) ||
        (SEQ(6) > SEQ(4)) ||
        (SEQ(8) - SEQ(4) != 1)) {
    // GCOV_EXCL_START
        size_t i;
        fprintf(stderr,"wrong sequence at L%d.  got {", __LINE__);
        for (i = 0; i < sizeof measured_sequence / sizeof measured_sequence[0]; ++i)
            fprintf(stderr,"%d%s",measured_sequence[i], i == (sizeof measured_sequence / sizeof measured_sequence[0]) - 1 ? "}.\n" : ",");
        return 1;
    // GCOV_EXCL_STOP
    }


    /* cursory exercise of compound reservation calls. */
    WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_lock_mutex(lock));
    WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_lock_mutex2shared_and_reserve_shared2mutex(lock));
    WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_lock_shared2mutex_redeem(lock));
    WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_lock_unlock(lock));

    WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_lock_shared_and_reserve_shared2mutex(lock));
    WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_lock_shared2mutex_redeem(lock));
    WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_lock_unlock(lock));

    WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_lock_shared_timed_and_reserve_shared2mutex(wolfsentry, lock, 1000));
    WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_lock_shared2mutex_redeem_timed(wolfsentry, lock, 1000));
    WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_lock_unlock(lock));


    WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_lock_free(wolfsentry, &lock));

    WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_shutdown(&wolfsentry));

    (void)alarm(0);

    return 0;
}

#else

TEST_SKIP(test_rw_locks)

#endif /* WOLFSENTRY_THREADSAFE */

#endif /* TEST_RWLOCKS */

#ifdef TEST_STATIC_ROUTES

#ifdef LWIP
#include "lwip-socket.h"
#else
#include <sys/socket.h>
#include <netinet/in.h>
#endif

#define PRIVATE_DATA_SIZE 32
#define PRIVATE_DATA_ALIGNMENT 16

static int test_static_routes (void) {

    struct wolfsentry_context *wolfsentry;
    wolfsentry_action_res_t action_results;
    int n_deleted;
    wolfsentry_ent_id_t id;
    wolfsentry_route_flags_t inexact_matches;

    struct {
        struct wolfsentry_sockaddr sa;
        byte addr_buf[4];
    } remote, local, remote_wildcard, local_wildcard;

    struct wolfsentry_eventconfig config = { .route_private_data_size = PRIVATE_DATA_SIZE, .route_private_data_alignment = PRIVATE_DATA_ALIGNMENT, .max_connection_count = 10 };

    WOLFSENTRY_EXIT_ON_FAILURE(
        wolfsentry_init(
            WOLFSENTRY_TEST_HPI,
            &config,
            &wolfsentry));

    remote.sa.sa_family = local.sa.sa_family = AF_INET;
    remote.sa.sa_proto = local.sa.sa_proto = IPPROTO_TCP;
    remote.sa.sa_port = 12345;
    local.sa.sa_port = 443;
    remote.sa.addr_len = local.sa.addr_len = sizeof remote.addr_buf * BITS_PER_BYTE;
    remote.sa.interface = local.sa.interface = 1;
    memcpy(remote.sa.addr,"\0\1\2\3",sizeof remote.addr_buf);
    memcpy(local.sa.addr,"\377\376\375\374",sizeof local.addr_buf);

    wolfsentry_route_flags_t flags = WOLFSENTRY_ROUTE_FLAG_TCPLIKE_PORT_NUMBERS, flags_wildcard;
    WOLFSENTRY_SET_BITS(flags, WOLFSENTRY_ROUTE_FLAG_DIRECTION_IN);

    WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_route_insert_static(wolfsentry, NULL /* caller_arg */, &remote.sa, &local.sa, flags, 0 /* event_label_len */, 0 /* event_label */, &id, &action_results));

    memcpy(remote.sa.addr,"\4\5\6\7",sizeof remote.addr_buf);
    memcpy(local.sa.addr,"\373\372\371\370",sizeof local.addr_buf);

    WOLFSENTRY_CLEAR_BITS(flags, WOLFSENTRY_ROUTE_FLAG_DIRECTION_IN);
    WOLFSENTRY_SET_BITS(flags, WOLFSENTRY_ROUTE_FLAG_DIRECTION_OUT);

    WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_route_insert_static(wolfsentry, NULL /* caller_arg */, &remote.sa, &local.sa, flags, 0 /* event_label_len */, 0 /* event_label */, &id, &action_results));

#if 0
    puts("table after first 2 inserts:");
    for (struct wolfsentry_route *i = (struct wolfsentry_route *)wolfsentry->routes_static.header.head;
         i;
         i = (struct wolfsentry_route *)(i->header.next))
        WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_route_render(i, stdout));
    putchar('\n');
#endif

    WOLFSENTRY_SET_BITS(flags, WOLFSENTRY_ROUTE_FLAG_DIRECTION_IN);
    WOLFSENTRY_CLEAR_BITS(flags, WOLFSENTRY_ROUTE_FLAG_DIRECTION_OUT);

    WOLFSENTRY_EXIT_ON_SUCCESS(wolfsentry_route_delete_static(wolfsentry, NULL /* caller_arg */, &remote.sa, &local.sa, flags, 0 /* event_label_len */, 0 /* event_label */, &action_results, &n_deleted));

    WOLFSENTRY_CLEAR_BITS(flags, WOLFSENTRY_ROUTE_FLAG_DIRECTION_IN);
    WOLFSENTRY_SET_BITS(flags, WOLFSENTRY_ROUTE_FLAG_DIRECTION_OUT);

    WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_route_delete_static(wolfsentry, NULL /* caller_arg */, &remote.sa, &local.sa, flags, 0 /* event_label_len */, 0 /* event_label */, &action_results, &n_deleted));
    WOLFSENTRY_EXIT_ON_FALSE(n_deleted == 1);

    WOLFSENTRY_EXIT_ON_SUCCESS(wolfsentry_route_delete_static(wolfsentry, NULL /* caller_arg */, &remote.sa, &local.sa, flags, 0 /* event_label_len */, 0 /* event_label */, &action_results, &n_deleted));

    WOLFSENTRY_SET_BITS(flags, WOLFSENTRY_ROUTE_FLAG_GREENLISTED);
    WOLFSENTRY_CLEAR_BITS(flags, WOLFSENTRY_ROUTE_FLAG_PENALTYBOXED);
    memcpy(remote.sa.addr,"\3\4\5\6",sizeof remote.addr_buf);
    memcpy(local.sa.addr,"\373\372\371\370",sizeof local.addr_buf);

    WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_route_insert_static(wolfsentry, NULL /* caller_arg */, &remote.sa, &local.sa, flags, 0 /* event_label_len */, 0 /* event_label */, &id, &action_results));

    WOLFSENTRY_CLEAR_BITS(flags, WOLFSENTRY_ROUTE_FLAG_GREENLISTED);
    WOLFSENTRY_SET_BITS(flags, WOLFSENTRY_ROUTE_FLAG_PENALTYBOXED);
    memcpy(remote.sa.addr,"\2\3\4\5",sizeof remote.addr_buf);
    memcpy(local.sa.addr,"\373\372\371\370",sizeof local.addr_buf);


    WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_route_insert_static(wolfsentry, NULL /* caller_arg */, &remote.sa, &local.sa, flags, 0 /* event_label_len */, 0 /* event_label */, &id, &action_results));

    WOLFSENTRY_EXIT_ON_SUCCESS(wolfsentry_route_insert_static(wolfsentry, NULL /* caller_arg */, &remote.sa, &local.sa, flags, 0 /* event_label_len */, 0 /* event_label */, &id, &action_results));

    flags |= WOLFSENTRY_ROUTE_FLAG_DIRECTION_IN;
    WOLFSENTRY_CLEAR_BITS(flags, WOLFSENTRY_ROUTE_FLAG_DIRECTION_OUT);

    WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_route_insert_static(wolfsentry, NULL /* caller_arg */, &remote.sa, &local.sa, flags, 0 /* event_label_len */, 0 /* event_label */, &id, &action_results));

    struct wolfsentry_route_table *static_routes;
    WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_route_get_table_static(wolfsentry, &static_routes));

    struct wolfsentry_route *route_ref;
    WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_route_get_reference(
                                 wolfsentry,
                                 static_routes,
                                 &remote.sa,
                                 &local.sa,
                                 flags,
                                 0 /* event_label_len */,
                                 0 /* event_label */,
                                 1 /* exact_p */,
                                 &inexact_matches,
                                 &route_ref));

    byte *private_data;
    size_t private_data_size;
    WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_route_get_private_data(
                                 wolfsentry,
                                 route_ref,
                                 (void **)&private_data,
                                 &private_data_size));

    if (private_data_size < PRIVATE_DATA_SIZE) {
    // GCOV_EXCL_START
        printf("private_data_size is %zu but expected %d.\n",private_data_size,PRIVATE_DATA_SIZE);
        return 1;
    // GCOV_EXCL_STOP
    }
    if ((PRIVATE_DATA_ALIGNMENT > 0) && ((uintptr_t)private_data % (uintptr_t)PRIVATE_DATA_ALIGNMENT)) {
    // GCOV_EXCL_START
        printf("private_data (%p) is not aligned to %d.\n",private_data,PRIVATE_DATA_ALIGNMENT);
        return 1;
    // GCOV_EXCL_STOP
    }

    {
        byte *i, *i_end;
        for (i = private_data, i_end = private_data + private_data_size; i < i_end; ++i)
            *i = 'x';
    }

#if 0
    puts("table after deleting 4.5.6.7 and inserting 3 more:");
    for (struct wolfsentry_route *i = (struct wolfsentry_route *)wolfsentry->routes_static.header.head;
         i;
         i = (struct wolfsentry_route *)(i->header.next))
        WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_route_render(i, stdout));
#endif

    WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_route_drop_reference(wolfsentry, route_ref, &action_results));
    WOLFSENTRY_EXIT_ON_TRUE(WOLFSENTRY_CHECK_BITS(action_results, WOLFSENTRY_ACTION_RES_DEALLOCATED));

    /* now test basic eventless dispatch using exact-match ents in the static table. */

    WOLFSENTRY_CLEAR_ALL_BITS(action_results);
    wolfsentry_ent_id_t route_id;

    WOLFSENTRY_CLEAR_BITS(flags, WOLFSENTRY_ROUTE_FLAG_DIRECTION_IN);
    WOLFSENTRY_SET_BITS(flags, WOLFSENTRY_ROUTE_FLAG_DIRECTION_OUT);
    memcpy(remote.sa.addr,"\3\4\5\6",sizeof remote.addr_buf);
    memcpy(local.sa.addr,"\373\372\371\370",sizeof local.addr_buf);

    WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_route_event_dispatch(wolfsentry, &remote.sa, &local.sa, flags, NULL /* event_label */, 0 /* event_label_len */, NULL /* caller_arg */,
                                                           &route_id, &inexact_matches, &action_results));

    WOLFSENTRY_EXIT_ON_FALSE(WOLFSENTRY_CHECK_BITS(action_results, WOLFSENTRY_ACTION_RES_ACCEPT));
    WOLFSENTRY_EXIT_ON_TRUE(WOLFSENTRY_CHECK_BITS(action_results, WOLFSENTRY_ACTION_RES_REJECT));
    WOLFSENTRY_EXIT_ON_FALSE(inexact_matches == 0);

    flags |= WOLFSENTRY_ROUTE_FLAG_DIRECTION_IN;
    WOLFSENTRY_CLEAR_BITS(flags, WOLFSENTRY_ROUTE_FLAG_DIRECTION_OUT);
    WOLFSENTRY_EXIT_ON_SUCCESS(wolfsentry_route_event_dispatch(wolfsentry, &remote.sa, &local.sa, flags, NULL /* event_label */, 0 /* event_label_len */, NULL /* caller_arg */,
                                                           &route_id, &inexact_matches, &action_results));

    memcpy(remote.sa.addr,"\2\3\4\5",sizeof remote.addr_buf);
    memcpy(local.sa.addr,"\373\372\371\370",sizeof local.addr_buf);

    WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_route_event_dispatch(wolfsentry, &remote.sa, &local.sa, flags, NULL /* event_label */, 0 /* event_label_len */, NULL /* caller_arg */,
                                                           &route_id, &inexact_matches, &action_results));

    WOLFSENTRY_EXIT_ON_FALSE(WOLFSENTRY_CHECK_BITS(action_results, WOLFSENTRY_ACTION_RES_REJECT));
    WOLFSENTRY_EXIT_ON_TRUE(WOLFSENTRY_CHECK_BITS(action_results, WOLFSENTRY_ACTION_RES_ACCEPT));
    WOLFSENTRY_EXIT_ON_FALSE(inexact_matches == 0);

    WOLFSENTRY_CLEAR_BITS(flags, WOLFSENTRY_ROUTE_FLAG_DIRECTION_IN);
    WOLFSENTRY_SET_BITS(flags, WOLFSENTRY_ROUTE_FLAG_DIRECTION_OUT);
    WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_route_event_dispatch(wolfsentry, &remote.sa, &local.sa, flags, NULL /* event_label */, 0 /* event_label_len */, NULL /* caller_arg */,
                                                           &route_id, &inexact_matches, &action_results));

    WOLFSENTRY_EXIT_ON_FALSE(WOLFSENTRY_CHECK_BITS(action_results, WOLFSENTRY_ACTION_RES_REJECT));
    WOLFSENTRY_EXIT_ON_TRUE(WOLFSENTRY_CHECK_BITS(action_results, WOLFSENTRY_ACTION_RES_ACCEPT));
    WOLFSENTRY_EXIT_ON_FALSE(inexact_matches == 0);

    memcpy(remote.sa.addr,"\0\1\2\3",sizeof remote.addr_buf);
    memcpy(local.sa.addr,"\377\376\375\374",sizeof local.addr_buf);
    WOLFSENTRY_EXIT_ON_SUCCESS(wolfsentry_route_event_dispatch(wolfsentry, &remote.sa, &local.sa, flags, NULL /* event_label */, 0 /* event_label_len */, NULL /* caller_arg */,
                                                           &route_id, &inexact_matches, &action_results));

    flags |= WOLFSENTRY_ROUTE_FLAG_DIRECTION_IN;
    WOLFSENTRY_CLEAR_BITS(flags, WOLFSENTRY_ROUTE_FLAG_DIRECTION_OUT);
    WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_route_event_dispatch(wolfsentry, &remote.sa, &local.sa, flags, NULL /* event_label */, 0 /* event_label_len */, NULL /* caller_arg */,
                                                           &route_id, &inexact_matches, &action_results));

    WOLFSENTRY_EXIT_ON_TRUE(WOLFSENTRY_CHECK_BITS(action_results, WOLFSENTRY_ACTION_RES_REJECT));
    WOLFSENTRY_EXIT_ON_TRUE(WOLFSENTRY_CHECK_BITS(action_results, WOLFSENTRY_ACTION_RES_ACCEPT));
    WOLFSENTRY_EXIT_ON_FALSE(inexact_matches == 0);


    /* now test eventless dispatch using wildcard/prefix matches in the static table. */


    WOLFSENTRY_CLEAR_BITS(flags, WOLFSENTRY_ROUTE_FLAG_GREENLISTED);
    WOLFSENTRY_SET_BITS(flags, WOLFSENTRY_ROUTE_FLAG_PENALTYBOXED);
    WOLFSENTRY_CLEAR_BITS(flags, WOLFSENTRY_ROUTE_FLAG_DIRECTION_IN);
    WOLFSENTRY_SET_BITS(flags, WOLFSENTRY_ROUTE_FLAG_DIRECTION_OUT);
    memcpy(remote.sa.addr,"\4\5\6\7",sizeof remote.addr_buf);
    memcpy(local.sa.addr,"\373\372\371\370",sizeof local.addr_buf);

    int prefixlen;
    for (prefixlen = sizeof remote.addr_buf * BITS_PER_BYTE;
         prefixlen >= 8;
         --prefixlen) {
        remote.sa.addr_len = (wolfsentry_addr_bits_t)prefixlen;
        WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_route_insert_static(wolfsentry, NULL /* caller_arg */, &remote.sa, &local.sa, flags, 0 /* event_label_len */, 0 /* event_label */, &id, &action_results));

        remote.sa.addr_len = sizeof remote.addr_buf * BITS_PER_BYTE;
        WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_route_event_dispatch(wolfsentry, &remote.sa, &local.sa, flags, NULL /* event_label */, 0 /* event_label_len */, NULL /* caller_arg */,
                                                           &route_id, &inexact_matches, &action_results));

        WOLFSENTRY_EXIT_ON_FALSE(route_id == id);
        WOLFSENTRY_EXIT_ON_FALSE(WOLFSENTRY_CHECK_BITS(action_results, WOLFSENTRY_ACTION_RES_REJECT));
        WOLFSENTRY_EXIT_ON_TRUE(WOLFSENTRY_CHECK_BITS(action_results, WOLFSENTRY_ACTION_RES_ACCEPT));
        WOLFSENTRY_EXIT_ON_TRUE(prefixlen < (int)(sizeof remote.addr_buf * BITS_PER_BYTE) ? ! WOLFSENTRY_CHECK_BITS(inexact_matches, WOLFSENTRY_ROUTE_FLAG_SA_REMOTE_ADDR_WILDCARD) : WOLFSENTRY_CHECK_BITS(inexact_matches, WOLFSENTRY_ROUTE_FLAG_SA_REMOTE_ADDR_WILDCARD));

        remote.sa.addr_len = (wolfsentry_addr_bits_t)prefixlen;
        WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_route_delete_static(wolfsentry, NULL /* caller_arg */, &remote.sa, &local.sa, flags, 0 /* event_label_len */, 0 /* event_label */, &action_results, &n_deleted));
        WOLFSENTRY_EXIT_ON_FALSE(n_deleted == 1);


        local.sa.addr_len = (wolfsentry_addr_bits_t)prefixlen;
        WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_route_insert_static(wolfsentry, NULL /* caller_arg */, &remote.sa, &local.sa, flags, 0 /* event_label_len */, 0 /* event_label */, &id, &action_results));

        local.sa.addr_len = sizeof remote.addr_buf * BITS_PER_BYTE;
        WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_route_event_dispatch(wolfsentry, &remote.sa, &local.sa, flags, NULL /* event_label */, 0 /* event_label_len */, NULL /* caller_arg */,
                                                           &route_id, &inexact_matches, &action_results));

        WOLFSENTRY_EXIT_ON_FALSE(WOLFSENTRY_CHECK_BITS(action_results, WOLFSENTRY_ACTION_RES_REJECT));
        WOLFSENTRY_EXIT_ON_TRUE(WOLFSENTRY_CHECK_BITS(action_results, WOLFSENTRY_ACTION_RES_ACCEPT));
        WOLFSENTRY_EXIT_ON_TRUE(prefixlen < (int)(sizeof local.addr_buf * BITS_PER_BYTE) ? ! WOLFSENTRY_CHECK_BITS(inexact_matches, WOLFSENTRY_ROUTE_FLAG_SA_LOCAL_ADDR_WILDCARD) : WOLFSENTRY_CHECK_BITS(inexact_matches, WOLFSENTRY_ROUTE_FLAG_SA_LOCAL_ADDR_WILDCARD));

        local.sa.addr_len = (wolfsentry_addr_bits_t)prefixlen;
        WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_route_delete_static(wolfsentry, NULL /* caller_arg */, &remote.sa, &local.sa, flags, 0 /* event_label_len */, 0 /* event_label */, &action_results, &n_deleted));
        WOLFSENTRY_EXIT_ON_FALSE(n_deleted == 1);

    }


    remote_wildcard = remote;
    local_wildcard = local;
    flags_wildcard = flags;

    remote_wildcard.sa.sa_port = 0;
    WOLFSENTRY_SET_BITS(flags_wildcard, WOLFSENTRY_ROUTE_FLAG_SA_REMOTE_PORT_WILDCARD);

    WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_route_insert_static(wolfsentry, NULL /* caller_arg */, &remote_wildcard.sa, &local_wildcard.sa, flags_wildcard, 0 /* event_label_len */, 0 /* event_label */, &id, &action_results));

    WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_route_event_dispatch(wolfsentry, &remote.sa, &local.sa, flags, NULL /* event_label */, 0 /* event_label_len */, NULL /* caller_arg */,
                                                           &route_id, &inexact_matches, &action_results));
    WOLFSENTRY_EXIT_ON_FALSE(route_id == id);


    local.sa.sa_port = 8765;
    WOLFSENTRY_EXIT_ON_SUCCESS(wolfsentry_route_event_dispatch(wolfsentry, &remote.sa, &local.sa, flags, NULL /* event_label */, 0 /* event_label_len */, NULL /* caller_arg */,
                                                           &route_id, &inexact_matches, &action_results));
    local.sa.sa_port = local_wildcard.sa.sa_port;

    WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_route_event_dispatch(wolfsentry, &remote.sa, &local.sa, flags, NULL /* event_label */, 0 /* event_label_len */, NULL /* caller_arg */,
                                                           &route_id, &inexact_matches, &action_results));

    WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_route_delete_by_id(wolfsentry, NULL /* caller_arg */, route_id, NULL /* event_label */, 0 /* event_label_len */, &action_results));

    WOLFSENTRY_EXIT_ON_FALSE(WOLFSENTRY_CHECK_BITS(action_results, WOLFSENTRY_ACTION_RES_DEALLOCATED));

    remote_wildcard = remote;
    local_wildcard = local;
    flags_wildcard = flags;

    local_wildcard.sa.sa_port = 0;
    WOLFSENTRY_SET_BITS(flags_wildcard, WOLFSENTRY_ROUTE_FLAG_SA_LOCAL_PORT_WILDCARD);

    WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_route_insert_static(wolfsentry, NULL /* caller_arg */, &remote_wildcard.sa, &local_wildcard.sa, flags_wildcard, 0 /* event_label_len */, 0 /* event_label */, &id, &action_results));

    WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_route_event_dispatch(wolfsentry, &remote.sa, &local.sa, flags, NULL /* event_label */, 0 /* event_label_len */, NULL /* caller_arg */,
                                                           &route_id, &inexact_matches, &action_results));
    WOLFSENTRY_EXIT_ON_FALSE(route_id == id);
    WOLFSENTRY_EXIT_ON_FALSE(WOLFSENTRY_CHECK_BITS(inexact_matches, WOLFSENTRY_ROUTE_FLAG_SA_LOCAL_PORT_WILDCARD));

    WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_route_delete_static(wolfsentry, NULL /* caller_arg */, &remote_wildcard.sa, &local_wildcard.sa, flags_wildcard, 0 /* event_label_len */, 0 /* event_label */, &action_results, &n_deleted));
    WOLFSENTRY_EXIT_ON_FALSE(n_deleted == 1);


    remote_wildcard = remote;
    local_wildcard = local;
    flags_wildcard = flags;

    remote_wildcard.sa.sa_port = local_wildcard.sa.sa_port = 0;
    remote_wildcard.sa.sa_proto = local_wildcard.sa.sa_proto = 0;
    WOLFSENTRY_SET_BITS(flags_wildcard, WOLFSENTRY_ROUTE_FLAG_SA_PROTO_WILDCARD);
    WOLFSENTRY_SET_BITS(flags_wildcard, WOLFSENTRY_ROUTE_FLAG_SA_REMOTE_PORT_WILDCARD);
    WOLFSENTRY_SET_BITS(flags_wildcard, WOLFSENTRY_ROUTE_FLAG_SA_LOCAL_PORT_WILDCARD);

    WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_route_insert_static(wolfsentry, NULL /* caller_arg */, &remote_wildcard.sa, &local_wildcard.sa, flags_wildcard, 0 /* event_label_len */, 0 /* event_label */, &id, &action_results));

    WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_route_event_dispatch(wolfsentry, &remote.sa, &local.sa, flags, NULL /* event_label */, 0 /* event_label_len */, NULL /* caller_arg */,
                                                           &route_id, &inexact_matches, &action_results));
    WOLFSENTRY_EXIT_ON_FALSE(route_id == id);
    WOLFSENTRY_EXIT_ON_FALSE(WOLFSENTRY_CHECK_BITS(inexact_matches, WOLFSENTRY_ROUTE_FLAG_SA_PROTO_WILDCARD));
    WOLFSENTRY_EXIT_ON_FALSE(WOLFSENTRY_CHECK_BITS(inexact_matches, WOLFSENTRY_ROUTE_FLAG_SA_REMOTE_PORT_WILDCARD));
    WOLFSENTRY_EXIT_ON_FALSE(WOLFSENTRY_CHECK_BITS(inexact_matches, WOLFSENTRY_ROUTE_FLAG_SA_LOCAL_PORT_WILDCARD));

    WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_route_delete_static(wolfsentry, NULL /* caller_arg */, &remote_wildcard.sa, &local_wildcard.sa, flags_wildcard, 0 /* event_label_len */, 0 /* event_label */, &action_results, &n_deleted));
    WOLFSENTRY_EXIT_ON_FALSE(n_deleted == 1);


    remote_wildcard = remote;
    local_wildcard = local;
    flags_wildcard = flags;

    local_wildcard.sa.addr_len = 0;
    WOLFSENTRY_SET_BITS(flags_wildcard, WOLFSENTRY_ROUTE_FLAG_SA_LOCAL_ADDR_WILDCARD);

    WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_route_insert_static(wolfsentry, NULL /* caller_arg */, &remote_wildcard.sa, &local_wildcard.sa, flags_wildcard, 0 /* event_label_len */, 0 /* event_label */, &id, &action_results));

    WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_route_event_dispatch(wolfsentry, &remote.sa, &local.sa, flags, NULL /* event_label */, 0 /* event_label_len */, NULL /* caller_arg */,
                                                           &route_id, &inexact_matches, &action_results));
    WOLFSENTRY_EXIT_ON_FALSE(route_id == id);
    WOLFSENTRY_EXIT_ON_FALSE(WOLFSENTRY_CHECK_BITS(inexact_matches, WOLFSENTRY_ROUTE_FLAG_SA_LOCAL_ADDR_WILDCARD));

    WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_route_delete_static(wolfsentry, NULL /* caller_arg */, &remote_wildcard.sa, &local_wildcard.sa, flags_wildcard, 0 /* event_label_len */, 0 /* event_label */, &action_results, &n_deleted));
    WOLFSENTRY_EXIT_ON_FALSE(n_deleted == 1);


    remote_wildcard = remote;
    local_wildcard = local;
    flags_wildcard = flags;

    remote_wildcard.sa.sa_port = 0;
    WOLFSENTRY_SET_BITS(flags_wildcard, WOLFSENTRY_ROUTE_FLAG_SA_REMOTE_PORT_WILDCARD);
    local_wildcard.sa.addr_len = 0;
    WOLFSENTRY_SET_BITS(flags_wildcard, WOLFSENTRY_ROUTE_FLAG_SA_LOCAL_ADDR_WILDCARD);

    WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_route_insert_static(wolfsentry, NULL /* caller_arg */, &remote_wildcard.sa, &local_wildcard.sa, flags_wildcard, 0 /* event_label_len */, 0 /* event_label */, &id, &action_results));

    WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_route_event_dispatch(wolfsentry, &remote.sa, &local.sa, flags, NULL /* event_label */, 0 /* event_label_len */, NULL /* caller_arg */,
                                                           &route_id, &inexact_matches, &action_results));
    WOLFSENTRY_EXIT_ON_FALSE(route_id == id);
    WOLFSENTRY_EXIT_ON_FALSE(WOLFSENTRY_CHECK_BITS(inexact_matches, WOLFSENTRY_ROUTE_FLAG_SA_REMOTE_PORT_WILDCARD));
    WOLFSENTRY_EXIT_ON_FALSE(WOLFSENTRY_CHECK_BITS(inexact_matches, WOLFSENTRY_ROUTE_FLAG_SA_LOCAL_ADDR_WILDCARD));

    WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_route_delete_static(wolfsentry, NULL /* caller_arg */, &remote_wildcard.sa, &local_wildcard.sa, flags_wildcard, 0 /* event_label_len */, 0 /* event_label */, &action_results, &n_deleted));
    WOLFSENTRY_EXIT_ON_FALSE(n_deleted == 1);


    remote_wildcard = remote;
    local_wildcard = local;
    flags_wildcard = flags;

    remote_wildcard.sa.addr_len = 0;
    WOLFSENTRY_SET_BITS(flags_wildcard, WOLFSENTRY_ROUTE_FLAG_SA_REMOTE_ADDR_WILDCARD);

    WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_route_insert_static(wolfsentry, NULL /* caller_arg */, &remote_wildcard.sa, &local_wildcard.sa, flags_wildcard, 0 /* event_label_len */, 0 /* event_label */, &id, &action_results));

    WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_route_event_dispatch(wolfsentry, &remote.sa, &local.sa, flags, NULL /* event_label */, 0 /* event_label_len */, NULL /* caller_arg */,
                                                           &route_id, &inexact_matches, &action_results));
    WOLFSENTRY_EXIT_ON_FALSE(route_id == id);
    WOLFSENTRY_EXIT_ON_FALSE(WOLFSENTRY_CHECK_BITS(inexact_matches, WOLFSENTRY_ROUTE_FLAG_SA_REMOTE_ADDR_WILDCARD));

    WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_route_delete_static(wolfsentry, NULL /* caller_arg */, &remote_wildcard.sa, &local_wildcard.sa, flags_wildcard, 0 /* event_label_len */, 0 /* event_label */, &action_results, &n_deleted));
    WOLFSENTRY_EXIT_ON_FALSE(n_deleted == 1);


    remote_wildcard = remote;
    local_wildcard = local;
    flags_wildcard = flags;

    local_wildcard.sa.interface = 0;
    WOLFSENTRY_SET_BITS(flags_wildcard, WOLFSENTRY_ROUTE_FLAG_LOCAL_INTERFACE_WILDCARD);

    WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_route_insert_static(wolfsentry, NULL /* caller_arg */, &remote_wildcard.sa, &local_wildcard.sa, flags_wildcard, 0 /* event_label_len */, 0 /* event_label */, &id, &action_results));

    WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_route_event_dispatch(wolfsentry, &remote.sa, &local.sa, flags, NULL /* event_label */, 0 /* event_label_len */, NULL /* caller_arg */,
                                                           &route_id, &inexact_matches, &action_results));
    WOLFSENTRY_EXIT_ON_FALSE(route_id == id);
    WOLFSENTRY_EXIT_ON_FALSE(WOLFSENTRY_CHECK_BITS(inexact_matches, WOLFSENTRY_ROUTE_FLAG_LOCAL_INTERFACE_WILDCARD));

    WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_route_delete_static(wolfsentry, NULL /* caller_arg */, &remote_wildcard.sa, &local_wildcard.sa, flags_wildcard, 0 /* event_label_len */, 0 /* event_label */, &action_results, &n_deleted));
    WOLFSENTRY_EXIT_ON_FALSE(n_deleted == 1);


    remote_wildcard = remote;
    local_wildcard = local;
    flags_wildcard = flags;

    remote_wildcard.sa.interface = 0;
    WOLFSENTRY_SET_BITS(flags_wildcard, WOLFSENTRY_ROUTE_FLAG_REMOTE_INTERFACE_WILDCARD);

    WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_route_insert_static(wolfsentry, NULL /* caller_arg */, &remote_wildcard.sa, &local_wildcard.sa, flags_wildcard, 0 /* event_label_len */, 0 /* event_label */, &id, &action_results));

    WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_route_event_dispatch(wolfsentry, &remote.sa, &local.sa, flags, NULL /* event_label */, 0 /* event_label_len */, NULL /* caller_arg */,
                                                           &route_id, &inexact_matches, &action_results));
    WOLFSENTRY_EXIT_ON_FALSE(route_id == id);
    WOLFSENTRY_EXIT_ON_FALSE(WOLFSENTRY_CHECK_BITS(inexact_matches, WOLFSENTRY_ROUTE_FLAG_REMOTE_INTERFACE_WILDCARD));

    WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_route_delete_static(wolfsentry, NULL /* caller_arg */, &remote_wildcard.sa, &local_wildcard.sa, flags_wildcard, 0 /* event_label_len */, 0 /* event_label */, &action_results, &n_deleted));
    WOLFSENTRY_EXIT_ON_FALSE(n_deleted == 1);


    local.sa.interface = 2;
    remote_wildcard = remote;
    local_wildcard = local;
    flags_wildcard = flags;

    WOLFSENTRY_SET_BITS(flags_wildcard, WOLFSENTRY_ROUTE_FLAG_SA_FAMILY_WILDCARD);
    local_wildcard.sa.sa_family = remote_wildcard.sa.sa_family = 0;
    local_wildcard.sa.addr_len = remote_wildcard.sa.addr_len = 0;
    WOLFSENTRY_SET_BITS(flags_wildcard, WOLFSENTRY_ROUTE_FLAG_SA_LOCAL_ADDR_WILDCARD);
    WOLFSENTRY_SET_BITS(flags_wildcard, WOLFSENTRY_ROUTE_FLAG_SA_REMOTE_ADDR_WILDCARD);
    remote_wildcard.sa.sa_port = local_wildcard.sa.sa_port = 0;
    remote_wildcard.sa.sa_proto = local_wildcard.sa.sa_proto = 0;
    WOLFSENTRY_SET_BITS(flags_wildcard, WOLFSENTRY_ROUTE_FLAG_SA_PROTO_WILDCARD);
    WOLFSENTRY_SET_BITS(flags_wildcard, WOLFSENTRY_ROUTE_FLAG_SA_REMOTE_PORT_WILDCARD);
    WOLFSENTRY_SET_BITS(flags_wildcard, WOLFSENTRY_ROUTE_FLAG_SA_LOCAL_PORT_WILDCARD);

    WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_route_insert_static(wolfsentry, NULL /* caller_arg */, &remote_wildcard.sa, &local_wildcard.sa, flags_wildcard, 0 /* event_label_len */, 0 /* event_label */, &id, &action_results));

    WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_route_event_dispatch(wolfsentry, &remote.sa, &local.sa, flags, NULL /* event_label */, 0 /* event_label_len */, NULL /* caller_arg */,
                                                           &route_id, &inexact_matches, &action_results));
    WOLFSENTRY_EXIT_ON_FALSE(route_id == id);
    WOLFSENTRY_EXIT_ON_FALSE(WOLFSENTRY_CHECK_BITS(inexact_matches, WOLFSENTRY_ROUTE_FLAG_SA_FAMILY_WILDCARD));
    WOLFSENTRY_EXIT_ON_TRUE(WOLFSENTRY_CHECK_BITS(inexact_matches, WOLFSENTRY_ROUTE_FLAG_LOCAL_INTERFACE_WILDCARD));

    WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_route_delete_static(wolfsentry, NULL /* caller_arg */, &remote_wildcard.sa, &local_wildcard.sa, flags_wildcard, 0 /* event_label_len */, 0 /* event_label */, &action_results, &n_deleted));
    WOLFSENTRY_EXIT_ON_FALSE(n_deleted == 1);
    WOLFSENTRY_EXIT_ON_SUCCESS(wolfsentry_route_delete_static(wolfsentry, NULL /* caller_arg */, &remote_wildcard.sa, &local_wildcard.sa, flags_wildcard, 0 /* event_label_len */, 0 /* event_label */, &action_results, &n_deleted));


#ifndef NO_STDIO
    {
        wolfsentry_errcode_t ret;
        struct wolfsentry_cursor *cursor;
        struct wolfsentry_route *route;
        struct wolfsentry_route_exports route_exports;
        wolfsentry_hitcount_t n_seen = 0;
        WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_route_table_iterate_start(wolfsentry, static_routes, &cursor));
        for (ret = wolfsentry_route_table_iterate_current(wolfsentry, static_routes, cursor, &route);
             ret >= 0;
             ret = wolfsentry_route_table_iterate_next(wolfsentry, static_routes, cursor, &route)) {
            WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_route_export(wolfsentry, route, &route_exports));
            WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_route_exports_render(&route_exports, stdout));
            ++n_seen;
        }
        WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_route_table_iterate_end(wolfsentry, static_routes, &cursor));
        WOLFSENTRY_EXIT_ON_FALSE(n_seen == wolfsentry->routes_static.header.n_ents);
    }
#endif

    remote.sa.sa_family = local.sa.sa_family = AF_INET;
    remote.sa.sa_proto = local.sa.sa_proto = IPPROTO_TCP;
    remote.sa.sa_port = 12345;
    local.sa.sa_port = 443;
    remote.sa.addr_len = local.sa.addr_len = sizeof remote.addr_buf * BITS_PER_BYTE;
    remote.sa.interface = local.sa.interface = 1;
    memcpy(remote.sa.addr,"\0\1\2\3",sizeof remote.addr_buf);
    memcpy(local.sa.addr,"\377\376\375\374",sizeof local.addr_buf);

    WOLFSENTRY_CLEAR_ALL_BITS(flags);
    WOLFSENTRY_SET_BITS(flags, WOLFSENTRY_ROUTE_FLAG_TCPLIKE_PORT_NUMBERS|WOLFSENTRY_ROUTE_FLAG_DIRECTION_IN);

    WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_route_delete_static(wolfsentry, NULL /* caller_arg */, &remote.sa, &local.sa, flags, 0 /* event_label_len */, 0 /* event_label */, &action_results, &n_deleted));
    WOLFSENTRY_EXIT_ON_FALSE(n_deleted == 1);
    WOLFSENTRY_EXIT_ON_SUCCESS(wolfsentry_route_delete_static(wolfsentry, NULL /* caller_arg */, &remote.sa, &local.sa, flags, 0 /* event_label_len */, 0 /* event_label */, &action_results, &n_deleted));

    WOLFSENTRY_CLEAR_BITS(flags, WOLFSENTRY_ROUTE_FLAG_GREENLISTED);
    WOLFSENTRY_SET_BITS(flags, WOLFSENTRY_ROUTE_FLAG_PENALTYBOXED);
    WOLFSENTRY_CLEAR_BITS(flags, WOLFSENTRY_ROUTE_FLAG_DIRECTION_IN);
    WOLFSENTRY_SET_BITS(flags, WOLFSENTRY_ROUTE_FLAG_DIRECTION_OUT);
    memcpy(remote.sa.addr,"\2\3\4\5",sizeof remote.addr_buf);
    memcpy(local.sa.addr,"\373\372\371\370",sizeof local.addr_buf);

    WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_route_delete_static(wolfsentry, NULL /* caller_arg */, &remote.sa, &local.sa, flags, 0 /* event_label_len */, 0 /* event_label */, &action_results, &n_deleted));
    WOLFSENTRY_EXIT_ON_FALSE(n_deleted == 1);
    WOLFSENTRY_EXIT_ON_SUCCESS(wolfsentry_route_delete_static(wolfsentry, NULL /* caller_arg */, &remote.sa, &local.sa, flags, 0 /* event_label_len */, 0 /* event_label */, &action_results, &n_deleted));

    WOLFSENTRY_SET_BITS(flags, WOLFSENTRY_ROUTE_FLAG_DIRECTION_IN);
    WOLFSENTRY_CLEAR_BITS(flags, WOLFSENTRY_ROUTE_FLAG_DIRECTION_OUT);

    WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_route_delete_static(wolfsentry, NULL /* caller_arg */, &remote.sa, &local.sa, flags, 0 /* event_label_len */, 0 /* event_label */, &action_results, &n_deleted));
    WOLFSENTRY_EXIT_ON_FALSE(n_deleted == 1);
    WOLFSENTRY_EXIT_ON_SUCCESS(wolfsentry_route_delete_static(wolfsentry, NULL /* caller_arg */, &remote.sa, &local.sa, flags, 0 /* event_label_len */, 0 /* event_label */, &action_results, &n_deleted));

    WOLFSENTRY_SET_BITS(flags, WOLFSENTRY_ROUTE_FLAG_GREENLISTED);
    WOLFSENTRY_CLEAR_BITS(flags, WOLFSENTRY_ROUTE_FLAG_PENALTYBOXED);
    WOLFSENTRY_CLEAR_BITS(flags, WOLFSENTRY_ROUTE_FLAG_DIRECTION_IN);
    WOLFSENTRY_SET_BITS(flags, WOLFSENTRY_ROUTE_FLAG_DIRECTION_OUT);
    memcpy(remote.sa.addr,"\3\4\5\6",sizeof remote.addr_buf);
    memcpy(local.sa.addr,"\373\372\371\370",sizeof local.addr_buf);

    WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_route_delete_static(wolfsentry, NULL /* caller_arg */, &remote.sa, &local.sa, flags, 0 /* event_label_len */, 0 /* event_label */, &action_results, &n_deleted));
    WOLFSENTRY_EXIT_ON_FALSE(n_deleted == 1);
    WOLFSENTRY_EXIT_ON_SUCCESS(wolfsentry_route_delete_static(wolfsentry, NULL /* caller_arg */, &remote.sa, &local.sa, flags, 0 /* event_label_len */, 0 /* event_label */, &action_results, &n_deleted));

    WOLFSENTRY_EXIT_ON_FALSE(wolfsentry->routes_static.header.n_ents == 0);

    printf("all subtests succeeded -- %d distinct ents inserted and deleted.\n",wolfsentry->mk_id_cb_state.id_counter);

    WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_shutdown(&wolfsentry));

    return 0;
}

#undef PRIVATE_DATA_SIZE
#undef PRIVATE_DATA_ALIGNMENT

#endif /* TEST_STATIC_ROUTES */

#ifdef TEST_DYNAMIC_RULES

#ifdef LWIP
#include "lwip-socket.h"
#else
#include <sys/socket.h>
#include <netinet/in.h>
#endif

#define PRIVATE_DATA_SIZE 32
#define PRIVATE_DATA_ALIGNMENT 8

// GCOV_EXCL_START
static wolfsentry_errcode_t wolfsentry_action_dummy_callback(
    struct wolfsentry_context *wolfsentry,
    const struct wolfsentry_action *action,
    void *handler_context,
    void *caller_arg,
    const struct wolfsentry_event *event,
    wolfsentry_action_type_t action_type,
    struct wolfsentry_route_table *route_table,
    const struct wolfsentry_route *route,
    wolfsentry_action_res_t *action_results)
{
    (void)wolfsentry;
    (void)action;
    (void)handler_context;
    (void)caller_arg;
    (void)event;
    (void)action_type;
    (void)route_table;
    (void)route;
    (void)action_results;

    return 0;
}
// GCOV_EXCL_STOP


static int test_dynamic_rules (void) {

    struct wolfsentry_context *wolfsentry;
#if 0
    wolfsentry_action_res_t action_results;
    int n_deleted;
    int ret;
    struct {
        struct wolfsentry_sockaddr sa;
        byte addr_buf[4];
    } src, dst;
#endif

    wolfsentry_ent_id_t id;

    struct wolfsentry_eventconfig config = { .route_private_data_size = PRIVATE_DATA_SIZE, .route_private_data_alignment = PRIVATE_DATA_ALIGNMENT, .max_connection_count = 10 };

    WOLFSENTRY_EXIT_ON_FAILURE(
        wolfsentry_init(
            WOLFSENTRY_TEST_HPI,
            &config,
            &wolfsentry));

    WOLFSENTRY_EXIT_ON_FAILURE(
        wolfsentry_event_insert(
            wolfsentry,
            "connect",
            -1 /* label_len */,
            10,
            NULL /* config */,
            WOLFSENTRY_EVENT_FLAG_NONE,
            &id));

    /* track port scanning */
    WOLFSENTRY_EXIT_ON_FAILURE(
        wolfsentry_event_insert(
            wolfsentry,
            "connection_refused",
            -1 /* label_len */,
            10,
            NULL /* config */,
            WOLFSENTRY_EVENT_FLAG_NONE,
            &id));

    WOLFSENTRY_EXIT_ON_FAILURE(
        wolfsentry_event_insert(
            wolfsentry,
            "disconnect",
            -1 /* label_len */,
            10,
            NULL /* config */,
            WOLFSENTRY_EVENT_FLAG_NONE,
            &id));

    WOLFSENTRY_EXIT_ON_FAILURE(
        wolfsentry_event_insert(
            wolfsentry,
            "authentication_succeeded",
            -1 /* label_len */,
            10,
            NULL /* config */,
            WOLFSENTRY_EVENT_FLAG_NONE,
            &id));

    WOLFSENTRY_EXIT_ON_FAILURE(
        wolfsentry_event_insert(
            wolfsentry,
            "authentication_failed",
            -1 /* label_len */,
            10,
            NULL /* config */,
            WOLFSENTRY_EVENT_FLAG_NONE,
            &id));

    WOLFSENTRY_EXIT_ON_FAILURE(
        wolfsentry_event_insert(
            wolfsentry,
            "negotiation_abandoned",
            -1 /* label_len */,
            10,
            NULL /* config */,
            WOLFSENTRY_EVENT_FLAG_NONE,
            &id));

    WOLFSENTRY_EXIT_ON_FAILURE(
        wolfsentry_event_insert(
            wolfsentry,
            "insertion_side_effect_demo",
            -1 /* label_len */,
            10,
            NULL /* config */,
            WOLFSENTRY_EVENT_FLAG_NONE,
            &id));

    WOLFSENTRY_EXIT_ON_FAILURE(
        wolfsentry_event_insert(
            wolfsentry,
            "match_side_effect_demo",
            -1 /* label_len */,
            10,
            NULL /* config */,
            WOLFSENTRY_EVENT_FLAG_NONE,
            &id));

    WOLFSENTRY_EXIT_ON_FAILURE(
        wolfsentry_event_insert(
            wolfsentry,
            "deletion_side_effect_demo",
            -1 /* label_len */,
            10,
            NULL /* config */,
            WOLFSENTRY_EVENT_FLAG_NONE,
            &id));

#if 0
int wolfsentry_event_set_subevent(
    struct wolfsentry_context *wolfsentry,
    const char *event_label,
    int event_label_len,
    wolfsentry_action_type_t subevent_type,
    const char *subevent_label,
    int subevent_label_len);
#endif

    WOLFSENTRY_EXIT_ON_FAILURE(
        wolfsentry_action_insert(
            wolfsentry,
            "insert_always",
            -1 /* label_len */,
            WOLFSENTRY_ACTION_FLAG_NONE,
            wolfsentry_action_dummy_callback,
            NULL /* handler_context */,
            &id));

    WOLFSENTRY_EXIT_ON_FAILURE(
        wolfsentry_action_insert(
            wolfsentry,
            "insert_alway",
            -1 /* label_len */,
            WOLFSENTRY_ACTION_FLAG_NONE,
            wolfsentry_action_dummy_callback,
            NULL /* handler_context */,
            &id));

    {
        static char too_long_label[WOLFSENTRY_MAX_LABEL_BYTES + 2];
        memset(too_long_label, 'x', sizeof too_long_label - 1);

        too_long_label[sizeof too_long_label - 1] = 0;

        WOLFSENTRY_EXIT_ON_FALSE(WOLFSENTRY_ERROR_CODE_IS(
                                     wolfsentry_action_insert(
                                         wolfsentry,
                                         too_long_label,
                                         -1 /* label_len */,
                                         WOLFSENTRY_ACTION_FLAG_NONE,
                                         wolfsentry_action_dummy_callback,
                                         NULL /* handler_context */,
                                         &id), STRING_ARG_TOO_LONG));
        WOLFSENTRY_EXIT_ON_FALSE(WOLFSENTRY_ERROR_CODE_IS(
                                     wolfsentry_action_insert(
                                         wolfsentry,
                                         too_long_label,
                                         sizeof too_long_label - 1,
                                         WOLFSENTRY_ACTION_FLAG_NONE,
                                         wolfsentry_action_dummy_callback,
                                         NULL /* handler_context */,
                                         &id), STRING_ARG_TOO_LONG));

        WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_action_insert(
                                       wolfsentry,
                                       too_long_label,
                                       sizeof too_long_label - 2,
                                       WOLFSENTRY_ACTION_FLAG_NONE,
                                       wolfsentry_action_dummy_callback,
                                       NULL /* handler_context */,
                                       &id));


        WOLFSENTRY_EXIT_ON_FALSE(WOLFSENTRY_ERROR_CODE_IS(
                                     wolfsentry_action_delete(
                                         wolfsentry,
                                         too_long_label,
                                         sizeof too_long_label - 1,
                                         NULL /* action_results */), STRING_ARG_TOO_LONG));

        WOLFSENTRY_EXIT_ON_FALSE(WOLFSENTRY_ERROR_CODE_IS(
                                     wolfsentry_action_delete(
                                         wolfsentry,
                                         too_long_label,
                                         -1,
                                         NULL /* action_results */), STRING_ARG_TOO_LONG));

        WOLFSENTRY_EXIT_ON_FALSE(WOLFSENTRY_ERROR_CODE_IS(
                                     wolfsentry_action_delete(
                                         wolfsentry,
                                         NULL,
                                         -1,
                                         NULL /* action_results */), INVALID_ARG));

        too_long_label[sizeof too_long_label - 2] = 0;

        WOLFSENTRY_EXIT_ON_FALSE(WOLFSENTRY_ERROR_CODE_IS(
                                     wolfsentry_action_insert(
                                         wolfsentry,
                                         too_long_label,
                                         -1 /* label_len */,
                                         WOLFSENTRY_ACTION_FLAG_NONE,
                                         wolfsentry_action_dummy_callback,
                                         NULL /* handler_context */,
                                         &id), ITEM_ALREADY_PRESENT));

        WOLFSENTRY_EXIT_ON_FALSE(WOLFSENTRY_ERROR_CODE_IS(
                                     wolfsentry_action_insert(
                                         wolfsentry,
                                         NULL,
                                         -1 /* label_len */,
                                         WOLFSENTRY_ACTION_FLAG_NONE,
                                         wolfsentry_action_dummy_callback,
                                         NULL /* handler_context */,
                                         &id), INVALID_ARG));

        WOLFSENTRY_EXIT_ON_FALSE(WOLFSENTRY_ERROR_CODE_IS(
                                     wolfsentry_action_insert(
                                         wolfsentry,
                                         too_long_label,
                                         0 /* label_len */,
                                         WOLFSENTRY_ACTION_FLAG_NONE,
                                         wolfsentry_action_dummy_callback,
                                         NULL /* handler_context */,
                                         &id), INVALID_ARG));

        WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_action_delete(
                                       wolfsentry,
                                       too_long_label,
                                       -1 /* label_len */,
                                       NULL /* action_results */));
    }

    WOLFSENTRY_EXIT_ON_FAILURE(
        wolfsentry_action_insert(
            wolfsentry,
            "set_connect_wildcards",
            -1 /* label_len */,
            WOLFSENTRY_ACTION_FLAG_NONE,
            wolfsentry_action_dummy_callback,
            NULL /* handler_context */,
            &id));

    WOLFSENTRY_EXIT_ON_FAILURE(
        wolfsentry_action_insert(
            wolfsentry,
            "set_connectionreset_wildcards",
            -1 /* label_len */,
            WOLFSENTRY_ACTION_FLAG_NONE,
            wolfsentry_action_dummy_callback,
            NULL /* handler_context */,
            &id));

    WOLFSENTRY_EXIT_ON_FAILURE(
        wolfsentry_action_insert(
            wolfsentry,
            "increment_derogatory",
            -1 /* label_len */,
            WOLFSENTRY_ACTION_FLAG_NONE,
            wolfsentry_action_dummy_callback,
            NULL /* handler_context */,
            &id));

    WOLFSENTRY_EXIT_ON_FAILURE(
        wolfsentry_action_insert(
            wolfsentry,
            "increment_commendable",
            -1 /* label_len */,
            WOLFSENTRY_ACTION_FLAG_NONE,
            wolfsentry_action_dummy_callback,
            NULL /* handler_context */,
            &id));

    WOLFSENTRY_EXIT_ON_FAILURE(
        wolfsentry_action_insert(
            wolfsentry,
            "check_counts",
            -1 /* label_len */,
            WOLFSENTRY_ACTION_FLAG_NONE,
            wolfsentry_action_dummy_callback,
            NULL /* handler_context */,
            &id));

    {
        struct wolfsentry_action *action;
        wolfsentry_action_flags_t flags;

        WOLFSENTRY_EXIT_ON_FALSE(
            WOLFSENTRY_ERROR_CODE_IS(
                wolfsentry_action_get_reference(
                    wolfsentry,
                    "checXXXounts",
                    -1 /* label_len */,
                    &action),
                ITEM_NOT_FOUND));

        WOLFSENTRY_EXIT_ON_FALSE(
            WOLFSENTRY_ERROR_CODE_IS(
                wolfsentry_action_get_reference(
                    wolfsentry,
                    "checXXXounts",
                    0 /* label_len */,
                    &action),
                INVALID_ARG));

        WOLFSENTRY_EXIT_ON_FALSE(
            WOLFSENTRY_ERROR_CODE_IS(
                wolfsentry_action_get_reference(
                    wolfsentry,
                    "checXXXounts",
                    WOLFSENTRY_MAX_LABEL_BYTES + 1 /* label_len */,
                    &action),
                STRING_ARG_TOO_LONG));

        WOLFSENTRY_EXIT_ON_FAILURE(
            wolfsentry_action_get_reference(
                wolfsentry,
                "check_counts",
                -1 /* label_len */,
                &action));

        WOLFSENTRY_EXIT_ON_FAILURE(
            wolfsentry_action_get_flags(
                action,
                &flags));
        WOLFSENTRY_EXIT_ON_FALSE(flags == WOLFSENTRY_ACTION_FLAG_NONE);

        {
            wolfsentry_action_flags_t flags_before, flags_after;
            WOLFSENTRY_EXIT_ON_FAILURE(
                wolfsentry_action_update_flags(
                    action,
                    WOLFSENTRY_ACTION_FLAG_DISABLED,
                    WOLFSENTRY_ACTION_FLAG_NONE,
                    &flags_before,
                    &flags_after));
            WOLFSENTRY_EXIT_ON_FALSE(flags_before == WOLFSENTRY_ACTION_FLAG_NONE);
            WOLFSENTRY_EXIT_ON_FALSE(flags_after == WOLFSENTRY_ACTION_FLAG_DISABLED);
        }

        WOLFSENTRY_EXIT_ON_FAILURE(
            wolfsentry_action_get_flags(
                action,
                &flags));
        WOLFSENTRY_EXIT_ON_FALSE(flags == WOLFSENTRY_ACTION_FLAG_DISABLED);

        {
            wolfsentry_action_flags_t flags_before, flags_after;
            WOLFSENTRY_EXIT_ON_FAILURE(
                wolfsentry_action_update_flags(
                    action,
                    WOLFSENTRY_ACTION_FLAG_NONE,
                    WOLFSENTRY_ACTION_FLAG_DISABLED,
                    &flags_before,
                    &flags_after));
            WOLFSENTRY_EXIT_ON_FALSE(flags_before == WOLFSENTRY_ACTION_FLAG_DISABLED);
            WOLFSENTRY_EXIT_ON_FALSE(flags_after == WOLFSENTRY_ACTION_FLAG_NONE);
        }

        WOLFSENTRY_EXIT_ON_FAILURE(
            wolfsentry_action_get_flags(
                action,
                &flags));
        WOLFSENTRY_EXIT_ON_FALSE(flags == WOLFSENTRY_ACTION_FLAG_NONE);

        WOLFSENTRY_EXIT_ON_FAILURE(
            wolfsentry_action_drop_reference(wolfsentry, action, NULL));
    }

    WOLFSENTRY_EXIT_ON_FAILURE(
        wolfsentry_action_insert(
            wolfsentry,
            "add_to_greenlist",
            -1 /* label_len */,
            WOLFSENTRY_ACTION_FLAG_NONE,
            wolfsentry_action_dummy_callback,
            NULL /* handler_context */,
            &id));

    WOLFSENTRY_EXIT_ON_FAILURE(
        wolfsentry_action_insert(
            wolfsentry,
            "del_from_greenlist",
            -1 /* label_len */,
            WOLFSENTRY_ACTION_FLAG_NONE,
            wolfsentry_action_dummy_callback,
            NULL /* handler_context */,
            &id));

    WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_shutdown(&wolfsentry));

    return 0;
}

#undef PRIVATE_DATA_SIZE
#undef PRIVATE_DATA_ALIGNMENT

#endif /* TEST_DYNAMIC_RULES */

#ifdef TEST_JSON

#include "wolfsentry/wolfsentry_json.h"

#ifdef LWIP
#include "lwip-socket.h"
#else
#include <sys/socket.h>
#include <netinet/in.h>
#endif

#define PRIVATE_DATA_SIZE 32
#define PRIVATE_DATA_ALIGNMENT 16

static wolfsentry_errcode_t test_action(
    struct wolfsentry_context *wolfsentry,
    const struct wolfsentry_action *action,
    void *handler_arg,
    void *caller_arg,
    const struct wolfsentry_event *trigger_event,
    wolfsentry_action_type_t action_type,
    struct wolfsentry_route_table *route_table,
    const struct wolfsentry_route *route,
    wolfsentry_action_res_t *action_results)
{
    const struct wolfsentry_event *parent_event = wolfsentry_route_parent_event(route);
    (void)wolfsentry;
    (void)handler_arg;
    (void)route_table;
    (void)action_results;
    printf("action callback: a=\"%s\" parent_event=\"%s\" trigger=\"%s\" t=%u r_id=%u caller_arg=%p\n",
           wolfsentry_action_get_label(action),
           wolfsentry_event_get_label(parent_event),
           wolfsentry_event_get_label(trigger_event),
           action_type,
           wolfsentry_get_object_id(route),
           caller_arg);
    return 0;
}

static wolfsentry_errcode_t json_feed_file(struct wolfsentry_context *wolfsentry, const char *fname, wolfsentry_config_load_flags_t flags) {
    wolfsentry_errcode_t ret;
    struct wolfsentry_json_process_state *jps;
    FILE *f;
    char buf[512], err_buf[512];

    if (strcmp(fname,"-"))
        f = fopen(fname, "r");
    else
        f = stdin; // GCOV_EXCL_LINE
    if (! f) {
    // GCOV_EXCL_START
        fprintf(stderr, "fopen(%s): %s\n",fname,strerror(errno));
        WOLFSENTRY_ERROR_RETURN(UNIT_TEST_FAILURE);
    // GCOV_EXCL_STOP
    }

    ret = wolfsentry_config_json_init(
        wolfsentry,
        flags,
        &jps);
    if (ret < 0)
        goto out;

    for (;;) {
        size_t n = fread(buf, 1, sizeof buf, f);
        if ((n < sizeof buf) && ferror(f)) {
        // GCOV_EXCL_START
            fprintf(stderr,"fread(%s): %s\n",fname, strerror(errno));
            ret = WOLFSENTRY_ERROR_ENCODE(UNIT_TEST_FAILURE);
            goto out;
        // GCOV_EXCL_STOP
        }

        ret = wolfsentry_config_json_feed(jps, buf, n, err_buf, sizeof err_buf);
        if (ret < 0) {
        // GCOV_EXCL_START
            fprintf(stderr, "%.*s\n", (int)sizeof err_buf, err_buf);
            goto out;
        // GCOV_EXCL_STOP
        }
        if ((n < sizeof buf) && feof(f))
            break;
    }
    ret = wolfsentry_config_json_fini(&jps, err_buf, sizeof err_buf);
    if (ret < 0) {
    // GCOV_EXCL_START
        fprintf(stderr, "%.*s\n", (int)sizeof err_buf, err_buf);
        goto out;
    // GCOV_EXCL_STOP
    }

    ret = WOLFSENTRY_ERROR_ENCODE(OK);

  out:

    if (f != stdin)
        fclose(f);

    return ret;
}


static int test_json(const char *fname) {
    wolfsentry_errcode_t ret;
    struct wolfsentry_context *wolfsentry;
    wolfsentry_ent_id_t id;

    struct wolfsentry_eventconfig config = { .route_private_data_size = PRIVATE_DATA_SIZE, .route_private_data_alignment = PRIVATE_DATA_ALIGNMENT };

    WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_init(WOLFSENTRY_TEST_HPI,
                                               &config,
                                               &wolfsentry));

    WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_action_insert(
                                   wolfsentry,
                                   "handle-insert",
                                   WOLFSENTRY_LENGTH_NULL_TERMINATED,
                                   WOLFSENTRY_ACTION_FLAG_NONE,
                                   test_action,
                                   NULL,
                                   &id));

    WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_action_insert(
                                   wolfsentry,
                                   "handle-delete",
                                   WOLFSENTRY_LENGTH_NULL_TERMINATED,
                                   WOLFSENTRY_ACTION_FLAG_NONE,
                                   test_action,
                                   NULL,
                                   &id));

    WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_action_insert(
                                   wolfsentry,
                                   "handle-match",
                                   WOLFSENTRY_LENGTH_NULL_TERMINATED,
                                   WOLFSENTRY_ACTION_FLAG_NONE,
                                   test_action,
                                   NULL,
                                   &id));

    WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_action_insert(
                                   wolfsentry,
                                   "notify-on-match",
                                   WOLFSENTRY_LENGTH_NULL_TERMINATED,
                                   WOLFSENTRY_ACTION_FLAG_NONE,
                                   test_action,
                                   NULL,
                                   &id));

    WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_action_insert(
                                   wolfsentry,
                                   "handle-connect",
                                   WOLFSENTRY_LENGTH_NULL_TERMINATED,
                                   WOLFSENTRY_ACTION_FLAG_NONE,
                                   test_action,
                                   NULL,
                                   &id));

    WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_action_insert(
                                   wolfsentry,
                                   "handle-connect2",
                                   WOLFSENTRY_LENGTH_NULL_TERMINATED,
                                   WOLFSENTRY_ACTION_FLAG_NONE,
                                   test_action,
                                   NULL,
                                   &id));

    WOLFSENTRY_EXIT_ON_FAILURE(json_feed_file(wolfsentry, fname, WOLFSENTRY_CONFIG_LOAD_FLAG_DRY_RUN));

    WOLFSENTRY_EXIT_ON_FAILURE(json_feed_file(wolfsentry, fname, WOLFSENTRY_CONFIG_LOAD_FLAG_LOAD_THEN_COMMIT));

    WOLFSENTRY_EXIT_ON_SUCCESS(json_feed_file(wolfsentry, fname, WOLFSENTRY_CONFIG_LOAD_FLAG_LOAD_THEN_COMMIT));

    WOLFSENTRY_EXIT_ON_FAILURE(json_feed_file(wolfsentry, fname, WOLFSENTRY_CONFIG_LOAD_FLAG_NONE));

    {
        struct wolfsentry_context *clone;

        WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_context_clone(wolfsentry, &clone, WOLFSENTRY_CLONE_FLAG_AS_AT_CREATION));
        WOLFSENTRY_EXIT_ON_FAILURE(json_feed_file(clone, fname, WOLFSENTRY_CONFIG_LOAD_FLAG_NONE));
        WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_context_exchange(wolfsentry, clone));

        WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_context_free(&clone));
    }


    {
        struct wolfsentry_cursor *cursor;
        struct wolfsentry_route *route;
        struct wolfsentry_route_exports route_exports;
        struct wolfsentry_route_table *static_routes;
        WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_route_get_table_static(wolfsentry, &static_routes));
        WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_route_table_iterate_start(wolfsentry, static_routes, &cursor));
        for (ret = wolfsentry_route_table_iterate_current(wolfsentry, static_routes, cursor, &route);
             ret >= 0;
             ret = wolfsentry_route_table_iterate_next(wolfsentry, static_routes, cursor, &route)) {
            WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_route_export(wolfsentry, route, &route_exports));
            WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_route_exports_render(&route_exports, stdout));
        }
        WOLFSENTRY_EXIT_ON_FAILURE(wolfsentry_route_table_iterate_end(wolfsentry, static_routes, &cursor));
    }

    {
        struct {
            struct wolfsentry_sockaddr sa;
            byte addr_buf[4];
        } remote, local;
        wolfsentry_route_flags_t inexact_matches;
        wolfsentry_action_res_t action_results;

        remote.sa.sa_family = local.sa.sa_family = AF_INET;
        remote.sa.sa_proto = local.sa.sa_proto = IPPROTO_TCP;
        remote.sa.sa_port = 12345;
        local.sa.sa_port = 443;
        remote.sa.addr_len = local.sa.addr_len = sizeof remote.addr_buf * BITS_PER_BYTE;
        remote.sa.interface = local.sa.interface = 1;
        memcpy(remote.sa.addr,"\177\0\0\1",sizeof remote.addr_buf);
        memcpy(local.sa.addr,"\177\0\0\1",sizeof local.addr_buf);

        WOLFSENTRY_EXIT_ON_FAILURE(
            wolfsentry_route_event_dispatch(
                wolfsentry,
                &remote.sa,
                &local.sa,
                WOLFSENTRY_ROUTE_FLAG_DIRECTION_IN,
                "call-in-from-unit-test",
                WOLFSENTRY_LENGTH_NULL_TERMINATED,
                (void *)0x12345678 /* caller_arg */,
                &id,
                &inexact_matches, &action_results));

        WOLFSENTRY_EXIT_ON_FALSE(WOLFSENTRY_CHECK_BITS(action_results, WOLFSENTRY_ACTION_RES_ACCEPT));
    }

    return wolfsentry_shutdown(&wolfsentry);
}

#endif /* TEST_JSON */


int main (int argc, char* argv[]) {
    wolfsentry_errcode_t ret = 0;
    int err = 0;
    (void)argc;
    (void)argv;

#ifdef WOLFSENTRY_ERROR_STRINGS
    WOLFSENTRY_EXIT_ON_FAILURE(WOLFSENTRY_REGISTER_SOURCE());
    WOLFSENTRY_EXIT_ON_FAILURE(WOLFSENTRY_REGISTER_ERROR(UNIT_TEST_FAILURE, "failure within unit test"));
#endif

#ifdef TEST_INIT
    ret = test_init();
    if (! WOLFSENTRY_ERROR_CODE_IS(ret, OK)) {
    // GCOV_EXCL_START
        printf("test_init failed, " WOLFSENTRY_ERROR_FMT "\n", WOLFSENTRY_ERROR_FMT_ARGS(ret));
        err = 1;
    // GCOV_EXCL_STOP
    }
#endif

#ifdef TEST_RWLOCKS
    ret = test_rw_locks();
    if (! WOLFSENTRY_ERROR_CODE_IS(ret, OK)) {
    // GCOV_EXCL_START
        printf("test_rw_locks failed, " WOLFSENTRY_ERROR_FMT "\n", WOLFSENTRY_ERROR_FMT_ARGS(ret));
        err = 1;
    // GCOV_EXCL_STOP
    }
#endif

#ifdef TEST_STATIC_ROUTES
    ret = test_static_routes();
    if (! WOLFSENTRY_ERROR_CODE_IS(ret, OK)) {
    // GCOV_EXCL_START
        printf("test_static_routes failed, " WOLFSENTRY_ERROR_FMT "\n", WOLFSENTRY_ERROR_FMT_ARGS(ret));
        err = 1;
    // GCOV_EXCL_STOP
    }
#endif

#ifdef TEST_DYNAMIC_RULES
    ret = test_dynamic_rules();
    if (! WOLFSENTRY_ERROR_CODE_IS(ret, OK)) {
    // GCOV_EXCL_START
        printf("test_dynamic_rules failed, " WOLFSENTRY_ERROR_FMT "\n", WOLFSENTRY_ERROR_FMT_ARGS(ret));
        err = 1;
    // GCOV_EXCL_STOP
    }
#endif

#ifdef TEST_JSON
#ifdef WOLFSENTRY_PROTOCOL_NAMES
    ret = test_json(TEST_JSON_CONFIG_PATH);
    if (! WOLFSENTRY_ERROR_CODE_IS(ret, OK)) {
    // GCOV_EXCL_START
        printf("test_json failed for " TEST_JSON_CONFIG_PATH ", " WOLFSENTRY_ERROR_FMT "\n", WOLFSENTRY_ERROR_FMT_ARGS(ret));
        err = 1;
    // GCOV_EXCL_STOP
    }
#endif
    ret = test_json(TEST_NUMERIC_JSON_CONFIG_PATH);
    if (! WOLFSENTRY_ERROR_CODE_IS(ret, OK)) {
    // GCOV_EXCL_START
        printf("test_json failed for " TEST_NUMERIC_JSON_CONFIG_PATH ", " WOLFSENTRY_ERROR_FMT "\n", WOLFSENTRY_ERROR_FMT_ARGS(ret));
        err = 1;
    // GCOV_EXCL_STOP
    }
#endif

    return err;
}
