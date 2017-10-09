//ModCifrado
//Es posible que haya librerias que no se usan.
#pragma once
//#include <tchar.h>
#include <iostream>
#include <string>
//#include <windows.h>
#include <unistd.h>
#include <vector>
#include <stdexcept>
#include <algorithm>
#include <fstream>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <ctype.h>
//#include <stdlib.h>
#include <setjmp.h>
#include <stdio.h>
#include <iomanip>
#include <ctime>
#include <sstream>
#include <cassert>
#include <functional>
#include <iterator>
#include <bitset>
#include <openssl/evp.h>
#include <openssl/aes.h>
#include <openssl/err.h>

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

static const unsigned char key[] = {
	0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
	0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff,
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
	0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f
};

std::string hex_str_to_bin_str(const std::string& hex)
{
	// TODO use a loop from <algorithm> or smth
	std::string bin;
	for (unsigned i = 0; i != hex.length(); ++i)
		bin += hex_char_to_bin(hex[i]);
	return bin;
}

int main(){
	unsigned char text[] = "hello world!";
	unsigned char enc_out[80];
	unsigned char dec_out[80];

	AES_KEY enc_key, dec_key;

	AES_set_encrypt_key(key, 128, &enc_key);
	AES_encrypt(text, enc_out, &enc_key);

	AES_set_decrypt_key(key, 128, &dec_key);
	AES_decrypt(enc_out, dec_out, &dec_key);

	int i;

	printf("original:\t");
	for (i = 0; *(text + i) != 0x00; i++)
		printf("%X ", *(text + i));
	printf("\nencrypted:\t");
	for (i = 0; *(enc_out + i) != 0x00; i++)
		printf("%X ", *(enc_out + i));
	printf("\ndecrypted:\t");
	for (i = 0; *(dec_out + i) != 0x00; i++)
		printf("%X ", *(dec_out + i));
	printf("\n");

	return 0;
}

void handleErrors(void)
{
	ERR_print_errors_fp(stderr);
	abort();
}

int encrypt(unsigned char *plaintext, int plaintext_len, unsigned char *aad,
	int aad_len, unsigned char *key, unsigned char *iv,
	unsigned char *ciphertext, unsigned char *tag)
{
	EVP_CIPHER_CTX *ctx = NULL;
	int len = 0, ciphertext_len = 0;

	/* Create and initialise the context */
	if (!(ctx = EVP_CIPHER_CTX_new())) handleErrors();

	/* Initialise the encryption operation. */
	if (1 != EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL))
		handleErrors();

	/* Set IV length if default 12 bytes (96 bits) is not appropriate */
	if (1 != EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, 16, NULL))
		handleErrors();

	/* Initialise key and IV */
	if (1 != EVP_EncryptInit_ex(ctx, NULL, NULL, key, iv)) handleErrors();

	/* Provide any AAD data. This can be called zero or more times as
	* required
	*/
	if (aad && aad_len > 0)
	{
		if (1 != EVP_EncryptUpdate(ctx, NULL, &len, aad, aad_len))
			handleErrors();
	}

	/* Provide the message to be encrypted, and obtain the encrypted output.
	* EVP_EncryptUpdate can be called multiple times if necessary
	*/
	if (plaintext)
	{
		if (1 != EVP_EncryptUpdate(ctx, ciphertext, &len, plaintext, plaintext_len))
			handleErrors();

		ciphertext_len = len;
	}

	/* Finalise the encryption. Normally ciphertext bytes may be written at
	* this stage, but this does not occur in GCM mode
	*/
	if (1 != EVP_EncryptFinal_ex(ctx, ciphertext + len, &len)) handleErrors();
	ciphertext_len += len;

	/* Get the tag */
	if (1 != EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, tag))
		handleErrors();

	/* Clean up */
	EVP_CIPHER_CTX_free(ctx);

	return ciphertext_len;
}


void GenerateFinalKey() {
	//Se recibirían las claves en un formato vector de strings.
	std::vector<std::string> subclaves;

	subclaves = { "98560f952432d0ecbef5e27a91dc4a6f1b401b40feaffa74614e1da4cd74b513" , "59e6515f4f51a7204e0cb28489857902243ab733a1caf4ec3280012d5a64188f" };

	//La variable hash al final del proceso es la clave final.
	string hash = hex_str_to_bin_str(subclaves[0]);

	/*for (int i = 1; i < subclaves.size(); i++) {
		string pal = hex_str_to_bin_str(subclaves[i]);

		for (int i = 0; i < 256; i++) {

			hash[i] = ((hash[i] - '0') ^ (pal[i] - '0')) + '0';
			//obtengo clave final
		}
	}*/


	//A partir de aquí empezaría el cifrado del fichero, pero no termina de funcionar.
	/*int bytes_read, bytes_written;
	unsigned char indata[AES_BLOCK_SIZE];
	unsigned char outdata[AES_BLOCK_SIZE];*/

	/* ckey and ivec are the two 128-bits keys necesary to
	en- and recrypt your data.  Note that ckey can be
	192 or 256 bits as well
	unsigned char ckey[] = "thiskeyisverybad";
	unsigned char ivec[] = "dontusethisinput";*/

	/* data structure that contains the key itself
	AES_KEY key;*/

	/* set the encryption key
	AES_set_encrypt_key(ckey, 128, &key);*/

	/* set where on the 128 bit encrypted block to begin encryption
	int num = 0;

	FILE *ifp = fopen("text.txt", "rb");
	FILE *ofp = fopen("outORIG.txt", "wb");

	while (true) {
		bytes_read = fread(indata, 1, AES_BLOCK_SIZE, ifp);

		AES_cfb128_encrypt(indata, outdata, bytes_read, &key, ivec, &num,
			AES_ENCRYPT); //or AES_DECRYPT

		bytes_written = fwrite(outdata, 1, bytes_read, ofp);
		if (bytes_read < AES_BLOCK_SIZE)
			break;
	}*/
}

