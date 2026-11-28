#include "tecflix/configuracion.hpp"

#include <cstdlib>

namespace tecflix {

std::string leerEntorno(const char* nombre, const std::string& porDefecto) {
    const char* valor = std::getenv(nombre);
    return (valor != nullptr && *valor != '\0') ? std::string(valor) : porDefecto;
}

std::string leerEntornoObligatorio(const char* nombre) {
    const std::string valor = leerEntorno(nombre, "");
    if (valor.empty()) throw ErrorConfiguracion(std::string("Falta la variable de entorno obligatoria ") + nombre);
    return valor;
}

int64_t leerEntornoEntero(const char* nombre, int64_t porDefecto) {
    const std::string texto = leerEntorno(nombre, "");
    if (texto.empty()) return porDefecto;
    try {
        size_t usados = 0;
        const long long numero = std::stoll(texto, &usados);
        if (usados == texto.size()) return numero;
    } catch (const std::exception&) {
        // se reporta abajo
    }
    throw ErrorConfiguracion(std::string("La variable de entorno ") + nombre + " debe ser un número entero");
}

}  // namespace tecflix
