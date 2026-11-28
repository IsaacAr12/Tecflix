#ifndef STREAMING_CONTROLADOR_STREAMING_HPP
#define STREAMING_CONTROLADOR_STREAMING_HPP

#include <httplib.h>

#include <cstdint>
#include <string>

#include "servicio_streaming.hpp"
#include "tecflix/telemetria.hpp"

namespace tecflix::streaming {

// Rutas REST del API de streaming (SPEC 8.2). Traduce entre HTTP y ServicioStreaming;
// no contiene lógica de negocio. Todas exigen X-Usuario-Id.
//
//   POST   /videos                        multipart: titulo, descripcion, tema, etiquetas, visibilidad y al final archivo
//   GET    /videos/{id}/manifest.mpd      application/dash+xml
//   GET    /videos/{id}/{segmento}.m4s    video/iso.segment, con soporte de Range
//   DELETE /videos/{id}
class ControladorStreaming {
public:
    static constexpr size_t kLargoMaximoCampoTexto = 10 * 1024;

    ControladorStreaming(ServicioStreaming& servicio, uint64_t maximoBytesSubida);

    void registrarRutas(httplib::Server& servidor);

    // "etiquetas" llega como texto JSON, por ejemplo ["btree","disco"].
    static std::vector<std::string> parsearEtiquetas(const std::string& texto);

    // Bytes que realmente se envían, considerando el encabezado Range.
    static uint64_t bytesEnviados(const httplib::Request& req, uint64_t tamanoArchivo);

private:
    void subir(const httplib::Request& req, httplib::Response& res, const httplib::ContentReader& lector);
    void servirArchivo(const httplib::Request& req, httplib::Response& res);

    ServicioStreaming& servicio_;
    uint64_t maximoBytesSubida_;
    Contador segmentosServidos_;
    Contador bytesServidos_;
};

}  // namespace tecflix::streaming

#endif  // STREAMING_CONTROLADOR_STREAMING_HPP
