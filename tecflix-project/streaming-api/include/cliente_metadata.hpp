#ifndef STREAMING_CLIENTE_METADATA_HPP
#define STREAMING_CLIENTE_METADATA_HPP

#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>

#include "tecflix/cliente_http_interno.hpp"

namespace tecflix::streaming {

// Lo que el API de streaming necesita saber de un video.
struct InfoVideo {
    std::string id;
    uint32_t autorId = 0;
    std::string estado;  // procesando | listo | error
    int64_t tamanoOriginalBytes = 0;
};

// Comunicación con el API de metadata (SPEC 7.2). Interfaz para poder simularla en las pruebas.
class IClienteMetadata {
public:
    virtual ~IClienteMetadata() = default;

    // POST /videos (interno). Devuelve el id del video creado.
    virtual std::string crear(const nlohmann::json& datos) = 0;

    // GET /videos/{id} con el X-Usuario-Id del solicitante. Metadata aplica la regla de
    // visibilidad: nullopt si no existe o el usuario no lo puede ver (SPEC 6.7).
    virtual std::optional<InfoVideo> obtener(const std::string& id, uint32_t usuarioId) = 0;

    // PATCH /videos/{id} (interno).
    virtual void actualizarProcesamiento(const std::string& id, const nlohmann::json& cambios) = 0;

    // DELETE /videos/{id} (interno). Si ya no existía no es un error.
    virtual void eliminar(const std::string& id) = 0;
};

class ClienteMetadataHttp : public IClienteMetadata {
public:
    ClienteMetadataHttp(std::string url, std::string tokenInterno);

    std::string crear(const nlohmann::json& datos) override;
    std::optional<InfoVideo> obtener(const std::string& id, uint32_t usuarioId) override;
    void actualizarProcesamiento(const std::string& id, const nlohmann::json& cambios) override;
    void eliminar(const std::string& id) override;

private:
    ClienteHttpInterno http_;
};

}  // namespace tecflix::streaming

#endif  // STREAMING_CLIENTE_METADATA_HPP
