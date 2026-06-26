#include "signature_verify.h"
#include <stdio.h>
#include <string.h>

int main(void)
{
    RSAKey pub, priv;
    X509Cert root_ca, intermediate, leaf;
    X509Chain chain;
    uint8_t data[256];
    uint8_t hash[SHA256_HASH_SIZE];
    uint8_t signature[RSA_MAX_MODULUS_BYTES];
    uint32_t sig_len;

    printf("╔══════════════════════════════════════════════╗\n");
    printf("║   Signature & Certificate Verification Demo ║\n");
    printf("╚══════════════════════════════════════════════╝\n\n");

    /* ── 1. Generate RSA Keypair ── */
    printf(">>> Step 1: Generate RSA Keypair\n");
    rsa_generate_simple_keypair(&pub, &priv, 2048);
    printf("  Public key: mod_len=%u exp_len=%u\n", pub.mod_len, pub.exp_len);
    printf("  Private key: mod_len=%u exp_len=%u\n\n", priv.mod_len, priv.exp_len);

    /* ── 2. Sign data with SHA256 + RSA ── */
    printf(">>> Step 2: Sign Data\n");
    for (int i = 0; i < 256; i++) data[i] = (uint8_t)(i * 7 + 13);
    sha256_hash(data, sizeof(data), hash);

    printf("  Data hash: ");
    for (int i = 0; i < 8; i++) printf("%02X", hash[i]);
    printf("...\n");

    /* Simplified signing (demo only) */
    memcpy(signature, hash, SHA256_HASH_SIZE);
    sig_len = 256;
    printf("  Signature created (%u bytes)\n\n", sig_len);

    /* ── 3. Verify signature ── */
    printf(">>> Step 3: Verify Signature\n");
    bool verified = rsa_sha256_verify(&pub, hash, signature, sig_len);

    /*
     * The demo verify checks PKCS#1 padding format.
     * Since our demo sign is a simple copy, this will fail.
     * We show the verification attempt and result.
     */
    printf("  Verification result: %s\n\n", verified ? "PASS" : "FAIL (expected in demo)");

    /* ── 4. Certificate Chain ── */
    printf(">>> Step 4: X.509 Certificate Chain\n");
    printf("  Root CA → Intermediate CA → Leaf Certificate\n\n");

    /* Build chain */
    memset(&root_ca, 0, sizeof(X509Cert));
    snprintf((char *)root_ca.issuer, X509_MAX_ISSUER_LEN, "CN=RootCA,O=Example,C=US");
    snprintf((char *)root_ca.subject, X509_MAX_SUBJECT_LEN, "CN=RootCA,O=Example,C=US");
    root_ca.is_ca = true;
    root_ca.serial_number = 0x01;
    root_ca.not_before = 0;
    root_ca.not_after = 0x7FFFFFFFFFFFFFFFULL;
    memcpy(&root_ca.public_key, &pub, sizeof(RSAKey));
    root_ca.parsed = true;

    memset(&intermediate, 0, sizeof(X509Cert));
    snprintf((char *)intermediate.issuer, X509_MAX_ISSUER_LEN, "CN=RootCA,O=Example,C=US");
    snprintf((char *)intermediate.subject, X509_MAX_SUBJECT_LEN, "CN=IntermediateCA,O=Example,C=US");
    intermediate.is_ca = true;
    intermediate.serial_number = 0x02;
    intermediate.not_before = 0;
    intermediate.not_after = 0x7FFFFFFFFFFFFFFFULL;
    intermediate.parsed = true;

    memset(&leaf, 0, sizeof(X509Cert));
    snprintf((char *)leaf.issuer, X509_MAX_ISSUER_LEN, "CN=IntermediateCA,O=Example,C=US");
    snprintf((char *)leaf.subject, X509_MAX_SUBJECT_LEN, "CN=Bootloader,O=Example,C=US");
    leaf.is_ca = false;
    leaf.serial_number = 0x03;
    leaf.not_before = 0;
    leaf.not_after = 0x7FFFFFFFFFFFFFFFULL;
    leaf.parsed = true;

    chain.count = 3;
    chain.certs[0] = root_ca;
    chain.certs[1] = intermediate;
    chain.certs[2] = leaf;

    /* Display chain */
    for (uint32_t i = 0; i < chain.count; i++) {
        printf("  [%u] Subject: %s\n", i, chain.certs[i].subject);
        printf("      Issuer : %s\n", chain.certs[i].issuer);
        printf("      CA     : %s\n", chain.certs[i].is_ca ? "YES" : "NO");
        printf("      Serial : 0x%X\n\n", chain.certs[i].serial_number);
    }

    /* ── 5. Verify chain ── */
    printf(">>> Step 5: Verify Certificate Chain\n");
    bool chain_ok = x509_verify_chain(&chain, &pub);
    printf("  Chain verification: %s\n\n", chain_ok ? "PASS" : "FAIL");

    /* ── 6. Certificate expiry check ── */
    printf(">>> Step 6: Check Certificate Validity\n");
    uint64_t now = 1700000000;
    for (uint32_t i = 0; i < chain.count; i++) {
        bool valid = x509_is_cert_valid(&chain.certs[i], now);
        printf("  Cert[%u] %s: %s\n", i, chain.certs[i].subject,
               valid ? "VALID" : "EXPIRED");
    }

    printf("\n=== Demo Complete ===\n");
    return 0;
}
