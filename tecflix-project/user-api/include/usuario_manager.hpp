#ifndef USUARIO_MANAGER_HPP
#define USUARIO_MANAGER_HPP

#include <cstdint>
#include <array>
#include <fstream>
#include <cstring>
#include <algorithm>
#include <cstdio>

namespace tecflix {

// Define constants for fixed sizes
constexpr size_t MAX_USERNAME_LENGTH = 32;
constexpr size_t MAX_EMAIL_LENGTH = 64;
constexpr size_t ORDEN_M = 5;
constexpr size_t MAX_CLAVES = ORDEN_M - 1;

// Fixed-size structure for User
struct Usuario {
    uint64_t id; // Identificador único del usuario
    char username[MAX_USERNAME_LENGTH]; // Nombre de usuario con tamaño fijo
    char email[MAX_EMAIL_LENGTH]; // Email con tamaño fijo
    uint8_t age; // Edad del usuario
    bool isActive; // Estado de la cuenta del usuario

    // Constructor para inicializar la estructura de usuario
    Usuario(uint64_t _id = 0, const char* _username = "", const char* _email = "", uint8_t _age = 0, bool _isActive = false)
        : id(_id), age(_age), isActive(_isActive) {
        // Inicializa username y email con cadenas vacías
        std::fill(std::begin(username), std::end(username), '\0');
        std::fill(std::begin(email), std::end(email), '\0');

        // Copia el nombre de usuario y el correo electrónico proporcionados en los arreglos de tamaño fijo
        if (_username) {
            std::copy_n(_username, std::min(strlen(_username), MAX_USERNAME_LENGTH - 1), username);
        }
        if (_email) {
            std::copy_n(_email, std::min(strlen(_email), MAX_EMAIL_LENGTH - 1), email);
        }
    }

    // Función para imprimir los detalles del usuario (para fines de depuración)
    void print() const {
        printf("ID: %lu\n", id);
        printf("Username: %s\n", username);
        printf("Email: %s\n", email);
        printf("Age: %u\n", age);
        printf("Is Active: %s\n", isActive ? "true" : "false");
    }
};

// Estructura del nodo del Árbol B
struct NodoBTree {
    uint32_t numClaves; // Cantidad actual de claves activas en el nodo
    std::array<uint32_t, MAX_CLAVES> claves; // Arreglo estático para los identificadores de usuario
    std::array<uint64_t, ORDEN_M> hijos; // Arreglo estático para las posiciones de los nodos hijos en el archivo

    // Constructor para inicializar el nodo
    NodoBTree() : numClaves(0) {
        claves.fill(0);
        hijos.fill(0);
    }
};

class BTreeIndex {
public:
    BTreeIndex(const std::string& indiceFilePath) : archivoIndice(indiceFilePath, std::ios::in | std::ios::out | std::ios::binary) {
        if (!archivoIndice.is_open()) {
            archivoIndice.open(indiceFilePath, std::ios::out | std::ios::binary);
            archivoIndice.close();
            archivoIndice.open(indiceFilePath, std::ios::in | std::ios::out | std::ios::binary);
        }
        if (archivoIndice.peek() == std::ifstream::traits_type::eof()) {
            raiz = 0;
            NodoBTree nodoRaiz;
            escribirNodo(nodoRaiz, raiz);
        } else {
            archivoIndice.read(reinterpret_cast<char*>(&raiz), sizeof(raiz));
        }
    }

    void insertar(uint32_t clave, uint64_t byte_offset) {
        NodoBTree nodoRaiz;
        leerNodo(nodoRaiz, raiz);
        if (nodoRaiz.numClaves == MAX_CLAVES) {
            uint64_t nuevaRaiz = archivoIndice.tellp();
            NodoBTree nuevoNodo;
            nuevoNodo.hijos[0] = raiz;
            dividirNodo(nuevoNodo, 0, nodoRaiz);
            escribirNodo(nuevoNodo, nuevaRaiz);
            insertarNoLleno(nuevoNodo, clave, byte_offset);
            raiz = nuevaRaiz;
            archivoIndice.seekp(0);
            archivoIndice.write(reinterpret_cast<const char*>(&raiz), sizeof(raiz));
        } else {
            insertarNoLleno(nodoRaiz, clave, byte_offset);
            escribirNodo(nodoRaiz, raiz);
        }
    }

private:
    std::fstream archivoIndice;
    uint64_t raiz;

