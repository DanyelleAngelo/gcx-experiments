/***********************************************************************

Copyright 2014-2016 Kennon Conrad

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

***********************************************************************/

// GLZAcompress.c
//   Iteratively does the following until there are no symbols worth generating:
//     1. Counts the symbol occurances in the input data and calculates the log base 2 of each symbol's probability of occuring
//     2. Builds portions of the generalized suffix tree and searches them for the "most compressible" symbol strings
//     3. Invalidates less desireable strings that overlap with better ones
//     4. Replaces each occurence of the best strings with a new symbol and adds the best strings to the end of the file
//        with a unique (define) symbol marker at the start of the string
//
// Usage:
//   GLZAcompress [-m#] [-p#] [-r#] [-w0] <infilename> <outfilename>, where
//       -m# sets the production cost, default 6.0
//       -p# sets the profit ratio weighting power, default 2.0 for capital encoded or UTF8 compliant files, 0.5 otherwise
//       -r# sets the approximate RAM usage in millions of bytes
//       -w0 disables the first cycle word only search

#include <inttypes.h>
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

const uint8_t INSERT_SYMBOL_CHAR = 0xFE;
const uint8_t DEFINE_SYMBOL_CHAR = 0xFF;
const uint32_t START_MY_SYMBOLS = 0x00080000;
const uint32_t MAX_WRITE_SIZE = 0x200000;
const uint32_t MAX_PRIOR_MATCHES = 20;
const uint32_t MAX_STRING_LENGTH = 8000;
const uint32_t BASE_NODES_CHILD_ARRAY_SIZE = 16;
const uint32_t NUM_PRECALCULATED_INSTANCE_LOGS = 10000;
const uint32_t NUM_PRECALCULATED_MATCH_RATIO_LOGS = 2000;
const uint32_t MAX_SCORES = 30000;
const uint32_t MAX_SYMBOLS_DEFINED = 0x00900000;

static struct string_node {
  uint32_t symbol;
  uint32_t last_match_index;
  uint32_t sibling_node_num[2];
  uint32_t child_node_num;
  uint32_t num_extra_symbols;
  uint32_t instances;
} *string_nodes;

static struct match_node {
  uint32_t symbol;
  uint32_t num_symbols;
  uint32_t score_number;
  struct match_node *child_ptr;
  uint32_t sibling_node_num[16];
  struct match_node *miss_ptr;
  struct match_node *hit_ptr;
} *match_nodes, *match_node_ptr, *child_match_node_ptr, *search_node_ptr;

struct node_score_data {
  float score;
  uint32_t last_match_index1;
  uint32_t last_match_index2;
  uint16_t num_symbols;
} candidates[30000];

struct lcp_thread_data {
  uint32_t min_symbol;
  uint32_t max_symbol;
  uint32_t string_nodes_limit;
  uint32_t first_string_node_num;
} lcp_thread_data[12];

struct rank_scores_struct {
  size_t node_ptr;
  double score;
  uint16_t num_string_symbols;
  uint16_t num_extra_symbols;
} rank_scores_buffer[0x10000];

struct score_data {
  struct string_node *node_ptr;
  double string_entropy;
  uint16_t num_string_symbols;
  uint8_t next_sibling;
} node_data[20000];

struct overlap_check_data {
  uint32_t *start_symbol_ptr;
  uint32_t *stop_symbol_ptr;
} overlap_check_data[7];

struct find_substitutions_data {
  uint32_t *stop_symbol_ptr;
  uint32_t extra_match_symbols;
  uint32_t *start_symbol_ptr;
  uint32_t data[0x400000];
  atomic_uchar done;
  atomic_uint_least32_t write_index;
  atomic_uint_least32_t read_index;
} find_substitutions_data[6];

uint32_t this_symbol, max_string_length, max_scores, i1;
uint32_t num_simple_symbols, node_instances, num_match_nodes, best_score_num_symbols, sibling_node_number;
uint32_t new_symbol_number[30000];
uint32_t symbol_count[0x900000];
uint32_t *start_symbol_ptr, *stop_symbol_ptr, *end_symbol_ptr, *in_symbol_ptr, *out_symbol_ptr, *min_symbol_ptr;
uint32_t *base_string_nodes_child_node_num, *best_score_last_match_ptr;
uint32_t substitute_data[0x10000];
uint16_t node_ptrs_num, num_candidates, candidates_index[30000];
uint8_t cap_encoded, UTF8_compliant, candidate_bad[30000];
uint8_t *char_buffer, *in_char_ptr, *end_char_ptr;
double log2_num_symbols_plus_substitution_cost, min_score, production_cost, profit_ratio_power;
double new_symbol_cost[2000], log2_instances[10000];
double *symbol_entropy;
atomic_uint_least16_t rank_scores_write_index, rank_scores_read_index;
atomic_uint_least16_t substitute_data_write_index, substitute_data_read_index;
atomic_uintptr_t max_symbol_ptr, scan_symbol_ptr;


volatile uint32_t * old_data_ptr;


uint32_t * init_best_score_ptrs() {
  best_score_last_match_ptr = candidates[candidates_index[i1]].last_match_index1 + start_symbol_ptr;
  return(best_score_last_match_ptr - candidates[candidates_index[i1]].num_symbols + 1);
}


void init_match_node(uint32_t match_num_symbols, uint32_t match_score_number) {
  match_node_ptr->symbol = this_symbol;
  match_node_ptr->num_symbols = match_num_symbols;
  match_node_ptr->score_number = match_score_number;
  match_node_ptr->child_ptr = 0;
  uint64_t * sibling_nodes_ptr = (uint64_t *)&match_node_ptr->sibling_node_num[0];
  *sibling_nodes_ptr = 0;
  *(sibling_nodes_ptr+1) = 0;
  *(sibling_nodes_ptr+2) = 0;
  *(sibling_nodes_ptr+3) = 0;
  *(sibling_nodes_ptr+4) = 0;
  *(sibling_nodes_ptr+5) = 0;
  *(sibling_nodes_ptr+6) = 0;
  *(sibling_nodes_ptr+7) = 0;
  match_node_ptr->miss_ptr = 0;
  match_node_ptr->hit_ptr = 0;
  return;
}


void init_level_1_match_node(uint32_t match_symbol, uint32_t match_score_number) {
  match_node_ptr->symbol = match_symbol;
  match_node_ptr->num_symbols = 1;
  match_node_ptr->score_number = match_score_number;
  match_node_ptr->child_ptr = 0;
  uint64_t * sibling_nodes_ptr = (uint64_t *)&match_node_ptr->sibling_node_num[0];
  *sibling_nodes_ptr = 0;
  *(sibling_nodes_ptr+1) = 0;
  *(sibling_nodes_ptr+2) = 0;
  *(sibling_nodes_ptr+3) = 0;
  *(sibling_nodes_ptr+4) = 0;
  *(sibling_nodes_ptr+5) = 0;
  *(sibling_nodes_ptr+6) = 0;
  *(sibling_nodes_ptr+7) = 0;
  match_node_ptr->miss_ptr = 0;
  match_node_ptr->hit_ptr = 0;
  return;
}


void move_to_match_sibling(uint32_t this_symbol, uint8_t * sibling_number) {
  uint32_t shifted_symbol = this_symbol;
  *sibling_number = (uint8_t)(shifted_symbol & 0xF);
  while ((this_symbol != match_node_ptr->symbol) && (match_node_ptr->sibling_node_num[*sibling_number] != 0)) {
    match_node_ptr = &match_nodes[match_node_ptr->sibling_node_num[*sibling_number]];
    shifted_symbol = shifted_symbol >> 4;
    *sibling_number = (uint8_t)(shifted_symbol & 0xF);
  }
  return;
}


void move_to_existing_match_sibling(uint32_t this_symbol) {
  uint32_t shifted_symbol = this_symbol;
  uint8_t sibling_number = (uint8_t)(shifted_symbol & 0xF);
  while (this_symbol != match_node_ptr->symbol) {
    match_node_ptr = &match_nodes[match_node_ptr->sibling_node_num[sibling_number]];
    shifted_symbol = shifted_symbol >> 4;
    sibling_number = (uint8_t)(shifted_symbol & 0xF);
  }
  return;
}


void move_to_search_sibling() {
  uint8_t sibling_depth = 0;
  uint32_t shifted_symbol = this_symbol;
  uint8_t sibling_nibble = (uint8_t)(shifted_symbol & 0xF);
  while ((this_symbol != search_node_ptr->symbol) && (search_node_ptr->sibling_node_num[sibling_nibble] != 0)) {
    search_node_ptr = &match_nodes[search_node_ptr->sibling_node_num[sibling_nibble]];
    sibling_depth++;
    shifted_symbol = shifted_symbol >> 4;
    sibling_nibble = (uint8_t)(shifted_symbol & 0xF);
  }
  return;
}


void move_to_match_child_with_make(uint32_t this_symbol, uint32_t score_number) {
  if (match_node_ptr->child_ptr == 0) {
    match_node_ptr->child_ptr = &match_nodes[num_match_nodes++];
    match_node_ptr = match_node_ptr->child_ptr;
    init_match_node(best_score_num_symbols, score_number);
  }
  else {
    match_node_ptr = match_node_ptr->child_ptr;
    uint8_t sibling_number;
    move_to_match_sibling(this_symbol, &sibling_number);
    if (this_symbol != match_node_ptr->symbol) {
      match_node_ptr->sibling_node_num[sibling_number] = num_match_nodes;
      match_node_ptr = &match_nodes[num_match_nodes++];
      init_match_node(best_score_num_symbols, score_number);
    }
  }
  return;
}


void write_siblings_miss_ptr(struct match_node *child_ptr) {
  uint8_t sibling_nibble;
  child_ptr->miss_ptr = search_node_ptr->child_ptr;
  for (sibling_nibble=0 ; sibling_nibble<16 ; sibling_nibble++) {
    sibling_node_number = child_ptr->sibling_node_num[sibling_nibble];
    if (sibling_node_number != 0)
      write_siblings_miss_ptr(&match_nodes[sibling_node_number]);
  }
  return;
}


void write_all_children_miss_ptr() {
  uint8_t sibling_nibble;
  child_match_node_ptr = match_node_ptr->child_ptr;
  if (child_match_node_ptr->miss_ptr == 0) {
    child_match_node_ptr->miss_ptr = search_node_ptr->child_ptr;
    for (sibling_nibble=0 ; sibling_nibble<16 ; sibling_nibble++) {
      sibling_node_number = child_match_node_ptr->sibling_node_num[sibling_nibble];
      if (sibling_node_number != 0)
        write_siblings_miss_ptr(&match_nodes[sibling_node_number]);
    }
  }
  return;
}


struct string_node * create_suffix_node(uint32_t suffix_symbol, uint32_t symbol_index, uint32_t * next_string_node_num_ptr) {
  struct string_node * node_ptr = &string_nodes[(*next_string_node_num_ptr)++];
  node_ptr->symbol = suffix_symbol;
  node_ptr->last_match_index = symbol_index;
  node_ptr->sibling_node_num[0] = 0;
  node_ptr->sibling_node_num[1] = 0;
  node_ptr->child_node_num = 0;
  node_ptr->num_extra_symbols = 0;
  node_ptr->instances = 1;
  return(node_ptr);
}


struct string_node * split_node_for_overlap(struct string_node * node_ptr, uint32_t string_start_index, uint32_t * in_symbol_ptr,
    uint32_t * next_string_node_num_ptr) {
  uint32_t non_overlap_length = string_start_index - node_ptr->last_match_index;
  struct string_node * new_node_ptr = &string_nodes[*next_string_node_num_ptr];
  new_node_ptr->symbol = *(start_symbol_ptr + node_ptr->last_match_index + non_overlap_length);
  new_node_ptr->last_match_index = node_ptr->last_match_index + non_overlap_length;
  new_node_ptr->sibling_node_num[0] = 0;
  new_node_ptr->sibling_node_num[1] = 0;
  new_node_ptr->child_node_num = node_ptr->child_node_num;
  new_node_ptr->num_extra_symbols = node_ptr->num_extra_symbols - non_overlap_length;
  new_node_ptr->instances = node_ptr->instances;
  node_ptr->last_match_index = in_symbol_ptr - start_symbol_ptr;
  node_ptr->child_node_num = (*next_string_node_num_ptr)++;
  node_ptr->num_extra_symbols = non_overlap_length - 1;
  node_ptr->instances++;
  node_ptr = new_node_ptr;
  return(node_ptr);
}


void add_suffix(uint32_t this_symbol, uint32_t *in_symbol_ptr, uint32_t *next_string_node_num_ptr) {
  uint32_t search_symbol, string_start_index;
  uint32_t *base_node_child_num_ptr;
  uint32_t * first_symbol_ptr = in_symbol_ptr - 1;
  struct string_node *node_ptr, *next_node_ptr, *new_node_ptr;
  search_symbol = *in_symbol_ptr;
  if ((int)search_symbol < 0)
    return;
  base_node_child_num_ptr = &base_string_nodes_child_node_num[this_symbol * BASE_NODES_CHILD_ARRAY_SIZE + (search_symbol & 0xF)];
  if (*base_node_child_num_ptr == 0) { // first occurence of the symbol, so create a child
    *base_node_child_num_ptr = *next_string_node_num_ptr;
    new_node_ptr = create_suffix_node(search_symbol, in_symbol_ptr - start_symbol_ptr, next_string_node_num_ptr);
    return;
  }
  uint32_t shifted_search_symbol;
  string_start_index = first_symbol_ptr - start_symbol_ptr;
  node_ptr = &string_nodes[*base_node_child_num_ptr];
  shifted_search_symbol = search_symbol >> 4;
  while (search_symbol != node_ptr->symbol) { // follow siblings until match found or end of siblings found
    uint32_t *sibling_node_num_ptr;
    sibling_node_num_ptr = (uint32_t *)&node_ptr->sibling_node_num[shifted_search_symbol & 1];
    next_node_ptr = &string_nodes[*sibling_node_num_ptr];
    if (next_node_ptr != string_nodes) {
      node_ptr = next_node_ptr;
      shifted_search_symbol = shifted_search_symbol >> 1;
    }
    else { // no match so add sibling
      *sibling_node_num_ptr = *next_string_node_num_ptr;
      new_node_ptr = create_suffix_node(search_symbol, in_symbol_ptr - start_symbol_ptr, next_string_node_num_ptr);
      return;
    }
  }

  // found a matching sibling
  while (node_ptr->child_node_num) {
    // matching sibling with child so check length of match
    uint32_t num_extra_symbols = node_ptr->num_extra_symbols;
    uint32_t * node_symbol_ptr = start_symbol_ptr + node_ptr->last_match_index;
    if (num_extra_symbols) {
      uint32_t length = 1;
      do {
        if (*(node_symbol_ptr + length) != *(in_symbol_ptr + length)) { /* insert node in branch */
          node_ptr->num_extra_symbols = length - 1;
          new_node_ptr = &string_nodes[*next_string_node_num_ptr];
          new_node_ptr->symbol = *(start_symbol_ptr + node_ptr->last_match_index + length);
          new_node_ptr->last_match_index = node_ptr->last_match_index + length;
          new_node_ptr->sibling_node_num[0] = 0;
          new_node_ptr->sibling_node_num[1] = 0;
          new_node_ptr->child_node_num = node_ptr->child_node_num;
          new_node_ptr->num_extra_symbols = num_extra_symbols - length;
          new_node_ptr->instances = node_ptr->instances;
          node_ptr->child_node_num = *next_string_node_num_ptr;
          *next_string_node_num_ptr += 1;
          new_node_ptr->sibling_node_num[(*(in_symbol_ptr + length)) & 1] = *next_string_node_num_ptr;
          new_node_ptr = create_suffix_node(*(in_symbol_ptr + length), in_symbol_ptr - start_symbol_ptr + length,
              next_string_node_num_ptr);
          if (node_ptr->last_match_index + length <= string_start_index) {
            node_ptr->last_match_index = in_symbol_ptr - start_symbol_ptr;
            node_ptr->instances++;
          }
          else if (node_ptr->last_match_index < string_start_index)
            node_ptr = split_node_for_overlap(node_ptr, string_start_index, in_symbol_ptr, next_string_node_num_ptr);
          return;
        }
      } while (length++ != num_extra_symbols);
    }
    if (node_ptr->last_match_index + num_extra_symbols < string_start_index) {
      node_ptr->last_match_index = in_symbol_ptr - start_symbol_ptr;
      node_ptr->instances++;
    }
    else if (node_ptr->last_match_index < string_start_index)
      node_ptr = split_node_for_overlap(node_ptr, string_start_index, in_symbol_ptr, next_string_node_num_ptr);

    in_symbol_ptr += num_extra_symbols + 1;
    search_symbol = *in_symbol_ptr;
    if (in_symbol_ptr - first_symbol_ptr + 1 > MAX_STRING_LENGTH)
      search_symbol = 0xF0000000 - string_start_index;
    uint32_t * prior_node_num_ptr = &node_ptr->child_node_num;
    node_ptr = &string_nodes[*prior_node_num_ptr];
    if (search_symbol != node_ptr->symbol) { // follow siblings until match found or end of siblings found
      shifted_search_symbol = search_symbol;
      do {
        prior_node_num_ptr = &node_ptr->sibling_node_num[shifted_search_symbol & 1];
        if (*prior_node_num_ptr == 0) {
          *prior_node_num_ptr = *next_string_node_num_ptr;
          node_ptr = create_suffix_node(search_symbol, in_symbol_ptr - start_symbol_ptr, next_string_node_num_ptr);
          return;
        }
        node_ptr = &string_nodes[*prior_node_num_ptr];
        shifted_search_symbol >>= 1;
      } while (search_symbol != node_ptr->symbol);
    }
  }

  // Matching node without child - extend branch, add child for previous instance, add child sibling
  uint32_t length = 1;
  uint32_t * node_symbol_ptr = start_symbol_ptr + node_ptr->last_match_index;
  while ((*(node_symbol_ptr + length) == *(in_symbol_ptr + length))
      && (in_symbol_ptr + length - first_symbol_ptr + 1 <= MAX_STRING_LENGTH))
    length++;
  node_ptr->num_extra_symbols = length - 1;
  if (node_ptr->last_match_index + length <= string_start_index) {
    node_ptr->last_match_index = in_symbol_ptr - start_symbol_ptr;
    node_ptr->instances++;
  }
  else if (node_ptr->last_match_index < string_start_index)
    node_ptr = split_node_for_overlap(node_ptr, string_start_index, in_symbol_ptr, next_string_node_num_ptr);
  node_ptr->child_node_num = *next_string_node_num_ptr;
  node_ptr = create_suffix_node(*(node_symbol_ptr + length), node_symbol_ptr - start_symbol_ptr + length,
      next_string_node_num_ptr);
  node_ptr->sibling_node_num[*(in_symbol_ptr + length) & 1] = *next_string_node_num_ptr;
  if (in_symbol_ptr + length - first_symbol_ptr + 1 <= MAX_STRING_LENGTH)
    node_ptr = create_suffix_node(*(in_symbol_ptr + length), in_symbol_ptr - start_symbol_ptr + length,
        next_string_node_num_ptr);
  else
    node_ptr = create_suffix_node(0xF0000000 - string_start_index, in_symbol_ptr - start_symbol_ptr + length,
        next_string_node_num_ptr);
  return;
}


