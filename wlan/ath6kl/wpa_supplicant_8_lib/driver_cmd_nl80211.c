/*
 * Driver interaction with extended Linux CFG8021
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Alternatively, this software may be distributed under the terms of BSD
 * license.
 *
 */

#include "driver_nl80211.h"
#include "driver_cmd_common.h"

#include "wpa_supplicant_i.h"
#include "config.h"

#define WPA_EVENT_DRIVER_STATE		"CTRL-EVENT-DRIVER-STATE "

#define WPA_PS_ENABLED		0
#define WPA_PS_DISABLED		1

typedef struct android_wifi_priv_cmd {
	char *buf;
	int used_len;
	int total_len;
} android_wifi_priv_cmd;

int send_and_recv_msgs(struct wpa_driver_nl80211_data *drv, struct nl_msg *msg,
		       int (*valid_handler)(struct nl_msg *, void *),
		       void *valid_data);

static int drv_errors = 0;

static void wpa_driver_send_hang_msg(struct wpa_driver_nl80211_data *drv)
{
	drv_errors++;
	if (drv_errors > DRV_NUMBER_SEQUENTIAL_ERRORS) {
		drv_errors = 0;
		wpa_msg(drv->ctx, MSG_INFO, WPA_EVENT_DRIVER_STATE "HANGED");
	}
}

static int wpa_driver_set_power_save(void *priv, int state)
{
	struct i802_bss *bss = priv;
	struct wpa_driver_nl80211_data *drv = bss->drv;
	struct nl_msg *msg;
	int ret = -1;
	enum nl80211_ps_state ps_state;

	msg = nlmsg_alloc();
	if (!msg)
		return -1;

	genlmsg_put(msg, 0, 0, genl_family_get_id(drv->nl80211), 0, 0,
		    NL80211_CMD_SET_POWER_SAVE, 0);

	if (state == WPA_PS_ENABLED)
		ps_state = NL80211_PS_ENABLED;
	else
		ps_state = NL80211_PS_DISABLED;

	NLA_PUT_U32(msg, NL80211_ATTR_IFINDEX, drv->ifindex);
	NLA_PUT_U32(msg, NL80211_ATTR_PS_STATE, ps_state);

	ret = send_and_recv_msgs(drv, msg, NULL, NULL);
	msg = NULL;
	if (ret < 0)
		wpa_printf(MSG_ERROR, "nl80211: Set power mode fail: %d", ret);
nla_put_failure:
	nlmsg_free(msg);
	return ret;
}

static int get_power_mode_handler(struct nl_msg *msg, void *arg)
{
	struct nlattr *tb[NL80211_ATTR_MAX + 1];
	struct genlmsghdr *gnlh = nlmsg_data(nlmsg_hdr(msg));
	int *state = (int *)arg;

	nla_parse(tb, NL80211_ATTR_MAX, genlmsg_attrdata(gnlh, 0),
		  genlmsg_attrlen(gnlh, 0), NULL);

	if (!tb[NL80211_ATTR_PS_STATE])
		return NL_SKIP;

	if (state) {
		*state = (int)nla_get_u32(tb[NL80211_ATTR_PS_STATE]);
		wpa_printf(MSG_DEBUG, "nl80211: Get power mode = %d", *state);
		*state = (*state == NL80211_PS_ENABLED) ?
				WPA_PS_ENABLED : WPA_PS_DISABLED;
	}

	return NL_SKIP;
}

static int wpa_driver_get_power_save(void *priv, int *state)
{
	struct i802_bss *bss = priv;
	struct wpa_driver_nl80211_data *drv = bss->drv;
	struct nl_msg *msg;
	int ret = -1;
	enum nl80211_ps_state ps_state;

	msg = nlmsg_alloc();
	if (!msg)
		return -1;

	genlmsg_put(msg, 0, 0, genl_family_get_id(drv->nl80211), 0, 0,
		    NL80211_CMD_GET_POWER_SAVE, 0);

	NLA_PUT_U32(msg, NL80211_ATTR_IFINDEX, drv->ifindex);

	ret = send_and_recv_msgs(drv, msg, get_power_mode_handler, state);
	msg = NULL;
	if (ret < 0)
		wpa_printf(MSG_ERROR, "nl80211: Get power mode fail: %d", ret);
nla_put_failure:
	nlmsg_free(msg);
	return ret;
}

