#include <bitcoin/script.h>
#include <closingd/gen_closing_wire.h>
#include <common/close_tx.h>
#include <common/initial_commit_tx.h>
#include <common/utils.h>
#include <errno.h>
#include <gossipd/gen_gossip_wire.h>
#include <inttypes.h>
#include <lightningd/chaintopology.h>
#include <lightningd/channel.h>
#include <lightningd/closing_control.h>
#include <lightningd/lightningd.h>
#include <lightningd/log.h>
#include <lightningd/options.h>
#include <lightningd/peer_control.h>
#include <lightningd/subd.h>
#include <closingd/closing.h>


void peer_start_closingd(struct channel *channel,
			 const struct crypto_state *cs,
			 int peer_fd, int gossip_fd,
			 bool reconnected,
			 const u8 *channel_reestablish)
{
	u8 *initmsg;
	u64 minfee, startfee, feelimit;
	u64 num_revocations;
	u64 funding_msatoshi, our_msatoshi, their_msatoshi;
	struct lightningd *ld = channel->peer->ld;

	if (!channel->remote_shutdown_scriptpubkey) {
		channel_internal_error(channel,
				       "Can't start closing: no remote info");
		return;
	}

	struct subd *sd = tal(ld, struct subd);
	sd->name = "lightning_closingd";
	sd->channel = channel;
	sd->log = channel->log;

	channel_set_owner(channel, sd);	

	/* BOLT #2:
	 *
	 * A sending node MUST set `fee_satoshis` lower than or equal
	 * to the base fee of the final commitment transaction as
	 * calculated in [BOLT
	 * #3](03-transactions.md#fee-calculation).
	 */
	feelimit = commit_tx_base_fee(channel->channel_info.feerate_per_kw[LOCAL],
				      0);

	minfee = commit_tx_base_fee(get_feerate(ld->topology, FEERATE_SLOW), 0);
	startfee = commit_tx_base_fee(get_feerate(ld->topology, FEERATE_NORMAL),
				      0);

	if (startfee > feelimit)
		startfee = feelimit;
	if (minfee > feelimit)
		minfee = feelimit;

	num_revocations
		= revocations_received(&channel->their_shachain.chain);

	/* BOLT #3:
	 *
	 * The amounts for each output MUST BE rounded down to whole satoshis.
	 */
	/* Convert unit */
	funding_msatoshi = channel->funding_satoshi * 1000;
	/* What is not ours is theirs */
	our_msatoshi = channel->our_msatoshi;
	their_msatoshi = funding_msatoshi - our_msatoshi;
	initmsg = towire_closing_init(tmpctx,
				      cs,
				      &channel->seed,
				      &channel->funding_txid,
				      channel->funding_outnum,
				      channel->funding_satoshi,
				      &channel->channel_info.remote_fundingkey,
				      channel->funder,
				      our_msatoshi / 1000, /* Rounds down */
				      their_msatoshi / 1000, /* Rounds down */
				      channel->our_config.dust_limit_satoshis,
				      minfee, feelimit, startfee,
				      p2wpkh_for_keyidx(tmpctx, ld,
							channel->final_key_idx),
				      channel->remote_shutdown_scriptpubkey,
				      reconnected,
				      channel->next_index[LOCAL],
				      channel->next_index[REMOTE],
				      num_revocations,
				      deprecated_apis,
				      channel_reestablish);

	/* We don't expect a response: it will give us feedback on
	 * signatures sent and received, then closing_complete. */
	close_channel(channel, take(initmsg), NULL);	
}