void *rank_scores_thread(void *arg) {
  struct string_node *node_ptr, *next_child_ptr;
  double d_score;
  float score;
  uint32_t *score_last_symbol_ptr;
  uint16_t num_string_symbols, score_index, node_score_num_symbols;
  uint16_t local_write_index = 0;
  uint16_t node_ptrs_num = 0;

  atomic_store_explicit(&rank_scores_read_index, node_ptrs_num, memory_order_release);

  while (1) {
    while ((local_write_index == node_ptrs_num)
        && ((local_write_index = atomic_load_explicit(&rank_scores_write_index, memory_order_acquire))
          == node_ptrs_num)) /* wait */ ;
    node_ptr = (struct string_node *)rank_scores_buffer[node_ptrs_num].node_ptr;
    if ((size_t)node_ptr == 1)
      break;
    d_score = rank_scores_buffer[node_ptrs_num].score;
    if (d_score >= min_score) {
      score_last_symbol_ptr
          = start_symbol_ptr + node_ptr->last_match_index + rank_scores_buffer[node_ptrs_num].num_extra_symbols;
      score = (float)d_score;
      // find the position in the score list this node would go in
      uint16_t score_position, new_score_position, candidate_search_size;
      new_score_position = num_candidates;
      candidate_search_size = num_candidates + 1;
      do {
        candidate_search_size = (candidate_search_size + 1) >> 1;
        if (candidate_search_size > new_score_position)
          candidate_search_size = new_score_position;
        if (score > candidates[candidates_index[new_score_position - candidate_search_size]].score)
          new_score_position -= candidate_search_size;
      } while (candidate_search_size > 1);

      next_child_ptr = string_nodes + node_ptr->child_node_num;
      num_string_symbols = rank_scores_buffer[node_ptrs_num].num_string_symbols
          + rank_scores_buffer[node_ptrs_num].num_extra_symbols;
      uint32_t new_score_lmi1, new_score_lmi2, new_score_smi1_m_1, new_score_smi2_m_1;
      // check for overlaps with better score list nodes
      new_score_lmi1 = next_child_ptr->last_match_index - 1
          - (node_ptr->num_extra_symbols - rank_scores_buffer[node_ptrs_num].num_extra_symbols);
      new_score_lmi2 = (uint32_t)(score_last_symbol_ptr - start_symbol_ptr);

      if (new_score_lmi1 == new_score_lmi2) {
        uint32_t * sibling_node_num_ptr = &next_child_ptr->sibling_node_num[0];
        if (*sibling_node_num_ptr)
          new_score_lmi2 = string_nodes[*sibling_node_num_ptr].last_match_index - 1
              - (node_ptr->num_extra_symbols - rank_scores_buffer[node_ptrs_num].num_extra_symbols);
        else if (*(sibling_node_num_ptr + 1))
          new_score_lmi2 = string_nodes[*(sibling_node_num_ptr + 1)].last_match_index - 1
              - (node_ptr->num_extra_symbols - rank_scores_buffer[node_ptrs_num].num_extra_symbols);
        else {
          new_score_smi1_m_1 = new_score_lmi1 - num_string_symbols;
          score_position = 0;
          while (score_position < new_score_position) {
            uint32_t score_last_match_index1;
            score_index = candidates_index[score_position];
            node_score_num_symbols = candidates[score_index].num_symbols;
            score_last_match_index1 = candidates[score_index].last_match_index1;
            if (new_score_lmi1 <= score_last_match_index1 - node_score_num_symbols)
              score_position++;
            else {
              uint32_t score_last_match_index2;
              score_last_match_index2 = candidates[score_index].last_match_index2;
              if (score_last_match_index2 <= new_score_smi1_m_1)
                score_position++;
              else if ((score_last_match_index1 <= new_score_smi1_m_1)
                  && (new_score_lmi1 <= score_last_match_index2 - node_score_num_symbols))
                score_position++;
              else
                goto rank_scores_thread_node_done;
            }
          }
          // no better overlapping score list nodes, so put node in the list
          // look for subsequent overlaps that should be removed (only looks for one to avoid min score reduction)
          if (score_position < num_candidates) {
            do {
              score_index = candidates_index[score_position];
              uint32_t eslmi1 = candidates[score_index].last_match_index1;
              uint32_t eslmi2 = candidates[score_index].last_match_index2;
              node_score_num_symbols = candidates[score_index].num_symbols;
              if ((new_score_lmi1 > eslmi1 - node_score_num_symbols) && (eslmi2 > new_score_smi1_m_1)
                  && ((eslmi1 > new_score_smi1_m_1) || (new_score_lmi1 > eslmi2 - node_score_num_symbols)))
                goto rank_scores_thread_move_down;
            } while (++score_position != num_candidates);
            goto rank_scores_thread_check_max;
          }
        }
      }
      if (new_score_lmi2 < new_score_lmi1) {
        uint32_t temp_lmi = new_score_lmi1;
        new_score_lmi1 = new_score_lmi2;
        new_score_lmi2 = temp_lmi;
      }
      new_score_smi2_m_1 = new_score_lmi2 - num_string_symbols;
      new_score_smi1_m_1 = new_score_lmi1 - num_string_symbols;
      score_position = 0;
      while (score_position < new_score_position) {
        uint32_t score_last_match_index1;
        score_index = candidates_index[score_position];
        node_score_num_symbols = candidates[score_index].num_symbols;
        score_last_match_index1 = candidates[score_index].last_match_index1;
        if (new_score_lmi2 <= score_last_match_index1 - node_score_num_symbols)
          score_position++;
        else {
          uint32_t score_last_match_index2;
          score_last_match_index2 = candidates[score_index].last_match_index2;
          if (score_last_match_index2 <= new_score_smi1_m_1)
            score_position++;
          else if (score_last_match_index1 <= new_score_smi2_m_1) {
            if (new_score_lmi1 <= score_last_match_index1 - node_score_num_symbols) {
              if ((new_score_lmi2 <= score_last_match_index2 - node_score_num_symbols)
                  || (score_last_match_index2 <= new_score_smi2_m_1))
                score_position++;
              else
                goto rank_scores_thread_node_done;
            }
            else if (score_last_match_index1 <= new_score_smi1_m_1) {
              if (new_score_lmi2 <= score_last_match_index2 - node_score_num_symbols)
                score_position++;
              else if (score_last_match_index2 <= new_score_smi2_m_1) {
                if (new_score_lmi1 <= score_last_match_index2 - node_score_num_symbols)
                  score_position++;
                else
                  goto rank_scores_thread_node_done;
              }
              else
                goto rank_scores_thread_node_done;
            }
            else
              goto rank_scores_thread_node_done;
          }
          else
            goto rank_scores_thread_node_done;
        }
      }
      // no better overlapping score list nodes, so node will be put on the list
      // look for subsequent overlaps that should be removed (only looks for one to avoid min score reduction)
      if (score_position < num_candidates) {
        uint32_t eslmi1, eslmi2;
        score_index = candidates_index[score_position];
        eslmi1 = candidates[score_index].last_match_index1;
        eslmi2 = candidates[score_index].last_match_index2;
        node_score_num_symbols = candidates[score_index].num_symbols;

rank_scores_thread_check_overlap_lmi_not_equal:
        if ((new_score_lmi2 > eslmi1 - node_score_num_symbols)
            && (eslmi2 > new_score_smi1_m_1)
            && ((new_score_lmi1 > eslmi1 - node_score_num_symbols)
              || (eslmi1 > new_score_smi2_m_1)
              || ((new_score_lmi2 > eslmi2 - node_score_num_symbols) && (eslmi2 > new_score_smi2_m_1)))
            && ((eslmi1 > new_score_smi1_m_1)
              || (new_score_lmi1 > eslmi2 - node_score_num_symbols)
              || ((new_score_lmi2 > eslmi2 - node_score_num_symbols) && (eslmi2 > new_score_smi2_m_1))))
          goto rank_scores_thread_move_down;
        if (++score_position == num_candidates)
          goto rank_scores_thread_check_max;
        score_index = candidates_index[score_position];
        eslmi1 = candidates[score_index].last_match_index1;
        eslmi2 = candidates[score_index].last_match_index2;
        node_score_num_symbols = candidates[score_index].num_symbols;
        goto rank_scores_thread_check_overlap_lmi_not_equal;
      }

rank_scores_thread_check_max:
      if (num_candidates != max_scores) { // increment the list length if not at limit
        candidates_index[num_candidates] = num_candidates;
        num_candidates++;
      }
      else // otherwise throw away the lowest score instead of moving it
        score_position--;

rank_scores_thread_move_down:
      // move the lower scoring nodes down one location
      score_index = candidates_index[score_position];
      while (score_position > new_score_position) {
        candidates_index[score_position] = candidates_index[score_position - 1];
        score_position--;
      }
      // save the new score
      candidates_index[score_position] = score_index;
      candidates[score_index].score = score;
      candidates[score_index].num_symbols = num_string_symbols;
      candidates[score_index].last_match_index1 = new_score_lmi1;
      candidates[score_index].last_match_index2 = new_score_lmi2;
      if (num_candidates == max_scores)
        min_score = (double)candidates[candidates_index[max_scores-1]].score;
    }
rank_scores_thread_node_done:
    atomic_store_explicit(&rank_scores_read_index, ++node_ptrs_num, memory_order_release);
  }
  atomic_store_explicit(&rank_scores_read_index, ++node_ptrs_num, memory_order_release);
  return(0);
}


void score_base_node_tree(struct string_node* node_ptr, double string_entropy) {
  uint16_t num_string_symbols = 2;
  uint16_t level = 0;

  while (1) {
top_score_loop:
    node_instances = node_ptr->instances;
    if (node_instances >= 2)  {
      node_data[level].string_entropy = string_entropy;
      uint32_t symbol = node_ptr->symbol;
      string_entropy += symbol_entropy[symbol];
      uint32_t num_extra_symbols = 0;
      double repeats = (double)(node_instances - 1);
      while (num_extra_symbols != node_ptr->num_extra_symbols) {
        symbol = *(start_symbol_ptr + node_ptr->last_match_index + ++num_extra_symbols);
        string_entropy += symbol_entropy[symbol];
      }
      // calculate score
      double profit_per_substitution;
      if (node_instances < NUM_PRECALCULATED_MATCH_RATIO_LOGS)
        profit_per_substitution = string_entropy - new_symbol_cost[node_instances];
      else
        profit_per_substitution = string_entropy - (log2_num_symbols_plus_substitution_cost - log2(repeats));
      if (profit_per_substitution >= 0.0) {
        double total_bit_savings_minus_production_cost = repeats * profit_per_substitution - production_cost;
        if (total_bit_savings_minus_production_cost > 0.0) {
          double profit_ratio = profit_per_substitution / string_entropy;
          double score = total_bit_savings_minus_production_cost * pow(profit_ratio, profit_ratio_power);
          if (UTF8_compliant)
            if ((symbol == (uint32_t)' ')
                && (*(start_symbol_ptr + node_ptr->last_match_index + num_extra_symbols - 1) != (uint32_t)' '))
              score *= 0.03;
          if (score >= min_score) {
            if ((node_ptrs_num & 0xFFF) == 0)
              while ((uint16_t)(node_ptrs_num - atomic_load_explicit(&rank_scores_read_index, memory_order_acquire))
                  >= 0xF000) /* wait */ ;
            rank_scores_buffer[node_ptrs_num].score = score;
            rank_scores_buffer[node_ptrs_num].num_string_symbols = num_string_symbols;
            rank_scores_buffer[node_ptrs_num].num_extra_symbols = num_extra_symbols;
            rank_scores_buffer[node_ptrs_num].node_ptr = (size_t)node_ptr;
            atomic_store_explicit(&rank_scores_write_index, ++node_ptrs_num, memory_order_release);
          }
        }
      }
      node_data[level].node_ptr = node_ptr;
      node_data[level].num_string_symbols = num_string_symbols;
      node_data[level++].next_sibling = 0;
      num_string_symbols += num_extra_symbols + 1;
      node_ptr = &string_nodes[node_ptr->child_node_num];
      goto top_score_loop;
    }

    uint32_t sib_node_num = node_ptr->sibling_node_num[0];
    if (sib_node_num == 0) {
      sib_node_num = node_ptr->sibling_node_num[1];
      if (sib_node_num == 0) {
        while (level--) {
          uint8_t sibling_number = node_data[level].next_sibling;
          node_ptr = node_data[level].node_ptr;
          if ((sibling_number == 0) && (node_ptr->sibling_node_num[0])) {
            node_ptr = &string_nodes[node_ptr->sibling_node_num[0]];
            num_string_symbols = node_data[level].num_string_symbols;
            string_entropy = node_data[level].string_entropy;
            node_data[level++].next_sibling = 1;
            goto top_score_loop;
          }
          if (node_ptr->sibling_node_num[1]) {
            node_ptr = &string_nodes[node_ptr->sibling_node_num[1]];
            num_string_symbols = node_data[level].num_string_symbols;
            string_entropy = node_data[level].string_entropy;
            goto top_score_loop;
          }
        }
        return;
      }
      else
        node_ptr = &string_nodes[sib_node_num];
    }
    else {
      node_data[level].node_ptr = node_ptr;
      node_data[level].num_string_symbols = num_string_symbols;
      node_data[level].string_entropy = string_entropy;
      node_data[level++].next_sibling = 1;
      node_ptr = &string_nodes[sib_node_num];
    }
  }
}


void score_base_node_tree_cap(struct string_node* node_ptr, double string_entropy) {
  uint16_t num_string_symbols = 2;
  uint16_t level = 0;

  while (1) {
top_score_loop_cap:
    node_instances = node_ptr->instances;
    if (node_instances >= 2)  {
      node_data[level].string_entropy = string_entropy;
      uint32_t symbol = node_ptr->symbol;
      string_entropy += symbol_entropy[symbol];
      uint32_t num_extra_symbols = 0;
      double repeats = (double)(node_instances - 1);
      while (num_extra_symbols != node_ptr->num_extra_symbols) {
        symbol = *(start_symbol_ptr + node_ptr->last_match_index + ++num_extra_symbols);
        if (symbol == 0x20) {
          // calculate score
          double profit_per_substitution;
          if (node_instances < NUM_PRECALCULATED_MATCH_RATIO_LOGS)
            profit_per_substitution = string_entropy - new_symbol_cost[node_instances];
          else
            profit_per_substitution = string_entropy - (log2_num_symbols_plus_substitution_cost - log2(repeats));
          if (profit_per_substitution >= 0.0) {
            double total_bit_savings_minus_production_cost = repeats * profit_per_substitution - production_cost;
            if (total_bit_savings_minus_production_cost > 0.0) {
              double profit_ratio = profit_per_substitution / string_entropy;
              double score = total_bit_savings_minus_production_cost * pow(profit_ratio, profit_ratio_power) * 0.5;
              if (score >= min_score) {
                if ((node_ptrs_num & 0xFFF) == 0)
                  while ((uint16_t)(node_ptrs_num - atomic_load_explicit(&rank_scores_read_index, memory_order_acquire))
                      >= 0xF000) /* wait */ ;
                rank_scores_buffer[node_ptrs_num].score = score;
                rank_scores_buffer[node_ptrs_num].num_string_symbols = num_string_symbols;
                rank_scores_buffer[node_ptrs_num].num_extra_symbols = num_extra_symbols - 1;
                rank_scores_buffer[node_ptrs_num].node_ptr = (size_t)node_ptr;
                atomic_store_explicit(&rank_scores_write_index, ++node_ptrs_num, memory_order_release);
              }
            }
          }
        }
        string_entropy += symbol_entropy[symbol];
      }

      // calculate score
      double profit_per_substitution;
      if (node_instances < NUM_PRECALCULATED_MATCH_RATIO_LOGS)
        profit_per_substitution = string_entropy - new_symbol_cost[node_instances];
      else
        profit_per_substitution = string_entropy - (log2_num_symbols_plus_substitution_cost - log2(repeats));
      if (profit_per_substitution >= 0.0) {
        double total_bit_savings_minus_production_cost = repeats * profit_per_substitution - production_cost;
        if (total_bit_savings_minus_production_cost > 0.0) {
          double profit_ratio = profit_per_substitution / string_entropy;
          double score = total_bit_savings_minus_production_cost * pow(profit_ratio, profit_ratio_power);
          if (symbol == (uint32_t)' ') {
            if (*(start_symbol_ptr + node_ptr->last_match_index + num_extra_symbols - 1) != (uint32_t)' ')
              score *= 0.03;
            else
              score *= 0.5;
          }
          else if ((symbol & 0xF2) != 0x42)
            score *= 0.5;
          if (score >= min_score) {
            if ((node_ptrs_num & 0xFFF) == 0)
              while ((uint16_t)(node_ptrs_num - atomic_load_explicit(&rank_scores_read_index, memory_order_acquire))
                  >= 0xF000) /* wait */ ;
            rank_scores_buffer[node_ptrs_num].score = score;
            rank_scores_buffer[node_ptrs_num].num_string_symbols = num_string_symbols;
            rank_scores_buffer[node_ptrs_num].num_extra_symbols = num_extra_symbols;
            rank_scores_buffer[node_ptrs_num].node_ptr = (size_t)node_ptr;
            atomic_store_explicit(&rank_scores_write_index, ++node_ptrs_num, memory_order_release);
          }
        }
      }
      node_data[level].node_ptr = node_ptr;
      node_data[level].num_string_symbols = num_string_symbols;
      node_data[level++].next_sibling = 0;
      num_string_symbols += num_extra_symbols + 1;
      node_ptr = &string_nodes[node_ptr->child_node_num];
      goto top_score_loop_cap;
    }

    uint32_t sib_node_num = node_ptr->sibling_node_num[0];
    if (sib_node_num == 0) {
      sib_node_num = node_ptr->sibling_node_num[1];
      if (sib_node_num == 0) {
        while (level--) {
          uint8_t sibling_number = node_data[level].next_sibling;
          node_ptr = node_data[level].node_ptr;
          if ((sibling_number == 0) && (node_ptr->sibling_node_num[0])) {
            node_ptr = &string_nodes[node_ptr->sibling_node_num[0]];
            num_string_symbols = node_data[level].num_string_symbols;
            string_entropy = node_data[level].string_entropy;
            node_data[level++].next_sibling = 1;
            goto top_score_loop_cap;
          }
          if (node_ptr->sibling_node_num[1]) {
            node_ptr = &string_nodes[node_ptr->sibling_node_num[1]];
            num_string_symbols = node_data[level].num_string_symbols;
            string_entropy = node_data[level].string_entropy;
            goto top_score_loop_cap;
          }
        }
        return;
      }
      else
        node_ptr = &string_nodes[sib_node_num];
    }
    else {
      node_data[level].node_ptr = node_ptr;
      node_data[level].num_string_symbols = num_string_symbols;
      node_data[level].string_entropy = string_entropy;
      node_data[level++].next_sibling = 1;
      node_ptr = &string_nodes[sib_node_num];
    }
  }
}


