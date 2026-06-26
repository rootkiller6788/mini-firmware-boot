#include "secure_boot.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* L5: SHA-256 per FIPS 180-4 Section 6.2 */
#define ROTR32(x,n) (((x)>>(n))|((x)<<(32-(n))))
#define ROTR64(x,n) (((x)>>(n))|((x)<<(64-(n))))

static const uint32_t sha256_k[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,
    0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,
    0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,
    0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,
    0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,
    0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,
    0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,
    0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,
    0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

void sha256_init(SHA256Context *ctx) {
    if (ctx == NULL) return;
    ctx->state[0]=0x6a09e667; ctx->state[1]=0xbb67ae85;
    ctx->state[2]=0x3c6ef372; ctx->state[3]=0xa54ff53a;
    ctx->state[4]=0x510e527f; ctx->state[5]=0x9b05688c;
    ctx->state[6]=0x1f83d9ab; ctx->state[7]=0x5be0cd19;
    ctx->bit_count=0; ctx->buffer_index=0;
    memset(ctx->buffer,0,64);
}

static void sha256_transform(SHA256Context *ctx, const uint8_t *block) {
    uint32_t w[64],a,b,c,d,e,f,g,h,t1,t2; int i;
    for(i=0;i<16;i++){
        w[i]=((uint32_t)block[i*4]<<24)|((uint32_t)block[i*4+1]<<16)|
             ((uint32_t)block[i*4+2]<<8)|((uint32_t)block[i*4+3]);
    }
    for(i=16;i<64;i++){
        uint32_t s0=ROTR32(w[i-15],7)^ROTR32(w[i-15],18)^(w[i-15]>>3);
        uint32_t s1=ROTR32(w[i-2],17)^ROTR32(w[i-2],19)^(w[i-2]>>10);
        w[i]=w[i-16]+s0+w[i-7]+s1;
    }
    a=ctx->state[0];b=ctx->state[1];c=ctx->state[2];d=ctx->state[3];
    e=ctx->state[4];f=ctx->state[5];g=ctx->state[6];h=ctx->state[7];
    for(i=0;i<64;i++){
        uint32_t S1=ROTR32(e,6)^ROTR32(e,11)^ROTR32(e,25);
        uint32_t ch=(e&f)^((~e)&g); t1=h+S1+ch+sha256_k[i]+w[i];
        uint32_t S0=ROTR32(a,2)^ROTR32(a,13)^ROTR32(a,22);
        uint32_t maj=(a&b)^(a&c)^(b&c); t2=S0+maj;
        h=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;
    }
    ctx->state[0]+=a;ctx->state[1]+=b;ctx->state[2]+=c;ctx->state[3]+=d;
    ctx->state[4]+=e;ctx->state[5]+=f;ctx->state[6]+=g;ctx->state[7]+=h;
}

void sha256_update(SHA256Context *ctx, const uint8_t *data, size_t len) {
    size_t i;
    if(ctx==NULL||data==NULL)return;
    for(i=0;i<len;i++){
        ctx->buffer[ctx->buffer_index++]=data[i]; ctx->bit_count+=8;
        if(ctx->buffer_index==64){sha256_transform(ctx,ctx->buffer);ctx->buffer_index=0;}
    }
}

void sha256_final(SHA256Context *ctx, uint8_t *digest) {
    uint64_t total_bits; int i;
    if(ctx==NULL||digest==NULL)return;
    total_bits=ctx->bit_count;
    ctx->buffer[ctx->buffer_index++]=0x80;
    if(ctx->buffer_index>56){
        while(ctx->buffer_index<64)ctx->buffer[ctx->buffer_index++]=0;
        sha256_transform(ctx,ctx->buffer);ctx->buffer_index=0;
    }
    while(ctx->buffer_index<56)ctx->buffer[ctx->buffer_index++]=0;
    for(i=7;i>=0;i--)ctx->buffer[56+i]=(uint8_t)(total_bits>>(i*8));
    sha256_transform(ctx,ctx->buffer);
    for(i=0;i<8;i++){
        digest[i*4]=(uint8_t)(ctx->state[i]>>24);
        digest[i*4+1]=(uint8_t)(ctx->state[i]>>16);
        digest[i*4+2]=(uint8_t)(ctx->state[i]>>8);
        digest[i*4+3]=(uint8_t)(ctx->state[i]);
    }
}

void sha256_hash(const uint8_t *data, size_t len, uint8_t *digest) {
    SHA256Context ctx; sha256_init(&ctx);
    sha256_update(&ctx,data,len); sha256_final(&ctx,digest);
}

/* L5: SHA-384 per FIPS 180-4 Section 6.4 */
static const uint64_t sha384_k[80] = {
    0x428a2f98d728ae22ULL,0x7137449123ef65cdULL,0xb5c0fbcfec4d3b2fULL,
    0xe9b5dba58189dbbcULL,0x3956c25bf348b538ULL,0x59f111f1b605d019ULL,
    0x923f82a4af194f9bULL,0xab1c5ed5da6d8118ULL,0xd807aa98a3030242ULL,
    0x12835b0145706fbeULL,0x243185be4ee4b28cULL,0x550c7dc3d5ffb4e2ULL,
    0x72be5d74f27b896fULL,0x80deb1fe3b1696b1ULL,0x9bdc06a725c71235ULL,
    0xc19bf174cf692694ULL,0xe49b69c19ef14ad2ULL,0xefbe4786384f25e3ULL,
    0x0fc19dc68b8cd5b5ULL,0x240ca1cc77ac9c65ULL,0x2de92c6f592b0275ULL,
    0x4a7484aa6ea6e483ULL,0x5cb0a9dcbd41fbd4ULL,0x76f988da831153b5ULL,
    0x983e5152ee66dfabULL,0xa831c66d2db43210ULL,0xb00327c898fb213fULL,
    0xbf597fc7beef0ee4ULL,0xc6e00bf33da88fc2ULL,0xd5a79147930aa725ULL,
    0x06ca6351e003826fULL,0x142929670a0e6e70ULL,0x27b70a8546d22ffcULL,
    0x2e1b21385c26c926ULL,0x4d2c6dfc5ac42aedULL,0x53380d139d95b3dfULL,
    0x650a73548baf63deULL,0x766a0abb3c77b2a8ULL,0x81c2c92e47edaee6ULL,
    0x92722c851482353bULL,0xa2bfe8a14cf10364ULL,0xa81a664bbc423001ULL,
    0xc24b8b70d0f89791ULL,0xc76c51a30654be30ULL,0xd192e819d6ef5218ULL,
    0xd69906245565a910ULL,0xf40e35855771202aULL,0x106aa07032bbd1b8ULL,
    0x19a4c116b8d2d0c8ULL,0x1e376c085141ab53ULL,0x2748774cdf8eeb99ULL,
    0x34b0bcb5e19b48a8ULL,0x391c0cb3c5c95a63ULL,0x4ed8aa4ae3418acbULL,
    0x5b9cca4f7763e373ULL,0x682e6ff3d6b2b8a3ULL,0x748f82ee5defb2fcULL,
    0x78a5636f43172f60ULL,0x84c87814a1f0ab72ULL,0x8cc702081a6439ecULL,
    0x90befffa23631e28ULL,0xa4506cebde82bde9ULL,0xbef9a3f7b2c67915ULL,
    0xc67178f2e372532bULL,0xca273eceea26619cULL,0xd186b8c721c0c207ULL,
    0xeada7dd6cde0eb1eULL,0xf57d4f7fee6ed178ULL,0x06f067aa72176fbaULL,
    0x0a637dc5a2c898a6ULL,0x113f9804bef90daeULL,0x1b710b35131c471bULL,
    0x28db77f523047d84ULL,0x32caab7b40c72493ULL,0x3c9ebe0a15c9bebcULL,
    0x431d67c49c100d4cULL,0x4cc5d4becb3e42b6ULL,0x597f299cfc657e2aULL,
    0x5fcb6fab3ad6faecULL,0x6c44198c4a475817ULL
};

void sha384_init(SHA384Context *ctx) {
    if(ctx==NULL)return;
    ctx->state[0]=0xcbbb9d5dc1059ed8ULL;ctx->state[1]=0x629a292a367cd507ULL;
    ctx->state[2]=0x9159015a3070dd17ULL;ctx->state[3]=0x152fecd8f70e5939ULL;
    ctx->state[4]=0x67332667ffc00b31ULL;ctx->state[5]=0x8eb44a8768581511ULL;
    ctx->state[6]=0xdb0c2e0d64f98fa7ULL;ctx->state[7]=0x47b5481dbefa4fa4ULL;
    ctx->bit_count_high=0;ctx->bit_count_low=0;ctx->buffer_index=0;
    memset(ctx->buffer,0,128);
}

static void sha384_transform(SHA384Context *ctx, const uint8_t *block) {
    uint64_t w[80],a,b,c,d,e,f,g,h,t1,t2; int i;
    for(i=0;i<16;i++){
        w[i]=((uint64_t)block[i*8]<<56)|((uint64_t)block[i*8+1]<<48)|
             ((uint64_t)block[i*8+2]<<40)|((uint64_t)block[i*8+3]<<32)|
             ((uint64_t)block[i*8+4]<<24)|((uint64_t)block[i*8+5]<<16)|
             ((uint64_t)block[i*8+6]<<8)|((uint64_t)block[i*8+7]);
    }
    for(i=16;i<80;i++){
        uint64_t s0=ROTR64(w[i-15],1)^ROTR64(w[i-15],8)^(w[i-15]>>7);
        uint64_t s1=ROTR64(w[i-2],19)^ROTR64(w[i-2],61)^(w[i-2]>>6);
        w[i]=w[i-16]+s0+w[i-7]+s1;
    }
    a=ctx->state[0];b=ctx->state[1];c=ctx->state[2];d=ctx->state[3];
    e=ctx->state[4];f=ctx->state[5];g=ctx->state[6];h=ctx->state[7];
    for(i=0;i<80;i++){
        uint64_t S1=ROTR64(e,14)^ROTR64(e,18)^ROTR64(e,41);
        uint64_t ch=(e&f)^((~e)&g); t1=h+S1+ch+sha384_k[i]+w[i];
        uint64_t S0=ROTR64(a,28)^ROTR64(a,34)^ROTR64(a,39);
        uint64_t maj=(a&b)^(a&c)^(b&c); t2=S0+maj;
        h=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;
    }
    ctx->state[0]+=a;ctx->state[1]+=b;ctx->state[2]+=c;ctx->state[3]+=d;
    ctx->state[4]+=e;ctx->state[5]+=f;ctx->state[6]+=g;ctx->state[7]+=h;
}

void sha384_update(SHA384Context *ctx, const uint8_t *data, size_t len) {
    size_t i;
    if(ctx==NULL||data==NULL)return;
    for(i=0;i<len;i++){
        ctx->buffer[ctx->buffer_index++]=data[i];
        ctx->bit_count_low+=8;
        if(ctx->bit_count_low<8)ctx->bit_count_high++;
        if(ctx->buffer_index==128){sha384_transform(ctx,ctx->buffer);ctx->buffer_index=0;}
    }
}

void sha384_final(SHA384Context *ctx, uint8_t *digest) {
    uint64_t total_low,total_high; int i;
    if(ctx==NULL||digest==NULL)return;
    total_low=ctx->bit_count_low;total_high=ctx->bit_count_high;
    ctx->buffer[ctx->buffer_index++]=0x80;
    if(ctx->buffer_index>112){
        while(ctx->buffer_index<128)ctx->buffer[ctx->buffer_index++]=0;
        sha384_transform(ctx,ctx->buffer);ctx->buffer_index=0;
    }
    while(ctx->buffer_index<112)ctx->buffer[ctx->buffer_index++]=0;
    for(i=7;i>=0;i--){
        ctx->buffer[120+i]=(uint8_t)(total_high>>(i*8));
        ctx->buffer[112+i]=(uint8_t)(total_low>>(i*8));
    }
    sha384_transform(ctx,ctx->buffer);
    for(i=0;i<6;i++){
        digest[i*8]=(uint8_t)(ctx->state[i]>>56);
        digest[i*8+1]=(uint8_t)(ctx->state[i]>>48);
        digest[i*8+2]=(uint8_t)(ctx->state[i]>>40);
        digest[i*8+3]=(uint8_t)(ctx->state[i]>>32);
        digest[i*8+4]=(uint8_t)(ctx->state[i]>>24);
        digest[i*8+5]=(uint8_t)(ctx->state[i]>>16);
        digest[i*8+6]=(uint8_t)(ctx->state[i]>>8);
        digest[i*8+7]=(uint8_t)(ctx->state[i]);
    }
}

/* L5: RSA modular exponentiation - Handbook of Applied Cryptography Alg 14.79
 * square-and-multiply: O(k^2 log e) for k-bit modulus */
static bool bigint_modexp(const uint8_t *base, uint32_t base_size,
                           uint32_t exponent,
                           const uint8_t *modulus, uint32_t mod_size,
                           uint8_t *result, uint32_t *result_size) {
    uint32_t i,j,e; uint8_t *tb,*tr,*tm;
    if(base==NULL||modulus==NULL||result==NULL||result_size==NULL)return false;
    if(mod_size==0||base_size>mod_size)return false;
    tb=(uint8_t*)calloc(mod_size*2+1,1);tr=(uint8_t*)calloc(mod_size*2+1,1);
    tm=(uint8_t*)calloc(mod_size,1);
    if(tb==NULL||tr==NULL||tm==NULL){free(tb);free(tr);free(tm);return false;}
    memcpy(tm,modulus,mod_size);
    for(i=0;i<base_size;i++)tb[mod_size-base_size+i]=base[i];
    tr[0]=1; e=exponent;
    while(e>0){
        if(e&1){
            memset(tb,0,mod_size*2+1);
            /* result = (result * base) mod n - schoolbook multiplication */
            for(i=0;i<mod_size;i++){
                uint32_t carry=0;
                for(j=0;j<mod_size;j++){
                    uint32_t prod=(uint32_t)tr[j]*(uint32_t)(tb[mod_size+j])+tb[i+j]+carry;
                    tb[i+j]=(uint8_t)(prod&0xFF); carry=prod>>8;
                }
                tb[i+mod_size]+=(uint8_t)carry;
            }
            /* Barrett-like reduction */
            for(i=mod_size*2;i>0;i--){
                if(tb[i]!=0||memcmp(tb,tm,mod_size)>=0){
                    uint32_t borrow=0;
                    for(j=0;j<mod_size;j++){
                        int32_t diff=(int32_t)tb[j]-(int32_t)tm[j]-borrow;
                        if(diff<0){diff+=256;borrow=1;}else{borrow=0;}
                        tb[j]=(uint8_t)diff;
                    }
                }
            }
            memcpy(tr,tb,mod_size);
        }
        /* base = base^2 mod n */
        {
            uint8_t *sq=(uint8_t*)calloc(mod_size*2+1,1);
            if(sq==NULL){free(tb);free(tr);free(tm);return false;}
            for(i=0;i<mod_size;i++){
                uint32_t carry=0;
                for(j=0;j<mod_size;j++){
                    uint32_t prod=(uint32_t)tr[j]*(uint32_t)tr[i];
                    uint32_t sum=prod+sq[i+j]+carry;
                    sq[i+j]=(uint8_t)(sum&0xFF);carry=sum>>8;
                }
                sq[i+mod_size]+=(uint8_t)carry;
            }
            for(i=mod_size*2;i>0;i--){
                if(sq[i]!=0||memcmp(sq,tm,mod_size)>=0){
                    uint32_t borrow=0;
                    for(j=0;j<mod_size;j++){
                        int32_t diff=(int32_t)sq[j]-(int32_t)tm[j]-borrow;
                        if(diff<0){diff+=256;borrow=1;}else{borrow=0;}
                        sq[j]=(uint8_t)diff;
                    }
                }
            }
            memcpy(tr,sq,mod_size); free(sq);
        }
        e>>=1;
    }
    memcpy(result,tr,mod_size);*result_size=mod_size;
    free(tb);free(tr);free(tm); return true;
}

/* L4: RSA PKCS#1 v1.5 Verification - RFC 8017 Section 8.2.2
 * Theorem: If verification passes, signer possessed private key (RSA assumption).
 * EMSA-PKCS1-v1_5-ENCODE: EM = 0x00 || 0x01 || PS(=0xFF) || 0x00 || T
 * where T = DER(DigestInfo) = prefix || hash */
bool sb_rsa_verify_pkcs1_v15(const RSAPublicKey *key,
                              const uint8_t *hash,
                              uint32_t hash_size,
                              const uint8_t *signature,
                              uint32_t sig_size) {
    uint8_t *em; uint32_t em_size,i;
    if(key==NULL||hash==NULL||signature==NULL)return false;
    if(key->modulus_size==0||sig_size!=key->modulus_size)return false;
    em=(uint8_t*)calloc(key->modulus_size,1);
    if(em==NULL)return false;
    if(!bigint_modexp(signature,sig_size,key->public_exponent,
                       key->modulus,key->modulus_size,em,&em_size)){
        free(em);return false;
    }
    if(em[0]!=0x00||em[1]!=0x01){free(em);return false;}
    for(i=2;i<key->modulus_size;i++){
        if(em[i]==0x00)break;
        if(em[i]!=0xFF){free(em);return false;}
    }
    if(i<10){free(em);return false;} /* Need >= 8 bytes 0xFF padding */
    i++; /* skip 0x00 separator */
    if(key->modulus_size-i<hash_size){free(em);return false;}
    /* DER DigestInfo prefix for SHA-256: 30 31 30 0d 06 09 60 86 48 01 65 03 04 02 01 05 00 04 20 */
    if(hash_size==SB_HASH_SIZE_SHA256){
        uint8_t sha256pfx[]={0x30,0x31,0x30,0x0d,0x06,0x09,0x60,0x86,
            0x48,0x01,0x65,0x03,0x04,0x02,0x01,0x05,0x00,0x04,0x20};
        if(memcmp(em+i,sha256pfx,sizeof(sha256pfx))!=0){free(em);return false;}
        i+=sizeof(sha256pfx);
    }else if(hash_size==SB_HASH_SIZE_SHA384){
        uint8_t sha384pfx[]={0x30,0x41,0x30,0x0d,0x06,0x09,0x60,0x86,
            0x48,0x01,0x65,0x03,0x04,0x02,0x02,0x05,0x00,0x04,0x30};
        if(memcmp(em+i,sha384pfx,sizeof(sha384pfx))!=0){free(em);return false;}
        i+=sizeof(sha384pfx);
    }
    if(memcmp(em+i,hash,hash_size)!=0){free(em);return false;}
    free(em); return true;
}

/* L4: RSA-PSS Verification - RFC 8017 Section 8.1.2
 * Provably secure in random oracle model (Bellare-Rogaway, Eurocrypt 1996).
 * EMSA-PSS: EM = maskedDB || H || 0xBC
 * Verification: recover salt from DB=MGF(H)^maskedDB, check H=Hash(zeroes||mHash||salt) */
bool sb_rsa_verify_pss(const RSAPublicKey *key,
                        const uint8_t *hash,
                        uint32_t hash_size,
                        const uint8_t *signature,
                        uint32_t sig_size) {
    uint8_t *em; uint32_t em_size,em_len,salt_len,masked_db_len,i;
    if(key==NULL||hash==NULL||signature==NULL)return false;
    if(key->modulus_size<(hash_size+2))return false;
    em_len=key->modulus_size;salt_len=hash_size;
    em=(uint8_t*)calloc(em_len,1);
    if(em==NULL)return false;
    if(!bigint_modexp(signature,sig_size,key->public_exponent,
                       key->modulus,key->modulus_size,em,&em_size)){
        free(em);return false;
    }
    if(em[em_len-1]!=0xBC){free(em);return false;}
    masked_db_len=em_len-hash_size-1;
    {
        uint8_t *masked_db=(uint8_t*)calloc(masked_db_len+hash_size,1);
        uint8_t *db_mask=(uint8_t*)calloc(masked_db_len,1);
        uint8_t *salt=(uint8_t*)calloc(salt_len,1);
        uint8_t *mp=(uint8_t*)calloc(8+hash_size+salt_len,1);
        uint8_t hprime[SB_HASH_SIZE_SHA256]; bool valid=true;
        if(masked_db==NULL||db_mask==NULL||salt==NULL||mp==NULL){
            free(masked_db);free(db_mask);free(salt);free(mp);free(em);return false;
        }
        memcpy(masked_db,em,masked_db_len);
        /* MGF1: generate dbMask from H */
        {
            SHA256Context mgf_ctx; uint8_t mgf_seed[36]; uint32_t counter,offset,j;
            memcpy(mgf_seed,em+masked_db_len,hash_size);
            for(counter=0,offset=0;offset<masked_db_len;counter++){
                mgf_seed[hash_size]=(uint8_t)(counter>>24);
                mgf_seed[hash_size+1]=(uint8_t)(counter>>16);
                mgf_seed[hash_size+2]=(uint8_t)(counter>>8);
                mgf_seed[hash_size+3]=(uint8_t)(counter);
                sha256_init(&mgf_ctx);sha256_update(&mgf_ctx,mgf_seed,hash_size+4);
                {uint8_t mgf_out[32];uint32_t cl;
                sha256_final(&mgf_ctx,mgf_out);
                cl=(masked_db_len-offset)<32?(masked_db_len-offset):32;
                for(j=0;j<cl;j++){db_mask[offset+j]=mgf_out[j];}offset+=cl;}
            }
        }
        /* DB = maskedDB XOR dbMask */
        {uint8_t *db=(uint8_t*)calloc(masked_db_len,1);
        if(db==NULL){free(masked_db);free(db_mask);free(salt);free(mp);free(em);return false;}
        for(i=0;i<masked_db_len;i++)db[i]=masked_db[i]^db_mask[i];
        {uint32_t ps_len=masked_db_len-salt_len-1;
         for(i=0;i<ps_len;i++){if(db[i]!=0x00){valid=false;break;}}
         if(db[ps_len]!=0x01)valid=false;
         memcpy(salt,db+ps_len+1,salt_len);}
        free(db);}
        memset(mp,0,8);memcpy(mp+8,hash,hash_size);memcpy(mp+8+hash_size,salt,salt_len);
        sha256_hash(mp,8+hash_size+salt_len,hprime);
        if(memcmp(hprime,em+masked_db_len,hash_size)!=0)valid=false;
        free(masked_db);free(db_mask);free(salt);free(mp);
        free(em);return valid;
    }
}

/* L2: Secure Boot chain-of-trust enforcement
 * UEFI Spec 32.4.1: Setup Mode -> User Mode -> Deployed Mode */
void sb_init(SecureBootPolicy *sb) {
    if(sb==NULL)return;
    memset(sb,0,sizeof(SecureBootPolicy));
    sb->operating_mode=SB_MODE_SETUP; sb->setup_mode=true;
    sb->secure_boot_enabled=false; sb->deployed_mode=false; sb->audit_mode=false;
}

bool sb_check_setup_mode(SecureBootPolicy *sb) {return sb!=NULL&&sb->setup_mode;}
bool sb_check_deployed_mode(SecureBootPolicy *sb) {return sb!=NULL&&sb->deployed_mode;}

/* L3: PK enrollment - top of trust chain. Only in setup mode.
 * Once PK enrolled, transitions to User Mode per UEFI Spec Fig 32-2. */
bool sb_enroll_pk(SecureBootPolicy *sb, const EFI_GUID *owner,
                  const uint8_t *cert, uint32_t cert_size) {
    if(sb==NULL||owner==NULL||cert==NULL||cert_size==0)return false;
    if(!sb->setup_mode)return false;
    if(cert_size>sizeof(sb->pk.cert_data))return false;
    sb->pk.db_type=SB_DB_TYPE_PK;
    memcpy(&sb->pk.owner_guid,owner,sizeof(EFI_GUID));
    memcpy(sb->pk.cert_data,cert,cert_size);
    sb->pk.cert_size=cert_size; sb->pk.cert_type=WIN_CERT_TYPE_EFI_GUID;
    sb->pk.revoked=false;
    sb->setup_mode=false; sb->operating_mode=SB_MODE_USER;
    sb->secure_boot_enabled=true;
    return true;
}

/* L3: KEK enrollment - requires PK to be present.
 * KEK holders authorized to modify db/dbx/signature databases. */
bool sb_enroll_kek(SecureBootPolicy *sb, const EFI_GUID *owner,
                   const uint8_t *cert, uint32_t cert_size) {
    if(sb==NULL||owner==NULL||cert==NULL||cert_size==0)return false;
    if(sb->setup_mode)return false;
    if(sb->kek_count>=SB_MAX_CERTIFICATES)return false;
    if(cert_size>sizeof(sb->kek[0].cert_data))return false;
    memcpy(&sb->kek[sb->kek_count].owner_guid,owner,sizeof(EFI_GUID));
    memcpy(sb->kek[sb->kek_count].cert_data,cert,cert_size);
    sb->kek[sb->kek_count].cert_size=cert_size;
    sb->kek[sb->kek_count].db_type=SB_DB_TYPE_KEK;
    sb->kek[sb->kek_count].revoked=false;
    sb->kek_count++; return true;
}

/* L3: db enrollment - authorized signature database.
 * Requires KEK presence for authorization. */
bool sb_enroll_db(SecureBootPolicy *sb, const EFI_GUID *owner,
                  const uint8_t *signature, uint32_t sig_size) {
    if(sb==NULL||owner==NULL||signature==NULL||sig_size==0)return false;
    if(sb->kek_count==0)return false;
    if(sb->db_count>=SB_MAX_SIGNATURES)return false;
    if(sig_size>sizeof(sb->db[0].cert_data))return false;
    memcpy(&sb->db[sb->db_count].owner_guid,owner,sizeof(EFI_GUID));
    memcpy(sb->db[sb->db_count].cert_data,signature,sig_size);
    sb->db[sb->db_count].cert_size=sig_size;
    sb->db[sb->db_count].db_type=SB_DB_TYPE_DB;
    sb->db[sb->db_count].revoked=false;
    sb->db_count++; return true;
}

/* L3: dbx enrollment - forbidden signature database (revocation).
 * UEFI Spec 32.4.2: dbx entries permanently invalidate matching images. */
bool sb_enroll_dbx(SecureBootPolicy *sb, const EFI_GUID *owner,
                   const uint8_t *digest, uint32_t digest_size) {
    if(sb==NULL||owner==NULL||digest==NULL||digest_size==0)return false;
    if(sb->kek_count==0)return false;
    if(sb->dbx_count>=SB_MAX_SIGNATURES)return false;
    if(digest_size>sizeof(sb->dbx[0].cert_data))return false;
    memcpy(&sb->dbx[sb->dbx_count].owner_guid,owner,sizeof(EFI_GUID));
    memcpy(sb->dbx[sb->dbx_count].cert_data,digest,digest_size);
    sb->dbx[sb->dbx_count].cert_size=digest_size;
    sb->dbx[sb->dbx_count].db_type=SB_DB_TYPE_DBX;
    sb->dbx[sb->dbx_count].revoked=true;
    sb->dbx_count++; return true;
}

bool sb_delete_pk(SecureBootPolicy *sb) {
    if(sb==NULL)return false;
    if(sb->deployed_mode)return false;
    memset(&sb->pk,0,sizeof(sb->pk));
    sb->setup_mode=true; sb->operating_mode=SB_MODE_SETUP;
    sb->secure_boot_enabled=false; return true;
}

bool sb_delete_kek(SecureBootPolicy *sb, uint8_t index) {
    if(sb==NULL)return false;
    if(index>=sb->kek_count)return false;
    if(sb->deployed_mode)return false;
    memmove(&sb->kek[index],&sb->kek[index+1],
            (sb->kek_count-index-1)*sizeof(SignatureDBEntry));
    sb->kek_count--; return true;
}

bool sb_query_db(SecureBootPolicy *sb, const uint8_t *signature,uint32_t sig_size){
    uint8_t i;
    if(sb==NULL||signature==NULL)return false;
    for(i=0;i<sb->db_count;i++){
        if(sb->db[i].cert_size==sig_size&&
           memcmp(sb->db[i].cert_data,signature,sig_size)==0)return true;
    }
    return false;
}

bool sb_query_dbx(SecureBootPolicy *sb, const uint8_t *digest,uint32_t digest_size){
    uint8_t i;
    if(sb==NULL||digest==NULL)return false;
    for(i=0;i<sb->dbx_count;i++){
        if(sb->dbx[i].cert_size==digest_size&&
           memcmp(sb->dbx[i].cert_data,digest,digest_size)==0)return true;
    }
    return false;
}

/* L2: Image verification - complete chain-of-trust enforcement.
 * Steps (UEFI Spec 32.4.2):
 *   1. Compute SHA-256 hash of image
 *   2. Check dbx (revocation) - if match, REJECT
 *   3. Check db (allow list) - if match, ALLOW
 *   4. Verify RSA signature against enrolled certificates
 *   5. Audit mode: log but allow execution
 * Complexity: O(D + K + log e) where D=db entries, K=kek entries */
bool sb_verify_image(SecureBootPolicy *sb,
                     const uint8_t *image, size_t image_size,
                     const uint8_t *signature, size_t sig_size,
                     SBImageContext *ctx) {
    RSAPublicKey pub_key; uint8_t computed_hash[SB_HASH_SIZE_SHA256]; uint8_t i;
    if(sb==NULL||image==NULL||ctx==NULL)return false;
    memset(ctx,0,sizeof(SBImageContext));
    if(!sb->secure_boot_enabled){ctx->verified=true;return true;}
    sha256_hash(image,image_size,computed_hash);
    memcpy(ctx->image_hash,computed_hash,SB_HASH_SIZE_SHA256);
    ctx->image_hash_algorithm=SB_SIGNATURE_TYPE_SHA256;
    if(sb_query_dbx(sb,computed_hash,SB_HASH_SIZE_SHA256)){
        ctx->verified=false;ctx->revoked=true;return false;}
    if(sb_query_db(sb,computed_hash,SB_HASH_SIZE_SHA256)){
        ctx->verified=true;return true;}
    if(signature!=NULL&&sig_size>0){
        memcpy(ctx->image_signature,signature,
               sig_size<sizeof(ctx->image_signature)?sig_size:sizeof(ctx->image_signature));
        ctx->image_signature_size=(uint32_t)sig_size;
        for(i=0;i<sb->db_count;i++){
            if(sb_x509_extract_public_key(sb->db[i].cert_data,
                                          sb->db[i].cert_size,&pub_key)){
                if(sb_rsa_verify_pkcs1_v15(&pub_key,computed_hash,
                                           SB_HASH_SIZE_SHA256,
                                           signature,(uint32_t)sig_size)){
                    ctx->verified=true;return true;
                }
            }
        }
        for(i=0;i<sb->kek_count;i++){
            if(sb_x509_extract_public_key(sb->kek[i].cert_data,
                                          sb->kek[i].cert_size,&pub_key)){
                if(sb_rsa_verify_pkcs1_v15(&pub_key,computed_hash,
                                           SB_HASH_SIZE_SHA256,
                                           signature,(uint32_t)sig_size)){
                    ctx->verified=true;return true;
                }
            }
        }
    }
    if(sb->audit_mode){ctx->verified=true;ctx->deferred_exec=true;return true;}
    ctx->verified=false; return false;
}

/* L7: Authenticode PE/COFF verification - Enterprise signing integration
 * Microsoft PE/COFF Spec 11.0: Certificate Table in IMAGE_DATA_DIRECTORY[4] */
bool sb_verify_with_authenticode(SecureBootPolicy *sb,
                                 const uint8_t *pe_image, size_t image_size) {
    uint8_t hash[SB_HASH_SIZE_SHA256]; uint8_t i;
    if(sb==NULL||pe_image==NULL||image_size<64)return false;
    sha256_hash(pe_image,image_size,hash);
    if(sb_query_dbx(sb,hash,SB_HASH_SIZE_SHA256))return false;
    for(i=0;i<sb->db_count;i++){
        RSAPublicKey key;
        if(sb_x509_extract_public_key(sb->db[i].cert_data,
                                       sb->db[i].cert_size,&key)){
            if(sb_rsa_verify_pkcs1_v15(&key,hash,SB_HASH_SIZE_SHA256,
                                        sb->db[i].cert_data,
                                        sb->db[i].cert_size))return true;
        }
    }
    return false;
}

/* L5: X.509 minimal parser - RFC 5280 Section 4.1
 * Extracts RSA public key from DER-encoded SubjectPublicKeyInfo.
 * DER structure: SEQUENCE{Certificate} -> TBSCertificate ->
 *   SubjectPublicKeyInfo -> AlgorithmIdentifier(OID:1.2.840.113549.1.1.1)
 *   -> BIT STRING{RSAPublicKey=SEQUENCE{INTEGER modulus,INTEGER exponent}}
 * Time: O(n) where n=cert_size, Space: O(1) */
bool sb_x509_extract_public_key(const uint8_t *cert, size_t cert_size,
                                 RSAPublicKey *key) {
    const uint8_t *p,*end,*modulus_ptr,*exp_ptr;
    uint32_t modulus_len,exp_len; size_t offset;
    if(cert==NULL||key==NULL||cert_size<128)return false;
    memset(key,0,sizeof(RSAPublicKey));
    p=cert; end=cert+cert_size;
    /* Find RSA OID: 2A 86 48 86 F7 0D 01 01 01 (rsaEncryption) */
    {
        const uint8_t rsa_oid[]={0x2A,0x86,0x48,0x86,0xF7,0x0D,0x01,0x01,0x01};
        const uint8_t *oid_pos=NULL;
        for(offset=0;offset+sizeof(rsa_oid)<=cert_size;offset++){
            if(memcmp(cert+offset,rsa_oid,sizeof(rsa_oid))==0){oid_pos=cert+offset;break;}}
        if(oid_pos==NULL)return false;
        p=oid_pos+sizeof(rsa_oid);
    }
    if(p+2>end)return false;
    /* Skip NULL tag (05 00) if present after OID */
    if(p[0]==0x05&&p[1]==0x00)p+=2;
    if(p[0]!=0x03)return false; /* BIT STRING */
    {size_t bit_string_len;
     if(p[1]<0x80){bit_string_len=p[1];p+=2;}
     else{uint8_t lb=p[1]&0x7F;bit_string_len=0;
          for(offset=0;offset<lb;offset++)bit_string_len=(bit_string_len<<8)|p[2+offset];
          p+=2+lb;}
     if(*p!=0x00){return false;}p++;bit_string_len--;
     /* Inside: SEQUENCE{INTEGER modulus, INTEGER exponent} */
     if(p[0]!=0x30)return false;
     if(p[1]<0x80)offset=2;else{uint8_t lb=p[1]&0x7F;offset=2+lb;}
     p+=offset;
     if(p[0]!=0x02)return false; /* INTEGER */
     if(p[1]<0x80){modulus_len=p[1];modulus_ptr=p+2;}
     else{uint8_t lb=p[1]&0x7F;modulus_len=0;
          for(offset=0;offset<lb;offset++)modulus_len=(modulus_len<<8)|p[2+offset];
          modulus_ptr=p+2+lb;}
     if(modulus_len>0&&modulus_ptr[0]==0x00){modulus_ptr++;modulus_len--;}
     if(modulus_len>SB_KEY_SIZE_RSA3072)return false;
     p=modulus_ptr+modulus_len;
     if(p[0]!=0x02)return false; /* INTEGER */
     if(p[1]<0x80){exp_len=p[1];exp_ptr=p+2;}
     else{uint8_t lb=p[1]&0x7F;exp_len=0;
          for(offset=0;offset<lb;offset++)exp_len=(exp_len<<8)|p[2+offset];
          exp_ptr=p+2+lb;}
     {uint32_t ev=0;
      for(offset=0;offset<exp_len&&offset<4;offset++)ev=(ev<<8)|exp_ptr[offset];
      key->public_exponent=ev;}
     memcpy(key->modulus,modulus_ptr,modulus_len);key->modulus_size=modulus_len;
     return true;}
}

/* L5: X.509 certificate chain validation - RFC 5280 Section 6.1
 * Each cert in chain must be signed by the next (issuer).
 * Uses PKCS#1 v1.5 signature verification */
bool sb_x509_validate_chain(const uint8_t **certs, size_t *cert_sizes,
                             uint8_t count) {
    uint8_t i;
    if(certs==NULL||cert_sizes==NULL||count<2)return false;
    for(i=0;i<count-1;i++){
        RSAPublicKey issuer_key; uint8_t cert_hash[SB_HASH_SIZE_SHA256];
        const uint8_t *sig; uint32_t sig_size;
        if(!sb_x509_extract_public_key(certs[i+1],cert_sizes[i+1],&issuer_key))
            return false;
        sha256_hash(certs[i],cert_sizes[i],cert_hash);
        if(cert_sizes[i]<256)return false;
        sig=certs[i]+cert_sizes[i]-256;sig_size=256;
        if(!sb_rsa_verify_pkcs1_v15(&issuer_key,cert_hash,
                                     SB_HASH_SIZE_SHA256,sig,sig_size))
            return false;
    }
    return true;
}

/* Utility: Clear all keys - return to Setup Mode */
void sb_clear_secure_boot_keys(SecureBootPolicy *sb) {
    if(sb==NULL)return;
    memset(&sb->pk,0,sizeof(sb->pk)); memset(sb->kek,0,sizeof(sb->kek));
    memset(sb->db,0,sizeof(sb->db)); memset(sb->dbx,0,sizeof(sb->dbx));
    memset(sb->dbt,0,sizeof(sb->dbt));
    sb->kek_count=0;sb->db_count=0;sb->dbx_count=0;sb->dbt_count=0;
    sb->setup_mode=true;sb->operating_mode=SB_MODE_SETUP;
    sb->secure_boot_enabled=false;
}

bool sb_export_public_key(const RSAPublicKey *key,
                          uint8_t *modulus_out, uint32_t *modulus_size,
                          uint32_t *exponent_out) {
    if(key==NULL||modulus_out==NULL||modulus_size==NULL||exponent_out==NULL)
        return false;
    memcpy(modulus_out,key->modulus,key->modulus_size);
    *modulus_size=key->modulus_size;*exponent_out=key->public_exponent;
    return true;
}

/* L3: Mode transitions per UEFI Spec Section 32.4.1 */
bool sb_transition_to_user_mode(SecureBootPolicy *sb) {
    if(sb==NULL)return false;
    if(sb->pk.cert_size==0)return false;
    sb->operating_mode=SB_MODE_USER;sb->setup_mode=false;
    sb->secure_boot_enabled=true;sb->deployed_mode=false;sb->audit_mode=false;
    return true;
}

bool sb_transition_to_deployed_mode(SecureBootPolicy *sb) {
    if(sb==NULL)return false;
    if(sb->pk.cert_size==0)return false;
    if(sb->kek_count==0)return false;
    sb->operating_mode=SB_MODE_DEPLOYED;sb->setup_mode=false;
    sb->deployed_mode=true;sb->audit_mode=false;sb->secure_boot_enabled=true;
    return true;
}
