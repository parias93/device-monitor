#include <string>
#include <iostream>
#include <fstream>
#include <math.h>
#include <ctime>
#include <openssl/aes.h>
#include <openssl/err.h>
#include <openssl/rand.h>
#include "sha256.h"

//añadido Linux
#include <stdlib.h>
#include <string.h>
typedef unsigned char UCHAR;
typedef UCHAR *PUCHAR;
typedef unsigned long ULONG;
typedef char *PCHAR;

using namespace std;

const char* hex_char_to_bin(char c)
{
	// TODO handle default / error
	switch (toupper(c))
	{
	case '0': return "0000";
	case '1': return "0001";
	case '2': return "0010";
	case '3': return "0011";
	case '4': return "0100";
	case '5': return "0101";
	case '6': return "0110";
	case '7': return "0111";
	case '8': return "1000";
	case '9': return "1001";
	case 'A': return "1010";
	case 'B': return "1011";
	case 'C': return "1100";
	case 'D': return "1101";
	case 'E': return "1110";
	case 'F': return "1111";
}
	return 0;
}


struct ctr_state
{
	unsigned char ivec[AES_BLOCK_SIZE];
	unsigned int num;
	unsigned char ecount[AES_BLOCK_SIZE];
};

std::string hex_str_to_bin_str(const std::string& hex)
{
	// TODO use a loop from <algorithm> or smth
	std::string bin;
	for (unsigned i = 0; i != hex.length(); ++i)
		bin += hex_char_to_bin(hex[i]);
	return bin;
}

#ifdef CIBERDRONECIPHER_EXPORTS
#define CIBERDRONECIPHER_API __attribute__((visibility("default")))
#else
#define CIBERDRONECIPHER_API 
#endif

PUCHAR keyGenerate(PCHAR claves);
void init_ctr(struct ctr_state *state, const unsigned char iv[16]);
PUCHAR AES_CTRmodeExecute(PUCHAR file, ULONG fileSize, PUCHAR key, PUCHAR initVector);
PUCHAR cipherExecute(PUCHAR file, ULONG fileSize, PUCHAR key, PUCHAR initVector, string ciphAlg, string ciphMode);

extern "C" CIBERDRONECIPHER_API PUCHAR protectExecute(PUCHAR file, ULONG fileSize, PUCHAR ciphData, PUCHAR initVector, PCHAR subKeys);
extern "C" CIBERDRONECIPHER_API PUCHAR getInitVector();