//
//  BRBIP39Mnemonic.c
//
//  Created by Aaron Voisine on 9/7/15.
//  Copyright (c) 2015 breadwallet LLC
//
//  Permission is hereby granted, free of charge, to any person obtaining a copy
//  of this software and associated documentation files (the "Software"), to deal
//  in the Software without restriction, including without limitation the rights
//  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//  copies of the Software, and to permit persons to whom the Software is
//  furnished to do so, subject to the following conditions:
//
//  The above copyright notice and this permission notice shall be included in
//  all copies or substantial portions of the Software.
//
//  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
//  THE SOFTWARE.

#include "BRBIP39Mnemonic.h"
#include "BRCrypto.h"
#include "BRInt.h"
#include <string.h>
#include <assert.h>

// returns number of bytes written to phrase including NULL terminator, or phraseLen needed if phrase is NULL
size_t BRBIP39Encode(char *phrase, size_t phraseLen, const char *wordList[], const uint8_t *data, size_t dataLen)
{
    uint32_t x;
    uint8_t buf[dataLen + 32];
    const char *word;
    size_t i, len = 0;

    assert(wordList != NULL);
    assert(data != NULL || dataLen == 0);
    assert(dataLen > 0 && (dataLen % 4) == 0);
    if (! data || (dataLen % 4) != 0) return 0; // data length must be a multiple of 32 bits
    
    memcpy(buf, data, dataLen);
    BRSHA256(&buf[dataLen], data, dataLen); // append SHA256 checksum

    for (i = 0; i < dataLen*3/4; i++) {
        x = UInt32GetBE(&buf[i*11/8]);
        word = wordList[(x >> (32 - (11 + ((i*11) % 8)))) % BIP39_WORDLIST_COUNT];
        if (i > 0 && phrase && len < phraseLen) phrase[len] = ' ';
        if (i > 0) len++;
        if (phrase && len < phraseLen) strncpy(&phrase[len], word, phraseLen - len);
        len += strlen(word);
    }

    var_clean(&word);
    var_clean(&x);
    mem_clean(buf, sizeof(buf));
    return (! phrase || len + 1 <= phraseLen) ? len + 1 : 0;
}

// returns number of bytes written to data, or dataLen needed if data is NULL
size_t BRBIP39Decode(uint8_t *data, size_t dataLen, const char *wordList[], const char *phrase)
{
    uint32_t x, y, count = 0, idx[24], i;
    uint8_t b = 0, hash[32];
    const char *word = phrase;
    size_t r = 0;

    assert(wordList != NULL);
    assert(phrase != NULL);
    
    while (word && *word && count < 24) {
        for (i = 0, idx[count] = INT32_MAX; i < BIP39_WORDLIST_COUNT; i++) { // not fast, but simple and correct
            if (strncmp(word, wordList[i], strlen(wordList[i])) != 0 ||
                (word[strlen(wordList[i])] != ' ' && word[strlen(wordList[i])] != '\0')) continue;
            idx[count] = i;
            break;
        }
        
        if (idx[count] == INT32_MAX) break; // phrase contains unknown word
        count++;
        word = strchr(word, ' ');
        if (word) word++;
    }

    if ((count % 3) == 0 && (! word || *word == '\0')) { // check that phrase has correct number of words
        uint8_t buf[(count*11 + 7)/8];

        for (i = 0; i < (count*11 + 7)/8; i++) {
            x = idx[i*8/11];
            y = (i*8/11 + 1 < count) ? idx[i*8/11 + 1] : 0;
            b = ((x*BIP39_WORDLIST_COUNT + y) >> ((i*8/11 + 2)*11 - (i + 1)*8)) & 0xff;
            buf[i] = b;
        }
    
        BRSHA256(hash, buf, count*4/3);

        if (b >> (8 - count/3) == (hash[0] >> (8 - count/3))) { // verify checksum
            r = count*4/3;
            if (data && r <= dataLen) memcpy(data, buf, r);
        }
        
        mem_clean(buf, sizeof(buf));
    }

    var_clean(&b);
    var_clean(&x, &y);
    mem_clean(idx, sizeof(idx));
    return (! data || r <= dataLen) ? r : 0;
}

// verifies that all phrase words are contained in wordlist and checksum is valid
int BRBIP39PhraseIsValid(const char *wordList[], const char *phrase)
{
    assert(wordList != NULL);
    assert(phrase != NULL);
    return (BRBIP39Decode(NULL, 0, wordList, phrase) > 0);
}

// key64 must hold 64 bytes (512 bits), phrase and passphrase must be unicode NFKD normalized
// http://www.unicode.org/reports/tr15/#Norm_Forms
// BUG: does not currently support passphrases containing NULL characters
void BRBIP39DeriveKey(void *key64, const char *phrase, const char *passphrase)
{
    char salt[strlen("mnemonic") + (passphrase ? strlen(passphrase) : 0) + 1];

    assert(key64 != NULL);
    assert(phrase != NULL);
    
    if (phrase) {
        strcpy(salt, "mnemonic");
        if (passphrase) strcpy(salt + strlen("mnemonic"), passphrase);
        BRPBKDF2(key64, 64, BRSHA512, 512/8, phrase, strlen(phrase), salt, strlen(salt), 2048);
        mem_clean(salt, sizeof(salt));
    }
}
