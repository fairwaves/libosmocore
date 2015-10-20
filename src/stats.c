/*
 * (C) 2015 by Sysmocom s.f.m.c. GmbH
 *
 * All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#include <osmocom/core/stats.h>

#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <stdio.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <arpa/inet.h>

#include <osmocom/core/utils.h>
#include <osmocom/core/logging.h>
#include <osmocom/core/rate_ctr.h>
#include <osmocom/core/stat_item.h>

/* TODO: register properly */
#define DSTATS 0


static LLIST_HEAD(stats_reporter_list);
static void *stats_ctx = NULL;

static int stats_reporter_statsd_open(struct stats_reporter *srep);
static int stats_reporter_statsd_close(struct stats_reporter *srep);
static int stats_reporter_send(struct stats_reporter *srep, const char *data,
	int data_len);
static int stats_reporter_flush(struct stats_reporter *srep);

static int update_srep_config(struct stats_reporter *srep)
{
	int rc = 0;

	if (srep->type != STATS_REPORTER_STATSD) {
		srep->enabled = 0;
		return -ENOTSUP;
	}

	if (srep->running) {
		rc = stats_reporter_statsd_close(srep);
		srep->running = 0;
	}

	if (!srep->enabled)
		return rc;

	rc = stats_reporter_statsd_open(srep);
	if (rc < 0)
		srep->enabled = 0;
	else
		srep->running = 1;

	stats_flush();

	return rc;
}

struct stats_reporter *stats_reporter_alloc(enum stats_reporter_type type,
	const char *name)
{
	struct stats_reporter *srep;
	srep = talloc_zero(stats_ctx, struct stats_reporter);
	OSMO_ASSERT(srep);
	srep->type = type;
	if (name)
		srep->name = talloc_strdup(srep, name);
	srep->fd = -1;

	llist_add(&srep->list, &stats_reporter_list);

	return srep;
}

void stats_reporter_free(struct stats_reporter *srep)
{
	stats_reporter_disable(srep);
	llist_del(&srep->list);
	talloc_free(srep);
}

void stats_init(void *ctx)
{
	stats_ctx = ctx;
}

struct stats_reporter *stats_reporter_create_statsd(const char *name)
{
	struct stats_reporter *srep;
	srep = stats_reporter_alloc(STATS_REPORTER_STATSD, name);

	return srep;
}

struct stats_reporter *stats_reporter_find(enum stats_reporter_type type,
	const char *name)
{
	struct stats_reporter *srep;
	llist_for_each_entry(srep, &stats_reporter_list, list) {
		if (srep->type != type)
			continue;
		if (srep->name != name) {
			if (name == NULL || srep->name == NULL ||
				strcmp(name, srep->name) != 0)
				continue;
		}
		return srep;
	}
	return NULL;
}

int stats_reporter_set_remote_addr(struct stats_reporter *srep, const char *addr)
{
	int rc;
	struct sockaddr_in *sock_addr = (struct sockaddr_in *)&srep->dest_addr;
	struct in_addr inaddr;

	OSMO_ASSERT(addr != NULL);

	rc = inet_pton(AF_INET, addr, &inaddr);
	if (rc <= 0)
		return -EINVAL;

	sock_addr->sin_addr = inaddr;
	sock_addr->sin_family = AF_INET;
	srep->dest_addr_len = sizeof(*sock_addr);

	talloc_free(srep->dest_addr_str);
	srep->dest_addr_str = talloc_strdup(srep, addr);

	return update_srep_config(srep);
}

int stats_reporter_set_remote_port(struct stats_reporter *srep, int port)
{
	struct sockaddr_in *sock_addr = (struct sockaddr_in *)&srep->dest_addr;

	srep->dest_port = port;
	sock_addr->sin_port = htons(port);

	return update_srep_config(srep);
}

int stats_reporter_set_local_addr(struct stats_reporter *srep, const char *addr)
{
	int rc;
	struct sockaddr_in *sock_addr = (struct sockaddr_in *)&srep->bind_addr;
	struct in_addr inaddr;

	if (addr) {
		rc = inet_pton(AF_INET, addr, &inaddr);
		if (rc <= 0)
			return -EINVAL;
	} else {
		addr = INADDR_ANY;
	}

	sock_addr->sin_addr = inaddr;
	sock_addr->sin_family = AF_INET;
	srep->bind_addr_len = addr ? sizeof(*sock_addr) : 0;

	talloc_free(srep->bind_addr_str);
	srep->bind_addr_str = addr ? talloc_strdup(srep, addr) : NULL;

	return update_srep_config(srep);
}

int stats_reporter_set_interval(struct stats_reporter *srep, int interval)
{
	srep->interval = interval;

	return update_srep_config(srep);
}

