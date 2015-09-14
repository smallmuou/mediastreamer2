/*
mediastreamer2 library - modular sound and video processing and streaming
Copyright (C) 2006-2013 Belledonne Communications, Grenoble

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/

#include "mediastreamer2/mediastream.h"
#include "mediastreamer2_tester.h"
#include "mediastreamer2_tester_private.h"
#include <math.h>

#ifdef _MSC_VER
#define unlink _unlink
#endif

static RtpProfile rtp_profile;

#define T140_PAYLOAD_TYPE 98
#define T140_RED_PAYLOAD_TYPE 99

static int tester_init(void) {
	ms_init();
	ortp_init();
	rtp_profile_set_payload(&rtp_profile, T140_PAYLOAD_TYPE, &payload_type_t140);
	rtp_profile_set_payload(&rtp_profile, T140_RED_PAYLOAD_TYPE, &payload_type_t140_red);
	return 0;
}

static int tester_cleanup(void) {
	ms_exit();
	rtp_profile_clear_all(&rtp_profile);
	return 0;
}

typedef struct _text_stream_tester_stats_t {
	OrtpEvQueue *q;
	rtp_stats_t rtp;
	int number_of_received_char;
	char received_chars[128];
} text_stream_tester_stats_t;

typedef struct _text_stream_tester_t {
	TextStream *ts;
	text_stream_tester_stats_t stats;
	char* local_ip;
	int local_rtp;
	int local_rtcp;
	int payload_type;
} text_stream_tester_t;


static void reset_stats(text_stream_tester_stats_t *s) {
	memset(s, 0, sizeof(text_stream_tester_stats_t));
}

void text_stream_tester_set_local_ip(text_stream_tester_t* obj, const char* ip) {
	char* new_ip = ip ? ms_strdup(ip) : NULL;
	if (obj->local_ip) ms_free(obj->local_ip);
	obj->local_ip = new_ip;
}

text_stream_tester_t* text_stream_tester_new() {
	text_stream_tester_t* tst = ms_new0(text_stream_tester_t, 1);
	text_stream_tester_set_local_ip(tst, "127.0.0.1");
	tst->local_rtp = -1; /*random*/
	tst->local_rtcp = -1; /*random*/
	return  tst;
}

text_stream_tester_t* text_stream_tester_create(const char* local_ip, int local_rtp, int local_rtcp) {
	text_stream_tester_t *tst = text_stream_tester_new();
	if (local_ip)
		text_stream_tester_set_local_ip(tst, local_ip);
	tst->local_rtp = local_rtp;
	tst->local_rtcp = local_rtcp;
	return tst;
}

void text_stream_tester_destroy(text_stream_tester_t* obj) {
	if(obj->local_ip) ms_free(obj->local_ip);
	ms_free(obj);
}

static void create_text_stream(text_stream_tester_t *tst, int payload_type) {
	tst->ts = text_stream_new2(tst->local_ip, tst->local_rtp, tst->local_rtcp);
	tst->local_rtp = rtp_session_get_local_port(tst->ts->ms.sessions.rtp_session);
	tst->local_rtcp = rtp_session_get_local_rtcp_port(tst->ts->ms.sessions.rtp_session);
	reset_stats(&tst->stats);
	rtp_session_set_multicast_loopback(tst->ts->ms.sessions.rtp_session, TRUE);
	tst->stats.q = ortp_ev_queue_new();
	rtp_session_register_event_queue(tst->ts->ms.sessions.rtp_session, tst->stats.q);
	tst->payload_type = payload_type;
}

static void destroy_text_stream(text_stream_tester_t *tst) {
	text_stream_stop(tst->ts);
}

static void init_text_streams(text_stream_tester_t *tst1, text_stream_tester_t *tst2, bool_t avpf, bool_t one_way, OrtpNetworkSimulatorParams *params, int payload_type) {
	create_text_stream(tst1, payload_type);
	create_text_stream(tst2, payload_type);

	/* Configure network simulator. */
	if ((params != NULL) && (params->enabled == TRUE)) {
		rtp_session_enable_network_simulation(tst1->ts->ms.sessions.rtp_session, params);
		rtp_session_enable_network_simulation(tst2->ts->ms.sessions.rtp_session, params);
	}
	
	text_stream_start(tst1->ts, &rtp_profile, tst2->local_ip, tst2->local_rtp, tst2->local_ip, tst2->local_rtcp, payload_type);
	text_stream_start(tst2->ts, &rtp_profile, tst1->local_ip, tst1->local_rtp, tst1->local_ip, tst1->local_rtcp, payload_type);
}

