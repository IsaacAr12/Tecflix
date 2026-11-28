#ifndef TECFLIX_ERRORES_HPP
#define TECFLIX_ERRORES_HPP

#include <stdexcept>
#include <string>

namespace tecflix {

// Tipos de error de la tabla de la sección 6.4 del SPEC. La lógica de negocio lanza
// ErrorApi y la capa HTTP lo traduce a {"error": {"codigo", "mensaje"}}.
enum class TipoError {
    DatosInvalidos,         // 400
    NoAutenticado,          // 401
    SinPermiso,             // 403
    NoEncontrado,           // 404
    Conflicto,              // 409
    CuotaExcedida,          // 413
    ArchivoDemasiadoGrande, // 413
    ErrorInterno,           // 500
    ServicioNoDisponible    // 502
};

class ErrorApi : public std::runtime_error {
public:
    ErrorApi(TipoError tipo, const std::string& mensaje) : std::runtime_error(mensaje), tipo_(tipo) {}

    TipoError tipo() const noexcept { return tipo_; }
    int codigoHttp() const noexcept;
    const char* codigo() const noexcept;

private:
    TipoError tipo_;
};

int codigoHttp(TipoError tipo) noexcept;
const char* codigoError(TipoError tipo) noexcept;

}  // namespace tecflix

#endif  // TECFLIX_ERRORES_HPP