void score_base_node_tree_words(struct string_node* node_ptr, double string_entropy) {
  uint32_t sib_node_num;
  uint16_t num_string_symbols = 2;
  uint16_t level = 0;

  while (1) {
top_word_score_loop:
    if (*(start_symbol_ptr + node_ptr->last_match_index) == 0x20)
      goto pop_level;
    node_instances = node_ptr->instances;
    if ((node_instances >= 2) && (*(start_symbol_ptr + node_ptr->last_match_index) != 0x20)) {
      node_data[level].string_entropy = string_entropy;
      string_entropy += symbol_entropy[node_ptr->symbol];
      uint32_t num_extra_symbols = 0;
      while (num_extra_symbols != node_ptr->num_extra_symbols) {
        if (*(start_symbol_ptr + node_ptr->last_match_index + num_extra_symbols + 1) == 0x20) {
          uint32_t last_symbol = *(start_symbol_ptr + node_ptr->last_match_index + num_extra_symbols);
          if (((last_symbol >= 'a') && (last_symbol <= 'z')) || ((last_symbol >= '0') && (last_symbol <= '9'))
              || ((last_symbol >= 0x80) && (last_symbol < START_MY_SYMBOLS))) {
            // calculate score
            double repeats = (double)(node_instances - 1);
            double profit_per_substitution;
            if (node_instances < NUM_PRECALCULATED_MATCH_RATIO_LOGS)
              profit_per_substitution = string_entropy - new_symbol_cost[node_instances];
            else
              profit_per_substitution = string_entropy - (log2_num_symbols_plus_substitution_cost - log2(repeats));
            if (profit_per_substitution >= 0.0) {
              double total_bit_savings_minus_production_cost = repeats * profit_per_substitution - production_cost;
              if (total_bit_savings_minus_production_cost > 0.0) {
                double profit_ratio = profit_per_substitution / string_entropy;
                double score = total_bit_savings_minus_production_cost * profit_ratio * 0.5;
                if (score >= min_score) {
                  if ((node_ptrs_num & 0xFFF) == 0)
                    while ((uint16_t)(node_ptrs_num - atomic_load_explicit(&rank_scores_read_index, memory_order_acquire))
                        >= 0xF000) /* wait */ ;
                  rank_scores_buffer[node_ptrs_num].score = score;
                  rank_scores_buffer[node_ptrs_num].num_string_symbols = num_string_symbols;
                  rank_scores_buffer[node_ptrs_num].num_extra_symbols = num_extra_symbols;
                  rank_scores_buffer[node_ptrs_num].node_ptr = (size_t)node_ptr;
                  atomic_store_explicit(&rank_scores_write_index, ++node_ptrs_num, memory_order_release);
                }
              }
            }
          }
          goto score_siblings;
        }
        string_entropy += symbol_entropy[*(start_symbol_ptr + node_ptr->last_match_index + ++num_extra_symbols)];
      }

      // calculate score
      if (*(start_symbol_ptr + node_ptr->last_match_index + num_extra_symbols + 1) == 0x20) {
        uint32_t last_symbol = *(start_symbol_ptr + node_ptr->last_match_index + num_extra_symbols);
        if (((last_symbol >= 'a') && (last_symbol <= 'z')) || ((last_symbol >= '0') && (last_symbol <= '9'))
            || ((last_symbol >= 0x80) && (last_symbol < START_MY_SYMBOLS))) {
          double repeats = (double)(node_instances - 1);
          double profit_per_substitution;
          if (node_instances < NUM_PRECALCULATED_MATCH_RATIO_LOGS)
            profit_per_substitution = string_entropy - new_symbol_cost[node_instances];
          else
            profit_per_substitution = string_entropy - (log2_num_symbols_plus_substitution_cost - log2(repeats));
          if (profit_per_substitution >= 0.0) {
            double total_bit_savings_minus_production_cost = repeats * profit_per_substitution - production_cost;
            if (total_bit_savings_minus_production_cost > 0.0) {
              double profit_ratio = profit_per_substitution / string_entropy;
              double score = total_bit_savings_minus_production_cost * profit_ratio;
              if (score >= min_score) {
                if ((node_ptrs_num & 0xFFF) == 0)
                  while ((uint16_t)(node_ptrs_num - atomic_load_explicit(&rank_scores_read_index, memory_order_acquire))
                      >= 0xF000) /* wait */ ;
                rank_scores_buffer[node_ptrs_num].score = score;
                rank_scores_buffer[node_ptrs_num].num_string_symbols = num_string_symbols;
                rank_scores_buffer[node_ptrs_num].num_extra_symbols = num_extra_symbols;
                rank_scores_buffer[node_ptrs_num].node_ptr = (size_t)node_ptr;
                atomic_store_explicit(&rank_scores_write_index, ++node_ptrs_num, memory_order_release);
              }
            }
          }
        }
        goto score_siblings;
      }
      node_data[level].node_ptr = node_ptr;
      node_data[level].num_string_symbols = num_string_symbols;
      node_data[level++].next_sibling = 0;
      num_string_symbols += num_extra_symbols + 1;
      node_ptr = &string_nodes[node_ptr->child_node_num];
      goto top_word_score_loop;
    }

score_siblings:
    sib_node_num = node_ptr->sibling_node_num[0];
    if (sib_node_num == 0) {
      sib_node_num = node_ptr->sibling_node_num[1];
      if (sib_node_num == 0) {
pop_level:
        while (level--) {
          uint8_t sibling_number = node_data[level].next_sibling;
          node_ptr = node_data[level].node_ptr;
          if ((sibling_number == 0) && (node_ptr->sibling_node_num[0])) {
            node_ptr = &string_nodes[node_ptr->sibling_node_num[0]];
            num_string_symbols = node_data[level].num_string_symbols;
            string_entropy = node_data[level].string_entropy;
            node_data[level++].next_sibling = 1;
            goto top_word_score_loop;
          }
          if (node_ptr->sibling_node_num[1]) {
            node_ptr = &string_nodes[node_ptr->sibling_node_num[1]];
            num_string_symbols = node_data[level].num_string_symbols;
            string_entropy = node_data[level].string_entropy;
            goto top_word_score_loop;
          }
        }
        return;
      }
      else
        node_ptr = &string_nodes[sib_node_num];
    }
    else {
      node_data[level].node_ptr = node_ptr;
      node_data[level].num_string_symbols = num_string_symbols;
      node_data[level].string_entropy = string_entropy;
      node_data[level++].next_sibling = 1;
      node_ptr = &string_nodes[sib_node_num];
    }
  }
}


void *build_lcp_thread(void *arg) {
  struct lcp_thread_data *thread_data_ptr;
  uint32_t min_symbol, max_symbol, next_string_node_num, string_node_num_limit;
  uint32_t *in_symbol_ptr, *local_scan_symbol_ptr;

  thread_data_ptr = (struct lcp_thread_data *)arg;
  in_symbol_ptr = (uint32_t *)min_symbol_ptr;
  min_symbol = thread_data_ptr->min_symbol;
  max_symbol = thread_data_ptr->max_symbol;
  next_string_node_num = thread_data_ptr->first_string_node_num;
  string_node_num_limit = thread_data_ptr->string_nodes_limit - 3;

  while ((uint32_t *)atomic_load_explicit(&max_symbol_ptr, memory_order_relaxed) != in_symbol_ptr) {
    local_scan_symbol_ptr = (uint32_t *)atomic_load_explicit(&scan_symbol_ptr, memory_order_relaxed);
    while (in_symbol_ptr != local_scan_symbol_ptr) {
      do {
        if (next_string_node_num < string_node_num_limit) {
          uint32_t this_symbol;
          this_symbol = *in_symbol_ptr++;
          if ((this_symbol >= min_symbol) && (this_symbol <= max_symbol))
            add_suffix(this_symbol, in_symbol_ptr, &next_string_node_num);
        }
        else
          in_symbol_ptr = local_scan_symbol_ptr;
      } while (in_symbol_ptr != local_scan_symbol_ptr);
      local_scan_symbol_ptr = (uint32_t *)atomic_load_explicit(&scan_symbol_ptr, memory_order_relaxed);
    }
  }
  return(0);
}


void score_symbol_tree(uint32_t min_symbol, uint32_t max_symbol) {
  uint32_t * base_node_child_num_ptr = &base_string_nodes_child_node_num[min_symbol * BASE_NODES_CHILD_ARRAY_SIZE];
  uint32_t symbol = min_symbol;
  if (cap_encoded) {
    while (symbol <= max_symbol) {
      if (*base_node_child_num_ptr)
        score_base_node_tree_cap(&string_nodes[*base_node_child_num_ptr],symbol_entropy[symbol]);
      if (*++base_node_child_num_ptr)
        score_base_node_tree_cap(&string_nodes[*base_node_child_num_ptr],symbol_entropy[symbol]);
      if (*++base_node_child_num_ptr)
        score_base_node_tree_cap(&string_nodes[*base_node_child_num_ptr],symbol_entropy[symbol]);
      if (*++base_node_child_num_ptr)
        score_base_node_tree_cap(&string_nodes[*base_node_child_num_ptr],symbol_entropy[symbol]);
      if (*++base_node_child_num_ptr)
        score_base_node_tree_cap(&string_nodes[*base_node_child_num_ptr],symbol_entropy[symbol]);
      if (*++base_node_child_num_ptr)
        score_base_node_tree_cap(&string_nodes[*base_node_child_num_ptr],symbol_entropy[symbol]);
      if (*++base_node_child_num_ptr)
        score_base_node_tree_cap(&string_nodes[*base_node_child_num_ptr],symbol_entropy[symbol]);
      if (*++base_node_child_num_ptr)
        score_base_node_tree_cap(&string_nodes[*base_node_child_num_ptr],symbol_entropy[symbol]);
      if (*++base_node_child_num_ptr)
        score_base_node_tree_cap(&string_nodes[*base_node_child_num_ptr],symbol_entropy[symbol]);
      if (*++base_node_child_num_ptr)
        score_base_node_tree_cap(&string_nodes[*base_node_child_num_ptr],symbol_entropy[symbol]);
      if (*++base_node_child_num_ptr)
        score_base_node_tree_cap(&string_nodes[*base_node_child_num_ptr],symbol_entropy[symbol]);
      if (*++base_node_child_num_ptr)
        score_base_node_tree_cap(&string_nodes[*base_node_child_num_ptr],symbol_entropy[symbol]);
      if (*++base_node_child_num_ptr)
        score_base_node_tree_cap(&string_nodes[*base_node_child_num_ptr],symbol_entropy[symbol]);
      if (*++base_node_child_num_ptr)
        score_base_node_tree_cap(&string_nodes[*base_node_child_num_ptr],symbol_entropy[symbol]);
      if (*++base_node_child_num_ptr)
        score_base_node_tree_cap(&string_nodes[*base_node_child_num_ptr],symbol_entropy[symbol]);
      if (*++base_node_child_num_ptr)
        score_base_node_tree_cap(&string_nodes[*base_node_child_num_ptr],symbol_entropy[symbol]);
      ++base_node_child_num_ptr;
      symbol++;
    }
  }
  else {
    while (symbol <= max_symbol) {
      if (*base_node_child_num_ptr)
        score_base_node_tree(&string_nodes[*base_node_child_num_ptr],symbol_entropy[symbol]);
      if (*++base_node_child_num_ptr)
        score_base_node_tree(&string_nodes[*base_node_child_num_ptr],symbol_entropy[symbol]);
      if (*++base_node_child_num_ptr)
        score_base_node_tree(&string_nodes[*base_node_child_num_ptr],symbol_entropy[symbol]);
      if (*++base_node_child_num_ptr)
        score_base_node_tree(&string_nodes[*base_node_child_num_ptr],symbol_entropy[symbol]);
      if (*++base_node_child_num_ptr)
        score_base_node_tree(&string_nodes[*base_node_child_num_ptr],symbol_entropy[symbol]);
      if (*++base_node_child_num_ptr)
        score_base_node_tree(&string_nodes[*base_node_child_num_ptr],symbol_entropy[symbol]);
      if (*++base_node_child_num_ptr)
        score_base_node_tree(&string_nodes[*base_node_child_num_ptr],symbol_entropy[symbol]);
      if (*++base_node_child_num_ptr)
        score_base_node_tree(&string_nodes[*base_node_child_num_ptr],symbol_entropy[symbol]);
      if (*++base_node_child_num_ptr)
        score_base_node_tree(&string_nodes[*base_node_child_num_ptr],symbol_entropy[symbol]);
      if (*++base_node_child_num_ptr)
        score_base_node_tree(&string_nodes[*base_node_child_num_ptr],symbol_entropy[symbol]);
      if (*++base_node_child_num_ptr)
        score_base_node_tree(&string_nodes[*base_node_child_num_ptr],symbol_entropy[symbol]);
      if (*++base_node_child_num_ptr)
        score_base_node_tree(&string_nodes[*base_node_child_num_ptr],symbol_entropy[symbol]);
      if (*++base_node_child_num_ptr)
        score_base_node_tree(&string_nodes[*base_node_child_num_ptr],symbol_entropy[symbol]);
      if (*++base_node_child_num_ptr)
        score_base_node_tree(&string_nodes[*base_node_child_num_ptr],symbol_entropy[symbol]);
      if (*++base_node_child_num_ptr)
        score_base_node_tree(&string_nodes[*base_node_child_num_ptr],symbol_entropy[symbol]);
      if (*++base_node_child_num_ptr)
        score_base_node_tree(&string_nodes[*base_node_child_num_ptr],symbol_entropy[symbol]);
      ++base_node_child_num_ptr;
      symbol++;
    }
  }
  while (node_ptrs_num != atomic_load_explicit(&rank_scores_read_index, memory_order_acquire)) /* wait */ ;
  return;
}


void score_symbol_tree_words() {
  uint8_t index;
  for (index = 0 ; index < BASE_NODES_CHILD_ARRAY_SIZE ; index++) {
    uint32_t base_node = base_string_nodes_child_node_num[0x20 * BASE_NODES_CHILD_ARRAY_SIZE + index];
    if (base_node)
      score_base_node_tree_words(&string_nodes[base_node],symbol_entropy[0x20]);
  }
  while (node_ptrs_num != atomic_load_explicit(&rank_scores_read_index, memory_order_acquire)) /* wait */ ;
  return;
}


void *overlap_check_thread(void *arg) {
  struct overlap_check_data *thread_data_ptr;
  struct match_node *match_node_ptr;
  uint32_t this_symbol, prior_match_score_number[MAX_PRIOR_MATCHES];
  uint32_t *prior_match_end_ptr[MAX_PRIOR_MATCHES];
  uint32_t *in_symbol_ptr;
  uint32_t *end_symbol_ptr;
  uint32_t num_prior_matches = 0;

  thread_data_ptr = (struct overlap_check_data *)arg;
  in_symbol_ptr = thread_data_ptr->start_symbol_ptr;
  end_symbol_ptr = thread_data_ptr->stop_symbol_ptr;

thread_overlap_check_loop_no_match:
  if (in_symbol_ptr == end_symbol_ptr)
    return(0);
  this_symbol = *in_symbol_ptr++;
  if ((int)this_symbol < 0)
    goto thread_overlap_check_loop_no_match;
  if (match_nodes[this_symbol].num_symbols == 0)
    goto thread_overlap_check_loop_no_match;
  match_node_ptr = &match_nodes[this_symbol];

thread_overlap_check_loop_match:
  if (in_symbol_ptr == end_symbol_ptr)
    return(0);
  this_symbol = *in_symbol_ptr++;
  if ((int)this_symbol < 0)
    goto thread_overlap_check_loop_no_match;
  match_node_ptr = match_node_ptr->child_ptr;
  if (this_symbol != match_node_ptr->symbol) {
    uint32_t shifted_symbol = this_symbol;
    do {
      if (match_node_ptr->sibling_node_num[shifted_symbol & 0xF] != 0) {
        match_node_ptr = &match_nodes[match_node_ptr->sibling_node_num[shifted_symbol & 0xF]];
        shifted_symbol = shifted_symbol >> 4;
      }
      else {
        if (match_node_ptr->miss_ptr == 0) {
          if (match_nodes[this_symbol].num_symbols == 0)
            goto thread_overlap_check_loop_no_match;
          match_node_ptr = &match_nodes[this_symbol];
          goto thread_overlap_check_loop_match;
        }
        else {
          match_node_ptr = match_node_ptr->miss_ptr;
          shifted_symbol = this_symbol;
        }
      }
    } while (this_symbol != match_node_ptr->symbol);
  }
  if (match_node_ptr->child_ptr)
    goto thread_overlap_check_loop_match;

  // no child, so found a match - check for overlaps
  uint32_t i1;
  uint8_t found_same_score_prior_match = 0;
  uint32_t node_score_number = match_node_ptr->score_number;
  uint32_t prior_match_number = 0;
  while (prior_match_number < num_prior_matches) {
    if (in_symbol_ptr - match_node_ptr->num_symbols > prior_match_end_ptr[prior_match_number]) {
      num_prior_matches--;
      for (i1 = prior_match_number ; i1 < num_prior_matches ; i1++) {
        prior_match_end_ptr[i1] = prior_match_end_ptr[i1+1];
        prior_match_score_number[i1] = prior_match_score_number[i1+1];
      }
    }
    else { // overlapping symbol substitution strings, so invalidate the lower score
      if (prior_match_score_number[prior_match_number] > node_score_number)
        candidate_bad[prior_match_score_number[prior_match_number]] = 1;
      else if (prior_match_score_number[prior_match_number] != node_score_number)
        candidate_bad[node_score_number] = 1;
      else
        found_same_score_prior_match = 1;
      prior_match_number++;
    }
  }
  match_node_ptr = match_node_ptr->hit_ptr;
  if (found_same_score_prior_match == 0) {
    prior_match_end_ptr[num_prior_matches] = in_symbol_ptr - 1;
    prior_match_score_number[num_prior_matches++] = node_score_number;
  }
  if (match_node_ptr == 0)
    goto thread_overlap_check_loop_no_match;
  else
    goto thread_overlap_check_loop_match;
  return(0);
}


