#ifndef TECFLIX_CONFIGURACION_HPP
#define TECFLIX_CONFIGURACION_HPP

#include <cstdint>
#include <stdexcept>
#include <string>

namespace tecflix {

// Falta una variable de entorno obligatoria o tiene un valor inválido.
// main() la atrapa y termina el proceso con un mensaje claro (SPEC 6.2).
class ErrorConfiguracion : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Lectura de variables de entorno. Ninguna URL, puerto o secreto se escribe en el código.
std::string leerEntorno(const char* nombre, const std::string& porDefecto);
std::string leerEntornoObligatorio(const char* nombre);
int64_t leerEntornoEntero(const char* nombre, int64_t porDefecto);

}  // namespace tecflix

#endif  // TECFLIX_CONFIGURACION_HPP
