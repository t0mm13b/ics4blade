/*
 * WEXT Emulation for Android SIOCSIWPRIV ioctl with generic Linux Wireless Extensions
 * Copyright (c) 2010, Nicu Pavel <npavel@linuxconsulting.ro>
 * Copyright (c) 2003-2007, Jouni Malinen <j@w1.fi>
 *
 * Code based on Jim Huang <jserv@0xlab.org> e9bd7cc3d137eb56ebd4220d4077563743ab6723 
 * patch for 0xdroid
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Alternatively, this software may be distributed under the terms of BSD
 * license.
 *
 * See README and COPYING for more details.
 *
 */

#include "includes.h"
#include <sys/ioctl.h>
#include <net/if_arp.h>
#include <net/if.h>

#include "wireless_copy.h"
#include "common.h"
#include "driver.h"
#include "l2_packet.h"
#include "eloop.h"
#include "wpa_supplicant.h"
#include "priv_netlink.h"
#include "driver_awext.h"
#include "wpa.h"
#include "wpa_ctrl.h"
#include "wpa_supplicant_i.h"
#include "config_ssid.h"

#ifdef CONFIG_CLIENT_MLME
#include <netpacket/packet.h>
#include <hostapd_ioctl.h>
#include <ieee80211_common.h>
/* from net/mac80211.h */
enum {
	MODE_IEEE80211A = 0 /* IEEE 802.11a */,
	MODE_IEEE80211B = 1 /* IEEE 802.11b only */,
	MODE_ATHEROS_TURBO = 2 /* Atheros Turbo mode (2x.11a at 5 GHz) */,
	MODE_IEEE80211G = 3 /* IEEE 802.11g (and 802.11b compatibility) */,
	MODE_ATHEROS_TURBOG = 4 /* Atheros Turbo mode (2x.11g at 2.4 GHz) */,
	NUM_IEEE80211_MODES = 5
};

#include "mlme.h"

#ifndef ETH_P_ALL
#define ETH_P_ALL 0x0003
#endif
#endif /* CONFIG_CLIENT_MLME */

struct wpa_driver_awext_data {
	struct wpa_driver_wext_data *wext; /* structure for wext */
	void *ctx;
	char ifname[IFNAMSIZ + 1];
	int ioctl_sock;
	u8 ssid[32];
	unsigned int ssid_len;
};

static int wpa_driver_awext_set_auth_param(struct wpa_driver_awext_data *drv,
					  int idx, u32 value)
{
	struct iwreq iwr;
	int ret = 0;

	os_memset(&iwr, 0, sizeof(iwr));
	os_strncpy(iwr.ifr_name, drv->ifname, IFNAMSIZ);
	iwr.u.param.flags = idx & IW_AUTH_INDEX;
	iwr.u.param.value = value;

	if (ioctl(drv->ioctl_sock, SIOCSIWAUTH, &iwr) < 0) {
		if (errno != EOPNOTSUPP) {
			wpa_printf(MSG_DEBUG, "WEXT: SIOCSIWAUTH(param %d "
				   "value 0x%x) failed: %s)",
				   idx, value, strerror(errno));
		}
		ret = errno == EOPNOTSUPP ? -2 : -1;
	}

	return ret;
}

static int wpa_driver_awext_set_auth_alg(void *priv, int auth_alg)
{
	struct wpa_driver_awext_data *drv = priv;
	int algs = 0, res;

	if (auth_alg & AUTH_ALG_OPEN_SYSTEM)
		algs |= IW_AUTH_ALG_OPEN_SYSTEM;
	if (auth_alg & AUTH_ALG_SHARED_KEY)
		algs |= IW_AUTH_ALG_SHARED_KEY;
	if (auth_alg & AUTH_ALG_LEAP)
		algs |= IW_AUTH_ALG_LEAP;
	if (algs == 0) {
		/* at least one algorithm should be set */
		algs = IW_AUTH_ALG_OPEN_SYSTEM;
	}

	res = wpa_driver_awext_set_auth_param(drv, IW_AUTH_80211_AUTH_ALG, algs);
	drv->wext->auth_alg_fallback = res == -2;
	return res;
}

static int wpa_driver_awext_set_gen_ie(void *priv, const u8 *ie,
				      size_t ie_len)
{
	struct wpa_driver_awext_data *drv = priv;
	struct iwreq iwr;
	int ret = 0;

	os_memset(&iwr, 0, sizeof(iwr));
	os_strncpy(iwr.ifr_name, drv->ifname, IFNAMSIZ);
	iwr.u.data.pointer = (caddr_t) ie;
	iwr.u.data.length = ie_len;

	if (ioctl(drv->ioctl_sock, SIOCSIWGENIE, &iwr) < 0) {
		perror("ioctl[SIOCSIWGENIE]");
		ret = -1;
	}

	return ret;
}

