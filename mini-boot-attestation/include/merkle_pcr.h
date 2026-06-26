#ifndef MERKLE_PCR_H
#define MERKLE_PCR_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "tpm_quote.h"

/*
 * Merkle Tree over PCR Banks for Verifiable Attestation
 *
 * Theorem (Merkle, 1979): A Merkle tree of height h provides O(log n)
 * inclusion proofs. To verify that PCR_i = v at position i in a PCR bank
 * with root R, one needs only H(log n) sibling hashes.
 *
 * Application: Instead of transmitting all 24 PCR values for attestation,
 * the verifier can request a Merkle inclusion proof for specific PCRs.
 *
 * Structure:
 *   Root = H(H(PCR0||PCR1) || H(PCR2||PCR3) || ...)
 *
 * Combined with Quote: The TPM Quote signs the Merkle root of the PCR bank,
 * enabling selective disclosure of individual PCRs while maintaining
 * cryptographic binding to the full platform state.
 *
 * Reference:
 *   - Merkle, R. (1979) "Secrecy, Authentication, and Public Key Systems"
 *   - TCG TAP v1.0 Section 8.3 "PCR Composite"
 *   - Keylime: PCR policy with masks
 */

#define MERKLE_MAX_LEAVES      32
#define MERKLE_TREE_NODES_MAX  63
#define MERKLE_PROOF_DEPTH_MAX  6

typedef struct {
    TPMHash  hash;
    bool     is_left;
} MerkleSibling;

typedef struct {
    MerkleSibling siblings[MERKLE_PROOF_DEPTH_MAX];
    uint8_t       sibling_count;
    uint8_t       leaf_index;
    TPMHash       leaf_value;
    TPMHash       expected_root;
} MerkleProof;

typedef struct {
    TPMHash  nodes[MERKLE_TREE_NODES_MAX];
    uint8_t  node_count;
    uint8_t  leaf_count;
    uint8_t  depth;
    bool     built;
} MerkleTree;

void     merkle_tree_init(MerkleTree *tree);
int32_t  merkle_tree_build(MerkleTree *tree,
                           const TPMHash *leaves, uint8_t leaf_count);
int32_t  merkle_tree_get_root(const MerkleTree *tree, TPMHash *root);

int32_t  merkle_generate_proof(const MerkleTree *tree,
                                uint8_t leaf_index,
                                MerkleProof *proof);

int32_t  merkle_verify_proof(const MerkleProof *proof, bool *valid);

int32_t  merkle_root_from_pcr_bank(const TPMPcrComposite *pcr_bank,
                                    TPMHash *root);

int32_t  merkle_compare_roots(const TPMHash *root_a,
                               const TPMHash *root_b,
                               bool *match);

void     merkle_tree_dump(const MerkleTree *tree);
void     merkle_proof_dump(const MerkleProof *proof);

#endif