int stats_reporter_set_name_prefix(struct stats_reporter *srep, const char *prefix)
{
	talloc_free(srep->name_prefix);
	srep->name_prefix = prefix ? talloc_strdup(srep, prefix) : NULL;

	return update_srep_config(srep);
}

int stats_reporter_enable(struct stats_reporter *srep)
{
	srep->enabled = 1;

	return update_srep_config(srep);
}

int stats_reporter_disable(struct stats_reporter *srep)
{
	srep->enabled = 0;

	return update_srep_config(srep);
}

static int stats_reporter_statsd_open(struct stats_reporter *srep)
{
	int sock;
	int rc;

	if (srep->fd != -1)
		stats_reporter_statsd_close(srep);

	sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock == -1)
		return -errno;

	if (srep->bind_addr_len > 0) {
		rc = bind(sock, &srep->bind_addr, srep->bind_addr_len);
		if (rc == -1)
			goto failed;
	}

	srep->fd = sock;

	return 0;

failed:
	rc = -errno;
	close(sock);

	return rc;
}

static int stats_reporter_statsd_close(struct stats_reporter *srep)
{
	int rc;
	if (srep->fd == -1)
		return -EBADF;

	rc = close(srep->fd);
	srep->fd = -1;
	return rc == -1 ? -errno : 0;
}

static int stats_reporter_send(struct stats_reporter *srep, const char *data,
	int data_len)
{
	int rc;

	rc = sendto(srep->fd, data, data_len, MSG_NOSIGNAL | MSG_DONTWAIT,
		&srep->dest_addr, srep->dest_addr_len);

	if (rc == -1)
		rc = -errno;

	return rc;
}

static int stats_reporter_flush(struct stats_reporter *srep)
{
	return 0;
}

static int stats_reporter_statsd_send(struct stats_reporter *srep,
	const char *name1, const char *name2, int value, const char *unit)
{
	char buf[256];
	int nchars, rc;

	if (srep->name_prefix)
		nchars = snprintf(buf, sizeof(buf), "%s.%s.%s:%d|%s",
			srep->name_prefix, name1, name2,
			value, unit);
	else
		nchars = snprintf(buf, sizeof(buf), "%s.%s:%d|%s",
			name1, name2, value, unit);

	if (nchars >= sizeof(buf))
		/* Truncated */
		return -EMSGSIZE;

	rc = stats_reporter_send(srep, buf, nchars);

	return rc;
}

static int stats_reporter_statsd_send_counter(struct stats_reporter *srep,
	const struct rate_ctr_group_desc *gdesc,
	const struct rate_ctr_desc *desc,
	int64_t value, int64_t delta)
{
	return stats_reporter_statsd_send(srep, gdesc->group_name_prefix,
		desc->name, delta, "c");
}

static int stats_reporter_statsd_send_item(struct stats_reporter *srep,
	const struct stat_item_group_desc *gdesc,
	const struct stat_item_desc *desc, int value)
{
	return stats_reporter_statsd_send(srep, gdesc->group_name_prefix,
		desc->name, value, desc->unit);
}

static int stats_reporter_send_counter(struct stats_reporter *srep,
	const struct rate_ctr_group_desc *gdesc,
	const struct rate_ctr_desc *desc,
	int64_t value, int64_t delta)
{
	int rc;

	switch (srep->type) {
	case STATS_REPORTER_STATSD:
		rc = stats_reporter_statsd_send_counter(srep, gdesc, desc,
			value, delta);
		break;
	}

	return rc;
}

static int rate_ctr_handler(
	struct rate_ctr_group *ctrg, struct rate_ctr *ctr,
	const struct rate_ctr_desc *desc, void *sctx_)
{
	struct stats_reporter *srep;
	int rc;
	int64_t delta = rate_ctr_difference(ctr);

	if (delta == 0)
		return 0;

	llist_for_each_entry(srep, &stats_reporter_list, list) {
		if (!srep->running)
			continue;

		rc = stats_reporter_send_counter(srep, ctrg->desc, desc,
			ctr->current, delta);

		/* TODO: handle rc (log?, inc counter(!)?) */
	}

	return 0;
}

static int rate_ctr_group_handler(struct rate_ctr_group *ctrg, void *sctx_)
{
	rate_ctr_for_each_counter(ctrg, rate_ctr_handler, sctx_);

	return 0;
}

int stats_flush()
{
	// osmo_counters_for_each(handle_counter, &sctx);
	rate_ctr_for_each_group(rate_ctr_group_handler, NULL);
	// stat_item_for_each_group(stat_item_group_handler, &sctx);
}
