#include "merkle_pcr.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static void node_hash(const TPMHash *left, const TPMHash *right, TPMHash *out) {
    uint8_t concat[TPM_SHA256_DIGEST_SIZE * 2];
    memcpy(concat, left->digest, TPM_SHA256_DIGEST_SIZE);
    memcpy(concat + TPM_SHA256_DIGEST_SIZE, right->digest, TPM_SHA256_DIGEST_SIZE);
    uint32_t seed = 0x6A09E667;
    size_t i;
    for (i = 0; i < TPM_SHA256_DIGEST_SIZE * 2; i++) {
        seed ^= (uint32_t)concat[i] << ((i % 4) * 8);
        seed = (seed * 0x01000193) ^ (seed >> 16);
    }
    seed ^= (uint32_t)(TPM_SHA256_DIGEST_SIZE * 2);
    for (i = 0; i < 32; i++) {
        out->digest[i] = (uint8_t)((seed >> ((i % 4) * 8)) & 0xFF);
        seed = (seed * 0x5BD1E995) ^ ((seed >> 13) + i);
    }
}

static uint8_t next_power_of_two(uint8_t n) {
    uint8_t p = 1;
    while (p < n) p <<= 1;
    return p;
}

static uint8_t log2_ceil(uint8_t n) {
    uint8_t d = 0, v = n;
    while (v > 1) { v >>= 1; d++; }
    if ((uint8_t)(1 << d) < n) d++;
    return d;
}

void merkle_tree_init(MerkleTree *tree) {
    if (!tree) return;
    memset(tree, 0, sizeof(*tree));
}

int32_t merkle_tree_build(MerkleTree *tree,
                           const TPMHash *leaves, uint8_t leaf_count) {
    if (!tree || !leaves) return -1;
    if (leaf_count == 0 || leaf_count > MERKLE_MAX_LEAVES) return -2;
    merkle_tree_init(tree);
    uint8_t padded_count = next_power_of_two(leaf_count);
    tree->depth = log2_ceil(padded_count);
    tree->leaf_count = leaf_count;
    tree->node_count = 2 * padded_count - 1;
    TPMHash padded_leaves[MERKLE_MAX_LEAVES];
    uint8_t i;
    for (i = 0; i < leaf_count; i++) {
        memcpy(padded_leaves[i].digest, leaves[i].digest, TPM_SHA256_DIGEST_SIZE);
    }
    TPMHash zero_hash;
    memset(&zero_hash, 0, sizeof(zero_hash));
    for (i = leaf_count; i < padded_count; i++) {
        memcpy(padded_leaves[i].digest, zero_hash.digest, TPM_SHA256_DIGEST_SIZE);
    }
    uint8_t total_nodes = 2 * padded_count - 1;
    for (i = 0; i < padded_count; i++) {
        memcpy(tree->nodes[total_nodes - 1 - i].digest,
               padded_leaves[i].digest, TPM_SHA256_DIGEST_SIZE);
    }
    int16_t j;
    for (j = (int16_t)(total_nodes - 1 - padded_count); j >= 0; j--) {
        uint8_t li = (uint8_t)(2 * j + 1);
        uint8_t ri = (uint8_t)(2 * j + 2);
        if (li < total_nodes && ri < total_nodes) {
            node_hash(&tree->nodes[li], &tree->nodes[ri], &tree->nodes[j]);
        }
    }
    tree->node_count = total_nodes;
    tree->built = true;
    return 0;
}

int32_t merkle_tree_get_root(const MerkleTree *tree, TPMHash *root) {
    if (!tree || !root) return -1;
    if (!tree->built) return -2;
    memcpy(root->digest, tree->nodes[0].digest, TPM_SHA256_DIGEST_SIZE);
    return 0;
}

int32_t merkle_generate_proof(const MerkleTree *tree,
                               uint8_t leaf_index,
                               MerkleProof *proof) {
    if (!tree || !proof) return -1;
    if (!tree->built) return -2;
    if (leaf_index >= tree->leaf_count) return -3;
    memset(proof, 0, sizeof(*proof));
    proof->leaf_index = leaf_index;
    memcpy(proof->leaf_value.digest,
           tree->nodes[tree->node_count - 1 - leaf_index].digest,
           TPM_SHA256_DIGEST_SIZE);
    memcpy(proof->expected_root.digest, tree->nodes[0].digest,
           TPM_SHA256_DIGEST_SIZE);
    uint8_t idx = leaf_index;
    uint8_t pos = (uint8_t)(tree->node_count - 1 - idx);
    uint8_t depth;
    for (depth = 0; depth < tree->depth; depth++) {
        uint8_t parent = (uint8_t)((pos - 1) / 2);
        uint8_t sibling_pos;
        bool is_left;
        if (pos % 2 == 1) {
            sibling_pos = pos + 1;
            is_left = false;
        } else {
            sibling_pos = pos - 1;
            is_left = true;
        }
        if (sibling_pos < tree->node_count) {
            proof->siblings[depth].is_left = is_left;
            memcpy(proof->siblings[depth].hash.digest,
                   tree->nodes[sibling_pos].digest,
                   TPM_SHA256_DIGEST_SIZE);
            proof->sibling_count++;
        }
        pos = parent;
    }
    return 0;
}

