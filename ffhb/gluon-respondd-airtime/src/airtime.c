#include <sys/socket.h>
#include <linux/nl80211.h>
#include <netlink/netlink.h>
#include <netlink/genl/genl.h>
#include <netlink/genl/ctrl.h>
#include <net/if.h>

#include "airtime.h"

static struct airtime cur_airtime = {
	{ .frequency = 0 },
	{ .frequency = 0 },
};

/*
 * Excerpt from nl80211.h:
 * enum nl80211_survey_info - survey information
 *
 * These attribute types are used with %NL80211_ATTR_SURVEY_INFO
 * when getting information about a survey.
 *
 * @__NL80211_SURVEY_INFO_INVALID: attribute number 0 is reserved
 * @NL80211_SURVEY_INFO_FREQUENCY: center frequency of channel
 * @NL80211_SURVEY_INFO_NOISE: noise level of channel (u8, dBm)
 * @NL80211_SURVEY_INFO_IN_USE: channel is currently being used
 * @NL80211_SURVEY_INFO_CHANNEL_TIME: amount of time (in ms) that the radio
 *	spent on this channel
 * @NL80211_SURVEY_INFO_CHANNEL_TIME_BUSY: amount of the time the primary
 *	channel was sensed busy (either due to activity or energy detect)
 * @NL80211_SURVEY_INFO_CHANNEL_TIME_EXT_BUSY: amount of time the extension
 *	channel was sensed busy
 * @NL80211_SURVEY_INFO_CHANNEL_TIME_RX: amount of time the radio spent
 *	receiving data
 * @NL80211_SURVEY_INFO_CHANNEL_TIME_TX: amount of time the radio spent
 *	transmitting data
 * @NL80211_SURVEY_INFO_MAX: highest survey info attribute number
 *	currently defined
 * @__NL80211_SURVEY_INFO_AFTER_LAST: internal use
 */

static int survey_airtime_handler(struct nl_msg *msg, void *arg)
{
	struct genlmsghdr *gnlh;
	struct airtime_result *result;

	struct nlattr *tb[NL80211_ATTR_MAX + 1];
	struct nlattr *sinfo[NL80211_SURVEY_INFO_MAX + 1];
	static struct nla_policy survey_policy[NL80211_SURVEY_INFO_MAX + 1] = {
		[NL80211_SURVEY_INFO_FREQUENCY] = { .type = NLA_U32 },
	};

	result = (struct airtime_result *) arg;

	gnlh = nlmsg_data(nlmsg_hdr(msg));
	if (!gnlh)
		goto error;

	nla_parse(tb, NL80211_ATTR_MAX, genlmsg_attrdata(gnlh, 0),genlmsg_attrlen(gnlh, 0), NULL);

	if (!tb[NL80211_ATTR_SURVEY_INFO])
		goto abort;

	if (nla_parse_nested(sinfo, NL80211_SURVEY_INFO_MAX, tb[NL80211_ATTR_SURVEY_INFO], survey_policy))
		goto abort;

	// Channel inactive?
	if (!sinfo[NL80211_SURVEY_INFO_IN_USE])
		goto abort;

	uint64_t frequency  = nla_get_u32(sinfo[NL80211_SURVEY_INFO_FREQUENCY]);

	if (frequency != result->frequency) {
		result->frequency          = frequency;
		result->active_time.offset = nla_get_u64(sinfo[NL80211_SURVEY_INFO_CHANNEL_TIME]);
		result->busy_time.offset   = nla_get_u64(sinfo[NL80211_SURVEY_INFO_CHANNEL_TIME_BUSY]);
		result->rx_time.offset     = nla_get_u64(sinfo[NL80211_SURVEY_INFO_CHANNEL_TIME_RX]);
		result->tx_time.offset     = nla_get_u64(sinfo[NL80211_SURVEY_INFO_CHANNEL_TIME_TX]);
		result->active_time.current = 0;
		result->busy_time.current   = 0;
		result->rx_time.current     = 0;
		result->tx_time.current     = 0;
	} else {
		result->active_time.current = nla_get_u64(sinfo[NL80211_SURVEY_INFO_CHANNEL_TIME]) - result->active_time.offset;
		result->busy_time.current   = nla_get_u64(sinfo[NL80211_SURVEY_INFO_CHANNEL_TIME_BUSY]) - result->busy_time.offset;
		result->rx_time.current     = nla_get_u64(sinfo[NL80211_SURVEY_INFO_CHANNEL_TIME_RX]) - result->rx_time.offset;
		result->tx_time.current     = nla_get_u64(sinfo[NL80211_SURVEY_INFO_CHANNEL_TIME_TX]) - result->tx_time.offset;
	}

	result->noise = nla_get_u8(sinfo[NL80211_SURVEY_INFO_NOISE]);

error:
abort:
	return NL_SKIP;
}

static int get_airtime_for_interface(struct airtime_result *result, const char *interface) {
	int error = 0;
	int ctrl, ifx, flags;
	struct nl_sock *sk = NULL;
	struct nl_msg *msg = NULL;
	enum nl80211_commands cmd;

#define CHECK(x) { if (!(x)) { printf("error on line %d\n",  __LINE__); error = 1; goto out; } }

	CHECK(sk = nl_socket_alloc());
	CHECK(genl_connect(sk) >= 0);

	CHECK(ctrl = genl_ctrl_resolve(sk, NL80211_GENL_NAME));
	CHECK(nl_socket_modify_cb(sk, NL_CB_VALID, NL_CB_CUSTOM, survey_airtime_handler, result) == 0);
	CHECK(msg = nlmsg_alloc());

	/* device does not exist */
	if (!(ifx = if_nametoindex(interface))){
		error = -1;
		goto out;
	}

	cmd = NL80211_CMD_GET_SURVEY;
	flags = 0;
	flags |= NLM_F_DUMP;

	/* TODO: check return? */
	genlmsg_put(msg, 0, 0, ctrl, 0, flags, cmd, 0);

	NLA_PUT_U32(msg, NL80211_ATTR_IFINDEX, ifx);

	CHECK(nl_send_auto_complete(sk, msg) >= 0);
	CHECK(nl_recvmsgs_default(sk) >= 0);

#undef CHECK

nla_put_failure:
out:
	if (msg)
		nlmsg_free(msg);

	if (sk)
		nl_socket_free(sk);

	return error;
}

struct airtime* get_airtime(char *wifi_0_dev,char *wifi_1_dev) {
	get_airtime_for_interface(&cur_airtime.radio0, wifi_0_dev);
	get_airtime_for_interface(&cur_airtime.radio1, wifi_1_dev);

	return &cur_airtime;
}
