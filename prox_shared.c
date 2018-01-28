/*
  Copyright(c) 2010-2017 Intel Corporation.
  Copyright(c) 2016-2018 Viosoft Corporation.
  All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions
  are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in
      the documentation and/or other materials provided with the
      distribution.
    * Neither the name of Intel Corporation nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <stdio.h>
#include <rte_hash.h>
#include <rte_hash_crc.h>
#include <rte_version.h>

#include "quit.h"
#include "log.h"
#include "prox_shared.h"
#include "prox_globals.h"

#define INIT_HASH_TABLE_SIZE 8192

struct prox_shared {
	struct rte_hash *hash;
	size_t          size;
};

struct prox_shared sh_system;
struct prox_shared sh_socket[MAX_SOCKETS];
struct prox_shared sh_core[RTE_MAX_LCORE];

static char* get_sh_name(void)
{
	static char name[] = "prox_sh";

	name[0]++;
	return name;
}

struct rte_hash_parameters param = {
	.key_len = 256,
	.hash_func = rte_hash_crc,
	.hash_func_init_val = 0,
	.socket_id = 0,
};

static void prox_sh_create_hash(struct prox_shared *ps, size_t size)
{
	param.entries = size;
	param.name = get_sh_name();
	ps->hash = rte_hash_create(&param);
	PROX_PANIC(ps->hash == NULL, "Failed to create hash table for shared data");
	ps->size = size;
	if (ps->size == INIT_HASH_TABLE_SIZE)
		plog_info("Shared data tracking hash table created with size %zu\n", ps->size);
	else
		plog_info("Shared data tracking hash table grew to %zu\n", ps->size);
}

#if RTE_VERSION >= RTE_VERSION_NUM(2,1,0,0)
static int copy_hash(struct rte_hash *new_hash, struct rte_hash *old_hash)
{
	const void *next_key;
	void *next_data;
	uint32_t iter = 0;

	while (rte_hash_iterate(old_hash, &next_key, &next_data, &iter) >= 0) {
		if (rte_hash_add_key_data(new_hash, next_key, next_data) < 0)
			return -1;
	}

	return 0;
}
#endif

static int prox_sh_add(struct prox_shared *ps, const char *name, void *data)
{
	char key[256] = {0};
	int ret;

	strncpy(key, name, sizeof(key));
	if (ps->size == 0) {
		prox_sh_create_hash(ps, INIT_HASH_TABLE_SIZE);
	}

#if RTE_VERSION >= RTE_VERSION_NUM(2,1,0,0)
	do {
		ret = rte_hash_add_key_data(ps->hash, key, data);
		if (ret < 0) {
			struct rte_hash *old = ps->hash;
			int success;
			do {
				prox_sh_create_hash(ps, ps->size * 2);
				success = !copy_hash(ps->hash, old);
				if (success)
					rte_hash_free(old);
				else
					rte_hash_free(ps->hash);
			} while (!success);
		}
	} while (ret < 0);
#else
		PROX_PANIC(1, "DPDK < 2.1 not fully supported");
#endif
	return 0;
}

static void *prox_sh_find(struct prox_shared *sh, const char *name)
{
#if RTE_VERSION >= RTE_VERSION_NUM(2,1,0,0)
	char key[256] = {0};
	int ret;
	void *data;

	if (!sh->hash)
		return NULL;

	strncpy(key, name, sizeof(key));
	ret = rte_hash_lookup_data(sh->hash, key, &data);
	if (ret >= 0)
		return data;
#else
		PROX_PANIC(1, "DPDK < 2.1 not fully supported");
#endif
	return NULL;
}

int prox_sh_add_system(const char *name, void *data)
{
	return prox_sh_add(&sh_system, name, data);
}

int prox_sh_add_socket(const int socket_id, const char *name, void *data)
{
	if (socket_id >= MAX_SOCKETS)
		return -1;

	return prox_sh_add(&sh_socket[socket_id], name, data);
}

int prox_sh_add_core(const int core_id, const char *name, void *data)
{
	if (core_id >= RTE_MAX_LCORE)
		return -1;

	return prox_sh_add(&sh_core[core_id], name, data);
}

void *prox_sh_find_system(const char *name)
{
	return prox_sh_find(&sh_system, name);
}

void *prox_sh_find_socket(const int socket_id, const char *name)
{
	if (socket_id >= MAX_SOCKETS)
		return NULL;

	return prox_sh_find(&sh_socket[socket_id], name);
}

void *prox_sh_find_core(const int core_id, const char *name)
{
	if (core_id >= RTE_MAX_LCORE)
		return NULL;

	return prox_sh_find(&sh_core[core_id], name);
}
