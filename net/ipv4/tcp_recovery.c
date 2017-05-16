#include <linux/tcp.h>
#include <net/tcp.h>

int sysctl_tcp_recovery __read_mostly = TCP_RACK_LOST_RETRANS;

static void tcp_rack_mark_skb_lost(struct sock *sk, struct sk_buff *skb)
{
	struct tcp_sock *tp = tcp_sk(sk);

	tcp_skb_mark_lost_uncond_verify(tp, skb);
	if (TCP_SKB_CB(skb)->sacked & TCPCB_SACKED_RETRANS) {
		/* Account for retransmits that are lost again */
		TCP_SKB_CB(skb)->sacked &= ~TCPCB_SACKED_RETRANS;
		tp->retrans_out -= tcp_skb_pcount(skb);
		NET_ADD_STATS(sock_net(sk), LINUX_MIB_TCPLOSTRETRANSMIT,
			      tcp_skb_pcount(skb));
	}
}

static bool tcp_rack_sent_after(u64 t1, u64 t2, u32 seq1, u32 seq2)
{
	return t1 > t2 || (t1 == t2 && after(seq1, seq2));
}

/* RACK loss detection (IETF draft draft-ietf-tcpm-rack-01):
 *
 * Marks a packet lost, if some packet sent later has been (s)acked.

 * The underlying idea is similar to the traditional dupthresh and FACK
 * but they look at different metrics:
 *
 * dupthresh: 3 OOO packets delivered (packet count)
 * FACK: sequence delta to highest sacked sequence (sequence space)
 * RACK: sent time delta to the latest delivered packet (time domain)
 *
 * The advantage of RACK is it applies to both original and retransmitted
 * packet and therefore is robust against tail losses. Another advantage
 * is being more resilient to reordering by simply allowing some
 * "settling delay", instead of tweaking the dupthresh.
 *
 * The current version is only used after recovery starts but can be
 * easily extended to detect the first loss.
 */
int tcp_rack_mark_lost(struct sock *sk)
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct sk_buff *skb;
	u32 reo_wnd, prior_retrans = tp->retrans_out;

	if (inet_csk(sk)->icsk_ca_state < TCP_CA_Recovery || !tp->rack.advanced)
		return 0;

	/* Reset the advanced flag to avoid unnecessary queue scanning */
	tp->rack.advanced = 0;

	/* To be more reordering resilient, allow min_rtt/4 settling delay
	 * (lower-bounded to 1000uS). We use min_rtt instead of the smoothed
	 * RTT because reordering is often a path property and less related
	 * to queuing or delayed ACKs.
	 *
	 * TODO: measure and adapt to the observed reordering delay, and
	 * use a timer to retransmit like the delayed early retransmit.
	 */
	reo_wnd = 1000;
	if (tp->rack.reord && tcp_min_rtt(tp) != ~0U)
		reo_wnd = max(tcp_min_rtt(tp) >> 2, reo_wnd);

	tcp_for_write_queue(skb, sk) {
		struct tcp_skb_cb *scb = TCP_SKB_CB(skb);

		if (skb == tcp_send_head(sk))
			break;

		/* Skip ones already (s)acked */
		if (!after(scb->end_seq, tp->snd_una) ||
		    scb->sacked & TCPCB_SACKED_ACKED)
			continue;

		if (skb_mstamp_after(&tp->rack.mstamp, &skb->skb_mstamp)) {

			if (skb_mstamp_us_delta(&tp->rack.mstamp,
						&skb->skb_mstamp) <= reo_wnd)
				continue;

			/* skb is lost if packet sent later is sacked */
			tcp_skb_mark_lost_uncond_verify(tp, skb);
			if (scb->sacked & TCPCB_SACKED_RETRANS) {
				scb->sacked &= ~TCPCB_SACKED_RETRANS;
				tp->retrans_out -= tcp_skb_pcount(skb);
				NET_INC_STATS_BH(sock_net(sk),
						 LINUX_MIB_TCPLOSTRETRANSMIT);
			}
		} else if (!(scb->sacked & TCPCB_RETRANS)) {
			/* Original data are sent sequentially so stop early
			 * b/c the rest are all sent after rack_sent
			 */
			break;
		}
		if (tcp_rack_sent_after(tp->rack.mstamp, skb->skb_mstamp,
					tp->rack.end_seq, scb->end_seq)) {
			/* Step 3 in draft-cheng-tcpm-rack-00.txt:
			 * A packet is lost if its elapsed time is beyond
			 * the recent RTT plus the reordering window.
			 */
			u32 elapsed = tcp_stamp_us_delta(tp->tcp_mstamp,
							 skb->skb_mstamp);
			s32 remaining = tp->rack.rtt_us + reo_wnd - elapsed;

			if (remaining < 0) {
				tcp_rack_mark_skb_lost(sk, skb);
				continue;
			}
		}
	}
	return prior_retrans - tp->retrans_out;
}

/* Record the most recently (re)sent time among the (s)acked packets */
void tcp_rack_advance(struct tcp_sock *tp,
		      const struct skb_mstamp *xmit_time, u8 sacked)
{
	if (tp->rack.mstamp.v64 &&
	    !skb_mstamp_after(xmit_time, &tp->rack.mstamp))
		return;

	if (sacked & TCPCB_RETRANS) {
		struct skb_mstamp now;

		/* If the sacked packet was retransmitted, it's ambiguous
		 * whether the retransmission or the original (or the prior
		 * retransmission) was sacked.
		 *
		 * If the original is lost, there is no ambiguity. Otherwise
		 * we assume the original can be delayed up to aRTT + min_rtt.
		 * the aRTT term is bounded by the fast recovery or timeout,
		 * so it's at least one RTT (i.e., retransmission is at least
		 * an RTT later).
		 */
		skb_mstamp_get(&now);
		if (skb_mstamp_us_delta(&now, xmit_time) < tcp_min_rtt(tp))
			return;
	}

	tp->rack.mstamp = *xmit_time;
	tp->rack.rtt_us = rtt_us;
	tp->rack.end_seq = end_seq;
	tp->rack.advanced = 1;
}
