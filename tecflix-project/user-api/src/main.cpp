#include <iostream>
#include "btree_index.hpp"

int main() {
    // Crear una instancia de BTreeIndex con el archivo "test_indice.idx"
    BTreeIndex indice("test_indice.idx");

    // Insertar los valores10, 20y 30 en el árbol B
    indice.insertar(10);
    indice.insertar(20);
    indice.insertar(30);

    // Buscar el valor 20 en el árbol B y mostrar el resultado en consola
    bool encontrado = indice.buscar(20);
    std::cout << "El valor 20 " << (encontrado ? "fue encontrado" : "no fue encontrado") << " en el árbol B." << std::endl;

    return 0;
}