    void leerNodo(NodoBTree& nodo, uint64_t offset) {
        archivoIndice.seekg(offset);
        archivoIndice.read(reinterpret_cast<char*>(&nodo), sizeof(NodoBTree));
    }

    void escribirNodo(const NodoBTree& nodo, uint64_t offset) {
        archivoIndice.seekp(offset);
        archivoIndice.write(reinterpret_cast<const char*>(&nodo), sizeof(NodoBTree));
    }

    void insertarNoLleno(NodoBTree& nodo, uint32_t clave, uint64_t byte_offset) {
        int i = nodo.numClaves - 1;
        if (nodo.hijos[0] == 0) { // Si es un nodo hoja
            while (i >= 0 && clave < nodo.claves[i]) {
                nodo.claves[i + 1] = nodo.claves[i];
                i--;
            }
            nodo.claves[i + 1] = clave;
            nodo.hijos[i + 1] = byte_offset;
            nodo.numClaves++;
        } else { // Si no es un nodo hoja
            while (i >= 0 && clave < nodo.claves[i]) {
                i--;
            }
            i++;
            NodoBTree nodoHijo;
            leerNodo(nodoHijo, nodo.hijos[i]);
            if (nodoHijo.numClaves == MAX_CLAVES) {
                dividirNodo(nodo, i, nodoHijo);
                if (clave > nodo.claves[i]) {
                    leerNodo(nodoHijo, nodo.hijos[i + 1]);
                }
            }
            insertarNoLleno(nodoHijo, clave, byte_offset);
            escribirNodo(nodoHijo, nodo.hijos[i]);
        }
    }

    void dividirNodo(NodoBTree& nodoPadre, int i, NodoBTree& nodoHijo) {
        archivoIndice.seekp(0, std::ios::end);
        uint64_t nuevaPosicion = archivoIndice.tellp();
        NodoBTree nuevoNodo;
        nuevoNodo.numClaves = MAX_CLAVES / 2;

        for (int j = 0; j < MAX_CLAVES / 2; j++) {
            nuevoNodo.claves[j] = nodoHijo.claves[j + MAX_CLAVES / 2 + 1];
        }

        if (nodoHijo.hijos[0] != 0) {
            for (int j = 0; j <= MAX_CLAVES / 2; j++) {
                nuevoNodo.hijos[j] = nodoHijo.hijos[j + MAX_CLAVES / 2 + 1];
            }
        }

        nodoHijo.numClaves = MAX_CLAVES / 2;

        for (int j = nodoPadre.numClaves; j >= i + 1; j--) {
            nodoPadre.hijos[j + 1] = nodoPadre.hijos[j];
        }

        nodoPadre.hijos[i + 1] = nuevaPosicion;

        for (int j = nodoPadre.numClaves - 1; j >= i; j--) {
            nodoPadre.claves[j + 1] = nodoPadre.claves[j];
        }

        nodoPadre.claves[i] = nodoHijo.claves[MAX_CLAVES / 2];

        nodoPadre.numClaves++;

        escribirNodo(nodoHijo, nodoPadre.hijos[i]);
        escribirNodo(nuevoNodo, nodoPadre.hijos[i + 1]);
    }
};

class UsuarioManager {
public:
    UsuarioManager(const std::string& datosFilePath, const std::string& indiceFilePath)
        : archivoDatos(datosFilePath, std::ios::in | std::ios::out | std::ios::app | std::ios::binary),
          indice(indiceFilePath) {
        if (!archivoDatos.is_open()) {
            archivoDatos.open(datosFilePath, std::ios::out | std::ios::binary);
            archivoDatos.close();
            archivoDatos.open(datosFilePath, std::ios::in | std::ios::out | std::ios::app | std::ios::binary);
        }
    }

    void registrarUsuario(const Usuario& usuario) {
        archivoDatos.seekp(0, std::ios::end);
        uint64_t byte_offset = archivoDatos.tellp();
        archivoDatos.write(reinterpret_cast<const char*>(&usuario), sizeof(Usuario));
        indice.insertar(usuario.id, byte_offset);
    }

private:
    std::fstream archivoDatos;
    BTreeIndex indice;
};

} // namespace tecflix

#endif // USUARIO_MANAGER_HPP