void *find_substitutions_thread(void *arg) {
  struct match_node *match_node_ptr;
  uint32_t this_symbol, node_score_number;
  struct find_substitutions_data * thread_data_ptr = (struct find_substitutions_data *)arg;
  uint32_t * in_symbol_ptr = (uint32_t *)thread_data_ptr->start_symbol_ptr;
  uint32_t * end_symbol_ptr = thread_data_ptr->stop_symbol_ptr;
  uint32_t substitute_index = 0;
  uint32_t num_symbols_to_copy = 0;
  uint32_t local_read_index = 0;

  thread_data_ptr->extra_match_symbols = 0;
  atomic_store_explicit(&thread_data_ptr->write_index, substitute_index, memory_order_release);
  while (atomic_load_explicit(&thread_data_ptr->read_index, memory_order_acquire) != 0) /* wait */ ;
  if (in_symbol_ptr == end_symbol_ptr)
    goto thread_symbol_substitution_loop_end;
  this_symbol = *in_symbol_ptr++;
  if ((int)this_symbol >= 0) {
thread_symbol_substitution_loop_no_match_with_symbol:
    match_node_ptr = &match_nodes[this_symbol];
    if (match_node_ptr->num_symbols) {
      this_symbol = *in_symbol_ptr++;
      if ((int)this_symbol >= 0) {
        if (match_node_ptr->child_ptr == 0) {
          if (num_symbols_to_copy >= 100000) {
            while ((((substitute_index - local_read_index) & 0x3FFFFF) == 0x3FFFFF)
                && (((substitute_index - (local_read_index = atomic_load_explicit(&thread_data_ptr->read_index,
                    memory_order_acquire))) & 0x3FFFFF) == 0x3FFFFF)) /* wait */ ;
            thread_data_ptr->data[substitute_index] = num_symbols_to_copy;
            substitute_index = (substitute_index + 1) & 0x3FFFFF;
            atomic_store_explicit(&thread_data_ptr->write_index, substitute_index, memory_order_release);
            num_symbols_to_copy = 0;
          }
          if (in_symbol_ptr == end_symbol_ptr)
            goto thread_symbol_substitution_loop_end;
          this_symbol = *in_symbol_ptr++;
          if ((int)this_symbol >= 0)
            goto thread_symbol_substitution_loop_no_match_with_symbol;
          num_symbols_to_copy++;
          if (in_symbol_ptr == end_symbol_ptr)
            goto thread_symbol_substitution_loop_end;
          this_symbol = *in_symbol_ptr++;
          goto thread_symbol_substitution_loop_no_match_with_symbol;
        }
thread_symbol_substitution_loop_match_with_child:
        match_node_ptr = match_node_ptr->child_ptr;
        if (this_symbol != match_node_ptr->symbol) {
          uint32_t sibling_nibble = this_symbol;
          do {
            if (match_node_ptr->sibling_node_num[sibling_nibble & 0xF]) {
              match_node_ptr = &match_nodes[match_node_ptr->sibling_node_num[sibling_nibble & 0xF]];
              sibling_nibble = sibling_nibble >> 4;
            }
            else { // no match, so use miss node and output missed symbols
              if (match_node_ptr->miss_ptr == 0) {
                if (match_nodes[this_symbol].num_symbols) {
                  if (in_symbol_ptr > end_symbol_ptr) {
                    num_symbols_to_copy += match_node_ptr->num_symbols - (in_symbol_ptr - end_symbol_ptr);
                    goto thread_symbol_substitution_loop_end;
                  }
                  sibling_nibble = sibling_nibble >> 4;
                  num_symbols_to_copy += match_node_ptr->num_symbols - 1;
                  match_node_ptr = &match_nodes[this_symbol];
                }
                else {
                  if (in_symbol_ptr >= end_symbol_ptr) {
                    num_symbols_to_copy += match_node_ptr->num_symbols - (in_symbol_ptr - end_symbol_ptr);
                    goto thread_symbol_substitution_loop_end;
                  }
                  num_symbols_to_copy += match_node_ptr->num_symbols;
                  if (num_symbols_to_copy >= 100000) {
                    while ((((substitute_index - local_read_index) & 0x3FFFFF) == 0x3FFFFF)
                        && (((substitute_index - (local_read_index = atomic_load_explicit(&thread_data_ptr->read_index,
                          memory_order_acquire))) & 0x3FFFFF) == 0x3FFFFF)) /* wait */ ;
                    thread_data_ptr->data[substitute_index] = num_symbols_to_copy;
                    substitute_index = (substitute_index + 1) & 0x3FFFFF;
                    atomic_store_explicit(&thread_data_ptr->write_index, substitute_index, memory_order_release);
                    num_symbols_to_copy = 0;
                  }
                  if (in_symbol_ptr == end_symbol_ptr)
                    goto thread_symbol_substitution_loop_end;
                  this_symbol = *in_symbol_ptr++;
                  if ((int)this_symbol >= 0)
                    goto thread_symbol_substitution_loop_no_match_with_symbol;
                  num_symbols_to_copy++;
                  if (in_symbol_ptr == end_symbol_ptr)
                    goto thread_symbol_substitution_loop_end;
                  this_symbol = *in_symbol_ptr++;
                  goto thread_symbol_substitution_loop_no_match_with_symbol;
                }
              }
              else {
                num_symbols_to_copy += match_node_ptr->num_symbols - match_node_ptr->miss_ptr->num_symbols;
                if (in_symbol_ptr - match_node_ptr->miss_ptr->num_symbols >= end_symbol_ptr) {
                  num_symbols_to_copy -= in_symbol_ptr - end_symbol_ptr - match_node_ptr->miss_ptr->num_symbols;
                  goto thread_symbol_substitution_loop_end;
                }
                match_node_ptr = match_node_ptr->miss_ptr;
                sibling_nibble = this_symbol;
              }
            }
          } while (this_symbol != match_node_ptr->symbol);
        }
        if (match_node_ptr->child_ptr == 0) { // no child, so found a match
          if (num_symbols_to_copy) {
            while ((((substitute_index - local_read_index) & 0x3FFFFF) == 0x3FFFFF)
                && (((substitute_index - (local_read_index = atomic_load_explicit(&thread_data_ptr->read_index,
                  memory_order_acquire))) & 0x3FFFFF) == 0x3FFFFF)) /* wait */ ;
            thread_data_ptr->data[substitute_index] = num_symbols_to_copy;
            substitute_index = (substitute_index + 1) & 0x3FFFFF;
            atomic_store_explicit(&thread_data_ptr->write_index, substitute_index, memory_order_release);
            num_symbols_to_copy = 0;
          }
          node_score_number = match_node_ptr->score_number;
          while ((((substitute_index - local_read_index) & 0x3FFFFF) == 0x3FFFFF)
              && (((substitute_index - (local_read_index = atomic_load_explicit(&thread_data_ptr->read_index,
                memory_order_acquire))) & 0x3FFFFF) == 0x3FFFFF)) /* wait */ ;
          thread_data_ptr->data[substitute_index] = 0x80000000 + match_node_ptr->num_symbols;
          substitute_index = (substitute_index + 1) & 0x3FFFFF;
          atomic_store_explicit(&thread_data_ptr->write_index, substitute_index, memory_order_release);
          while ((((substitute_index - local_read_index) & 0x3FFFFF) == 0x3FFFFF)
              && (((substitute_index - (local_read_index = atomic_load_explicit(&thread_data_ptr->read_index,
                memory_order_acquire))) & 0x3FFFFF) == 0x3FFFFF)) /* wait */ ;
          thread_data_ptr->data[substitute_index] = num_simple_symbols + new_symbol_number[node_score_number];
          substitute_index = (substitute_index + 1) & 0x3FFFFF;
          atomic_store_explicit(&thread_data_ptr->write_index, substitute_index, memory_order_release);
          if (in_symbol_ptr >= end_symbol_ptr) {
            thread_data_ptr->extra_match_symbols = in_symbol_ptr - end_symbol_ptr;
            goto thread_symbol_substitution_loop_end;
          }
          this_symbol = *in_symbol_ptr++;
          if ((int)this_symbol >= 0)
            goto thread_symbol_substitution_loop_no_match_with_symbol;
          num_symbols_to_copy++;
          if (in_symbol_ptr == end_symbol_ptr)
            goto thread_symbol_substitution_loop_end;
          this_symbol = *in_symbol_ptr++;
          goto thread_symbol_substitution_loop_no_match_with_symbol;
        }
        if (num_symbols_to_copy >= 100000) {
          while ((((substitute_index - local_read_index) & 0x3FFFFF) == 0x3FFFFF)
              && (((substitute_index - (local_read_index = atomic_load_explicit(&thread_data_ptr->read_index,
                memory_order_acquire))) & 0x3FFFFF) == 0x3FFFFF)) /* wait */ ;
          thread_data_ptr->data[substitute_index] = num_symbols_to_copy;
          substitute_index = (substitute_index + 1) & 0x3FFFFF;
          atomic_store_explicit(&thread_data_ptr->write_index, substitute_index, memory_order_release);
          num_symbols_to_copy = 0;
        }
        this_symbol = *in_symbol_ptr++;
        if ((int)this_symbol >= 0)
          goto thread_symbol_substitution_loop_match_with_child;
        num_symbols_to_copy += match_node_ptr->num_symbols + 1;
        if (in_symbol_ptr >= end_symbol_ptr) {
          num_symbols_to_copy -= in_symbol_ptr - end_symbol_ptr;
          goto thread_symbol_substitution_loop_end;
        }
        this_symbol = *in_symbol_ptr++;
        goto thread_symbol_substitution_loop_no_match_with_symbol;
      }
      else { // define symbol
        num_symbols_to_copy += match_node_ptr->num_symbols + 1;
        if (in_symbol_ptr >= end_symbol_ptr) {
          num_symbols_to_copy -= in_symbol_ptr - end_symbol_ptr;
          goto thread_symbol_substitution_loop_end;
        }
        this_symbol = *in_symbol_ptr++;
        goto thread_symbol_substitution_loop_no_match_with_symbol;
      }
    }
    if (++num_symbols_to_copy <= 100000) {
      if (in_symbol_ptr == end_symbol_ptr)
        goto thread_symbol_substitution_loop_end;
      this_symbol = *in_symbol_ptr++;
      if ((int)this_symbol >= 0)
        goto thread_symbol_substitution_loop_no_match_with_symbol;
      num_symbols_to_copy++;
      if (in_symbol_ptr == end_symbol_ptr)
        goto thread_symbol_substitution_loop_end;
      this_symbol = *in_symbol_ptr++;
      goto thread_symbol_substitution_loop_no_match_with_symbol;
    }
    while ((((substitute_index - local_read_index) & 0x3FFFFF) == 0x3FFFFF)
       && (((substitute_index - (local_read_index = atomic_load_explicit(&thread_data_ptr->read_index,
            memory_order_acquire))) & 0x3FFFFF) == 0x3FFFFF)) /* wait */ ;
    thread_data_ptr->data[substitute_index] = num_symbols_to_copy;
    substitute_index = (substitute_index + 1) & 0x3FFFFF;
    atomic_store_explicit(&thread_data_ptr->write_index, substitute_index, memory_order_release);
    num_symbols_to_copy = 0;
    if (in_symbol_ptr == end_symbol_ptr)
      goto thread_symbol_substitution_loop_end;
    this_symbol = *in_symbol_ptr++;
    if ((int)this_symbol >= 0)
      goto thread_symbol_substitution_loop_no_match_with_symbol;
    num_symbols_to_copy = 1;
    if (in_symbol_ptr == end_symbol_ptr)
      goto thread_symbol_substitution_loop_end;
    this_symbol = *in_symbol_ptr++;
    goto thread_symbol_substitution_loop_no_match_with_symbol;
  }
  else { // define symbol
    num_symbols_to_copy++;
    if (in_symbol_ptr == end_symbol_ptr)
      goto thread_symbol_substitution_loop_end;
    this_symbol = *in_symbol_ptr++;
    goto thread_symbol_substitution_loop_no_match_with_symbol;
  }

thread_symbol_substitution_loop_end:
  if (num_symbols_to_copy) {
    while ((((substitute_index - local_read_index) & 0x3FFFFF) == 0x3FFFFF)
       && (((substitute_index - (local_read_index = atomic_load_explicit(&thread_data_ptr->read_index,
            memory_order_acquire))) & 0x3FFFFF) == 0x3FFFFF)) /* wait */ ;
    thread_data_ptr->data[substitute_index] = num_symbols_to_copy;
    substitute_index = (substitute_index + 1) & 0x3FFFFF;
    atomic_store_explicit(&thread_data_ptr->write_index, substitute_index, memory_order_release);
  }
  atomic_store_explicit(&thread_data_ptr->done, 1, memory_order_release);
  return(0);
}


void *substitute_thread(void *arg) {
  uint32_t * end_data_ptr;
  uint32_t * near_end_data_ptr;
  uint32_t data = 0;
  uint16_t substitute_data_index = 0;
  uint16_t local_write_index = 0;
//  uint32_t * old_data_ptr = start_symbol_ptr;
  old_data_ptr = start_symbol_ptr;

  atomic_store_explicit(&substitute_data_read_index, substitute_data_index, memory_order_release);
  while (1) {
    if (atomic_load_explicit(&substitute_data_write_index, memory_order_relaxed) != substitute_data_index) {
      local_write_index = atomic_load_explicit(&substitute_data_write_index, memory_order_acquire);
      while (substitute_data_index != local_write_index) {
        if ((int)(data = substitute_data[substitute_data_index++]) > 0) {
          end_data_ptr = out_symbol_ptr + data;
          near_end_data_ptr = end_data_ptr - 16;
          while (out_symbol_ptr <= near_end_data_ptr) {
            *out_symbol_ptr = *old_data_ptr;
            *(out_symbol_ptr + 1) = *(old_data_ptr + 1);
            *(out_symbol_ptr + 2) = *(old_data_ptr + 2);
            *(out_symbol_ptr + 3) = *(old_data_ptr + 3);
            *(out_symbol_ptr + 4) = *(old_data_ptr + 4);
            *(out_symbol_ptr + 5) = *(old_data_ptr + 5);
            *(out_symbol_ptr + 6) = *(old_data_ptr + 6);
            *(out_symbol_ptr + 7) = *(old_data_ptr + 7);
            *(out_symbol_ptr + 8) = *(old_data_ptr + 8);
            *(out_symbol_ptr + 9) = *(old_data_ptr + 9);
            *(out_symbol_ptr + 10) = *(old_data_ptr + 10);
            *(out_symbol_ptr + 11) = *(old_data_ptr + 11);
            *(out_symbol_ptr + 12) = *(old_data_ptr + 12);
            *(out_symbol_ptr + 13) = *(old_data_ptr + 13);
            *(out_symbol_ptr + 14) = *(old_data_ptr + 14);
            *(out_symbol_ptr + 15) = *(old_data_ptr + 15);
            old_data_ptr += 16;
            out_symbol_ptr += 16;
          }
          while (out_symbol_ptr != end_data_ptr)
            *out_symbol_ptr++ = *old_data_ptr++;
        }
        else if (data == 0xFFFFFFFF) {
          atomic_store_explicit(&substitute_data_read_index, substitute_data_index, memory_order_release);
          return(0);
        }
        else {
          old_data_ptr += (size_t)(data - 0x80000000);
          if (local_write_index == substitute_data_index) {
            atomic_store_explicit(&substitute_data_read_index, substitute_data_index, memory_order_release);
            while ((local_write_index = atomic_load_explicit(&substitute_data_write_index, memory_order_acquire))
                == substitute_data_index) /* wait */ ;
          }
          uint32_t symbol = substitute_data[substitute_data_index++];
          symbol_count[symbol]++;
          *out_symbol_ptr++ = symbol;
        }
      }
      atomic_store_explicit(&substitute_data_read_index, substitute_data_index, memory_order_release);
    }
  }
}


void print_usage() {
  fprintf(stderr,"Invalid format - Use GLZAcompress [-c#] [-p#] [-r#] [-w0] <infile> <outfile>\n");
  fprintf(stderr," where -c# sets the grammar production cost in bits\n");
  fprintf(stderr,"       -p# sets the profit power ratio.  0.0 is most compressive, larger\n");
  fprintf(stderr,"           values favor longer strings\n");
  fprintf(stderr,"       -r# sets memory usage in millions of bytes\n");
  fprintf(stderr,"       -w0 disables first cycle \"word\" only deduplication\n");
  return;
}