static int wpa_driver_awext_cipher2wext(int cipher)
{
	switch (cipher) {
	case CIPHER_NONE:
		return IW_AUTH_CIPHER_NONE;
	case CIPHER_WEP40:
		return IW_AUTH_CIPHER_WEP40;
	case CIPHER_TKIP:
		return IW_AUTH_CIPHER_TKIP;
	case CIPHER_CCMP:
		return IW_AUTH_CIPHER_CCMP;
	case CIPHER_WEP104:
		return IW_AUTH_CIPHER_WEP104;
	default:
		return 0;
	}
}


static int wpa_driver_awext_keymgmt2wext(int keymgmt)
{
	switch (keymgmt) {
	case KEY_MGMT_802_1X:
	case KEY_MGMT_802_1X_NO_WPA:
		return IW_AUTH_KEY_MGMT_802_1X;
	case KEY_MGMT_PSK:
		return IW_AUTH_KEY_MGMT_PSK;
	default:
		return 0;
	}
}


static int
wpa_driver_awext_auth_alg_fallback(struct wpa_driver_awext_data *drv,
				  struct wpa_driver_associate_params *params)
{
	struct iwreq iwr;
	int ret = 0;

	wpa_printf(MSG_DEBUG, "WEXT: Driver did not support "
		   "SIOCSIWAUTH for AUTH_ALG, trying SIOCSIWENCODE");

	os_memset(&iwr, 0, sizeof(iwr));
	os_strncpy(iwr.ifr_name, drv->ifname, IFNAMSIZ);
	/* Just changing mode, not actual keys */
	iwr.u.encoding.flags = 0;
	iwr.u.encoding.pointer = (caddr_t) NULL;
	iwr.u.encoding.length = 0;

	/*
	 * Note: IW_ENCODE_{OPEN,RESTRICTED} can be interpreted to mean two
	 * different things. Here they are used to indicate Open System vs.
	 * Shared Key authentication algorithm. However, some drivers may use
	 * them to select between open/restricted WEP encrypted (open = allow
	 * both unencrypted and encrypted frames; restricted = only allow
	 * encrypted frames).
	 */

	if (!drv->wext->use_crypt) {
		iwr.u.encoding.flags |= IW_ENCODE_DISABLED;
	} else {
		if (params->auth_alg & AUTH_ALG_OPEN_SYSTEM)
			iwr.u.encoding.flags |= IW_ENCODE_OPEN;
		if (params->auth_alg & AUTH_ALG_SHARED_KEY)
			iwr.u.encoding.flags |= IW_ENCODE_RESTRICTED;
	}

	if (ioctl(drv->ioctl_sock, SIOCSIWENCODE, &iwr) < 0) {
		perror("ioctl[SIOCSIWENCODE]");
		ret = -1;
	}

	return ret;
}



static int
wpa_driver_awext_associate(void *priv,
			  struct wpa_driver_associate_params *params)
{
	struct wpa_driver_awext_data *drv = priv;
	int ret = 0;
	int allow_unencrypted_eapol;
	int value, flags;

	wpa_printf(MSG_DEBUG, "%s", __FUNCTION__);

	if (wpa_driver_awext_get_ifflags(drv, &flags) == 0) {
		if (!(flags & IFF_UP)) {
			wpa_driver_awext_set_ifflags(drv, flags | IFF_UP);
		}
	}

	/*
	 * If the driver did not support SIOCSIWAUTH, fallback to
	 * SIOCSIWENCODE here.
	 */
	if (drv->wext->auth_alg_fallback &&
	    wpa_driver_awext_auth_alg_fallback(drv, params) < 0)
		ret = -1;

	if (!params->bssid &&
	    wpa_driver_awext_set_bssid(drv, NULL) < 0)
		ret = -1;

