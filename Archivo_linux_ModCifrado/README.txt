#Librería .so

#Descargar e instalar la librería openssl
#Descargar de drive los archivos de la carpeta archivos_linux sha256.h sha256.cpp ModCifradoSO.h ModCifrado.cpp
cd /(Path de la carpeta con los archivos)
g++ -fPIC -c -o ModCifradoSO.o ModCifradoSO.cpp -lcrypto -std=c++11
g++ -fPIC -c -o sha256.o sha256.cpp -lcrypto -std=c++11
g++ -shared -fPIC -o libModCifrado_Linux.so ModCifradoSO.o sha256.h ModCifradoSO.h sha256.h -std=c++11


#Probar librería

//sudo cp libModCifrado_Linux.so /usr/lib #también puede hacerse un link a la librería o indicar en que carpeta se encuentra la librería utilizando la opción -L
g++ -o "ModCifrado_ejecutable" ModCifrado.cpp -L. -lModCifrado_Linux -lcrypto -std=c++11


#Fuentes
#http://arco.esi.uclm.es/~dvilla/doc/repo/librerias/librerias.html