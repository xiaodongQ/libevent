/*
 * Copyright (c) 2003-2007 Niels Provos <provos@citi.umich.edu>
 * Copyright (c) 2007-2009 Niels Provos and Nick Mathewson
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifdef WIN32
#include <winsock2.h>
#include <windows.h>
#endif

#ifdef HAVE_CONFIG_H
#include "event-config.h"
#endif

#if 0
#include <sys/types.h>
#include <sys/stat.h>
#ifdef _EVENT_HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
#include <sys/queue.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#endif


#ifndef WIN32
#include <sys/socket.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#include <netdb.h>
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include <event2/util.h>
#include <event2/event.h>
#include <event2/event_compat.h>
#include <event2/dns.h>
#include <event2/dns_compat.h>

#include "event-config.h"
#include "regress.h"
#include "tinytest.h"
#include "tinytest_macros.h"

/* ============================================================ */
/* Code to wrap up old legacy test cases that used setup() and cleanup().
 *
 * Not all of the tests designated "legacy" are ones that used setup() and
 * cleanup(), of course.  A test is legacy it it uses setup()/cleanup(), OR
 * if it wants to find its event base/socketpair in global variables (ugh),
 * OR if it wants to communicate success/failure through test_ok.
 */

/* This is set to true if we're inside a legacy test wrapper.  It lets the
   setup() and cleanup() functions in regress.c know they're not needed.
 */
int in_legacy_test_wrapper = 0;

static void dnslogcb(int w, const char *m)
{
        TT_BLATHER((m));
}

/* creates a temporary file with the data in it */
evutil_socket_t
regress_make_tmpfile(const void *data, size_t datalen)
{
#ifndef WIN32
	char tmpfilename[32];
	int fd;
	strcpy(tmpfilename, "/tmp/eventtmp.XXXXXX");
	fd = mkstemp(tmpfilename);
	if (fd == -1)
		return (-1);
	if (write(fd, data, datalen) != datalen) {
		close(fd);
		return (-1);
	}
	lseek(fd, 0, SEEK_SET);
	/* remove it from the file system */
	unlink(tmpfilename);
	return (fd);
#else
	/* we need a windows implementation here */
	return (-1);
#endif
}

static void *
basic_test_setup(const struct testcase_t *testcase)
{
	struct event_base *base = NULL;
	int spair[2] = { -1, -1 };
	struct basic_test_data *data = NULL;

	if (testcase->flags & TT_NEED_SOCKETPAIR) {
		if (evutil_socketpair(AF_UNIX, SOCK_STREAM, 0, spair) == -1) {
			fprintf(stderr, "%s: socketpair\n", __func__);
			exit(1);
		}

		if (evutil_make_socket_nonblocking(spair[0]) == -1) {
			fprintf(stderr, "fcntl(O_NONBLOCK)");
			exit(1);
		}

		if (evutil_make_socket_nonblocking(spair[1]) == -1) {
			fprintf(stderr, "fcntl(O_NONBLOCK)");
			exit(1);
		}
	}
	if (testcase->flags & TT_NEED_BASE) {
		if (testcase->flags & TT_LEGACY)
			base = event_init();
		else
			base = event_base_new();
		if (!base)
			exit(1);
	}

        if (testcase->flags & TT_NEED_DNS) {
                evdns_set_log_fn(dnslogcb);
                if (evdns_init())
                        return NULL; /* fast failure *//*XXX asserts. */
        }

	data = calloc(1, sizeof(*data));
	if (!data)
		exit(1);
	data->base = base;
	data->pair[0] = spair[0];
	data->pair[1] = spair[1];
	return data;
}

static int
basic_test_cleanup(const struct testcase_t *testcase, void *ptr)
{
	struct basic_test_data *data = ptr;
	if (testcase->flags & TT_NEED_SOCKETPAIR) {
                if (data->pair[0] != -1)
                        EVUTIL_CLOSESOCKET(data->pair[0]);
                if (data->pair[1] != -1)
                        EVUTIL_CLOSESOCKET(data->pair[1]);
        }

        if (testcase->flags & TT_NEED_BASE) {
                event_base_free(data->base);
        }

        if (testcase->flags & TT_NEED_DNS) {
                evdns_shutdown(0);
        }

	free(data);

	return 1;
}

const struct testcase_setup_t basic_setup = {
	basic_test_setup, basic_test_cleanup
};

/* The "data" for a legacy test is just a pointer to the void fn(void)
   function implementing the test case.  We need to set up some globals,
   though, since that's where legacy tests expect to find a socketpair
   (sometimes) and a global event_base (sometimes).
 */
static void *
legacy_test_setup(const struct testcase_t *testcase)
{
	struct basic_test_data *data = basic_test_setup(testcase);
	global_base = data->base;
	pair[0] = data->pair[0];
	pair[1] = data->pair[1];
	data->legacy_test_fn = testcase->setup_data;
	return data;
}

/* This function is the implementation of every legacy test case.  It
   sets test_ok to 0, invokes the test function, and tells tinytest that
   the test failed if the test didn't set test_ok to 1.
 */
void
run_legacy_test_fn(void *ptr)
{
	struct basic_test_data *data = ptr;
	test_ok = called = 0;

        in_legacy_test_wrapper = 1;
	data->legacy_test_fn(); /* This part actually calls the test */
        in_legacy_test_wrapper = 0;

	if (!test_ok)
		tt_abort_msg("Legacy unit test failed");

end:
	test_ok = 0;
}

/* This function doesn't have to clean up ptr (which is just a pointer
   to the test function), but it may need to close the socketpair or
   free the event_base.
 */
static int
legacy_test_cleanup(const struct testcase_t *testcase, void *ptr)
{
	int r = basic_test_cleanup(testcase, ptr);
	pair[0] = pair[1] = -1;
	global_base = NULL;
	return r;
}

const struct testcase_setup_t legacy_setup = {
	legacy_test_setup, legacy_test_cleanup
};

/* ============================================================ */


struct testcase_t thread_testcases[] = {
#if defined(_EVENT_HAVE_PTHREADS) && !defined(_EVENT_DISABLE_THREAD_SUPPORT)
	{ "pthreads", regress_threads, TT_FORK, NULL, NULL, },
#else
	{ "pthreads", NULL, TT_SKIP, NULL, NULL },
#endif
	END_OF_TESTCASES
};

struct testgroup_t testgroups[] = {
	{ "main/", main_testcases },
	{ "heap/", minheap_testcases },
	{ "et/", edgetriggered_testcases },
	{ "evbuffer/", evbuffer_testcases },
	{ "signal/", signal_testcases },
	{ "util/", util_testcases },
	{ "bufferevent/", bufferevent_testcases },
	{ "http/", http_testcases },
	{ "dns/", dns_testcases },
	{ "rpc/", rpc_testcases },
	{ "thread/", thread_testcases },
#ifdef WIN32
	{ "iocp/", iocp_testcases },
#endif
        END_OF_GROUPS
};

int
main(int argc, const char **argv)
{
#ifdef WIN32
	WORD wVersionRequested;
	WSADATA wsaData;
	int	err;

	wVersionRequested = MAKEWORD(2, 2);

	err = WSAStartup(wVersionRequested, &wsaData);
#endif

#ifndef WIN32
	if (signal(SIGPIPE, SIG_IGN) == SIG_ERR)
		return 1;
#endif

#ifdef WIN32
	tinytest_skip(testgroups, "http/connection_retry");
#endif

        if (tinytest_main(argc,argv,testgroups))
                return 1;

	return 0;
}