int main(int argc, char* argv[]) {
  FILE *fd_in, *fd_out;
  uint64_t available_RAM;
  uint32_t in_size, num_file_symbols, next_new_symbol_number, num_compound_symbols, i2;
  uint32_t UTF8_value, max_UTF8_value, symbol, num_symbols_to_copy, num_simple_symbols_used;
  uint32_t first_symbol_number, node_score_number, suffix_node_number, next_string_node_num, string_node_num_limit;
  uint32_t *search_match_ptr, *match_strings, *match_string_start_ptr, *node_string_start_ptr, *base_node_child_num_ptr;
  int32_t arg_num;
  uint16_t scan_cycle = 0;
  uint8_t this_char, format, user_set_RAM_size, user_set_profit_ratio_power, user_set_production_cost, create_words;
  uint8_t *free_RAM_ptr, *write_ptr;
  double d_file_symbols, prior_min_score, new_min_score, order_0_entropy, log_file_symbols, RAM_usage;
  float prior_cycle_start_ratio, prior_cycle_end_ratio;

  pthread_t build_lcp_thread1, build_lcp_thread2, build_lcp_thread3, build_lcp_thread4, build_lcp_thread5, build_lcp_thread6;
  pthread_t rank_scores_thread1, substitute_thread1;
  pthread_t overlap_check_threads[7];
  pthread_t find_substitutions_threads[7];


  clock_t start_time = clock();

  for (i1 = 0 ; i1 < MAX_SCORES ; i1++)
    candidate_bad[i1] = 0;
  user_set_RAM_size = 0;
  user_set_profit_ratio_power = 0;
  user_set_production_cost = 0;
  create_words = 1;
  arg_num = 1;
  if (argc < 3) {
    print_usage();
    exit(EXIT_FAILURE);
  }
  while (*argv[arg_num] ==  '-') {
    if (*(argv[arg_num]+1) == 'c') {
      production_cost = (double)atof(argv[arg_num++]+2);
      user_set_production_cost = 1;
    }
    else if (*(argv[arg_num]+1) == 'p') {
      profit_ratio_power = (double)atof(argv[arg_num++]+2);
      user_set_profit_ratio_power = 1;
    }
    else if (*(argv[arg_num]+1) == 'r') {
      user_set_RAM_size = 1;
      RAM_usage = (double)atof(argv[arg_num++]+2);
      if (RAM_usage < 60.0) {
        fprintf(stderr,"ERROR: -r value must be >= 60.0 (MB)\n");
        exit(EXIT_FAILURE);
      }
    }
    else if (*(argv[arg_num]+1) == 'w') {
      if (*(argv[arg_num++]+2) == '0')
        create_words = 0;
      else {
        fprintf(stderr,"ERROR: -w value must be 0\n");
        exit(EXIT_FAILURE);
      }
    }
    else {
      fprintf(stderr,"ERROR - Invalid '-' format.  Only -m<value>, -p<value> and -r<value> allowed\n");
      exit(EXIT_FAILURE);
    }
    if (argc < arg_num + 2) {
      print_usage();
      exit(EXIT_FAILURE);
    }
  }

  if ((fd_in=fopen(argv[arg_num],"rb")) == NULL) {
    fprintf(stderr,"Error - unable to open input file '%s'\n",argv[arg_num]);
    exit(EXIT_FAILURE);
  }
  arg_num++;
  fseek(fd_in, 0, SEEK_END);
  in_size = ftell(fd_in);
  rewind(fd_in);

  // Determine whether the RAM can be allocated, if not reduce size until malloc successful or RAM too small
  uint64_t max_memory_usage;
  if (sizeof(uint32_t *) >= 8)
    max_memory_usage = 0x800000000;
  else
    max_memory_usage = 0x70000000;
  if (user_set_RAM_size) {
    available_RAM = (uint64_t)(RAM_usage * 1000000.0);
    if (available_RAM > max_memory_usage)
      available_RAM = max_memory_usage;
    start_symbol_ptr = (uint32_t *)malloc(available_RAM);
    if (start_symbol_ptr == 0) {
      fprintf(stderr,"ERROR - Insufficient RAM to compress - unable to allocate %Iu bytes\n",
          (size_t)((available_RAM * 10) / 9));
      exit(EXIT_FAILURE);
    }
    else if (available_RAM < 5.0 * (double)in_size) {
      fprintf(stderr,"ERROR - Insufficient RAM to compress - program requires at least %Iu MB\n",
          (size_t)(((uint64_t)in_size * 5 + 999999)/1000000));
      exit(EXIT_FAILURE);
    }
  }
  else {
    available_RAM = (uint64_t)((double)in_size * 250.0 + 60000000.0);
    if (available_RAM > max_memory_usage)
    available_RAM = max_memory_usage;
    if (available_RAM > 3000000000.0 + 8.0 * (double)in_size)
      available_RAM = 3000000000.0 + 8.0 * (double)in_size;
    do {
      start_symbol_ptr = (uint32_t *)malloc(available_RAM);
      if (start_symbol_ptr)
        break;
      available_RAM = (available_RAM / 10) * 9;
    } while (available_RAM > 1500000000);
    if ((start_symbol_ptr == 0) || (available_RAM < 5.0 * (double)in_size)) {
      fprintf(stderr,"ERROR - Insufficient RAM to compress - unable to allocate %Iu bytes\n",
          (size_t)((available_RAM * 10) / 9));
      exit(EXIT_FAILURE);
    }
  }
  fprintf(stderr,"Allocated %Iu bytes for data processing\n",(size_t)available_RAM);

  char_buffer = (uint8_t *)start_symbol_ptr + 4 * (uint64_t)in_size;
  in_symbol_ptr = start_symbol_ptr;

  i1 = fread(char_buffer,1,in_size,fd_in);
  fflush(fd_in);
  fclose(fd_in);
  fprintf(stderr,"Read %u byte input file\n",(unsigned int)i1);

  // parse the file to determine UTF8_compliant
  num_compound_symbols = 0;
  UTF8_compliant = 1;
  format = *char_buffer;
  cap_encoded = (format == 1) ? 1 : 0;
  in_char_ptr = char_buffer + 1;
  end_char_ptr = char_buffer + in_size;

  if (in_char_ptr >= end_char_ptr) {
    num_candidates = 0;
    goto write_file;
  }

  do {
    if (*in_char_ptr >= INSERT_SYMBOL_CHAR) {
      if (*(in_char_ptr+1) != DEFINE_SYMBOL_CHAR)
        in_char_ptr += 4;
      else {
        UTF8_compliant = 0;
        break;
      }
    }
    else if (*in_char_ptr >= 0x80) {
      if (*in_char_ptr < 0xC0) {
        UTF8_compliant = 0;
        break;
      }
      else if (*in_char_ptr < 0xE0) {
        if ((*(in_char_ptr+1) < 0x80) || (*(in_char_ptr+1) >= 0xC0)) {
          UTF8_compliant = 0;
          break;
        }
        else
          in_char_ptr += 2;
      }
      else if (*in_char_ptr < 0xF0) {
        if ((*(in_char_ptr+1) < 0x80) || (*(in_char_ptr+1) >= 0xC0) || (*(in_char_ptr+2) >= 0xC0) || (*(in_char_ptr+2) >= 0xC0)) {
          UTF8_compliant = 0;
          break;
        }
        else
          in_char_ptr += 3;
      }
      else if (*in_char_ptr < 0xF2) {
        if ((*(in_char_ptr+1) < 0x80) || (*(in_char_ptr+1) >= 0xC0) || (*(in_char_ptr+2) < 0x80) || (*(in_char_ptr+2) >= 0xC0)
            || (*(in_char_ptr+3) < 0x80) || (*(in_char_ptr+3) >= 0xC0)) {
          UTF8_compliant = 0;
          break;
        }
        else
          in_char_ptr += 4;
      }
      else {
        UTF8_compliant = 0;
        break;
      }
    }
    else
      in_char_ptr++;
  } while (in_char_ptr < end_char_ptr);
  if (in_char_ptr > end_char_ptr)
    UTF8_compliant = 0;
  if (user_set_profit_ratio_power == 0) {
    if ((cap_encoded != 0) || (UTF8_compliant != 0))
      profit_ratio_power = 2.0;
    else if ((format & 0xFE) == 0)
      profit_ratio_power = 1.0;
    else
      profit_ratio_power = 0.0;
  }

  fprintf(stderr,"cap encoded: %u, UTF8 compliant %u\n",(unsigned int)cap_encoded,(unsigned int)UTF8_compliant);

  // parse the file to determine num_compound_symbols and max_UTF8_value
  num_file_symbols = 0;
  in_char_ptr = char_buffer + 1;

  if (UTF8_compliant) {
    num_simple_symbols = START_MY_SYMBOLS;
    first_symbol_number = 0x80000000 + START_MY_SYMBOLS;
    max_UTF8_value = 0;
    while (in_char_ptr != end_char_ptr) {
      this_char = *in_char_ptr++;
      if (this_char < 0x80)
        *in_symbol_ptr++ = (uint32_t)this_char;
      else if (this_char == INSERT_SYMBOL_CHAR) {
        *in_symbol_ptr++ = START_MY_SYMBOLS + 0x10000 * (uint32_t)*in_char_ptr
            + 0x100 * (uint32_t)*(in_char_ptr+1) + (uint32_t)*(in_char_ptr+2);
        in_char_ptr += 3;
      }
      else if (this_char == DEFINE_SYMBOL_CHAR) {
        *in_symbol_ptr++ = first_symbol_number + 0x10000 * (uint32_t)*in_char_ptr
            + 0x100 * (uint32_t)*(in_char_ptr+1) + (uint32_t)*(in_char_ptr+2);
        in_char_ptr += 3;
        num_compound_symbols++;
      }
      else if (this_char >= 0x80) {
        if (this_char >= 0xF0) {
          UTF8_value = 0x40000 * (uint32_t)(this_char & 0x7) + 0x1000 * (uint32_t)(*in_char_ptr++ & 0x3F);
          UTF8_value += 0x40 * (uint32_t)(*in_char_ptr++ & 0x3F);
          UTF8_value += (uint32_t)*in_char_ptr++ & 0x3F;
        }
        else if (this_char >= 0xE0) {
          UTF8_value = 0x1000 * (uint32_t)(this_char & 0xF) + 0x40 * (uint32_t)(*in_char_ptr++ & 0x3F);
          UTF8_value += (uint32_t)*in_char_ptr++ & 0x3F;
        }
        else 
          UTF8_value = 0x40 * (uint32_t)(this_char & 0x1F) + (*in_char_ptr++ & 0x3F);
        if (UTF8_value > max_UTF8_value)
          max_UTF8_value = UTF8_value;
        *in_symbol_ptr++ = UTF8_value;
      }
      num_file_symbols++;
    }
    fprintf(stderr,"Found %u symbols, %u defines, maximum unicode value 0x%x\n",
        (unsigned int)num_file_symbols,(unsigned int)num_compound_symbols,(unsigned int)max_UTF8_value);
  }
  else {
    num_simple_symbols = 0x100;
    first_symbol_number = 0x80000000 + 0x100;
    while (in_char_ptr != end_char_ptr) {
      this_char = *in_char_ptr++;
      if (this_char < INSERT_SYMBOL_CHAR)
        *in_symbol_ptr++ = (uint32_t)this_char;
      else if (*in_char_ptr == DEFINE_SYMBOL_CHAR) {
        *in_symbol_ptr++ = (uint32_t)this_char;
        in_char_ptr++;
      }
      else {
        if (this_char == INSERT_SYMBOL_CHAR)
          *in_symbol_ptr++ = 0x100 + 0x10000 * (uint32_t)*in_char_ptr
              + 0x100 * (uint32_t)*(in_char_ptr+1) + (uint32_t)*(in_char_ptr+2);
        else {
          *in_symbol_ptr++ = first_symbol_number + 0x10000 * (uint32_t)*in_char_ptr
              + 0x100 * (uint32_t)*(in_char_ptr+1) + (uint32_t)*(in_char_ptr+2);
          num_compound_symbols++;
        }
        in_char_ptr += 3;
      }
      num_file_symbols++;
    }
    fprintf(stderr,"Found %u symbols, %u defines\n",(unsigned int)num_file_symbols,(unsigned int)num_compound_symbols);
  }
  end_symbol_ptr = in_symbol_ptr;
  *end_symbol_ptr = 0xFFFFFFFE;
  free_RAM_ptr = (uint8_t *)(end_symbol_ptr + 1);

  next_new_symbol_number = num_simple_symbols + num_compound_symbols;
  uint32_t *symbol_count_ptr;
  symbol_count_ptr = symbol_count;
  while (symbol_count_ptr < symbol_count + next_new_symbol_number)
    *symbol_count_ptr++ = 0;

  // parse the data to determine symbol_counts
  in_symbol_ptr = start_symbol_ptr;
  do {
    symbol = *in_symbol_ptr++;
    while ((int)symbol >= 0) {
      symbol_count[symbol]++;
      symbol = *in_symbol_ptr++;
    }
  } while (symbol != 0xFFFFFFFE);

  log2_instances[1] = 0.0;
  for (i1 = 2 ; i1 < NUM_PRECALCULATED_INSTANCE_LOGS ; i1++)
    log2_instances[i1] = log2((double)i1);

  num_simple_symbols_used = 0;
  for (i1 = 0 ; i1 < num_simple_symbols ; i1++)
    if (symbol_count[i1])
      num_simple_symbols_used++;    

  max_scores = 5000;
  min_score = 10.0;
  prior_min_score = min_score;
  prior_cycle_start_ratio = 0.0;
  prior_cycle_end_ratio = 1.0;

  do {
    next_new_symbol_number = num_simple_symbols + num_compound_symbols;
    num_file_symbols = end_symbol_ptr - start_symbol_ptr;
    d_file_symbols = (double)num_file_symbols;
    *end_symbol_ptr = 0xFFFFFFFE;
    if (user_set_production_cost == 0)
      production_cost = log2(d_file_symbols / (double)(num_compound_symbols + num_simple_symbols_used)) + 1.5;

    // Allocate memory for the log symbol count arrays
    if ((size_t)free_RAM_ptr % sizeof(double) != 0)
      free_RAM_ptr = (uint8_t *)(((size_t)free_RAM_ptr / sizeof(double) + 1) * sizeof(double));
    symbol_entropy = (double *)free_RAM_ptr;
    free_RAM_ptr += sizeof(double) * (size_t)next_new_symbol_number;

    // Set the memory addresses for the base_string_nodes_child_ptr array
    base_string_nodes_child_node_num = (uint32_t *)free_RAM_ptr;

    // pre-calculate log match ratios
    log2_num_symbols_plus_substitution_cost = log2(d_file_symbols) + 1.4;
    for (i1 = 2 ; i1 < NUM_PRECALCULATED_MATCH_RATIO_LOGS ; i1++)
      // offset by 1 because the first instance is not a repeat
      new_symbol_cost[i1] = log2_num_symbols_plus_substitution_cost - log2((double)(i1-1));

    order_0_entropy = 0.0;
    log_file_symbols = log2(d_file_symbols);
    i1 = 0;

    do {
      if (symbol_count[i1] != 0) {
        if (symbol_count[i1] < NUM_PRECALCULATED_INSTANCE_LOGS) {
          double this_symbol_entropy = log_file_symbols - log2_instances[symbol_count[i1]];
          symbol_entropy[i1] = this_symbol_entropy;
          order_0_entropy += (double)symbol_count[i1] * this_symbol_entropy;
        }
        else {
          double d_symbol_count = (double)symbol_count[i1];
          double this_symbol_entropy = log_file_symbols - log2(d_symbol_count);
          symbol_entropy[i1] = this_symbol_entropy;
          order_0_entropy += d_symbol_count * this_symbol_entropy;
        }
      }
    } while (++i1 < num_simple_symbols);

    if (num_compound_symbols != 0) {
      while (i1 < next_new_symbol_number) {
        if (symbol_count[i1] < NUM_PRECALCULATED_INSTANCE_LOGS) {
          double this_symbol_entropy = log_file_symbols - log2_instances[symbol_count[i1]];
          symbol_entropy[i1] = this_symbol_entropy;
          order_0_entropy += (double)symbol_count[i1++] * this_symbol_entropy;
        }
        else {
          double d_symbol_count = (double)symbol_count[i1];
          double this_symbol_entropy = log_file_symbols - log2(d_symbol_count);
          symbol_entropy[i1++] = this_symbol_entropy;
          order_0_entropy += d_symbol_count * this_symbol_entropy;
        }
      }
      double d_symbol_count = (double)num_compound_symbols;
      double this_symbol_entropy = log_file_symbols - log2(d_symbol_count);
      order_0_entropy += d_symbol_count * this_symbol_entropy;
    }
    fprintf(stderr,"%u: %u syms, dict. size %u, %.4f bits/sym, o0e %u bytes\n",
        (unsigned int)++scan_cycle,(unsigned int)num_file_symbols,(unsigned int)num_compound_symbols,
        (float)(order_0_entropy/d_file_symbols),(unsigned int)(order_0_entropy*0.125));

    // setup to build the suffix tree
    base_node_child_num_ptr = base_string_nodes_child_node_num;
    while (base_node_child_num_ptr < base_string_nodes_child_node_num + next_new_symbol_number * BASE_NODES_CHILD_ARRAY_SIZE) {
      *base_node_child_num_ptr = 0;
      *(base_node_child_num_ptr + 1) = 0;
      *(base_node_child_num_ptr + 2) = 0;
      *(base_node_child_num_ptr + 3) = 0;
      *(base_node_child_num_ptr + 4) = 0;
      *(base_node_child_num_ptr + 5) = 0;
      *(base_node_child_num_ptr + 6) = 0;
      *(base_node_child_num_ptr + 7) = 0;
      *(base_node_child_num_ptr + 8) = 0;
      *(base_node_child_num_ptr + 9) = 0;
      *(base_node_child_num_ptr + 10) = 0;
      *(base_node_child_num_ptr + 11) = 0;
      *(base_node_child_num_ptr + 12) = 0;
      *(base_node_child_num_ptr + 13) = 0;
      *(base_node_child_num_ptr + 14) = 0;
      *(base_node_child_num_ptr + 15) = 0;
      base_node_child_num_ptr += BASE_NODES_CHILD_ARRAY_SIZE;
    }
    num_candidates = 0;

    // Set the memory adddress for the suffix tree nodes
    string_nodes = (struct string_node *)((size_t)free_RAM_ptr
        + sizeof(uint32_t) * (size_t)next_new_symbol_number * BASE_NODES_CHILD_ARRAY_SIZE);
    string_node_num_limit = (uint32_t)(((uint8_t *)start_symbol_ptr + available_RAM - (uint8_t *)string_nodes)
        / sizeof(struct string_node));

    if (1.0 - prior_cycle_end_ratio < prior_cycle_end_ratio - prior_cycle_start_ratio) {
      if ((prior_cycle_start_ratio == 0.0) && (prior_cycle_end_ratio < 0.999)) {
        prior_cycle_start_ratio = 1.0 - 0.99 * prior_cycle_end_ratio;
        in_symbol_ptr = start_symbol_ptr + (uint32_t)(prior_cycle_start_ratio * (float)(end_symbol_ptr - start_symbol_ptr));
      }
      else if ((1.0 - prior_cycle_end_ratio) * 1.5 < prior_cycle_end_ratio - prior_cycle_start_ratio) {
        prior_cycle_start_ratio = 0.0;
        in_symbol_ptr = start_symbol_ptr;
      }
      else {
        prior_cycle_start_ratio = 1.0 - 0.97 * (prior_cycle_end_ratio - prior_cycle_start_ratio);
        in_symbol_ptr = start_symbol_ptr + (uint32_t)(prior_cycle_start_ratio * (float)(end_symbol_ptr - start_symbol_ptr));
      }
    }
    else {
      prior_cycle_start_ratio = prior_cycle_end_ratio;
      in_symbol_ptr = start_symbol_ptr + (uint32_t)(prior_cycle_start_ratio * (float)(end_symbol_ptr - start_symbol_ptr));
    }
    uint32_t * cycle_start_ptr = in_symbol_ptr;

    next_string_node_num = 1;
    fprintf(stderr,"Common prefix scan 0 - %x\r",(unsigned int)(next_new_symbol_number-1));

    uint32_t main_string_nodes_limit;
    if ((scan_cycle == 1) && cap_encoded && create_words) {
      max_scores = 30000;
      main_string_nodes_limit = string_node_num_limit - 3;
      while (next_string_node_num < main_string_nodes_limit) {
        this_symbol = *in_symbol_ptr++;
        if (this_symbol == ' ') {
          if (((*in_symbol_ptr >= 'a') && (*in_symbol_ptr <= 'z')) || ((*in_symbol_ptr >= '0') && (*in_symbol_ptr <= '9'))
              || ((*in_symbol_ptr >= 0x80) && (*in_symbol_ptr < START_MY_SYMBOLS)))
            add_suffix(this_symbol, in_symbol_ptr, &next_string_node_num);
        }
        else if (this_symbol == 0xFFFFFFFE) {
          in_symbol_ptr--;
          break; // exit loop on EOF
        }
      }

      node_ptrs_num = 0;
      atomic_store_explicit(&rank_scores_write_index, node_ptrs_num, memory_order_release);
      pthread_create(&rank_scores_thread1, NULL, rank_scores_thread, (void *)&rank_scores_buffer[0]);
      while (atomic_load_explicit(&rank_scores_read_index, memory_order_acquire) != 0) /* wait */ ;

      score_symbol_tree_words();
      while (node_ptrs_num != atomic_load_explicit(&rank_scores_read_index, memory_order_acquire)) /* wait */ ;
      rank_scores_buffer[node_ptrs_num].node_ptr = 1;
      atomic_store_explicit(&rank_scores_write_index, ++node_ptrs_num, memory_order_release);
      while (node_ptrs_num != atomic_load_explicit(&rank_scores_read_index, memory_order_acquire)) /* wait */ ;
      pthread_join(rank_scores_thread1, NULL);

      fprintf(stderr,"Read %u of %u symbols, start %.4f\n",(unsigned int)(in_symbol_ptr-cycle_start_ptr),
          (unsigned int)(end_symbol_ptr-start_symbol_ptr),prior_cycle_start_ratio);

      prior_cycle_end_ratio = (float)(in_symbol_ptr - start_symbol_ptr) / (float)(end_symbol_ptr - start_symbol_ptr);
      goto jump_loc;
    }

    uint32_t sum_symbols, symbols_limit; 
    uint32_t main_max_symbol;
    i1 = 1;
    sum_symbols = symbol_count[0];
    symbols_limit = ((num_file_symbols - num_compound_symbols) / 100) * 7;
    while (sum_symbols < symbols_limit)
      sum_symbols += symbol_count[i1++];
    main_max_symbol = i1 - 1;
    lcp_thread_data[0].min_symbol = i1;
    if (i1 < next_new_symbol_number - 1)
      sum_symbols += symbol_count[i1++];
    symbols_limit = ((num_file_symbols - num_compound_symbols) / 100) * 15;
    while (sum_symbols < symbols_limit)
      sum_symbols += symbol_count[i1++];
    lcp_thread_data[0].max_symbol = i1 - 1;
    lcp_thread_data[1].min_symbol = i1;
    if (i1 < next_new_symbol_number - 1)
      sum_symbols += symbol_count[i1++];
    symbols_limit = ((num_file_symbols - num_compound_symbols) / 100) * 23;
    while (sum_symbols < symbols_limit)
      sum_symbols += symbol_count[i1++];
    lcp_thread_data[1].max_symbol = i1 - 1;
    lcp_thread_data[2].min_symbol = i1;
    if (i1 < next_new_symbol_number - 1)
      sum_symbols += symbol_count[i1++];
    symbols_limit = ((num_file_symbols - num_compound_symbols) / 100) * 32;
    while (sum_symbols < symbols_limit)
      sum_symbols += symbol_count[i1++];
    lcp_thread_data[2].max_symbol = i1 - 1;
    lcp_thread_data[3].min_symbol = i1;
    if (i1 < next_new_symbol_number - 1)
      sum_symbols += symbol_count[i1++];
    symbols_limit = ((num_file_symbols - num_compound_symbols) / 100) * 42;
    while (sum_symbols < symbols_limit)
      sum_symbols += symbol_count[i1++];
    lcp_thread_data[3].max_symbol = i1 - 1;
    lcp_thread_data[4].min_symbol = i1;
    if (i1 < next_new_symbol_number - 1)
      sum_symbols += symbol_count[i1++];
    symbols_limit = ((num_file_symbols - num_compound_symbols) / 100) * 53;
    while (sum_symbols < symbols_limit)
      sum_symbols += symbol_count[i1++];
    lcp_thread_data[4].max_symbol = i1 - 1;
    lcp_thread_data[5].min_symbol = i1;
    if (i1 < next_new_symbol_number - 1)
      sum_symbols += symbol_count[i1++];
    symbols_limit = ((num_file_symbols - num_compound_symbols) / 100) * 65;
    while (sum_symbols < symbols_limit)
      sum_symbols += symbol_count[i1++];
    lcp_thread_data[5].max_symbol = i1 - 1;
    lcp_thread_data[6].min_symbol = i1;
    if (i1 < next_new_symbol_number - 1)
      sum_symbols += symbol_count[i1++];
    symbols_limit = ((num_file_symbols - num_compound_symbols) / 100) * 69;
    while (sum_symbols < symbols_limit)
      sum_symbols += symbol_count[i1++];
    lcp_thread_data[6].max_symbol = i1 - 1;
    lcp_thread_data[7].min_symbol = i1;
    if (i1 < next_new_symbol_number - 1)
      sum_symbols += symbol_count[i1++];
    symbols_limit = ((num_file_symbols - num_compound_symbols) / 100) * 76;
    while (sum_symbols < symbols_limit)
      sum_symbols += symbol_count[i1++];
    lcp_thread_data[7].max_symbol = i1 - 1;
    lcp_thread_data[8].min_symbol = i1;
    if (i1 < next_new_symbol_number - 1)
      sum_symbols += symbol_count[i1++];
    symbols_limit = ((num_file_symbols - num_compound_symbols) / 100) * 83;
    while (sum_symbols < symbols_limit)
      sum_symbols += symbol_count[i1++];
    lcp_thread_data[8].max_symbol = i1 - 1;
    lcp_thread_data[9].min_symbol = i1;
    if (i1 < next_new_symbol_number - 1)
      sum_symbols += symbol_count[i1++];
    symbols_limit = ((num_file_symbols - num_compound_symbols) / 100) * 89;
    while (sum_symbols < symbols_limit)
      sum_symbols += symbol_count[i1++];
    lcp_thread_data[9].max_symbol = i1 - 1;
    lcp_thread_data[10].min_symbol = i1;
    if (i1 < next_new_symbol_number - 1)
      sum_symbols += symbol_count[i1++];
    symbols_limit = ((num_file_symbols - num_compound_symbols) / 100) * 95;
    while (sum_symbols < symbols_limit)
      sum_symbols += symbol_count[i1++];
    lcp_thread_data[10].max_symbol = i1 - 1;
    lcp_thread_data[11].min_symbol = i1;
    lcp_thread_data[11].max_symbol = next_new_symbol_number - 1;

    min_symbol_ptr = in_symbol_ptr;

    lcp_thread_data[6].first_string_node_num = 0;
    main_string_nodes_limit = (string_node_num_limit / 100) * 9 - 3;
    lcp_thread_data[6].string_nodes_limit = (string_node_num_limit / 100) * 9;
    lcp_thread_data[0].first_string_node_num = (string_node_num_limit / 100) * 9;
    lcp_thread_data[7].first_string_node_num = (string_node_num_limit / 100) * 9;
    lcp_thread_data[0].string_nodes_limit = (string_node_num_limit / 100) * 22;
    lcp_thread_data[7].string_nodes_limit = (string_node_num_limit / 100) * 22;
    lcp_thread_data[1].first_string_node_num = (string_node_num_limit / 100) * 22;
    lcp_thread_data[8].first_string_node_num = (string_node_num_limit / 100) * 22;
    lcp_thread_data[1].string_nodes_limit = (string_node_num_limit / 100) * 35;
    lcp_thread_data[8].string_nodes_limit = (string_node_num_limit / 100) * 35;
    lcp_thread_data[2].first_string_node_num = (string_node_num_limit / 100) * 35;
    lcp_thread_data[9].first_string_node_num = (string_node_num_limit / 100) * 35;
    lcp_thread_data[2].string_nodes_limit = (string_node_num_limit / 100) * 49;
    lcp_thread_data[9].string_nodes_limit = (string_node_num_limit / 100) * 49;
    lcp_thread_data[3].first_string_node_num = (string_node_num_limit / 100) * 49;
    lcp_thread_data[10].first_string_node_num = (string_node_num_limit / 100) * 49;
    lcp_thread_data[3].string_nodes_limit = (string_node_num_limit / 100) * 65;
    lcp_thread_data[10].string_nodes_limit = (string_node_num_limit / 100) * 65;
    lcp_thread_data[4].first_string_node_num = (string_node_num_limit / 100) * 65;
    lcp_thread_data[11].first_string_node_num = (string_node_num_limit / 100) * 65;
    lcp_thread_data[4].string_nodes_limit = (string_node_num_limit / 100) * 82;
    lcp_thread_data[11].string_nodes_limit = (string_node_num_limit / 100) * 82;
    lcp_thread_data[5].first_string_node_num = (string_node_num_limit / 100) * 82;
    lcp_thread_data[5].string_nodes_limit = string_node_num_limit;

    atomic_store_explicit(&max_symbol_ptr, 0, memory_order_release);
    atomic_store_explicit(&scan_symbol_ptr, (uintptr_t)in_symbol_ptr, memory_order_release);

    pthread_create(&build_lcp_thread1,NULL,build_lcp_thread,(char *)&lcp_thread_data[0]);
    pthread_create(&build_lcp_thread2,NULL,build_lcp_thread,(char *)&lcp_thread_data[1]);
    pthread_create(&build_lcp_thread3,NULL,build_lcp_thread,(char *)&lcp_thread_data[2]);
    pthread_create(&build_lcp_thread4,NULL,build_lcp_thread,(char *)&lcp_thread_data[3]);
    pthread_create(&build_lcp_thread5,NULL,build_lcp_thread,(char *)&lcp_thread_data[4]);
    pthread_create(&build_lcp_thread6,NULL,build_lcp_thread,(char *)&lcp_thread_data[5]);

    if (cap_encoded) {
      while (next_string_node_num < main_string_nodes_limit) {
        this_symbol = *in_symbol_ptr++;
        while ((int)this_symbol >= 0) {
          if (this_symbol <= main_max_symbol) {
            atomic_store_explicit(&scan_symbol_ptr, (uintptr_t)in_symbol_ptr, memory_order_relaxed);
            add_suffix(this_symbol, in_symbol_ptr, &next_string_node_num);
            if (next_string_node_num < main_string_nodes_limit)
              this_symbol = *in_symbol_ptr++;
            else
              goto done_building_lcp_tree;
          }
          else
            this_symbol = *in_symbol_ptr++;
        }
        if (this_symbol == 0xFFFFFFFE) {
          in_symbol_ptr--;
          break; // exit loop on EOF
        }
      }
    }
    else {
      while (next_string_node_num < main_string_nodes_limit) {
        this_symbol = *in_symbol_ptr++;
        while ((int)this_symbol >= 0) {
          if (this_symbol <= main_max_symbol) {
            atomic_store_explicit(&scan_symbol_ptr, (uintptr_t)in_symbol_ptr, memory_order_relaxed);
            if ((next_string_node_num & 0xFFFF) == 0)
              fprintf(stderr,"Main processed %u of %u symbols \r",
                  (uint32_t)(in_symbol_ptr-1-cycle_start_ptr),(uint32_t)(end_symbol_ptr-start_symbol_ptr));
            add_suffix(this_symbol, in_symbol_ptr, &next_string_node_num);
            if (next_string_node_num < main_string_nodes_limit)
              this_symbol = *in_symbol_ptr++;
            else
              goto done_building_lcp_tree;
          }
          else
            this_symbol = *in_symbol_ptr++;
        }
        if (this_symbol == 0xFFFFFFFE) {
          in_symbol_ptr--;
          break; // exit loop on EOF
        }
      }
    }

done_building_lcp_tree:
    atomic_store_explicit(&scan_symbol_ptr, (uintptr_t)in_symbol_ptr, memory_order_release);
    atomic_store_explicit(&max_symbol_ptr, (uintptr_t)in_symbol_ptr, memory_order_release);

    node_ptrs_num = 0;
    atomic_store_explicit(&rank_scores_write_index, node_ptrs_num, memory_order_release);
    pthread_create(&rank_scores_thread1,NULL,rank_scores_thread,(void *)&rank_scores_buffer[0]);
    while (atomic_load_explicit(&rank_scores_read_index, memory_order_acquire) != 0) /* wait */ ;

    fprintf(stderr,"Score section 1                               \r");
    score_symbol_tree(0, main_max_symbol);
    while (node_ptrs_num != atomic_load_explicit(&rank_scores_read_index, memory_order_acquire)) /* wait */ ;

    fprintf(stderr,"Wait thread 1  \r");
    pthread_join(build_lcp_thread1,NULL);
    pthread_create(&build_lcp_thread1,NULL,build_lcp_thread,(char *)&lcp_thread_data[6]);
    fprintf(stderr,"Score section 2\r");
    score_symbol_tree(main_max_symbol + 1, lcp_thread_data[0].max_symbol);
    while (node_ptrs_num != atomic_load_explicit(&rank_scores_read_index, memory_order_acquire)) /* wait */ ;

    fprintf(stderr,"Wait thread 2  \r");
    pthread_join(build_lcp_thread2,NULL);
    pthread_create(&build_lcp_thread2,NULL,build_lcp_thread,(char *)&lcp_thread_data[7]);
    fprintf(stderr,"Score section 3\r");
    score_symbol_tree(lcp_thread_data[0].max_symbol + 1, lcp_thread_data[1].max_symbol);
    while (node_ptrs_num != atomic_load_explicit(&rank_scores_read_index, memory_order_acquire)) /* wait */ ;

    fprintf(stderr,"Wait thread 3  \r");
    pthread_join(build_lcp_thread3,NULL);
    pthread_create(&build_lcp_thread3,NULL,build_lcp_thread,(char *)&lcp_thread_data[8]);
    fprintf(stderr,"Score section 4\r");
    score_symbol_tree(lcp_thread_data[1].max_symbol + 1, lcp_thread_data[2].max_symbol);
    while (node_ptrs_num != atomic_load_explicit(&rank_scores_read_index, memory_order_acquire)) /* wait */ ;

    fprintf(stderr,"Wait thread 4  \r");
    pthread_join(build_lcp_thread4,NULL);
    pthread_create(&build_lcp_thread4,NULL,build_lcp_thread,(char *)&lcp_thread_data[9]);
    fprintf(stderr,"Score section 5\r");
    score_symbol_tree(lcp_thread_data[2].max_symbol + 1, lcp_thread_data[3].max_symbol);
    while (node_ptrs_num != atomic_load_explicit(&rank_scores_read_index, memory_order_acquire)) /* wait */ ;

    fprintf(stderr,"Wait thread 5  \r");
    pthread_join(build_lcp_thread5,NULL);
    pthread_create(&build_lcp_thread5,NULL,build_lcp_thread,(char *)&lcp_thread_data[10]);
    fprintf(stderr,"Score section 6\r");
    score_symbol_tree(lcp_thread_data[3].max_symbol + 1, lcp_thread_data[4].max_symbol);
    while (node_ptrs_num != atomic_load_explicit(&rank_scores_read_index, memory_order_acquire)) /* wait */ ;

    fprintf(stderr,"Wait thread 6  \r");
    pthread_join(build_lcp_thread6,NULL);
    pthread_create(&build_lcp_thread6,NULL,build_lcp_thread,(char *)&lcp_thread_data[11]);
    fprintf(stderr,"Score section 7\r");
    score_symbol_tree(lcp_thread_data[4].max_symbol + 1, lcp_thread_data[5].max_symbol);

    fprintf(stderr,"Wait thread 7  \r");
    pthread_join(build_lcp_thread1,NULL);
    fprintf(stderr,"Score section 8\r");
    score_symbol_tree(lcp_thread_data[5].max_symbol + 1, lcp_thread_data[6].max_symbol);

    fprintf(stderr,"Wait thread 8  \r");
    pthread_join(build_lcp_thread2,NULL);
    fprintf(stderr,"Score section 9\r");
    score_symbol_tree(lcp_thread_data[6].max_symbol + 1, lcp_thread_data[7].max_symbol);

    fprintf(stderr,"Wait thread 9  \r");
    pthread_join(build_lcp_thread3,NULL);
    fprintf(stderr,"Score section 10\r");
    score_symbol_tree(lcp_thread_data[7].max_symbol + 1, lcp_thread_data[8].max_symbol);

    fprintf(stderr,"Wait thread 10  \r");
    pthread_join(build_lcp_thread4,NULL);
    fprintf(stderr,"Score section 11\r");
    score_symbol_tree(lcp_thread_data[8].max_symbol + 1, lcp_thread_data[9].max_symbol);

    fprintf(stderr,"Wait thread 11  \r");
    pthread_join(build_lcp_thread5,NULL);
    fprintf(stderr,"Score section 12\r");
    score_symbol_tree(lcp_thread_data[9].max_symbol + 1, lcp_thread_data[10].max_symbol);

    fprintf(stderr,"Wait thread 12  \r");
    pthread_join(build_lcp_thread6,NULL);
    fprintf(stderr,"Score section 13\r");
    score_symbol_tree(lcp_thread_data[10].max_symbol + 1, lcp_thread_data[11].max_symbol);
    while (node_ptrs_num != atomic_load_explicit(&rank_scores_read_index, memory_order_acquire)) /* wait */ ;

    rank_scores_buffer[node_ptrs_num].node_ptr = 1;
    atomic_store_explicit(&rank_scores_write_index, ++node_ptrs_num, memory_order_release);
    while (node_ptrs_num != atomic_load_explicit(&rank_scores_read_index, memory_order_acquire)) /* wait */ ;
    pthread_join(rank_scores_thread1,NULL);

    fprintf(stderr,"Read %u of %u symbols, start %.4f\n",(unsigned int)(in_symbol_ptr-cycle_start_ptr),
        (unsigned int)(end_symbol_ptr-start_symbol_ptr),prior_cycle_start_ratio);

    prior_cycle_end_ratio = (float)(in_symbol_ptr-start_symbol_ptr)/(float)(end_symbol_ptr-start_symbol_ptr);

jump_loc:

    if (num_candidates) {
      fprintf(stderr,"Common prefix scan 0 - %x, score[0 - %hu] = %.5f - %.5f\n",
          (unsigned int)(next_new_symbol_number-1),(unsigned short int)num_candidates-1,
          candidates[candidates_index[0]].score,candidates[candidates_index[num_candidates-1]].score);

      free_RAM_ptr = (uint8_t *)(end_symbol_ptr + 1);
      match_nodes = (struct match_node *)free_RAM_ptr;
      match_nodes[0].num_symbols = 0;
      match_nodes[0].child_ptr = 0;

      if ((scan_cycle == 1) && cap_encoded) {
        float min_score = 0.0000005 * order_0_entropy;
        if (min_score < 100.0)
          min_score = 100.0;
        for (i1 = 0 ; i1 < num_candidates ; i1++)
          if (candidates[candidates_index[i1]].score < min_score) {
            num_candidates = i1;
            break;
          }
      }
      else {
        for (i1 = 1 ; i1 < num_candidates ; i1++)
          if (candidates[candidates_index[i1]].score < 0.1 * candidates[candidates_index[0]].score - 1.0) {
            num_candidates = i1;
            break;
          }
      }

      // build a prefix tree of the match strings
      num_match_nodes = 1;
      i1 = 0;
      while (i1 < num_candidates) {
        uint32_t *best_score_match_ptr;
        best_score_match_ptr = init_best_score_ptrs();
        match_node_ptr = match_nodes;
        while (best_score_match_ptr <= best_score_last_match_ptr) {
          this_symbol = *best_score_match_ptr;
          if (match_node_ptr->child_ptr == 0) {
            match_node_ptr->child_ptr = &match_nodes[num_match_nodes++];
            match_node_ptr = match_node_ptr->child_ptr;
            init_match_node(best_score_num_symbols, i1);
          }
          else {
            match_node_ptr = match_node_ptr->child_ptr;
            uint8_t sibling_number;
            move_to_match_sibling(this_symbol, &sibling_number);
            if (this_symbol == match_node_ptr->symbol) {
              if (match_node_ptr->child_ptr == 0) {
                candidate_bad[i1] = 1;
                break;
              }
            }
            else {
              match_node_ptr->sibling_node_num[sibling_number] = num_match_nodes;
              match_node_ptr = &match_nodes[num_match_nodes++];
              init_match_node(0, i1);
            }
          }
          best_score_match_ptr++;
        }
        if (match_node_ptr->child_ptr != 0)
          candidate_bad[i1] = 1;
        i1++;
      }

      // span nodes entering the longest suffix matches and invalidating lower score if substring match found
      i1 = 0;
      while (i1 < num_candidates) {
        uint32_t *best_score_match_ptr;
        best_score_match_ptr = init_best_score_ptrs();
        // read the first symbol
        this_symbol = *best_score_match_ptr++;
        match_node_ptr = &match_nodes[1];
        move_to_existing_match_sibling(this_symbol);
        while (best_score_match_ptr <= best_score_last_match_ptr) {
          // starting with the second symbol, look for suffixes that are in the prefix tree
          search_match_ptr = best_score_match_ptr;
          search_node_ptr = match_nodes;
          while (1) { // follow the tree until find child = 0 or sibling = 0
            if (search_node_ptr->child_ptr == 0) { // found a scored string that is a substring of this string
              if (search_node_ptr->score_number > i1)
                candidate_bad[search_node_ptr->score_number] = 1;
              else if (search_node_ptr->score_number != i1)
                candidate_bad[i1] = 1;
              break;
            }
            search_node_ptr = search_node_ptr->child_ptr;
            if (search_node_ptr == 0) // no children so exit suffix search
              break;
            this_symbol = *search_match_ptr;
            move_to_search_sibling();
            if (this_symbol != search_node_ptr->symbol) // no child match so exit suffix search
              break;
            match_node_ptr->miss_ptr = search_node_ptr;
            search_match_ptr++;
          }
          this_symbol = *best_score_match_ptr++;
        }
        i1++;
      }

      // Redo the tree build and miss values with just the valid score symbols
      match_node_ptr = match_nodes + next_new_symbol_number;
      while (match_node_ptr-- != match_nodes)
        match_node_ptr->num_symbols = 0;
      num_match_nodes = next_new_symbol_number;

      i1 = 0;
      while (i1 < num_candidates) {
        if (candidate_bad[i1] == 0) {
          uint32_t *best_score_match_ptr;
          best_score_match_ptr = init_best_score_ptrs();
          this_symbol = *best_score_match_ptr++;
          match_node_ptr = &match_nodes[this_symbol];
          best_score_num_symbols = 1;
          if (match_node_ptr->num_symbols == 0)
            init_level_1_match_node(this_symbol, i1);
          while (best_score_match_ptr <= best_score_last_match_ptr) {
            this_symbol = *best_score_match_ptr++;
            best_score_num_symbols++;
            move_to_match_child_with_make(this_symbol, i1);
          }
        }
        i1++;
      }

      // span nodes entering the longest (first) suffix match for each node
      i1 = 0;
      while (i1 < num_candidates) {
        if (candidate_bad[i1] == 0) {
          uint32_t *best_score_suffix_ptr;
          best_score_suffix_ptr = init_best_score_ptrs();
          suffix_node_number = *best_score_suffix_ptr++;
          // starting at the node of the 2nd symbol in string, match strings with prefix tree until no match found,
          //   for each match node found, if suffix miss symbol is zero, set it to the tree symbol node
          while (best_score_suffix_ptr <= best_score_last_match_ptr) {
            // follow the suffix until the end (or break on no tree matches)
            this_symbol = *best_score_suffix_ptr++;
            suffix_node_number = match_nodes[suffix_node_number].child_ptr - match_nodes;
            uint32_t shifted_symbol = this_symbol;
            while (this_symbol != match_nodes[suffix_node_number].symbol) {
              suffix_node_number = match_nodes[suffix_node_number].sibling_node_num[shifted_symbol & 0xF];
              shifted_symbol = shifted_symbol >> 4;
            }
            match_node_ptr = &match_nodes[suffix_node_number];
            uint32_t *best_score_match_ptr;
            best_score_match_ptr = best_score_suffix_ptr;

            if (match_nodes[this_symbol].num_symbols != 0) {
              search_node_ptr = &match_nodes[this_symbol];
              if (match_node_ptr->child_ptr == 0) {
                if (match_node_ptr->hit_ptr == 0)
                  match_node_ptr->hit_ptr = search_node_ptr;
              }
              else
                write_all_children_miss_ptr();

              while (best_score_match_ptr <= best_score_last_match_ptr) {
                // follow the tree until end of match string or find child = 0 or sibling = 0
                if (search_node_ptr->child_ptr == 0) // no child, so done with this suffix
                  break;
                this_symbol = *best_score_match_ptr++;
                match_node_ptr = match_node_ptr->child_ptr;
                move_to_existing_match_sibling(this_symbol);
                search_node_ptr = search_node_ptr->child_ptr;
                move_to_search_sibling();
                if (this_symbol != search_node_ptr->symbol) // no matching sibling, so done with this suffix
                  break;
                if (match_node_ptr->child_ptr == 0) {
                  if (match_node_ptr->hit_ptr == 0)
                    match_node_ptr->hit_ptr = search_node_ptr;
                }
                else
                  write_all_children_miss_ptr();
              }
            }
          }
        }
        i1++;
      }

      // scan the data, following prefix tree
      fprintf(stderr,"Overlap search\r");

      uint32_t prior_match_score_number[MAX_PRIOR_MATCHES];
      uint32_t *prior_match_end_ptr[MAX_PRIOR_MATCHES]; 
      uint32_t num_prior_matches = 0;
      in_symbol_ptr = start_symbol_ptr;
      size_t block_size = (end_symbol_ptr - start_symbol_ptr) / 8;
      uint32_t * block_ptr = start_symbol_ptr + block_size;
      stop_symbol_ptr = block_ptr + MAX_STRING_LENGTH;

      if (stop_symbol_ptr > end_symbol_ptr)
        stop_symbol_ptr = end_symbol_ptr;
      overlap_check_data[0].start_symbol_ptr = block_ptr;
      block_ptr += block_size;
      overlap_check_data[0].stop_symbol_ptr = block_ptr + MAX_STRING_LENGTH;
      overlap_check_data[1].start_symbol_ptr = block_ptr;
      block_ptr += block_size;
      overlap_check_data[1].stop_symbol_ptr = block_ptr + MAX_STRING_LENGTH;
      overlap_check_data[2].start_symbol_ptr = block_ptr;
      block_ptr += block_size;
      overlap_check_data[2].stop_symbol_ptr = block_ptr + MAX_STRING_LENGTH;
      overlap_check_data[3].start_symbol_ptr = block_ptr;
      block_ptr += block_size;
      overlap_check_data[3].stop_symbol_ptr = block_ptr + MAX_STRING_LENGTH;
      overlap_check_data[4].start_symbol_ptr = block_ptr;
      block_ptr += block_size;
      overlap_check_data[4].stop_symbol_ptr = block_ptr + MAX_STRING_LENGTH;
      overlap_check_data[5].start_symbol_ptr = block_ptr;
      block_ptr += block_size;
      overlap_check_data[5].stop_symbol_ptr = block_ptr + MAX_STRING_LENGTH;
      overlap_check_data[6].start_symbol_ptr = block_ptr;
      overlap_check_data[6].stop_symbol_ptr = end_symbol_ptr;
      i1 = 5;
      while (overlap_check_data[i1].stop_symbol_ptr > end_symbol_ptr) {
        overlap_check_data[i1].stop_symbol_ptr = end_symbol_ptr;
        if (i1-- == 0)
          break;
      }

      for (i1 = 0 ; i1 < 7 ; i1++)
        pthread_create(&overlap_check_threads[i1],NULL,overlap_check_thread,(char *)&overlap_check_data[i1]);

      uint8_t found_same_score_prior_match;
      uint32_t prior_match_number;

main_overlap_check_loop_no_match:
      if (in_symbol_ptr == stop_symbol_ptr)
        goto main_overlap_check_loop_end;
      this_symbol = *in_symbol_ptr++;
      if ((int)this_symbol < 0)
        goto main_overlap_check_loop_no_match;
      if (match_nodes[this_symbol].num_symbols == 0)
        goto main_overlap_check_loop_no_match;
      match_node_ptr = &match_nodes[this_symbol];

main_overlap_check_loop_match:
      if (in_symbol_ptr == stop_symbol_ptr)
        goto main_overlap_check_loop_end;
      this_symbol = *in_symbol_ptr++;
      if ((int)this_symbol < 0)
        goto main_overlap_check_loop_no_match;

      match_node_ptr = match_node_ptr->child_ptr;
      if (this_symbol != match_node_ptr->symbol) {
        uint32_t shifted_symbol = this_symbol;
        do {
          if (match_node_ptr->sibling_node_num[shifted_symbol & 0xF] != 0) {
            match_node_ptr = &match_nodes[match_node_ptr->sibling_node_num[shifted_symbol & 0xF]];
            shifted_symbol = shifted_symbol >> 4;
          }
          else {
            if (match_node_ptr->miss_ptr == 0) {
              if (match_nodes[this_symbol].num_symbols == 0)
                goto main_overlap_check_loop_no_match;
              match_node_ptr = &match_nodes[this_symbol];
              goto main_overlap_check_loop_match;
            }
            else {
              match_node_ptr = match_node_ptr->miss_ptr;
              shifted_symbol = this_symbol;
            }
          }
        } while (this_symbol != match_node_ptr->symbol);
      }
      if (match_node_ptr->child_ptr)
        goto main_overlap_check_loop_match;

      // no child, so found a match - check for overlaps
      found_same_score_prior_match = 0;
      prior_match_number = 0;
      node_score_number = match_node_ptr->score_number;
      while (prior_match_number < num_prior_matches) {
        if (in_symbol_ptr - candidates[candidates_index[node_score_number]].num_symbols
            > prior_match_end_ptr[prior_match_number]) {
          num_prior_matches--;
          for (i2 = prior_match_number ; i2 < num_prior_matches ; i2++) {
            prior_match_end_ptr[i2] = prior_match_end_ptr[i2+1];
            prior_match_score_number[i2] = prior_match_score_number[i2+1];
          }
        }
        else { // overlapping symbol substitution strings, so invalidate the lower score
          if (prior_match_score_number[prior_match_number] > node_score_number)
            candidate_bad[prior_match_score_number[prior_match_number]] = 1;
          else if (prior_match_score_number[prior_match_number] != node_score_number)
            candidate_bad[node_score_number] = 1;
          else
            found_same_score_prior_match = 1;
          prior_match_number++;
        }
      }
      match_node_ptr = match_node_ptr->hit_ptr;
      if (found_same_score_prior_match == 0) {
        prior_match_end_ptr[num_prior_matches] = in_symbol_ptr - 1;
        prior_match_score_number[num_prior_matches++] = node_score_number;
      }
      if (match_node_ptr == 0)
        goto main_overlap_check_loop_no_match;
      else
        goto main_overlap_check_loop_match;

main_overlap_check_loop_end:
      for (i1 = 0 ; i1 < 7 ; i1++)
        pthread_join(overlap_check_threads[i1],NULL);

      max_string_length = 0;
      i1 = 0;
      while (i1 < num_candidates) {
        if ((candidate_bad[i1] == 0) && (candidates[candidates_index[i1]].num_symbols > max_string_length))
          max_string_length = candidates[candidates_index[i1]].num_symbols;
        i1++;
      }
      match_strings = (uint32_t *)((size_t)free_RAM_ptr + (size_t)num_match_nodes * sizeof(struct match_node));

      if ((char *)start_symbol_ptr + available_RAM < (char *)(match_strings + max_string_length * num_candidates)) {
        uint32_t new_num_candidates = ((uint32_t *)((char *)start_symbol_ptr + available_RAM) - match_strings) / max_string_length;
        for (i1 = new_num_candidates ; i1 < num_candidates ; i1++)
          candidate_bad[i1] = 0;
        num_candidates = new_num_candidates;
      }

      // Redo the tree build and miss values with the final valid score symbols
      match_node_ptr = match_nodes + next_new_symbol_number;
      while (match_node_ptr-- != match_nodes)
        match_node_ptr->num_symbols = 0;

      num_match_nodes = next_new_symbol_number;
      i2 = num_compound_symbols;
      i1 = 0;
      while (i1 < num_candidates) {
        if (candidate_bad[i1] == 0) {
          uint32_t *best_score_match_ptr;
          best_score_match_ptr = init_best_score_ptrs();
          this_symbol = *best_score_match_ptr++;
          best_score_num_symbols = 1;
          match_node_ptr = &match_nodes[this_symbol];
          if (match_node_ptr->num_symbols == 0)
            init_level_1_match_node(this_symbol, i1);
          while (best_score_match_ptr <= best_score_last_match_ptr) {
            this_symbol = *best_score_match_ptr++;
            best_score_num_symbols++;
            move_to_match_child_with_make(this_symbol, i1);
          }
          symbol_count[num_simple_symbols+i2] = 0;
          new_symbol_number[i1] = i2++;
        }
        i1++;
      }

      // span nodes entering the longest (first) suffix match for each node
      i1 = 0;
      while (i1 < num_candidates) {
        if (candidate_bad[i1] == 0) {
          uint32_t *best_score_suffix_ptr;
          best_score_suffix_ptr = init_best_score_ptrs();
          suffix_node_number = *best_score_suffix_ptr++;
          // starting at the node of the 2nd symbol in string, match strings with prefix tree until no match found,
          //   for each match node found, if suffix miss symbol is zero, set it to the tree symbol node
          while (best_score_suffix_ptr <= best_score_last_match_ptr) {
            // follow the suffix until the end (or break on no tree matches)
            this_symbol = *best_score_suffix_ptr++;
            suffix_node_number = match_nodes[suffix_node_number].child_ptr - match_nodes;
            uint32_t shifted_symbol = this_symbol;
            while (this_symbol != match_nodes[suffix_node_number].symbol) {
              suffix_node_number = match_nodes[suffix_node_number].sibling_node_num[shifted_symbol & 0xF];
              shifted_symbol = shifted_symbol >> 4;
            }
            match_node_ptr = &match_nodes[suffix_node_number];
            uint32_t *best_score_match_ptr;
            best_score_match_ptr = best_score_suffix_ptr;

            if (match_nodes[this_symbol].num_symbols != 0) {
              search_node_ptr = &match_nodes[this_symbol];
              if (match_node_ptr->child_ptr == 0) {
                if (match_node_ptr->hit_ptr == 0)
                  match_node_ptr->hit_ptr = search_node_ptr;
              }
              else
                write_all_children_miss_ptr();

              while (best_score_match_ptr <= best_score_last_match_ptr) {
                // follow the tree until end of match string or find child = 0 or sibling = 0
                if (search_node_ptr->child_ptr == 0) // no child, so done with this suffix
                  break;
                this_symbol = *best_score_match_ptr++;
                match_node_ptr = match_node_ptr->child_ptr;
                move_to_existing_match_sibling(this_symbol);
                search_node_ptr = search_node_ptr->child_ptr;
                move_to_search_sibling();
                if (this_symbol != search_node_ptr->symbol) // no matching sibling, so done with this suffix
                  break;
                if (match_node_ptr->child_ptr == 0) {
                  if (match_node_ptr->hit_ptr == 0)
                    match_node_ptr->hit_ptr = search_node_ptr;
                }
                else
                  write_all_children_miss_ptr();
              }
            }
          }
          // save the match strings so they can be added to the end of the data after symbol substitution is done
          match_string_start_ptr = &match_strings[(uint32_t)i1 * max_string_length];
          node_string_start_ptr = start_symbol_ptr + candidates[candidates_index[i1]].last_match_index1
              - candidates[candidates_index[i1]].num_symbols + 1;
          for (i2 = 0 ; i2 < candidates[candidates_index[i1]].num_symbols ; i2++)
            *(match_string_start_ptr + i2) = *(node_string_start_ptr + i2);
        }
        i1++;
      }

      fprintf(stderr,"Replacing data with new dictionary symbols\r");
      // scan the data following the prefix tree and substitute new symbols on end matches (child is 0)
      if (end_symbol_ptr - start_symbol_ptr >= 1000000) {
        stop_symbol_ptr = start_symbol_ptr + ((end_symbol_ptr - start_symbol_ptr) >> 3);
        find_substitutions_data[0].start_symbol_ptr = stop_symbol_ptr;
        block_size = (end_symbol_ptr - start_symbol_ptr) / 7;
        block_ptr = stop_symbol_ptr + block_size;
        find_substitutions_data[0].stop_symbol_ptr = block_ptr;
        find_substitutions_data[1].start_symbol_ptr = block_ptr;
        block_ptr += block_size;
        find_substitutions_data[1].stop_symbol_ptr = block_ptr;
        find_substitutions_data[2].start_symbol_ptr = block_ptr;
        block_ptr += block_size;
        find_substitutions_data[2].stop_symbol_ptr = block_ptr;
        find_substitutions_data[3].start_symbol_ptr = block_ptr;
        block_ptr += block_size;
        find_substitutions_data[3].stop_symbol_ptr = block_ptr;
        find_substitutions_data[4].start_symbol_ptr = block_ptr;
        block_ptr += block_size;
        find_substitutions_data[4].stop_symbol_ptr = block_ptr;
        find_substitutions_data[5].start_symbol_ptr = block_ptr;
        find_substitutions_data[5].stop_symbol_ptr = end_symbol_ptr;
        for (i1 = 0 ; i1 < 6 ; i1++) {
          atomic_store_explicit(&find_substitutions_data[i1].done, 0, memory_order_relaxed);
          atomic_store_explicit(&find_substitutions_data[i1].read_index, 0, memory_order_release);
          pthread_create(&find_substitutions_threads[i1], NULL, find_substitutions_thread, (char *)&find_substitutions_data[i1]);
        }
      }
      else
        stop_symbol_ptr = end_symbol_ptr;

      uint32_t extra_match_symbols = 0;
      uint16_t substitute_index = 0;
      num_symbols_to_copy = 0;
      in_symbol_ptr = start_symbol_ptr;
      out_symbol_ptr = start_symbol_ptr;

      atomic_store_explicit(&substitute_data_write_index, substitute_index, memory_order_release);
      pthread_create(&substitute_thread1, NULL, substitute_thread,NULL);
      while (atomic_load_explicit(&substitute_data_read_index, memory_order_acquire) != 0) /* wait */ ;
      this_symbol = *in_symbol_ptr++;

main_symbol_substitution_loop_no_match_with_symbol:
      match_node_ptr = &match_nodes[this_symbol];
      if (match_node_ptr->num_symbols) {
        this_symbol = *in_symbol_ptr++;
        if ((int)this_symbol >= 0) {
          if (match_node_ptr->child_ptr == 0) {
            if (num_symbols_to_copy >= 100000) {
              if ((substitute_index & 0x7FFF) == 0) {
                while ((uint16_t)(substitute_index - atomic_load_explicit(&substitute_data_read_index, memory_order_acquire))
                    >= 0x8000) /* wait */ ;
                substitute_data[substitute_index++] = num_symbols_to_copy;
                atomic_store_explicit(&substitute_data_write_index, substitute_index, memory_order_release);
              }
              else
                substitute_data[substitute_index++] = num_symbols_to_copy;
              num_symbols_to_copy = 0;
            }
            if (in_symbol_ptr == stop_symbol_ptr)
              goto main_symbol_substitution_loop_end;
            this_symbol = *in_symbol_ptr++;
            if ((int)this_symbol >= 0)
              goto main_symbol_substitution_loop_no_match_with_symbol;
            num_symbols_to_copy++;
            if (in_symbol_ptr == stop_symbol_ptr)
              goto main_symbol_substitution_loop_end;
            this_symbol = *in_symbol_ptr++;
            goto main_symbol_substitution_loop_no_match_with_symbol;
          }

main_symbol_substitution_loop_match_with_child:
          match_node_ptr = match_node_ptr->child_ptr;
          if (this_symbol != match_node_ptr->symbol) {
            uint32_t sibling_nibble = this_symbol;
            do {
              if (match_node_ptr->sibling_node_num[sibling_nibble & 0xF]) {
                match_node_ptr = &match_nodes[match_node_ptr->sibling_node_num[sibling_nibble & 0xF]];
                sibling_nibble = sibling_nibble >> 4;
              }
              else { // no match, so use miss node and output missed symbols
                if (match_node_ptr->miss_ptr == 0) {
                  if (match_nodes[this_symbol].num_symbols) {
                    if (in_symbol_ptr > stop_symbol_ptr) {
                      num_symbols_to_copy += match_node_ptr->num_symbols - (in_symbol_ptr - stop_symbol_ptr);
                      goto main_symbol_substitution_loop_end;
                    }
                    sibling_nibble = sibling_nibble >> 4;
                    num_symbols_to_copy += match_node_ptr->num_symbols - 1;
                    match_node_ptr = &match_nodes[this_symbol];
                  }
                  else {
                    if (in_symbol_ptr >= stop_symbol_ptr) {
                      num_symbols_to_copy += match_node_ptr->num_symbols - (in_symbol_ptr - stop_symbol_ptr);
                      goto main_symbol_substitution_loop_end;
                    }
                    num_symbols_to_copy += match_node_ptr->num_symbols;
                    if (num_symbols_to_copy >= 100000) {
                      if ((substitute_index & 0x7FFF) == 0) {
                        while ((uint16_t)(substitute_index - atomic_load_explicit(&substitute_data_read_index,
                            memory_order_acquire)) >= 0x8000) /* wait */ ;
                        substitute_data[substitute_index++] = num_symbols_to_copy;
                        atomic_store_explicit(&substitute_data_write_index, substitute_index, memory_order_release);
                      }
                      else
                        substitute_data[substitute_index++] = num_symbols_to_copy;
                      num_symbols_to_copy = 0;
                    }
                    if (in_symbol_ptr == stop_symbol_ptr)
                      goto main_symbol_substitution_loop_end;
                    this_symbol = *in_symbol_ptr++;
                    if ((int)this_symbol >= 0)
                      goto main_symbol_substitution_loop_no_match_with_symbol;
                    num_symbols_to_copy++;
                    if (in_symbol_ptr == stop_symbol_ptr)
                      goto main_symbol_substitution_loop_end;
                    this_symbol = *in_symbol_ptr++;
                    goto main_symbol_substitution_loop_no_match_with_symbol;
                  }
                }
                else {
                  num_symbols_to_copy += match_node_ptr->num_symbols - match_node_ptr->miss_ptr->num_symbols;
                  if (in_symbol_ptr - match_node_ptr->miss_ptr->num_symbols >= stop_symbol_ptr) {
                    num_symbols_to_copy -= in_symbol_ptr - stop_symbol_ptr - match_node_ptr->miss_ptr->num_symbols;
                    goto main_symbol_substitution_loop_end;
                  }
                  match_node_ptr = match_node_ptr->miss_ptr;
                  sibling_nibble = this_symbol;
                }
              }
            } while (this_symbol != match_node_ptr->symbol);
          }
          if (match_node_ptr->child_ptr == 0) { // no child, so found a match
            if (num_symbols_to_copy) {
              if ((substitute_index & 0x7FFF) == 0)
                while ((uint16_t)(substitute_index - atomic_load_explicit(&substitute_data_read_index, memory_order_acquire))
                    >= 0x8000) /* wait */ ;
              substitute_data[substitute_index++] = num_symbols_to_copy;
              num_symbols_to_copy = 0;
            }
            node_score_number = match_node_ptr->score_number;
            if (((substitute_index + 1) & 0x7FFE) == 0) {
              while ((uint16_t)(substitute_index - atomic_load_explicit(&substitute_data_read_index, memory_order_acquire))
                  >= 0x7FFF) /* wait */ ;
              substitute_data[substitute_index++] = 0x80000000 + match_node_ptr->num_symbols;
              substitute_data[substitute_index++] = num_simple_symbols + new_symbol_number[node_score_number];
              atomic_store_explicit(&substitute_data_write_index, substitute_index, memory_order_release);
            }
            else {
              substitute_data[substitute_index++] = 0x80000000 + match_node_ptr->num_symbols;
              substitute_data[substitute_index++] = num_simple_symbols + new_symbol_number[node_score_number];
              atomic_store_explicit(&substitute_data_write_index, substitute_index, memory_order_release);
            }
            if (in_symbol_ptr >= stop_symbol_ptr) {
              extra_match_symbols = in_symbol_ptr - stop_symbol_ptr;
              goto main_symbol_substitution_loop_end;
            }
            this_symbol = *in_symbol_ptr++;
            if ((int)this_symbol >= 0)
              goto main_symbol_substitution_loop_no_match_with_symbol;
            num_symbols_to_copy++;
            if (in_symbol_ptr == stop_symbol_ptr)
              goto main_symbol_substitution_loop_end;
            this_symbol = *in_symbol_ptr++;
            goto main_symbol_substitution_loop_no_match_with_symbol;
          }
          if (num_symbols_to_copy >= 100000) {
            if ((substitute_index & 0x7FFF) == 0) {
              while ((uint16_t)(substitute_index - atomic_load_explicit(&substitute_data_read_index, memory_order_acquire))
                  >= 0x8000) /* wait */ ;
              substitute_data[substitute_index++] = num_symbols_to_copy;
              atomic_store_explicit(&substitute_data_write_index, substitute_index, memory_order_release);
            }
            else
              substitute_data[substitute_index++] = num_symbols_to_copy;
            num_symbols_to_copy = 0;
          }
          this_symbol = *in_symbol_ptr++;
          if ((int)this_symbol >= 0)
            goto main_symbol_substitution_loop_match_with_child;
          num_symbols_to_copy += match_node_ptr->num_symbols + 1;
          if (in_symbol_ptr >= stop_symbol_ptr) {
            num_symbols_to_copy -= in_symbol_ptr - stop_symbol_ptr;
            goto main_symbol_substitution_loop_end;
          }
          this_symbol = *in_symbol_ptr++;
          goto main_symbol_substitution_loop_no_match_with_symbol;
        }
        else { // define symbol
          num_symbols_to_copy += match_node_ptr->num_symbols + 1;
          if (in_symbol_ptr >= stop_symbol_ptr) {
            num_symbols_to_copy -= in_symbol_ptr - stop_symbol_ptr;
            goto main_symbol_substitution_loop_end;
          }
          this_symbol = *in_symbol_ptr++;
          goto main_symbol_substitution_loop_no_match_with_symbol;
        }
      }
      if (++num_symbols_to_copy >= 100000) {
        if ((substitute_index & 0x7FFF) == 0) {
          while ((uint16_t)(substitute_index - atomic_load_explicit(&substitute_data_read_index, memory_order_acquire))
              >= 0x8000) /* wait */ ;
          substitute_data[substitute_index++] = num_symbols_to_copy;
          atomic_store_explicit(&substitute_data_write_index, substitute_index, memory_order_release);
        }
        else
          substitute_data[substitute_index++] = num_symbols_to_copy;
        num_symbols_to_copy = 0;
      }
      if (in_symbol_ptr == stop_symbol_ptr)
        goto main_symbol_substitution_loop_end;
      this_symbol = *in_symbol_ptr++;
      if ((int)this_symbol >= 0)
        goto main_symbol_substitution_loop_no_match_with_symbol;
      num_symbols_to_copy++;
      if (in_symbol_ptr == stop_symbol_ptr)
        goto main_symbol_substitution_loop_end;
      this_symbol = *in_symbol_ptr++;
      goto main_symbol_substitution_loop_no_match_with_symbol;

main_symbol_substitution_loop_end:
      if (num_symbols_to_copy) {
        if ((substitute_index & 0x7FFF) == 0)
          while ((uint16_t)(substitute_index - atomic_load_explicit(&substitute_data_read_index, memory_order_acquire))
              >= 0x8000) /* wait */ ;
        substitute_data[substitute_index++] = num_symbols_to_copy;
      }
      atomic_store_explicit(&substitute_data_write_index, substitute_index, memory_order_release);

      if (end_symbol_ptr - start_symbol_ptr >= 1000000) {
        for (i1 = 0 ; i1 < 6 ; i1++) {
          uint32_t local_substitutions_write_index;
          uint32_t substitutions_index = 0;

          if (extra_match_symbols != 0) {
            while ((local_substitutions_write_index = atomic_load_explicit(&find_substitutions_data[i1].write_index,
                memory_order_acquire)) == 0) /* wait */ ;
            if (((int)find_substitutions_data[i1].data[0] >= (int)extra_match_symbols)) {
              if (find_substitutions_data[i1].data[0] > extra_match_symbols)
                find_substitutions_data[i1].data[0] -= extra_match_symbols;
              else
                substitutions_index = 1;
            }
            else {
              while (atomic_load_explicit(&find_substitutions_data[i1].done, memory_order_acquire) == 0) {
                substitutions_index = atomic_load_explicit(&find_substitutions_data[i1].write_index, memory_order_relaxed);
                atomic_store_explicit(&find_substitutions_data[i1].read_index, substitutions_index, memory_order_relaxed);
              }
              pthread_join(find_substitutions_threads[i1],NULL);
              substitutions_index = 0;
              local_substitutions_write_index = 0;
              find_substitutions_data[i1].start_symbol_ptr += extra_match_symbols;
              atomic_store_explicit(&find_substitutions_data[i1].done, 0, memory_order_relaxed);
              atomic_store_explicit(&find_substitutions_data[i1].write_index, 0, memory_order_release);
              pthread_create(&find_substitutions_threads[i1],NULL,find_substitutions_thread,
                  (char *)&find_substitutions_data[i1]);
            }
            extra_match_symbols = 0;
          }

          while ((atomic_load_explicit(&find_substitutions_data[i1].done, memory_order_acquire) == 0)
              || (substitutions_index != atomic_load_explicit(&find_substitutions_data[i1].write_index, memory_order_acquire))) {
            local_substitutions_write_index
                = atomic_load_explicit(&find_substitutions_data[i1].write_index, memory_order_acquire);
            if (substitutions_index != local_substitutions_write_index) {
              if (((local_substitutions_write_index - substitutions_index) & 0x3FFFFF) >= 0x40) {
                do {
                  if (((substitute_index + 0x3F) & 0x7FC0) == 0)
                    while ((uint16_t)(substitute_index - atomic_load_explicit(&substitute_data_read_index, memory_order_acquire))
                        >= 0x7FC1) /* wait */ ;
                  uint32_t end_substitutions_index = (substitutions_index + 0x40) & 0x3FFFFF;
                  do {
                    substitute_data[substitute_index++] = find_substitutions_data[i1].data[substitutions_index];
                    substitutions_index = (substitutions_index + 1) & 0x3FFFFF;
                  } while (substitutions_index != end_substitutions_index);
                  atomic_store_explicit(&substitute_data_write_index, substitute_index, memory_order_release);
                } while (((local_substitutions_write_index - substitutions_index) & 0x3FFFFF) >= 0x40);
              }
              while (substitutions_index != local_substitutions_write_index) {
                if ((substitute_index & 0x7FFF) == 0) {
                  atomic_store_explicit(&substitute_data_write_index, substitute_index, memory_order_release);
                  while ((uint16_t)(substitute_index - atomic_load_explicit(&substitute_data_read_index, memory_order_acquire))
                      >= 0x8000) /* wait */ ;
                }
                substitute_data[substitute_index++] = find_substitutions_data[i1].data[substitutions_index];
                substitutions_index = (substitutions_index + 1) & 0x3FFFFF;
                atomic_store_explicit(&substitute_data_write_index, substitute_index, memory_order_release);
              }
            }
            atomic_store_explicit(&find_substitutions_data[i1].read_index, substitutions_index, memory_order_release);
          }
          pthread_join(find_substitutions_threads[i1], NULL);
          extra_match_symbols += find_substitutions_data[i1].extra_match_symbols;
        }
      }
      if ((substitute_index & 0x7FFF) == 0)
        while (substitute_index != (uint16_t)atomic_load_explicit(&substitute_data_read_index, memory_order_acquire)) /* wait */ ;
      substitute_data[substitute_index++] = 0xFFFFFFFF;
      atomic_store_explicit(&substitute_data_write_index, substitute_index, memory_order_release);
      while (substitute_index != (uint16_t)atomic_load_explicit(&substitute_data_read_index, memory_order_acquire)) /* wait */ ;
      pthread_join(substitute_thread1, NULL);

      // Add the new symbol definitions to the end of the data
      i1 = 0;
      while (i1 < num_candidates) {
        if (candidate_bad[i1] == 0) {
          uint32_t *match_string_ptr, *match_string_end_ptr;
          *out_symbol_ptr++ = first_symbol_number + num_compound_symbols++;
          match_string_ptr = match_strings + max_string_length * (uint32_t)i1;
          match_string_end_ptr = match_string_ptr + candidates[candidates_index[i1++]].num_symbols;
          while (match_string_ptr != match_string_end_ptr) {
            symbol_count[*match_string_ptr] -= symbol_count[num_simple_symbols+num_compound_symbols-1] - 1;
            *out_symbol_ptr++ = *match_string_ptr++;
          }
        }
        else
          candidate_bad[i1++] = 0;
      }
      end_symbol_ptr = out_symbol_ptr;
      *end_symbol_ptr = 0xFFFFFFFE;
      free_RAM_ptr = (uint8_t *)(end_symbol_ptr + 1);
    }

    if (num_candidates) {
      if (scan_cycle > 1) {
        if (num_candidates == max_scores) {
          if (min_score < prior_min_score) {
            if (scan_cycle > 50) {
              if (scan_cycle > 100)
                new_min_score = 0.993 * min_score * (min_score / prior_min_score) - 0.001;
              else
                new_min_score = 0.99 * min_score * (min_score / prior_min_score) - 0.001;
            }
            else
              new_min_score = 0.98 * min_score * (min_score / prior_min_score) - 0.001;
          }
          else
            new_min_score = 0.47 * (prior_min_score + min_score) - 0.001;
        }
        else {
          if (min_score < prior_min_score)
            new_min_score = 0.95 * min_score * (min_score / prior_min_score) - 0.001;
          else
            new_min_score = 0.45 * (prior_min_score + min_score) - 0.001;
        }
      }
      else {
        new_min_score = (double)(0.75 * min_score) - 0.001;
        prior_min_score = min_score;
      }
    }
    else if (min_score > 0.000000001) {
      new_min_score = 0.000000001;
      num_candidates = 1;
    }
    if (min_score < prior_min_score)
      prior_min_score = min_score;
    if (new_min_score < prior_min_score)
      min_score = new_min_score;
    else
      min_score = 0.98 * prior_min_score;
    if (min_score < 0.000000001)
      min_score = 0.000000001;

    max_scores
        = (max_scores + 2 * (((29 * (num_simple_symbols + num_compound_symbols - next_new_symbol_number)) >> 5) + 5000)) / 3;
    if (max_scores > MAX_SCORES)
      max_scores = MAX_SCORES;
    if ((scan_cycle == 1) && cap_encoded && create_words)
      max_scores = 5000;
  } while ((num_candidates) && (num_simple_symbols + num_compound_symbols + MAX_SCORES < MAX_SYMBOLS_DEFINED));

write_file:
  if ((fd_out = fopen(argv[arg_num],"wb+")) == NULL) {
    fprintf(stderr,"ERROR - unable to open output file '%s'\n",argv[arg_num]);
    exit(EXIT_FAILURE);
  }
  if (in_size) {
    in_char_ptr = char_buffer;
    *in_char_ptr++ = format;
    in_symbol_ptr = start_symbol_ptr;
    if (UTF8_compliant) {
      while (in_symbol_ptr != end_symbol_ptr) {
        uint32_t symbol_value;
        symbol_value = *in_symbol_ptr++;
        if (symbol_value < 0x80)
          *in_char_ptr++ = (uint8_t)symbol_value;
        else if (symbol_value < 0x800) {
          *in_char_ptr++ = 0xC0 + (symbol_value >> 6);
          *in_char_ptr++ = 0x80 + (symbol_value & 0x3F);
        }
        else if (symbol_value < 0x10000) {
          *in_char_ptr++ = 0xE0 + (symbol_value >> 12);
          *in_char_ptr++ = 0x80 + ((symbol_value >> 6) & 0x3F);
          *in_char_ptr++ = 0x80 + (symbol_value & 0x3F);
        }
        else if (symbol_value < START_MY_SYMBOLS) {
          *in_char_ptr++ = 0xF0 + (symbol_value >> 18);
          *in_char_ptr++ = 0x80 + ((symbol_value >> 12) & 0x3F);
          *in_char_ptr++ = 0x80 + ((symbol_value >> 6) & 0x3F);
          *in_char_ptr++ = 0x80 + (symbol_value & 0x3F);
        }
        else if ((int)symbol_value >= 0) {
          symbol_value -= START_MY_SYMBOLS;
          *in_char_ptr++ = INSERT_SYMBOL_CHAR;
          *in_char_ptr++ = (uint8_t)((symbol_value >> 16) & 0xFF);
          *in_char_ptr++ = (uint8_t)((symbol_value >> 8) & 0xFF);
          *in_char_ptr++ = (uint8_t)(symbol_value & 0xFF);
        }
        else {
          symbol_value -= 0x80000000 + START_MY_SYMBOLS;
          *in_char_ptr++ = DEFINE_SYMBOL_CHAR;
          *in_char_ptr++ = (uint8_t)((symbol_value >> 16) & 0xFF);
          *in_char_ptr++ = (uint8_t)((symbol_value >> 8) & 0xFF);
          *in_char_ptr++ = (uint8_t)(symbol_value & 0xFF);
        }
      }
    }
    else {
      while (in_symbol_ptr != end_symbol_ptr) {
        uint32_t symbol_value;
        symbol_value = *in_symbol_ptr++;
        if (symbol_value < INSERT_SYMBOL_CHAR)
          *in_char_ptr++ = (uint8_t)symbol_value;
        else if (symbol_value == INSERT_SYMBOL_CHAR) {
          *in_char_ptr++ = INSERT_SYMBOL_CHAR;
          *in_char_ptr++ = DEFINE_SYMBOL_CHAR;
        }
        else if (symbol_value == DEFINE_SYMBOL_CHAR) {
          *in_char_ptr++ = DEFINE_SYMBOL_CHAR;
          *in_char_ptr++ = DEFINE_SYMBOL_CHAR;
        }
        else if ((int)symbol_value >= 0) {
          symbol_value -= 0x100;
          *in_char_ptr++ = INSERT_SYMBOL_CHAR;
          *in_char_ptr++ = (uint8_t)((symbol_value >> 16) & 0xFF);
          *in_char_ptr++ = (uint8_t)((symbol_value >> 8) & 0xFF);
          *in_char_ptr++ = (uint8_t)(symbol_value & 0xFF);
        }
        else {
          symbol_value -= 0x80000000 + 0x100;
          *in_char_ptr++ = DEFINE_SYMBOL_CHAR;
          *in_char_ptr++ = (uint8_t)((symbol_value >> 16) & 0xFF);
          *in_char_ptr++ = (uint8_t)((symbol_value >> 8) & 0xFF);
          *in_char_ptr++ = (uint8_t)(symbol_value & 0xFF);
        }
      }
    }
    in_size = in_char_ptr - char_buffer;
    write_ptr = char_buffer;
    while (write_ptr + MAX_WRITE_SIZE < char_buffer + in_size) {
      fwrite(write_ptr,1,MAX_WRITE_SIZE,fd_out);
      write_ptr += MAX_WRITE_SIZE;
      fflush(fd_out);
    }
    fwrite(write_ptr,1,char_buffer+in_size-write_ptr,fd_out);
  }
  fclose(fd_out);
  free(start_symbol_ptr);
  fprintf(stderr,"%u grammar productions created in %0.3f seconds.\n",
      num_compound_symbols,(float)(clock()-start_time)/CLOCKS_PER_SEC);
  return(0);
}