	if (wpa_driver_awext_set_mode(drv, params->mode) < 0)
		ret = -1;
	/* TODO: should consider getting wpa version and cipher/key_mgmt suites
	 * from configuration, not from here, where only the selected suite is
	 * available */
	if (wpa_driver_awext_set_gen_ie(drv, params->wpa_ie, params->wpa_ie_len)
	    < 0)
		ret = -1;
	if (params->wpa_ie == NULL || params->wpa_ie_len == 0)
		value = IW_AUTH_WPA_VERSION_DISABLED;
	else if (params->wpa_ie[0] == RSN_INFO_ELEM)
		value = IW_AUTH_WPA_VERSION_WPA2;
	else
		value = IW_AUTH_WPA_VERSION_WPA;
	if (wpa_driver_awext_set_auth_param(drv,
					   IW_AUTH_WPA_VERSION, value) < 0)
		ret = -1;
	value = wpa_driver_awext_cipher2wext(params->pairwise_suite);
	if (wpa_driver_awext_set_auth_param(drv,
					   IW_AUTH_CIPHER_PAIRWISE, value) < 0)
		ret = -1;
	value = wpa_driver_awext_cipher2wext(params->group_suite);
	if (wpa_driver_awext_set_auth_param(drv,
					   IW_AUTH_CIPHER_GROUP, value) < 0)
		ret = -1;
	value = wpa_driver_awext_keymgmt2wext(params->key_mgmt_suite);
	if (wpa_driver_awext_set_auth_param(drv,
					   IW_AUTH_KEY_MGMT, value) < 0)
		ret = -1;
	value = params->key_mgmt_suite != KEY_MGMT_NONE ||
		params->pairwise_suite != CIPHER_NONE ||
		params->group_suite != CIPHER_NONE ||
		params->wpa_ie_len;
	if (wpa_driver_awext_set_auth_param(drv,
					   IW_AUTH_PRIVACY_INVOKED, value) < 0)
		ret = -1;

	/* Allow unencrypted EAPOL messages even if pairwise keys are set when
	 * not using WPA. IEEE 802.1X specifies that these frames are not
	 * encrypted, but WPA encrypts them when pairwise keys are in use. */
	if (params->key_mgmt_suite == KEY_MGMT_802_1X ||
	    params->key_mgmt_suite == KEY_MGMT_PSK)
		allow_unencrypted_eapol = 0;
	else
		allow_unencrypted_eapol = 1;
	
	if (wpa_driver_awext_set_auth_param(drv,
					   IW_AUTH_RX_UNENCRYPTED_EAPOL,
					   allow_unencrypted_eapol) < 0)
		ret = -1;
	if (params->freq && wpa_driver_awext_set_freq(drv, params->freq) < 0)
		ret = -1;
	if (wpa_driver_awext_set_ssid(drv, params->ssid, params->ssid_len) < 0)
		ret = -1;
	if (params->bssid &&
	    wpa_driver_awext_set_bssid(drv, params->bssid) < 0)
		ret = -1;

	return ret;
}


static int wpa_driver_awext_set_countermeasures(void *priv,
					       int enabled)
{
	struct wpa_driver_awext_data *drv = priv;
	wpa_printf(MSG_DEBUG, "%s", __FUNCTION__);
	return wpa_driver_awext_set_auth_param(drv,
					      IW_AUTH_TKIP_COUNTERMEASURES,
					      enabled);
}

static int wpa_driver_awext_set_drop_unencrypted(void *priv,
						int enabled)
{
	struct wpa_driver_awext_data *drv = priv;
	wpa_printf(MSG_DEBUG, "%s", __FUNCTION__);
	drv->wext->use_crypt = enabled;
	return wpa_driver_awext_set_auth_param(drv, IW_AUTH_DROP_UNENCRYPTED,
					      enabled);
}

