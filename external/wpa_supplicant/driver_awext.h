/*
 * WPA Supplicant - driver_wext exported functions
 * Copyright (c) 2003-2005, Jouni Malinen <j@w1.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Alternatively, this software may be distributed under the terms of BSD
 * license.
 *
 * See README and COPYING for more details.
 */

#ifndef DRIVER_AWEXT_H
#define DRIVER_AWEXT_H

#include "driver_wext.h"

struct wpa_driver_wext_data {
	void *ctx;
	int event_sock;
	int ioctl_sock;
	int mlme_sock;
	char ifname[IFNAMSIZ + 1];
	int ifindex;
	int ifindex2;
	int if_removed;
	u8 *assoc_req_ies;
	size_t assoc_req_ies_len;
	u8 *assoc_resp_ies;
	size_t assoc_resp_ies_len;
	struct wpa_driver_capa capa;
	int has_capability;
	int we_version_compiled;

	/* for set_auth_alg fallback */
	int use_crypt;
	int auth_alg_fallback;

	int operstate;

	char mlmedev[IFNAMSIZ + 1];

	int scan_complete_events;
	int errors;
};

struct wpa_driver_awext_data;

int wpa_driver_awext_get_ifflags(void *priv, int *flags);
int wpa_driver_awext_set_ifflags(struct wpa_driver_awext_data *drv, int flags);
int wpa_driver_awext_get_bssid(void *priv, u8 *bssid);
int wpa_driver_awext_set_bssid(void *priv, const u8 *bssid);
int wpa_driver_awext_get_ssid(void *priv, u8 *ssid);
int wpa_driver_awext_set_ssid(void *priv, const u8 *ssid, size_t ssid_len);
int wpa_driver_awext_set_freq(void *priv, int freq);
int wpa_driver_awext_set_mode(void *priv, int mode);
int wpa_driver_awext_set_key(void *priv, wpa_alg alg,
			    const u8 *addr, int key_idx,
			    int set_tx, const u8 *seq, size_t seq_len,
			    const u8 *key, size_t key_len);
int wpa_driver_awext_scan(void *priv, const u8 *ssid, size_t ssid_len);
int wpa_driver_awext_get_scan_results(void *priv,
				     struct wpa_scan_result *results,
				     size_t max_size);

void wpa_driver_awext_scan_timeout(void *eloop_ctx, void *timeout_ctx);

int wpa_driver_awext_alternative_ifindex(struct wpa_driver_awext_data *drv,
					const char *ifname);

void * wpa_driver_awext_init(void *ctx, const char *ifname);
void wpa_driver_awext_deinit(void *priv);

int wpa_driver_awext_set_operstate(void *priv, int state);
int wpa_driver_awext_get_version(struct wpa_driver_awext_data *drv);

#ifdef ANDROID
#define WEXT_NUMBER_SCAN_CHANNELS_FCC	11
#define WEXT_NUMBER_SCAN_CHANNELS_ETSI	13
#define WEXT_NUMBER_SCAN_CHANNELS_MKK1	14

#define WPA_DRIVER_WEXT_WAIT_US		400000
#define MAX_DRV_CMD_SIZE		248
#endif

#endif /* DRIVER_AWEXT_H */
