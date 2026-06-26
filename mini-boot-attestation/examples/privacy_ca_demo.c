#include "tpm_quote.h"
#include "aik_identity.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int main(void) {
    printf("=== Privacy CA Protocol Demo ===\n\n");

    printf("[1] TPM Manufacturer provisions Endorsement Key (EK)\n");
    TPMKeyPublic ek_pub;
    tpm_create_ek(&ek_pub);
    printf("    EK created, modulus_size=%u, hierarchy=%d\n",
           ek_pub.modulus_size, ek_pub.hierarchy);

    printf("\n[2] Manufacturer issues EK Certificate\n");
    EKCertificate ek_cert;
    memset(&ek_cert, 0, sizeof(ek_cert));
    memcpy(ek_cert.modulus, ek_pub.modulus, ek_pub.modulus_size);
    ek_cert.modulus_size = ek_pub.modulus_size;
    memcpy(ek_cert.exponent, ek_pub.exponent, 3);

    int i;
    uint8_t manufacturer_id[] = "MFR-CA-001-TPMVENDOR";
    memcpy(ek_cert.manufacturer_ca_id, manufacturer_id, 24);

    tpm_get_ek_pub_hash(&ek_pub, ek_cert.ek_pub_hash);

    for (i = 0; i < 16; i++) ek_cert.serial_number[i] = (uint8_t)(0x10 + i);
    for (i = 0; i < AIK_CERT_SIZE / 2; i++) ek_cert.signature[i] = (uint8_t)(0xE0 + (i % 16));
    ek_cert.signature_size = AIK_CERT_SIZE / 2;

    printf("    EK Certificate issued by: %.24s\n", ek_cert.manufacturer_ca_id);
    printf("    EK pub hash: ");
    for (i = 0; i < 8; i++) printf("%02X", ek_cert.ek_pub_hash[i]);
    printf("\n");

    printf("\n[3] Verify EK Certificate against EK public key\n");
    bool ek_cert_valid = false;
    tpm_verify_ek_certificate(&ek_cert, &ek_pub, &ek_cert_valid);
    printf("    EK certificate verification: %s\n",
           ek_cert_valid ? "PASS" : "FAIL");

    printf("\n[4] Device creates Storage Root Key (SRK)\n");
    TPMKeyPublic srk;
    memset(&srk, 0, sizeof(srk));
    srk.hierarchy = TPM_KEY_HIERARCHY_SRK;
    for (i = 0; i < 256; i++) srk.modulus[i] = (uint8_t)(i + 0x30);
    srk.modulus_size = 256;
    srk.exponent[0] = 0x01; srk.exponent[1] = 0x00; srk.exponent[2] = 0x01;
    printf("    SRK created\n");

    printf("\n[5] Device creates Attestation Identity Key (AIK)\n");
    TPMKeyAIK aik;
    tpm_create_aik(&aik, &srk, &ek_pub);
    printf("    AIK created, name: ");
    for (i = 0; i < 8; i++) printf("%02X", aik.aik_name[i]);
    printf("\n");

    printf("\n[6] Device sends AIK public + EK certificate to Privacy CA\n");
    TPMKeyPublic aik_pub;
    memset(&aik_pub, 0, sizeof(aik_pub));
    memcpy(aik_pub.modulus, aik.aik_pub_modulus, aik.aik_pub_modulus_size);
    aik_pub.modulus_size = aik.aik_pub_modulus_size;
    memcpy(aik_pub.exponent, aik.aik_pub_exponent, 3);
    aik_pub.hierarchy = TPM_KEY_HIERARCHY_AIK;

    printf("    Sending: AIK pub (modulus_size=%u) + EK cert\n", aik_pub.modulus_size);

    printf("\n[7] Privacy CA validates EK certificate (chain: manufacturer CA)\n");
    bool ek_chain_valid = ek_cert_valid;
    printf("    EK cert chain validation: %s\n", ek_chain_valid ? "PASS" : "FAIL");

    if (!ek_chain_valid) {
        printf("    ERROR: EK certificate chain invalid. Aborting.\n");
        return 1;
    }

    printf("\n[8] Privacy CA issues AIK Credential\n");
    AIKCredential aik_cred;
    uint8_t privacy_ca_id[] = "PRIV-CA-DEMO-001";
    tpm_aik_certify(&aik_cred, &aik_pub, &ek_pub, &ek_cert, privacy_ca_id);
    aik_credential_dump(&aik_cred);

    printf("\n[9] Privacy CA encrypts AIK Credential for EK (MakeCredential)\n");
    AIKEncryptedCredential enc_cred;
    tpm_make_credential(&enc_cred, &aik_cred, &ek_pub);
    printf("    Encrypted credential size: %u bytes\n", enc_cred.size);

    printf("\n[10] Device receives encrypted credential, activates with TPM\n");
    AIKCredential activated_cred;
    tpm_activate_credential(&activated_cred, &enc_cred, &aik);
    printf("    Credential activated. AIK name: ");
    for (i = 0; i < 8; i++) printf("%02X", activated_cred.aik_name[i]);
    printf("\n");

    printf("\n[11] Verify AIK Credential\n");
    bool aik_cred_valid = false;
    uint8_t privacy_ca_pub[AIK_KEY_SIZE];
    for (i = 0; i < AIK_KEY_SIZE; i++) privacy_ca_pub[i] = (uint8_t)(0xCA + (i % 16));

    tpm_verify_aik_credential(&activated_cred, &aik_pub, privacy_ca_pub, &aik_cred_valid);
    printf("    AIK credential verification: %s\n",
           aik_cred_valid ? "PASS" : "FAIL");

    printf("\n[12] AIK now ready for attestation quoting\n");
    printf("    The device can now use AIK to sign TPM Quotes\n");
    printf("    Verifier can check AIK credential chain: MFR CA -> EK -> AIK\n");

    printf("\n=== Privacy CA Protocol Demo Complete ===\n");
    return 0;
}
