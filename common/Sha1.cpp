// Sha1.cpp - thin wrapper over OpenSSL's EVP digest API
#include "Sha1.h"

#include <new>

using namespace std;

Sha1::Sha1() {
    ctx = EVP_MD_CTX_new();
    if (ctx == NULL || EVP_DigestInit_ex(ctx, EVP_sha1(), NULL) != 1) {
        EVP_MD_CTX_free(ctx);
        throw bad_alloc();   // out of memory - nothing sensible to do
    }
}

Sha1::~Sha1() {
    EVP_MD_CTX_free(ctx);
}

void Sha1::update(const char* data, long long len) {
    EVP_DigestUpdate(ctx, data, len);
}

// Finish the hash and convert the 20 raw bytes into 40 hex characters
string Sha1::hexDigest() {
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int len = 0;
    EVP_DigestFinal_ex(ctx, digest, &len);

    const char* hexChars = "0123456789abcdef";
    string result;
    for (unsigned int i = 0; i < len; i++) {
        result += hexChars[digest[i] >> 4];    // high 4 bits
        result += hexChars[digest[i] & 0xF];   // low 4 bits
    }
    return result;
}

string Sha1::hashOf(const char* data, long long len) {
    Sha1 sha;
    sha.update(data, len);
    return sha.hexDigest();
}