static int wpa_driver_set_backgroundscan_params(void *priv)
{
	struct i802_bss *bss = priv;
	struct wpa_driver_nl80211_data *drv = bss->drv;
	struct wpa_supplicant *wpa_s;
	struct ifreq ifr;
	android_wifi_priv_cmd priv_cmd;
	int ret = 0, i = 0, bp;
	char buf[WEXT_PNO_MAX_COMMAND_SIZE];
	struct wpa_ssid *ssid_conf;

	if (drv == NULL) {
		wpa_printf(MSG_ERROR, "%s: drv is NULL. Exiting", __func__);
		return -1;
	}
	if (drv->ctx == NULL) {
		wpa_printf(MSG_ERROR, "%s: drv->ctx is NULL. Exiting", __func__);
		return -1;
	}
	wpa_s = (struct wpa_supplicant *)(drv->ctx);
	if (wpa_s->conf == NULL) {
		wpa_printf(MSG_ERROR, "%s: wpa_s->conf is NULL. Exiting", __func__);
		return -1;
	}
	ssid_conf = wpa_s->conf->ssid;

	bp = WEXT_PNOSETUP_HEADER_SIZE;
	os_memcpy(buf, WEXT_PNOSETUP_HEADER, bp);
	buf[bp++] = WEXT_PNO_TLV_PREFIX;
	buf[bp++] = WEXT_PNO_TLV_VERSION;
	buf[bp++] = WEXT_PNO_TLV_SUBVERSION;
	buf[bp++] = WEXT_PNO_TLV_RESERVED;

	while ((i < WEXT_PNO_AMOUNT) && (ssid_conf != NULL)) {
		/* Check that there is enough space needed for 1 more SSID, the other sections and null termination */
		if ((bp + WEXT_PNO_SSID_HEADER_SIZE + MAX_SSID_LEN + WEXT_PNO_NONSSID_SECTIONS_SIZE + 1) >= (int)sizeof(buf))
			break;
		if ((!ssid_conf->disabled) && (ssid_conf->ssid_len <= MAX_SSID_LEN)){
			wpa_printf(MSG_DEBUG, "For PNO Scan: %s", ssid_conf->ssid);
			buf[bp++] = WEXT_PNO_SSID_SECTION;
			buf[bp++] = ssid_conf->ssid_len;
			os_memcpy(&buf[bp], ssid_conf->ssid, ssid_conf->ssid_len);
			bp += ssid_conf->ssid_len;
			i++;
		}
		ssid_conf = ssid_conf->next;
	}

	buf[bp++] = WEXT_PNO_SCAN_INTERVAL_SECTION;
	os_snprintf(&buf[bp], WEXT_PNO_SCAN_INTERVAL_LENGTH + 1, "%x", WEXT_PNO_SCAN_INTERVAL);
	bp += WEXT_PNO_SCAN_INTERVAL_LENGTH;

	buf[bp++] = WEXT_PNO_REPEAT_SECTION;
	os_snprintf(&buf[bp], WEXT_PNO_REPEAT_LENGTH + 1, "%x", WEXT_PNO_REPEAT);
	bp += WEXT_PNO_REPEAT_LENGTH;

	buf[bp++] = WEXT_PNO_MAX_REPEAT_SECTION;
	os_snprintf(&buf[bp], WEXT_PNO_MAX_REPEAT_LENGTH + 1, "%x", WEXT_PNO_MAX_REPEAT);
	bp += WEXT_PNO_MAX_REPEAT_LENGTH + 1;

	memset(&ifr, 0, sizeof(ifr));
	memset(&priv_cmd, 0, sizeof(priv_cmd));
	os_strncpy(ifr.ifr_name, bss->ifname, IFNAMSIZ);

	priv_cmd.buf = buf;
	priv_cmd.used_len = bp;
	priv_cmd.total_len = bp;
	ifr.ifr_data = &priv_cmd;

	ret = ioctl(drv->ioctl_sock, SIOCDEVPRIVATE + 1, &ifr);

	if (ret < 0) {
		wpa_printf(MSG_ERROR, "ioctl[SIOCSIWPRIV] (pnosetup): %d", ret);
		wpa_driver_send_hang_msg(drv);
	} else {
		drv_errors = 0;
	}
	return ret;
}

