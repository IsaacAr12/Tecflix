#ifndef METADATA_CONTROLADOR_METADATA_HPP
#define METADATA_CONTROLADOR_METADATA_HPP

#include <httplib.h>

#include <cstdint>
#include <string>

#include "servicio_metadata.hpp"

namespace tecflix::metadata {

// Rutas REST del API de metadata (SPEC 7.2). Solo traduce entre HTTP y
// ServicioMetadata; no contiene lógica de negocio.
//
//   GET    /videos?ordenar=titulo|autor|fecha&orden=asc|desc&pagina=1&limite=20   X-Usuario-Id
//   GET    /videos/{id}                                                           X-Usuario-Id
//   POST   /videos                                                                interno
//   PUT    /videos/{id}                                                           X-Usuario-Id (autor)
//   PATCH  /videos/{id}                                                           interno
//   DELETE /videos/{id}                                                           interno
class ControladorMetadata {
public:
    ControladorMetadata(ServicioMetadata& servicio, std::string tokenInterno);

    void registrarRutas(httplib::Server& servidor);

    // Lee los parámetros de GET /videos. Lanza ErrorApi(DatosInvalidos) si son inválidos.
    static ConsultaListado consultaDesdeParametros(const httplib::Request& req, uint32_t solicitanteId);

private:
    ServicioMetadata& servicio_;
    std::string tokenInterno_;
};

}  // namespace tecflix::metadata

#endif  // METADATA_CONTROLADOR_METADATA_HPP
