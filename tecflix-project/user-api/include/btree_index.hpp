#ifndef BTREE_INDEX_HPP
#define BTREE_INDEX_HPP
#include <fstream>
#include <array>
namespace tecflix {

// Define constantes para tamaños fijos
constexpr size_t ORDEN_M = 5;
constexpr size_t MAX_CLAVES = ORDEN_M - 1;

// Estructura ParClaveValor para asociar 'id_usuario' con su 'byte_offset'
struct ParClaveValor {
    uint32_t id_usuario;
    uint64_t byte_offset;
};

// Estructura para un nodo del árbol B
struct NodoBTree {
    uint32_t numClaves;
    std::array<ParClaveValor, MAX_CLAVES> elementos;
    std::array<uint64_t, ORDEN_M> hijos;
};

class BTreeIndex {
public:
    // Constructor de la clase BTreeIndex
    BTreeIndex(const char* nombreArchivo);

    // Destructor de la clase BTreeIndex
    ~BTreeIndex();

    // Función para buscar una clave en el árbol B
    uint64_t buscar(uint32_t clave);

    // Función para insertar una clave en el árbol B
    void insertar(uint32_t id_usuario, uint64_t data_offset);

private:
    std::fstream archivoIndice;
    uint64_t raiz;

    // Función para dividir un nodo en dos cuando está lleno
    void dividirNodo(uint64_t posicionPadre, int indiceHijo, NodoBTree& nodoHijo);

    // Función auxiliar para insertar una clave en un nodo que no está lleno
    void insertarNoLleno(uint64_t nodoPosicion, uint32_t id_usuario, uint64_t data_offset);
    // Función para leer un nodo desde el archivo
    void leerNodo(NodoBTree& nodo, uint64_t posicion);

    // Función para escribir un nodo en el archivo
    void escribirNodo(const NodoBTree& nodo, uint64_t posicion);
};

} // namespace tecflix

#endif // BTREE_INDEX_HPP