static void uninit_text_streams(text_stream_tester_t *tst1, text_stream_tester_t *tst2) {
	destroy_text_stream(tst1);
	destroy_text_stream(tst2);
}

static void event_queue_cb(MediaStream *ms, void *user_pointer) {
	text_stream_tester_t *tst = (text_stream_tester_t *)user_pointer;
	if (tst->stats.q != NULL) {
		OrtpEvent *ev = NULL;
		while ((ev = ortp_ev_queue_get(tst->stats.q)) != NULL) {
			OrtpEventType evt = ortp_event_get_type(ev);
			OrtpEventData *evd = ortp_event_get_data(ev);
			if (evt == ORTP_EVENT_RTT_CHARACTER_RECEIVED) {
				ms_message("Received RTT char: %lu, %c", (unsigned long)evd->info.received_rtt_character, (char)evd->info.received_rtt_character);
				tst->stats.received_chars[tst->stats.number_of_received_char++] = (char)evd->info.received_rtt_character;
			}
			ortp_event_destroy(ev);
		}
	}
}

static void basic_text_stream(void) {
	text_stream_tester_t* marielle = text_stream_tester_new();
	text_stream_tester_t* margaux = text_stream_tester_new();
	const char* helloworld = "Hello World !";
	int i = 0, strcmpresult = -2;
	
	init_text_streams(marielle, margaux, FALSE, FALSE, NULL, T140_PAYLOAD_TYPE /* ignored */);
	
	for (; i < strlen(helloworld); i++) {
		char c = helloworld[i];
		text_stream_putchar32(margaux->ts, (uint32_t)c);
	}
	
	BC_ASSERT_TRUE(wait_for_until_with_parse_events(&marielle->ts->ms, &margaux->ts->ms, &marielle->stats.number_of_received_char, strlen(helloworld), 5000, event_queue_cb, marielle, NULL, NULL));
	ms_message("Received message is: %s", marielle->stats.received_chars);
	strcmpresult = strcmp(marielle->stats.received_chars, helloworld);
	BC_ASSERT_EQUAL(strcmpresult, 0, int, "%d");

	uninit_text_streams(marielle, margaux);
	text_stream_tester_destroy(marielle);
	text_stream_tester_destroy(margaux);
}

static void basic_text_stream2(void) {
	text_stream_tester_t* marielle = text_stream_tester_new();
	text_stream_tester_t* margaux = text_stream_tester_new();
	const char* helloworld = "Hello World !";
	int i = 0, strcmpresult = -2;
	int dummy = 0;
	
	init_text_streams(marielle, margaux, FALSE, FALSE, NULL, T140_PAYLOAD_TYPE /* ignored */);
	
	for (; i < strlen(helloworld); i++) {
		char c = helloworld[i];
		text_stream_putchar32(margaux->ts, (uint32_t)c);
		wait_for_until_with_parse_events(&marielle->ts->ms, &margaux->ts->ms, &dummy, 1, 500, event_queue_cb, marielle, NULL, NULL);
	}
	
	BC_ASSERT_TRUE(wait_for_until(&marielle->ts->ms, &margaux->ts->ms, &marielle->stats.number_of_received_char, strlen(helloworld), 1000));
	ms_message("Received message is: %s", marielle->stats.received_chars);
	strcmpresult = strcmp(marielle->stats.received_chars, helloworld);
	BC_ASSERT_EQUAL(strcmpresult, 0, int, "%d");

	uninit_text_streams(marielle, margaux);
	text_stream_tester_destroy(marielle);
	text_stream_tester_destroy(margaux);
}

static test_t tests[] = {
	{ "Basic text stream: copy paste short text", basic_text_stream },
	{ "Basic text stream: slow typing", basic_text_stream2 }
};

test_suite_t text_stream_test_suite = {
	"TextStream",
	tester_init,
	tester_cleanup,
	sizeof(tests) / sizeof(tests[0]),
	tests
};