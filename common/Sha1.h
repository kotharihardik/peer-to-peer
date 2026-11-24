#pragma once
// =====================================================================
// Sha1 - computes a SHA1 hash, piece by piece (uses OpenSSL)
// =====================================================================
// Incremental hashing: data is fed in chunks with update(), so a 1 GB file is
// hashed with a 512 KB buffer instead of being loaded into memory.

#include <openssl/evp.h>
#include <string>

using namespace std;

class Sha1 {
public:
    Sha1();
    ~Sha1();   // RAII: frees the OpenSSL context

    void update(const char* data, long long len);   // add more data
    string hexDigest();                              // finish -> 40 hex chars

    // Shortcut: hash one buffer in one call
    static string hashOf(const char* data, long long len);

private:
    EVP_MD_CTX* ctx;

    Sha1(const Sha1&) = delete;
    Sha1& operator=(const Sha1&) = delete;
};