static int wpa_driver_awext_mlme(struct wpa_driver_awext_data *drv,
				const u8 *addr, int cmd, int reason_code)
{
	struct iwreq iwr;
	struct iw_mlme mlme;
	int ret = 0;

	os_memset(&iwr, 0, sizeof(iwr));
	os_strncpy(iwr.ifr_name, drv->ifname, IFNAMSIZ);
	os_memset(&mlme, 0, sizeof(mlme));
	mlme.cmd = cmd;
	mlme.reason_code = reason_code;
	mlme.addr.sa_family = ARPHRD_ETHER;
	os_memcpy(mlme.addr.sa_data, addr, ETH_ALEN);
	iwr.u.data.pointer = (caddr_t) &mlme;
	iwr.u.data.length = sizeof(mlme);

	if (ioctl(drv->ioctl_sock, SIOCSIWMLME, &iwr) < 0) {
		perror("ioctl[SIOCSIWMLME]");
		ret = -1;
	}

	return ret;
}
#ifdef CONFIG_CLIENT_MLME
static int wpa_driver_awext_open_mlme(struct wpa_driver_awext_data *drv)
{
	int flags, ifindex, s, *i;
	struct sockaddr_ll addr;
	struct iwreq iwr;

	os_memset(&iwr, 0, sizeof(iwr));
	os_strncpy(iwr.ifr_name, drv->ifname, IFNAMSIZ);
	i = (int *) iwr.u.name;
	*i++ = PRISM2_PARAM_USER_SPACE_MLME;
	*i++ = 1;

	if (ioctl(drv->ioctl_sock, PRISM2_IOCTL_PRISM2_PARAM, &iwr) < 0) {
		wpa_printf(MSG_ERROR, "WEXT: Failed to configure driver to "
			   "use user space MLME");
		return -1;
	}

	ifindex = if_nametoindex(drv->wext->mlmedev);
	if (ifindex == 0) {
		wpa_printf(MSG_ERROR, "WEXT: mlmedev='%s' not found",
			   drv->mlmedev);
		return -1;
	}

	if (wpa_driver_awext_get_ifflags_ifname(drv, drv->wext->mlmedev, &flags) != 0
	    || wpa_driver_awext_set_ifflags_ifname(drv, drv->wext->mlmedev,
						  flags | IFF_UP) != 0) {
		wpa_printf(MSG_ERROR, "WEXT: Could not set interface "
			   "'%s' UP", drv->mlmedev);
		return -1;
	}

	s = socket(PF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
	if (s < 0) {
		perror("socket[PF_PACKET,SOCK_RAW]");
		return -1;
	}

	os_memset(&addr, 0, sizeof(addr));
	addr.sll_family = AF_PACKET;
	addr.sll_ifindex = ifindex;

	if (bind(s, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
		perror("bind(MLME)");
		return -1;
	}

	if (eloop_register_read_sock(s, wpa_driver_awext_mlme_read, drv, NULL))
	{
		wpa_printf(MSG_ERROR, "WEXT: Could not register MLME read "
			   "socket");
		close(s);
		return -1;
	}

	return s;
}


#endif /* CONFIG_CLIENT_MLME */

static int wpa_driver_awext_pmksa(struct wpa_driver_awext_data *drv,
				 u32 cmd, const u8 *bssid, const u8 *pmkid)
{
	struct iwreq iwr;
	struct iw_pmksa pmksa;
	int ret = 0;

	os_memset(&iwr, 0, sizeof(iwr));
	os_strncpy(iwr.ifr_name, drv->ifname, IFNAMSIZ);
	os_memset(&pmksa, 0, sizeof(pmksa));
	pmksa.cmd = cmd;
	pmksa.bssid.sa_family = ARPHRD_ETHER;
	if (bssid)
		os_memcpy(pmksa.bssid.sa_data, bssid, ETH_ALEN);
	if (pmkid)
		os_memcpy(pmksa.pmkid, pmkid, IW_PMKID_LEN);
	iwr.u.data.pointer = (caddr_t) &pmksa;
	iwr.u.data.length = sizeof(pmksa);

	if (ioctl(drv->ioctl_sock, SIOCSIWPMKSA, &iwr) < 0) {
		if (errno != EOPNOTSUPP)
			perror("ioctl[SIOCSIWPMKSA]");
		ret = -1;
	}

	return ret;
}

static int wpa_driver_awext_add_pmkid(void *priv, const u8 *bssid,
				     const u8 *pmkid)
{
	struct wpa_driver_awext_data *drv = priv;
	return wpa_driver_awext_pmksa(drv, IW_PMKSA_ADD, bssid, pmkid);
}


static int wpa_driver_awext_remove_pmkid(void *priv, const u8 *bssid,
		 			const u8 *pmkid)
{
	struct wpa_driver_awext_data *drv = priv;
	return wpa_driver_awext_pmksa(drv, IW_PMKSA_REMOVE, bssid, pmkid);
}


static int wpa_driver_awext_flush_pmkid(void *priv)
{
	struct wpa_driver_awext_data *drv = priv;
	return wpa_driver_awext_pmksa(drv, IW_PMKSA_FLUSH, NULL, NULL);
}


static int wpa_driver_awext_get_capa(void *priv, struct wpa_driver_capa *capa)
{
	struct wpa_driver_awext_data *drv = priv;
	if (!drv->wext->has_capability)
		return -1;
	os_memcpy(capa, &drv->wext->capa, sizeof(*capa));
	return 0;
}


static int wpa_driver_awext_set_param(void *priv, const char *param)
{
#ifdef CONFIG_CLIENT_MLME
	struct wpa_driver_awext_data *drv = priv;
	const char *pos, *pos2;
	size_t len;

	if (param == NULL)
		return 0;

	wpa_printf(MSG_DEBUG, "%s: param='%s'", __func__, param);

	pos = os_strstr(param, "mlmedev=");
	if (pos) {
		pos += 8;
		pos2 = os_strchr(pos, ' ');
		if (pos2)
			len = pos2 - pos;
		else
			len = os_strlen(pos);
		if (len + 1 > sizeof(drv->wext->mlmedev))
			return -1;
		os_memcpy(drv->wext->mlmedev, pos, len);
		drv->wext->mlmedev[len] = '\0';
		wpa_printf(MSG_DEBUG, "WEXT: Using user space MLME with "
			   "mlmedev='%s'", drv->wext->mlmedev);
		drv->wext->capa.flags |= WPA_DRIVER_FLAGS_USER_SPACE_MLME;

		drv->wext->mlme_sock = wpa_driver_awext_open_mlme(drv->wext);
		if (drv->wext->mlme_sock < 0)
			return -1;
	}
#endif /* CONFIG_CLIENT_MLME */

	return 0;
}

static int wpa_driver_awext_deauthenticate(void *priv, const u8 *addr,
					  int reason_code)
{
	struct wpa_driver_awext_data *drv = priv;
	wpa_printf(MSG_DEBUG, "%s", __FUNCTION__);
	return wpa_driver_awext_mlme(drv, addr, IW_MLME_DEAUTH, reason_code);
}

static int wpa_driver_awext_disassociate(void *priv, const u8 *addr,
					int reason_code)
{
	struct wpa_driver_awext_data *drv = priv;
	wpa_printf(MSG_DEBUG, "%s", __FUNCTION__);
	return wpa_driver_awext_mlme(drv, addr, IW_MLME_DISASSOC,
				    reason_code);
}

int wpa_driver_awext_get_bssid(void *priv, u8 *bssid)
{
	struct wpa_driver_awext_data *drv = priv;
	return wpa_driver_wext_get_bssid(drv->wext, bssid);
}

int wpa_driver_awext_set_bssid(void *priv, const u8 *bssid)
{
	struct wpa_driver_awext_data *drv = priv;
	return wpa_driver_wext_set_bssid(drv->wext, bssid);
}

int wpa_driver_awext_get_ssid(void *priv, u8 *ssid)
{
	struct wpa_driver_awext_data *drv = priv;
	return wpa_driver_wext_get_ssid(drv->wext, ssid);
}

int wpa_driver_awext_set_ssid(void *priv, const u8 *ssid, size_t ssid_len)
{
	struct wpa_driver_awext_data *drv = priv;

	if (ssid_len > 32)
		return -1;
		
	os_memset(drv->ssid, 0, 32);
	os_memcpy(drv->ssid, ssid, ssid_len);
	drv->ssid_len = ssid_len;
	
	return wpa_driver_wext_set_ssid(drv->wext, ssid, ssid_len);
}

int wpa_driver_awext_set_freq(void *priv, int freq)
{
	struct wpa_driver_awext_data *drv = priv;
	return wpa_driver_wext_set_freq(drv->wext, freq);
}

int wpa_driver_awext_get_ifflags(void *priv, int *flags)
{
	struct wpa_driver_awext_data *drv = priv;
	return wpa_driver_wext_get_ifflags(drv->wext, flags);
}

int wpa_driver_awext_set_ifflags(struct wpa_driver_awext_data *drv, int flags)
{
	return wpa_driver_wext_set_ifflags(drv->wext, flags);
}

void * wpa_driver_awext_init(void *ctx, const char *ifname)
{
	struct wpa_driver_awext_data *drv;
	drv = os_zalloc(sizeof(*drv));
	if (drv == NULL)
		return NULL;
	drv->wext = wpa_driver_wext_init(ctx, ifname);
	if (drv->wext == NULL)
	{
		os_free(drv);
		return NULL;
	}
	
	drv->ctx = ctx;
	
	os_strncpy(drv->ifname, ifname, sizeof(drv->ifname));
	drv->ioctl_sock = socket(PF_INET, SOCK_DGRAM, 0);
	if (drv->ioctl_sock < 0) 
	{
		wpa_driver_wext_deinit(drv->wext);
		os_free(drv);
		return NULL;
	}
	
	return drv;
}

void wpa_driver_awext_deinit(void *priv)
{
	struct wpa_driver_awext_data *drv = priv;
	wpa_driver_wext_deinit(drv->wext);
	close(drv->ioctl_sock);
	os_free(drv);
}

void wpa_driver_awext_scan_timeout(void *eloop_ctx, void *timeout_ctx)
{
	return wpa_driver_wext_scan_timeout(eloop_ctx, timeout_ctx);
}

int wpa_driver_awext_scan(void *priv, const u8 *ssid, size_t ssid_len)
{
	struct wpa_driver_awext_data *drv = priv;
	return wpa_driver_wext_scan(drv->wext, ssid, ssid_len);
}

int wpa_driver_awext_get_scan_results(void *priv,
				     struct wpa_scan_result *results,
				     size_t max_size)
{
	struct wpa_driver_awext_data *drv = priv;
	return wpa_driver_wext_get_scan_results(drv->wext, results, max_size);
}

int wpa_driver_awext_set_key(void *priv, wpa_alg alg,
			    const u8 *addr, int key_idx,
			    int set_tx, const u8 *seq, size_t seq_len,
			    const u8 *key, size_t key_len)
{
	struct wpa_driver_awext_data *drv = priv;
	return wpa_driver_wext_set_key(drv->wext, alg, addr, key_idx, set_tx, seq, seq_len, key, key_len);
}

int wpa_driver_awext_set_mode(void *priv, int mode)
{
	struct wpa_driver_awext_data *drv = priv;
	return wpa_driver_wext_set_mode(drv->wext, mode);
}

int wpa_driver_awext_alternative_ifindex(struct wpa_driver_awext_data *drv,
					const char *ifname)
{
	return wpa_driver_wext_alternative_ifindex(drv->wext, ifname);
}

int wpa_driver_awext_set_operstate(void *priv, int state)
{
	struct wpa_driver_awext_data *drv = priv;
	return wpa_driver_wext_set_operstate(drv->wext, state);
}

int wpa_driver_awext_set_channel(void *priv, wpa_hw_mode phymode, int chan,
				int freq)
{
	struct wpa_driver_awext_data *drv = priv;
	return wpa_driver_awext_set_freq(drv->wext, freq);
}

int wpa_driver_awext_get_version(struct wpa_driver_awext_data *drv)
{
	return wpa_driver_wext_get_version(drv->wext);
}

int wpa_driver_awext_set_wpa(void *priv, int enabled)
{
    struct wpa_driver_awext_data *drv = priv;
    return wpa_driver_awext_set_auth_param(drv, IW_AUTH_WPA_ENABLED, enabled);
}

#ifdef ANDROID
static int wpa_driver_priv_driver_cmd(void *priv, char *cmd, char *buf, size_t buf_len)
{

	struct wpa_driver_awext_data *drv = priv;
	int ret = -1;
	int flags;

	wpa_printf(MSG_DEBUG, "AWEXT: %s %s", __func__, cmd);

	if (os_strcasecmp(cmd, "start") == 0) {
		wpa_msg(drv->ctx, MSG_INFO, WPA_EVENT_DRIVER_STATE "STARTED");
		wpa_printf(MSG_DEBUG,"Start command");
		return (ret);
	}

	if (os_strcasecmp(cmd, "stop") == 0) {
		wpa_msg(drv->ctx, MSG_INFO, WPA_EVENT_DRIVER_STATE "STOPPED");
		wpa_printf(MSG_DEBUG,"Stop command");
		if ((wpa_driver_awext_get_ifflags(drv, &flags) == 0) &&
		    (flags & IFF_UP)) {
			wpa_printf(MSG_ERROR, "WEXT: %s when iface is UP", cmd);
			wpa_driver_awext_set_ifflags(drv, flags & ~IFF_UP);
		}
		
	}
	else if (os_strcasecmp(cmd, "reload") == 0) {
		wpa_printf(MSG_DEBUG,"Reload command");
		wpa_msg(drv->ctx, MSG_INFO, WPA_EVENT_DRIVER_STATE "HANGED");
		return ret;
	}
	else if (os_strcasecmp(cmd, "macaddr") == 0) {
		struct ifreq ifr;
		os_memset(&ifr, 0, sizeof(ifr));
		os_strncpy(ifr.ifr_name, drv->ifname, IFNAMSIZ);

		if (ioctl(drv->ioctl_sock, SIOCGIFHWADDR, &ifr) < 0) {
			perror("ioctl[SIOCGIFHWADDR]");
			ret = -1;
		} else {
			u8 *macaddr = (u8 *) ifr.ifr_hwaddr.sa_data;
			ret = snprintf(buf, buf_len, "Macaddr = " MACSTR "\n",
			               MAC2STR(macaddr));
		}
	}
	else if (os_strcasecmp(cmd, "scan-passive") == 0) {
		wpa_printf(MSG_DEBUG,"Scan Passive command");
	}
	else if (os_strcasecmp(cmd, "scan-active") == 0) {
		wpa_printf(MSG_DEBUG,"Scan Active command");
	}
	else if (os_strcasecmp(cmd, "linkspeed") == 0) {
		struct iwreq wrq;
		unsigned int linkspeed;
		os_strncpy(wrq.ifr_name, drv->ifname, IFNAMSIZ);
		wpa_printf(MSG_DEBUG,"Link Speed command");
		if (ioctl(drv->ioctl_sock, SIOCGIWRATE, &wrq) < 0) {
			perror("ioctl[SIOCGIWRATE]");
			ret = -1;
		} else {
			linkspeed = wrq.u.bitrate.value / 1000000;
			ret = snprintf(buf, buf_len, "LinkSpeed %d\n", linkspeed);
		}
	}
	else if (os_strncasecmp(cmd, "scan-channels", 13) == 0) {
	}
	else if ((os_strcasecmp(cmd, "rssi") == 0) || (os_strcasecmp(cmd, "rssi-approx") == 0)) {
		struct iwreq wrq;
		struct iw_statistics stats;
		signed int rssi;
		wpa_printf(MSG_DEBUG, ">>>. DRIVER AWEXT RSSI ");
		wrq.u.data.pointer = (caddr_t) &stats;
		wrq.u.data.length = sizeof(stats);
		wrq.u.data.flags = 1; /* Clear updated flag */
		strncpy(wrq.ifr_name, drv->ifname, IFNAMSIZ);

		if (ioctl(drv->ioctl_sock, SIOCGIWSTATS, &wrq) < 0) {
			perror("ioctl[SIOCGIWSTATS]");
			ret = -1;
		} else {
			if (stats.qual.updated & IW_QUAL_DBM) {
				/* Values in dBm, stored in u8 with range 63 : -192 */
				rssi = ( stats.qual.level > 63 ) ?
					stats.qual.level - 0x100 :
					stats.qual.level;
			} else {
				rssi = stats.qual.level;
			}

			if (drv->ssid_len != 0 && drv->ssid_len < buf_len) {
				os_memcpy((void *) buf, (void *) (drv->ssid),
						drv->ssid_len );
				ret = drv->ssid_len;
				ret += snprintf(&buf[ret], buf_len-ret,
						" rssi %d\n", rssi);
				if (ret < (int)buf_len) {
					return( ret );
				}
				ret = -1;
			}
		}
	}
	else if (os_strncasecmp(cmd, "powermode", 9) == 0) {
	}
	else if (os_strncasecmp(cmd, "getpower", 8) == 0) {
	}
	else if (os_strncasecmp(cmd, "get-rts-threshold", 17) == 0) {
		struct iwreq wrq;
		unsigned int rtsThreshold;

		strncpy(wrq.ifr_name, drv->ifname, IFNAMSIZ);

		if (ioctl(drv->ioctl_sock, SIOCGIWRTS, &wrq) < 0) {
			perror("ioctl[SIOCGIWRTS]");
			ret = -1;
		} else {
			rtsThreshold = wrq.u.rts.value;
			wpa_printf(MSG_DEBUG,"Get RTS Threshold command = %d",
				rtsThreshold);
			ret = snprintf(buf, buf_len, "rts-threshold = %u\n",
				rtsThreshold);
			if (ret < (int)buf_len) {
				return( ret );
			}
		}
	}
	else if (os_strncasecmp(cmd, "set-rts-threshold", 17) == 0) {
		struct iwreq wrq;
		unsigned int rtsThreshold;
		char *cp = cmd + 17;
		char *endp;

		strncpy(wrq.ifr_name, drv->ifname, IFNAMSIZ);

		if (*cp != '\0') {
			rtsThreshold = (unsigned int)strtol(cp, &endp, 0);
			if (endp != cp) {
				wrq.u.rts.value = rtsThreshold;
				wrq.u.rts.fixed = 1;
				wrq.u.rts.disabled = 0;

				if (ioctl(drv->ioctl_sock, SIOCSIWRTS, &wrq) < 0) {
					perror("ioctl[SIOCGIWRTS]");
					ret = -1;
				} else {
					rtsThreshold = wrq.u.rts.value;
					wpa_printf(MSG_DEBUG,"Set RTS Threshold command = %d", rtsThreshold);
					ret = 0;
				}
			}
		}
	}
	else if (os_strcasecmp(cmd, "btcoexscan-start") == 0) {
	}
	else if (os_strcasecmp(cmd, "btcoexscan-stop") == 0) {
	}
	else if (os_strcasecmp(cmd, "rxfilter-start") == 0) {
		wpa_printf(MSG_DEBUG,"Rx Data Filter Start command");
	}
	else if (os_strcasecmp(cmd, "rxfilter-stop") == 0) {
		wpa_printf(MSG_DEBUG,"Rx Data Filter Stop command");
	}
	else if (os_strcasecmp(cmd, "rxfilter-statistics") == 0) {
	}
	else if (os_strncasecmp(cmd, "rxfilter-add", 12) == 0 ) {
	}
	else if (os_strncasecmp(cmd, "rxfilter-remove",15) == 0) {
	}
	else if (os_strcasecmp(cmd, "snr") == 0) {
		struct iwreq wrq;
		struct iw_statistics stats;
		int snr, rssi, noise;

		wrq.u.data.pointer = (caddr_t) &stats;
		wrq.u.data.length = sizeof(stats);
		wrq.u.data.flags = 1; /* Clear updated flag */
		strncpy(wrq.ifr_name, drv->ifname, IFNAMSIZ);

		if (ioctl(drv->ioctl_sock, SIOCGIWSTATS, &wrq) < 0) {
			perror("ioctl[SIOCGIWSTATS]");
			ret = -1;
		} else {
			if (stats.qual.updated & IW_QUAL_DBM) {
				/* Values in dBm, stored in u8 with range 63 : -192 */
				rssi = ( stats.qual.level > 63 ) ?
					stats.qual.level - 0x100 :
					stats.qual.level;
				noise = ( stats.qual.noise > 63 ) ?
					stats.qual.noise - 0x100 :
					stats.qual.noise;
			} else {
				rssi = stats.qual.level;
				noise = stats.qual.noise;
			}

			snr = rssi - noise;

			ret = snprintf(buf, buf_len, "snr = %u\n", (unsigned int)snr);
			if (ret < (int)buf_len) {
				return( ret );
			}
		}
	}
	else if (os_strncasecmp(cmd, "btcoexmode", 10) == 0) {
	}
	else if( os_strcasecmp(cmd, "btcoexstat") == 0 ) {
	}
	else {
		wpa_printf(MSG_DEBUG,"Unsupported command");
	}
	return (ret);
}
#endif

const struct wpa_driver_ops wpa_driver_awext_ops = {
	.name = "awext",
	.desc = "Android wireless extensions emulation",
	.get_bssid = wpa_driver_awext_get_bssid,
	.get_ssid = wpa_driver_awext_get_ssid,
	.set_wpa = wpa_driver_awext_set_wpa,
	.set_key = wpa_driver_awext_set_key,
	.set_countermeasures = wpa_driver_awext_set_countermeasures,
	.set_drop_unencrypted = wpa_driver_awext_set_drop_unencrypted,
	.scan = wpa_driver_awext_scan,
	.get_scan_results = wpa_driver_awext_get_scan_results,
	.deauthenticate = wpa_driver_awext_deauthenticate,
	.disassociate = wpa_driver_awext_disassociate,
	.associate = wpa_driver_awext_associate,
	.set_auth_alg = wpa_driver_awext_set_auth_alg,
	.init = wpa_driver_awext_init,
	.deinit = wpa_driver_awext_deinit,

	.set_param = wpa_driver_awext_set_param,
	.add_pmkid = wpa_driver_awext_add_pmkid,
	.remove_pmkid = wpa_driver_awext_remove_pmkid,
	.flush_pmkid = wpa_driver_awext_flush_pmkid,
	.get_capa = wpa_driver_awext_get_capa,
	.set_operstate = wpa_driver_awext_set_operstate,
#ifdef CONFIG_CLIENT_MLME
	.get_hw_feature_data = wpa_driver_awext_get_hw_feature_data,
	.set_channel = wpa_driver_awext_set_channel,
	.set_ssid = wpa_driver_awext_set_ssid,
	.set_bssid = wpa_driver_awext_set_bssid,
	.send_mlme = wpa_driver_awext_send_mlme,
	.mlme_add_sta = wpa_driver_awext_mlme_add_sta,
	.mlme_remove_sta = wpa_driver_awext_mlme_remove_sta,
#endif /* CONFIG_CLIENT_MLME */
#ifdef ANDROID
	.driver_cmd = wpa_driver_priv_driver_cmd,
#endif
};
