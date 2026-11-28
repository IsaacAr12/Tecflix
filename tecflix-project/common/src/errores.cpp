#include "tecflix/errores.hpp"

namespace tecflix {

int codigoHttp(TipoError tipo) noexcept {
    switch (tipo) {
        case TipoError::DatosInvalidos:         return 400;
        case TipoError::NoAutenticado:          return 401;
        case TipoError::SinPermiso:             return 403;
        case TipoError::NoEncontrado:           return 404;
        case TipoError::Conflicto:              return 409;
        case TipoError::CuotaExcedida:          return 413;
        case TipoError::ArchivoDemasiadoGrande: return 413;
        case TipoError::ErrorInterno:           return 500;
        case TipoError::ServicioNoDisponible:   return 502;
    }
    return 500;
}

const char* codigoError(TipoError tipo) noexcept {
    switch (tipo) {
        case TipoError::DatosInvalidos:         return "DATOS_INVALIDOS";
        case TipoError::NoAutenticado:          return "NO_AUTENTICADO";
        case TipoError::SinPermiso:             return "SIN_PERMISO";
        case TipoError::NoEncontrado:           return "NO_ENCONTRADO";
        case TipoError::Conflicto:              return "CONFLICTO";
        case TipoError::CuotaExcedida:          return "CUOTA_EXCEDIDA";
        case TipoError::ArchivoDemasiadoGrande: return "ARCHIVO_DEMASIADO_GRANDE";
        case TipoError::ErrorInterno:           return "ERROR_INTERNO";
        case TipoError::ServicioNoDisponible:   return "SERVICIO_NO_DISPONIBLE";
    }
    return "ERROR_INTERNO";
}

int ErrorApi::codigoHttp() const noexcept { return tecflix::codigoHttp(tipo_); }

const char* ErrorApi::codigo() const noexcept { return codigoError(tipo_); }

}  // namespace tecflix