int32_t merkle_verify_proof(const MerkleProof *proof, bool *valid) {
    if (!proof || !valid) return -1;
    if (proof->sibling_count == 0) {
        *valid = (memcmp(proof->leaf_value.digest,
                         proof->expected_root.digest,
                         TPM_SHA256_DIGEST_SIZE) == 0);
        return 0;
    }
    TPMHash current;
    memcpy(current.digest, proof->leaf_value.digest, TPM_SHA256_DIGEST_SIZE);
    uint8_t i;
    for (i = 0; i < proof->sibling_count; i++) {
        TPMHash combined;
        if (proof->siblings[i].is_left) {
            node_hash(&proof->siblings[i].hash, &current, &combined);
        } else {
            node_hash(&current, &proof->siblings[i].hash, &combined);
        }
        memcpy(current.digest, combined.digest, TPM_SHA256_DIGEST_SIZE);
    }
    *valid = (memcmp(current.digest, proof->expected_root.digest,
                     TPM_SHA256_DIGEST_SIZE) == 0);
    return 0;
}

int32_t merkle_root_from_pcr_bank(const TPMPcrComposite *pcr_bank,
                                   TPMHash *root) {
    if (!pcr_bank || !root) return -1;
    MerkleTree tree;
    merkle_tree_init(&tree);
    TPMHash leaves[MERKLE_MAX_LEAVES];
    uint8_t leaf_count = 0;
    uint8_t i;
    for (i = 0; i < TPM_MAX_PCRS && leaf_count < MERKLE_MAX_LEAVES; i++) {
        uint8_t byte_idx = i / 8;
        uint8_t bit_idx = i % 8;
        if (pcr_bank->pcr_selections[0].pcr_select[byte_idx] & (1 << bit_idx)) {
            memcpy(leaves[leaf_count].digest,
                   pcr_bank->pcr_digests[i].digest,
                   TPM_SHA256_DIGEST_SIZE);
            leaf_count++;
        }
    }
    if (leaf_count == 0) {
        memset(root->digest, 0, TPM_SHA256_DIGEST_SIZE);
        return 0;
    }
    int32_t ret = merkle_tree_build(&tree, leaves, leaf_count);
    if (ret != 0) return ret;
    return merkle_tree_get_root(&tree, root);
}

int32_t merkle_compare_roots(const TPMHash *root_a,
                              const TPMHash *root_b,
                              bool *match) {
    if (!root_a || !root_b || !match) return -1;
    *match = (memcmp(root_a->digest, root_b->digest, TPM_SHA256_DIGEST_SIZE) == 0);
    return 0;
}

void merkle_tree_dump(const MerkleTree *tree) {
    if (!tree) { printf("MerkleTree: (null)\n"); return; }
    printf("=== Merkle Tree ===\n");
    printf("  Leaves: %u  Padded: %u  Depth: %u  Nodes: %u  Built: %s\n",
           tree->leaf_count, next_power_of_two(tree->leaf_count),
           tree->depth, tree->node_count, tree->built ? "YES" : "NO");
    if (tree->built && tree->node_count > 0) {
        printf("  Root: ");
        uint8_t i;
        for (i = 0; i < 16; i++) printf("%02X", tree->nodes[0].digest[i]);
        printf("...\n");
    }
    printf("===================\n");
}

void merkle_proof_dump(const MerkleProof *proof) {
    if (!proof) { printf("MerkleProof: (null)\n"); return; }
    printf("=== Merkle Proof ===\n");
    printf("  Leaf Index: %u  Siblings: %u\n",
           proof->leaf_index, proof->sibling_count);
    uint8_t i;
    for (i = 0; i < proof->sibling_count; i++) {
        printf("  Sib[%u]: %s ", i, proof->siblings[i].is_left ? "L" : "R");
        uint8_t j;
        for (j = 0; j < 8; j++)
            printf("%02X", proof->siblings[i].hash.digest[j]);
        printf("...\n");
    }
    printf("====================\n");
}
