#include "ModCifradoSO.h"


#define DRONEFS_READ_BUFFER_SIZE   10240


/*internas */

//añadido Linux
char *_strdup(const char *str) {
    size_t len = strlen(str);
    char *x = (char *)malloc(len+1); /* 1 for the null terminator */
    if(!x) return NULL; /* malloc could not allocate memory */
    memcpy(x,str,len+1); /* copy the string into the new buffer */
    return x;
}

PUCHAR keyGenerate(PCHAR claves) {

	string key(claves);
	string hash = sha256(key);

	PUCHAR hashReturn = (PUCHAR)_strdup(hash.c_str());

	return hashReturn;
}

void init_ctr(struct ctr_state *state, const unsigned char iv[16]) {

	state->num = 0;
	memset(state->ecount, 0, 16);
	memcpy(state->ivec, iv, 16);
}

PUCHAR AES_CTRmodeExecute(PUCHAR file, ULONG fileSize, PUCHAR key, PUCHAR initVector) {

	AES_KEY enc_key;
	AES_set_encrypt_key(key, 256, &enc_key);

	struct ctr_state state;
	init_ctr(&state, initVector);

	PUCHAR cipheredFile = (PUCHAR)malloc(sizeof(UCHAR)* (fileSize + 1));

	ULONG filePointer = 0;
	while (filePointer < fileSize) {
		AES_ctr128_encrypt(file + filePointer, cipheredFile + filePointer, fileSize, &enc_key, state.ivec, state.ecount, &state.num);
		filePointer += AES_BLOCK_SIZE;
	}

	cipheredFile[fileSize] = 0;

	return cipheredFile;
}

PUCHAR cipherExecute(PUCHAR file, ULONG fileSize, PUCHAR key, PUCHAR initVector, string ciphAlg, string ciphMode) {

	if (ciphAlg == "AES" && ciphMode == "CTR") {
		PUCHAR cipherReturn = (PUCHAR)malloc(sizeof(UCHAR)* (fileSize + 1));
		cipherReturn = AES_CTRmodeExecute(file, fileSize, key, initVector);
		return cipherReturn;
	}
}

/*externas*/
CIBERDRONECIPHER_API PUCHAR getInitVector() {

	UCHAR iv[AES_BLOCK_SIZE];
	RAND_bytes(iv, AES_BLOCK_SIZE);

	return iv;
}

//añadido por error: 'stoul' is not a member of 'std' ULONG ciphLength = std::stoul(ciphLengthStr);
unsigned long my_stoul (std::string const& str, size_t *idx = 0, int base = 10) {
    char *endp;
    unsigned long value = strtoul(str.c_str(), &endp, base);
    if (endp == str.c_str()) {
        throw std::invalid_argument("my_stoul");
    }
    if (value == ULONG_MAX && errno == ERANGE) {
        throw std::out_of_range("my_stoul");
    }
    if (idx) {
        *idx = endp - str.c_str();
    }
    return value;
}

CIBERDRONECIPHER_API PUCHAR protectExecute(PUCHAR file, ULONG fileSize, PUCHAR ciphData, PUCHAR initVector, PCHAR subKeys) {
	
	string ciphDataStr((PCHAR)ciphData);
	string delimiter = "-";
	size_t pos = 0;
	int tokenPos = 0;
	std::string tokens[3];
	while ((pos = ciphDataStr.find(delimiter)) != std::string::npos) {
		tokens[tokenPos] = ciphDataStr.substr(0, pos);
		ciphDataStr.erase(0, pos + delimiter.length());
		tokenPos++;
	}
	tokens[tokenPos] = ciphDataStr;

	string ciphAlgStr = tokens[0];
	string ciphLengthStr = tokens[1];
	string ciphModeStr = tokens[2];

	ULONG ciphLength = my_stoul(ciphLengthStr);

	//Get final key
	PUCHAR key = keyGenerate(subKeys);

	//Get init vector
	UCHAR iv[AES_BLOCK_SIZE];
	memcpy(iv, initVector, sizeof(iv));

	//Cipher return
	PUCHAR cipherModuleReturn = (PUCHAR)malloc(sizeof(UCHAR)* (fileSize + 1));
	PUCHAR cipheredFile = cipherExecute(file, fileSize, key, iv, ciphAlgStr, ciphModeStr);

	memcpy(cipherModuleReturn, cipheredFile, fileSize);
	cipherModuleReturn[fileSize] = 0;

	return cipherModuleReturn;
}



