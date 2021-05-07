#include <stdio.h>
#include <string.h>
#include<stdbool.h>
#include "protocol.h"
#include "datalink.h"

#define DATA_TIMER  2000
#define ACK_TIMER   1000
#define MAX_SEQ 7  //定义最大序号
#define NR_BUFS ((MAX_SEQ + 1)/2)   //由最大序号计算最大发送窗口
#define inc(k) if (k<MAX_SEQ) k = k+1; else k = 0;

typedef  unsigned int seq_nr;
//typedef  unsigned char packet[PKT_LEN];

struct FRAME {
	unsigned char kind; /* FRAME_DATA */
	unsigned char ack;
	unsigned char seq;
	unsigned char data[PKT_LEN];
	unsigned int  padding;
};



static seq_nr next_frame_to_send = 0, nbuffered = 0, frame_expected = 0, too_far = NR_BUFS, ack_expected = 0;
unsigned char out_buffer[NR_BUFS][PKT_LEN];
unsigned char in_buffer[NR_BUFS][PKT_LEN];
static bool arrived[NR_BUFS];
static int phl_ready = 0;
static bool no_nak = true;

//between函数，用来判断序号是否在窗口内
static bool between(seq_nr a, seq_nr b, seq_nr c)
{
	return  ((a <= b) && (b < c)) || ((c < a) && (a <= b)) || ((b < c) && (c < a));
}

static void put_frame(unsigned char *frame, int len)
{
	*(unsigned int *)(frame + len) = crc32(frame, len);
	send_frame(frame, len + 4);
	phl_ready = 0;
}


//TOOD:send_data_frame需要加参数，书上的send_data_frame不是每次都是next_frame_to_send
static void send_data_frame(seq_nr frame_nr)
{
	struct FRAME s;

	s.kind = FRAME_DATA;
	s.seq = frame_nr;
	s.ack = (frame_expected + MAX_SEQ) % (MAX_SEQ + 1);
	memcpy(s.data, out_buffer[frame_nr%NR_BUFS], PKT_LEN);

	dbg_frame("Send DATA %d %d, ID %d\n", s.seq, s.ack, *(short *)s.data);

	put_frame((unsigned char *)&s, 3 + PKT_LEN);
	start_timer(frame_nr%NR_BUFS, DATA_TIMER);

	stop_ack_timer();
}

static void send_ack_frame(void)
{
	struct FRAME s;

	s.kind = FRAME_ACK;
	s.ack = (frame_expected + MAX_SEQ) % (MAX_SEQ + 1);

	dbg_frame("Send ACK  %d\n", s.ack);

	put_frame((unsigned char *)&s, 2);

	stop_ack_timer();
}

static void send_nak_frame(void)
{
	struct FRAME s;
	s.kind = FRAME_NAK;
	s.ack = (frame_expected + MAX_SEQ) % (MAX_SEQ + 1);
	no_nak = false;

	dbg_frame("Send NAK  %d\n", s.ack);
	put_frame((unsigned char *)&s, 2);

	stop_ack_timer();
}

int main(int argc, char **argv)
{
	
	int event, arg;
	struct FRAME f;
	int len = 0;
	for (int i = 0; i < NR_BUFS; i++)	arrived[i] = false;

	protocol_init(argc, argv);
	lprintf("Designed by Shan Yuxuan, build: " __DATE__"  "__TIME__"\n");

	enable_network_layer();

	for (;;) {
		event = wait_for_event(&arg);

		switch (event) {
		case NETWORK_LAYER_READY:
			dbg_event("Network layer ready:\n");
			get_packet(out_buffer[next_frame_to_send%NR_BUFS]);
			nbuffered++;
			send_data_frame(next_frame_to_send);
			stop_ack_timer();
			inc(next_frame_to_send);
			
			break;

		case PHYSICAL_LAYER_READY:
			dbg_event("Physical layer ready:\n");
			phl_ready = 1;
			break;

		case FRAME_RECEIVED:
			dbg_event("Frame has received:\n");
			len = recv_frame((unsigned char *)&f, sizeof f);
			if (len < 5 || crc32((unsigned char *)&f, len) != 0) {
				dbg_event("**** Receiver Error, Bad CRC Checksum\n");

				if (no_nak)	send_nak_frame();
				break;
			}

			if (f.kind == FRAME_DATA) {
				dbg_frame("Recv DATA %d %d, ID %d\n", f.seq, f.ack, *(short *)f.data);

				if (f.seq != frame_expected && no_nak)	send_nak_frame();
				else	start_ack_timer(ACK_TIMER);
				dbg_frame("123\n");
				if (between(frame_expected, f.seq, too_far) && (arrived[f.seq%NR_BUFS] == false)) {
					arrived[f.seq%NR_BUFS] = true;
					dbg_frame("1234\n");
					memcpy(in_buffer[f.seq%NR_BUFS], f.data, len -	 7);
					dbg_frame("12345\n");
					while (arrived[frame_expected%NR_BUFS]) {
						//dbg_frame("99\n");
						put_packet(in_buffer[frame_expected%NR_BUFS], len - 7);
						dbg_frame("100\n");
						no_nak = true;
						arrived[frame_expected] = false;
						inc(frame_expected);
						inc(too_far);
						start_ack_timer(ACK_TIMER);
					}
					dbg_frame("123456\n");
				}
			}
			if (f.kind == FRAME_NAK && between(ack_expected, (f.ack + 1) % (MAX_SEQ + 1), next_frame_to_send)) {
				dbg_frame("Recv NAK %d\n", f.ack);
				send_data_frame((f.ack + 1) % (MAX_SEQ + 1));
			}
				

			while (between(ack_expected, f.ack, next_frame_to_send)) {
				nbuffered--;
				stop_timer(ack_expected%NR_BUFS);
				inc(ack_expected);
			}

			break;

		case DATA_TIMEOUT:
			dbg_event("---- DATA %d timeout\n", arg);
			send_data_frame(ack_expected);
			break;

		case ACK_TIMEOUT:
			dbg_event("----ACK %d timeout\n", arg);
			send_ack_frame();
			break;
		}
		if (nbuffered < NR_BUFS && phl_ready)
			enable_network_layer();
		else
			disable_network_layer();
	}
}
