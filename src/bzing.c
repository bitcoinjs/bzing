/*
 * Copyright (c) 2012 Stefan Thomas <justmoon@members.fsf.org>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "api/bzing_db.h"
#include "api/bzing_util.h"
#include "bzing_chain.h"
#include "bzing_parser.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

bzing_handle
bzing_alloc(void)
{
  bzing_handle hnd = NULL;
  int result;

#ifdef BZ_ENGINE_BDB
  DB *dbp;
#endif

  hnd = malloc(sizeof(hnd));
  //hnd->inv = alignhash_init_inv();
  hnd->engine_id = BZ_EID_DEFAULT;

  printf("%d %d\n", hnd->engine_id, BZ_EID_DEFAULT);

#ifdef BZ_ENGINE_BDB
  uint32_t flags;
#endif

  switch (hnd->engine_id) {
#ifdef BZ_ENGINE_KHASH
  case BZ_EID_KHASH:
    hnd->kh_inv = kh_init(256);
    break;
#endif
#ifdef BZ_ENGINE_LMC
  case BZ_EID_LMC:
    hnd->lmc_inv = local_memcache_create("main", 0, 512000, 0, &hnd->lmc_error);
    if (!hnd->lmc_inv) {
      fprintf(stderr, "Couldn't create localmemcache: %s\n",
              (char *) &hnd->lmc_error.error_str);
      return NULL;
    }
    break;
#endif
#ifdef BZ_ENGINE_TC
  case BZ_EID_TC:
    hnd->tc_inv = tchdbnew();
    if (!tchdbopen(hnd->tc_inv, "main.tch", HDBOWRITER | HDBOCREAT)) {
      result = tchdbecode(hnd->tc_inv);
      fprintf(stderr, "open error: %s\n", tchdberrmsg(result));
    }
    break;
#endif
#ifdef BZ_ENGINE_BDB
  case BZ_EID_BDB:
    result = db_create(&dbp, NULL, 0);
    if (result != 0) {
      fprintf(stderr, "open error: %s\n", db_strerror(result));
      return NULL;
    }
    hnd->bdb_inv = dbp;

    flags = DB_CREATE;

    result = hnd->bdb_inv->open(hnd->bdb_inv, // DB pointer
                                NULL,         // Transaction pointer
                                "main.db",    // Database file
                                NULL,         // Database name (optional)
                                DB_BTREE,     // Database access method
                                flags,        // Open flags
                                0);           // File mode (using defaults)
    if (result != 0) {

    }
    break;
#endif
  default:
    // TODO: error
    break;
  }
  return hnd;
}

void
bzing_free(bzing_handle hnd)
{
  if (!hnd) return;

  //alignhash_destroy_inv(hnd->inv);
  switch (hnd->engine_id) {
#ifdef BZ_ENGINE_KHASH
  case BZ_EID_KHASH:
    kh_destroy(256, hnd->kh_inv);
    break;
#endif
#ifdef BZ_ENGINE_LMC
  case BZ_EID_LMC:
    local_memcache_free(hnd->lmc_inv, &hnd->lmc_error);
    break;
#endif
  default:
    // TODO: error
    break;
  }
  free(hnd);
}

void
bzing_reset(bzing_handle hnd)
{
#ifdef BZ_ENGINE_KHASH
  if (hnd->engine_id == BZ_EID_KHASH) {
    // TODO
  }
#endif
#ifdef BZ_ENGINE_LMC
  if (hnd->engine_id == BZ_EID_LMC) {
    local_memcache_drop_namespace("main", 0, 0, &hnd->lmc_error);
  }
#endif
}

#ifdef BZ_ENGINE_BDB
  DBT bdb_key, bdb_data;
#endif

static inline void
bzing_inv_add(bzing_handle hnd,
              bz_uint256_t hash, uint64_t data)
{
  int result;
#ifdef BZ_ENGINE_KHASH
  khiter_t iter;
#endif

  switch (hnd->engine_id) {
#ifdef BZ_ENGINE_KHASH
  case BZ_EID_KHASH:
    if (hnd->engine_id == BZ_EID_KHASH) {
      iter = kh_put(256, hnd->kh_inv, hash, &result);
      kh_val(hnd->kh_inv, iter) = data;
    }
    break;
#endif
#ifdef BZ_ENGINE_LMC
  case BZ_EID_LMC:
    local_memcache_set(hnd->lmc_inv, (char *) hash.d8, 32, (char *) &data, 8);
    break;
#endif
#ifdef BZ_ENGINE_TC
  case BZ_EID_TC:
    tchdbput(hnd->tc_inv, (char *) hash.d8, 32, (char *) &data, 8);
    break;
#endif
#ifdef BZ_ENGINE_BDB
  case BZ_EID_BDB:
    memset(&bdb_key, 0, sizeof(DBT));
    memset(&bdb_data, 0, sizeof(DBT));
    bdb_key.data = hash.d8;
    bdb_key.size = 32;
    bdb_data.data = (char *) &data;
    bdb_data.size = 8;

    result = hnd->bdb_inv->put(hnd->bdb_inv, NULL, &bdb_key, &bdb_data, 0);
    break;
#endif
  default:
    break;
  }
}

void
bzing_block_add(bzing_handle hnd,
                const uint8_t *data, size_t max_len, size_t *actual_len)
{
  int i;
  uint64_t n_tx, n_txin, n_txout, script_len, offset = 80, tx_start;
  bz_uint256_t block_hash, *tx_hashes = NULL, merkle_root;

  double_sha256(data, 80, &block_hash);

  /* ah_iter_t iter;
  iter = alignhash_set(inv, hnd->inv, block_hash.d64[0], &result);
  alignhash_value(hnd->inv, iter) = offset;*/

  /*kstring_t k_block_hash;
  k_block_hash.l = sizeof(bz_uint256_t);
  k_block_hash.m = kroundup32(k_block_hash.l);
  k_block_hash.s = (char *) &k_block_hash;
  kh_put(bin, hnd->inv, &k_block_hash, &result);*/

  bzing_inv_add(hnd, block_hash, offset);

  //print_uint256(&block_hash);

  n_tx = parse_var_int(data, &offset);
  if (n_tx > 0) {
    tx_hashes = malloc(sizeof(bz_uint256_t) * n_tx);
  }
  for (i = 0; i < n_tx; i++) {
    tx_start = offset;
    offset += 4;
    n_txin = parse_var_int(data, &offset);
    while (n_txin > 0) {
      offset += 36;
      script_len = parse_var_int(data, &offset);
      offset += script_len + 4;
      n_txin--;
    }
    n_txout = parse_var_int(data, &offset);
    while (n_txout > 0) {
      offset += 8;
      script_len = parse_var_int(data, &offset);
      offset += script_len;
      n_txout--;
    }
    offset += 4;
    double_sha256(data+tx_start, offset-tx_start, &tx_hashes[i]);

    bzing_inv_add(hnd, tx_hashes[i], offset);
  }
  calc_merkle_root(tx_hashes, n_tx, &merkle_root);
  if (0 != memcmp(merkle_root.d8, data+36, sizeof(bz_uint256_t))) {
    printf("Invalid merkle root!\n");
    // TODO: Handle error
  }
  if (n_tx > 0) {
    free(tx_hashes);
  }
  *actual_len = offset;
  //printf("Block size: %d\n", *actual_len);
}

void
bzing_index_regen(bzing_handle hnd,
                  const uint8_t *data, size_t len)
{
  size_t block_len, n_blocks = 0;
  uint64_t offset = 0;

  while (offset < (len-1)) {
    n_blocks++;
    printf("Block #%lu %llu\n", (long unsigned int) n_blocks, (long long unsigned int) offset);
    bzing_block_add(hnd, data + offset, len - offset, &block_len);
    offset += block_len;
  }
}
