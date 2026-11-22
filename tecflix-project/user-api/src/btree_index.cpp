#include "btree_index.hpp"
namespace tecflix {
// Constructor de la clase BTreeIndex
BTreeIndex::BTreeIndex(const char* nombreArchivo) : archivoIndice(nombreArchivo, std::ios::in | std::ios::out | std::ios::binary) {
    if (!archivoIndice.is_open()) {
        archivoIndice.open(nombreArchivo, std::ios::out | std::ios::binary);
        archivoIndice.close();
        archivoIndice.open(nombreArchivo, std::ios::in | std::ios::out | std::ios::binary);
        raiz = 0;
        NodoBTree nuevoNodo;
        nuevoNodo.numClaves = 0;
        for (int i = 0; i < ORDEN_M; ++i) {
            nuevoNodo.hijos[i] = 0;
        }
        escribirNodo(nuevoNodo, raiz);
    } else {
        archivoIndice.seekg(0);
        archivoIndice.read(reinterpret_cast<char*>(&raiz), sizeof(raiz));
    }
}

// Destructor de la clase BTreeIndex
BTreeIndex::~BTreeIndex() {
    archivoIndice.seekp(0);
    archivoIndice.write(reinterpret_cast<const char*>(&raiz), sizeof(raiz));
    archivoIndice.close();
}

// Función para buscar una clave en el árbol B
uint64_t BTreeIndex::buscar(uint32_t clave) {
    uint64_t posicionActual = raiz;
    while (posicionActual != 0) {
        NodoBTree nodo;
        leerNodo(nodo, posicionActual);
            int i = 0;
        while (i < nodo.numClaves && clave > nodo.elementos[i].id_usuario) {
                ++i;
            }
        if (i < nodo.numClaves && clave == nodo.elementos[i].id_usuario) {
            return nodo.elementos[i].byte_offset;
        }
        posicionActual = nodo.hijos[i];
    }
    return 0; // Clave no encontrada
}

// Función para insertar una clave en el árbol B
void BTreeIndex::insertar(uint32_t id_usuario, uint64_t data_offset) {
    if (raiz == 0) {
        // Crear un nuevo nodo raíz
        NodoBTree nuevoNodo;
        nuevoNodo.numClaves = 1;
        nuevoNodo.elementos[0] = {id_usuario, data_offset};
        for (int i = 1; i < ORDEN_M; ++i) {
            nuevoNodo.hijos[i] = 0;
        }
        escribirNodo(nuevoNodo, raiz);
        } else {
        NodoBTree raizNodo;
        leerNodo(raizNodo, raiz);
        if (raizNodo.numClaves == MAX_CLAVES) {
            // El nodo raíz está lleno, debemos dividirlo
            uint64_t nuevaRaizPosicion = archivoIndice.tellp();
            NodoBTree nuevaRaiz;
            nuevaRaiz.numClaves = 0;
            nuevaRaiz.hijos[0] = raiz;
            dividirNodo(nuevaRaizPosicion,0, raiz, raizNodo);
            insertarNoLleno(nuevaRaizPosicion, id_usuario, data_offset);
            raiz = nuevaRaizPosicion;
        } else {
            insertarNoLleno(raiz, id_usuario, data_offset);
    }
}
}

// Función para dividir un nodo en dos cuando está lleno
void BTreeIndex::dividirNodo(uint64_t posicionPadre, int indiceHijo, uint64_t posicionHijo, NodoBTree& nodoHijo) {
    NodoBTree nuevoNodo;
    nuevoNodo.numClaves = MAX_CLAVES /2;

    for (int j = 0; j < MAX_CLAVES / 2; ++j) {
        nuevoNodo.elementos[j] = nodoHijo.elementos[MAX_CLAVES / 2 + 1 + j];
    }

    if (!nodoHijo.hijos.empty()) {
        for (int j = 0; j <= MAX_CLAVES / 2; ++j) {
            nuevoNodo.hijos[j] = nodoHijo.hijos[MAX_CLAVES / 2 + 1 + j];
        }
    }

    nodoHijo.numClaves = MAX_CLAVES / 2;

    NodoBTree padreNodo;
    leerNodo(padreNodo, posicionPadre);

    for (int j = padreNodo.numClaves; j >= indiceHijo + 1; --j) {
        padreNodo.hijos[j + 1] = padreNodo.hijos[j];
    }

    padreNodo.hijos[indiceHijo + 1] = archivoIndice.tellp();

    for (int j = padreNodo.numClaves - 1; j >= indiceHijo; --j) {
        padreNodo.elementos[j + 1] = padreNodo.elementos[j];
    }

    padreNodo.elementos[indiceHijo] = nodoHijo.elementos[MAX_CLAVES / 2];

    padreNodo.numClaves++;

    escribirNodo(padreNodo, posicionPadre);
    escribirNodo(nodoHijo, posicionHijo);
    escribirNodo(nuevoNodo, archivoIndice.tellp());
}

// Función auxiliar para insertar una clave en un nodo que no está lleno
void BTreeIndex::insertarNoLleno(uint64_t nodoPosicion, uint32_t id_usuario, uint64_t data_offset) {
    NodoBTree nodo;
    leerNodo(nodo, nodoPosicion);

    int i = nodo.numClaves - 1;

    if (nodo.hijos[0] == 0) {
        // Es un nodo hoja
        while (i >= 0 && nodo.elementos[i].id_usuario > id_usuario) {
            nodo.elementos[i + 1] = nodo.elementos[i];
            --i;
        }
        nodo.elementos[i + 1] = {id_usuario, data_offset};
        ++nodo.numClaves;
    escribirNodo(nodo, nodoPosicion);
    } else {
        // No es un nodo hoja
        while (i >= 0 && nodo.elementos[i].id_usuario > id_usuario) {
            --i;
}
        ++i;
        NodoBTree hijoNodo;
        leerNodo(hijoNodo, nodo.hijos[i]);
        if (hijoNodo.numClaves == MAX_CLAVES) {
            dividirNodo(nodoPosicion, i, nodo.hijos[i], hijoNodo);
            if (nodo.elementos[i].id_usuario < id_usuario) {
                ++i;
            }
        }
        insertarNoLleno(nodo.hijos[i], id_usuario, data_offset);
    }
}

// Función para leer un nodo desde el archivo
void BTreeIndex::leerNodo(NodoBTree& nodo, uint64_t posicion) {
    archivoIndice.seekg(posicion);
    archivoIndice.read(reinterpret_cast<char*>(&nodo), sizeof(nodo));
}

// Función para escribir un nodo en el archivo
void BTreeIndex::escribirNodo(const NodoBTree& nodo, uint64_t posicion) {
    archivoIndice.seekp(posicion);
    archivoIndice.write(reinterpret_cast<const char*>(&nodo), sizeof(nodo));
}

} // namespace tecflix