int wpa_driver_nl80211_driver_cmd(void *priv, char *cmd, char *buf,
				  size_t buf_len )
{
	struct i802_bss *bss = priv;
	struct wpa_driver_nl80211_data *drv = bss->drv;
	struct ifreq ifr;
	android_wifi_priv_cmd priv_cmd;
	int ret = 0;

	if (os_strcasecmp(cmd, "STOP") == 0) {
		linux_set_iface_flags(drv->ioctl_sock, bss->ifname, 0);
		wpa_msg(drv->ctx, MSG_INFO, WPA_EVENT_DRIVER_STATE "STOPPED");
	} else if (os_strcasecmp(cmd, "START") == 0) {
		linux_set_iface_flags(drv->ioctl_sock, bss->ifname, 1);
		wpa_msg(drv->ctx, MSG_INFO, WPA_EVENT_DRIVER_STATE "STARTED");
	} else if (os_strcasecmp(cmd, "MACADDR") == 0) {
		u8 macaddr[ETH_ALEN] = {};

		ret = linux_get_ifhwaddr(drv->ioctl_sock, bss->ifname, macaddr);
		if (!ret)
			ret = os_snprintf(buf, buf_len,
					  "Macaddr = " MACSTR "\n", MAC2STR(macaddr));
	} else if (os_strcasecmp(cmd, "RELOAD") == 0) {
		wpa_msg(drv->ctx, MSG_INFO, WPA_EVENT_DRIVER_STATE "HANGED");
	} else if (os_strncasecmp(cmd, "POWERMODE ", 10) == 0) {
		int state;

		state = atoi(cmd + 10);
		ret = wpa_driver_set_power_save(priv, state);
		if (ret < 0)
			wpa_driver_send_hang_msg(drv);
		else
			drv_errors = 0;
	} else if (os_strncasecmp(cmd, "GETPOWER", 8) == 0) {
		int state = -1;

		ret = wpa_driver_get_power_save(priv, &state);
		if (!ret && (state != -1)) {
			ret = os_snprintf(buf, buf_len, "POWERMODE = %d\n", state);
			drv_errors = 0;
		} else {
			wpa_driver_send_hang_msg(drv);
		}
	} else if(os_strcmp(cmd, "SCAN-ACTIVE") == 0) {
		return 0; /* unsupported function */
	} else if(os_strcmp(cmd, "SCAN-PASSIVE") == 0) {
		return 0; /* unsupported function */
	} else if(os_strncmp(cmd, "RXFILTER-ADD ", 13) == 0) {
		return 0; /* Ignore it */
	} else if(os_strncmp(cmd, "RXFILTER-REMOVE ", 16) == 0) {
		return 0; /* Ignore it */
	} else if(os_strncmp(cmd, "BTCOEXMODE ", 11) == 0) {
                int mode;
                if (sscanf(cmd, "%*s %d", &mode)==1) {
                        /*
                         * Android disable BT-COEX when obtaining dhcp packet except there is headset is connected
                         * It enable the BT-COEX after dhcp process is finished
                         * We ignore since we have our way to do bt-coex during dhcp obtaining.
                         */
                        switch (mode) {
                        case 1: /* Disable*/
                                break;
                        case 0: /* Enable */
                                /* fall through */
                        case 2: /* Sense*/
                                /* fall through */
                        default:
                                break;
                        }
                        return 0; /* ignore it */
                }

	} else if(os_strcmp(cmd, "RXFILTER-START") == 0) {
		// STUB
		return 0;
	} else if(os_strcmp(cmd, "RXFILTER-STOP") == 0) {
		// STUB
		return 0;
	} else { /* Use private command */
		wpa_printf("MSG_ERROR", "%s: issuing private command %s\n", __func__, cmd);
		if (os_strcasecmp(cmd, "BGSCAN-START") == 0) {
			ret = wpa_driver_set_backgroundscan_params(priv);
			if (ret < 0) {
				return ret;
			}
			os_memcpy(buf, "PNOFORCE 1", 11);
		} else if (os_strcasecmp(cmd, "BGSCAN-STOP") == 0) {
			os_memcpy(buf, "PNOFORCE 0", 11);
		} else {
			os_memcpy(buf, cmd, strlen(cmd) + 1);
		}
		memset(&ifr, 0, sizeof(ifr));
		memset(&priv_cmd, 0, sizeof(priv_cmd));
		os_strncpy(ifr.ifr_name, bss->ifname, IFNAMSIZ);

		priv_cmd.buf = buf;
		priv_cmd.used_len = buf_len;
		priv_cmd.total_len = buf_len;
		ifr.ifr_data = &priv_cmd;

		if ((ret = ioctl(drv->ioctl_sock, SIOCDEVPRIVATE + 1, &ifr)) < 0) {
			wpa_printf(MSG_ERROR, "%s: failed to issue private commands\n", __func__);
			wpa_driver_send_hang_msg(drv);
		} else {
			drv_errors = 0;
			ret = 0;
			if ((os_strcasecmp(cmd, "LINKSPEED") == 0) ||
			    (os_strcasecmp(cmd, "RSSI") == 0) ||
			    (os_strcasecmp(cmd, "GETBAND") == 0) ||
			    (os_strcasecmp(cmd, "P2P_GET_NOA") == 0))
				ret = strlen(buf);

			wpa_printf(MSG_DEBUG, "%s %s len = %d, %d", __func__, buf, ret, strlen(buf));
		}
	}
	return ret;
}

int wpa_driver_set_p2p_noa(void *priv, u8 count, int start, int duration)
{
	char buf[MAX_DRV_CMD_SIZE];

	memset(buf, 0, sizeof(buf));
	wpa_printf(MSG_DEBUG, "%s: Entry", __func__);
	snprintf(buf, sizeof(buf), "P2P_SET_NOA %d %d %d", count, start, duration);
	return wpa_driver_nl80211_driver_cmd(priv, buf, buf, strlen(buf)+1);
}

int wpa_driver_get_p2p_noa(void *priv, u8 *buf, size_t len)
{
	char rbuf[MAX_DRV_CMD_SIZE];
	char *cmd = "P2P_GET_NOA";
	int ret;

	wpa_printf(MSG_DEBUG, "%s: Entry", __func__);
	os_memset(buf, 0, len);
	ret = wpa_driver_nl80211_driver_cmd(priv, cmd, rbuf, sizeof(rbuf));
	if (ret <= 0)
		return 0;
	ret >>= 1;
	if (ret > (int)len)
		ret = (int)len;
	hexstr2bin(rbuf, buf, ret);
	return ret;
}

int wpa_driver_set_p2p_ps(void *priv, int legacy_ps, int opp_ps, int ctwindow)
{
	char buf[MAX_DRV_CMD_SIZE];

	memset(buf, 0, sizeof(buf));
	wpa_printf(MSG_DEBUG, "%s: Entry", __func__);
	snprintf(buf, sizeof(buf), "P2P_SET_PS %d %d %d", legacy_ps, opp_ps, ctwindow);
	return wpa_driver_nl80211_driver_cmd(priv, buf, buf, strlen(buf) + 1);
}

int wpa_driver_set_ap_wps_p2p_ie(void *priv, const struct wpabuf *beacon,
				 const struct wpabuf *proberesp,
				 const struct wpabuf *assocresp)
{
	char buf[MAX_WPSP2PIE_CMD_SIZE];
	struct wpabuf *ap_wps_p2p_ie = NULL;
	char *_cmd = "SET_AP_WPS_P2P_IE";
	char *pbuf;
	int ret = 0;
	int i;
	struct cmd_desc {
		int cmd;
		const struct wpabuf *src;
	} cmd_arr[] = {
		{0x1, beacon},
		{0x2, proberesp},
		{0x4, assocresp},
		{-1, NULL}
	};

	wpa_printf(MSG_DEBUG, "%s: Entry", __func__);
	for (i = 0; cmd_arr[i].cmd != -1; i++) {
		os_memset(buf, 0, sizeof(buf));
		pbuf = buf;
		pbuf += sprintf(pbuf, "%s %d", _cmd, cmd_arr[i].cmd);
		*pbuf++ = '\0';
		ap_wps_p2p_ie = cmd_arr[i].src ?
			wpabuf_dup(cmd_arr[i].src) : NULL;
		if (ap_wps_p2p_ie) {
			os_memcpy(pbuf, wpabuf_head(ap_wps_p2p_ie), wpabuf_len(ap_wps_p2p_ie));
			ret = wpa_driver_nl80211_driver_cmd(priv, buf, buf,
				strlen(_cmd) + 3 + wpabuf_len(ap_wps_p2p_ie));
			wpabuf_free(ap_wps_p2p_ie);
			if (ret < 0)
				break;
		}
	}

	return ret;
}
