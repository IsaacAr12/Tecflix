#include "cliente_metadata.hpp"

#include "gestor_almacenamiento.hpp"

namespace tecflix::streaming {

namespace {

// Los ids se validan antes de armar la URL.
void exigirIdValido(const std::string& id) {
    if (!GestorAlmacenamiento::idValido(id)) throw ErrorApi(TipoError::NoEncontrado, "El video no existe");
}

}  // namespace

ClienteMetadataHttp::ClienteMetadataHttp(std::string url, std::string tokenInterno)
    : http_(std::move(url), std::move(tokenInterno), "API de metadata") {}

std::string ClienteMetadataHttp::crear(const nlohmann::json& datos) {
    const httplib::Response res = http_.post("/videos", datos);
    if (res.status != 201) throw http_.errorDeRespuesta(res, "crear video");
    const nlohmann::json cuerpo = nlohmann::json::parse(res.body, nullptr, false);
    if (!cuerpo.is_object() || !cuerpo.contains("id") || !cuerpo["id"].is_string()) {
        throw ErrorApi(TipoError::ServicioNoDisponible, "El API de metadata no devolvió el id del video");
    }
    return cuerpo["id"].get<std::string>();
}

std::optional<InfoVideo> ClienteMetadataHttp::obtener(const std::string& id, uint32_t usuarioId) {
    exigirIdValido(id);
    const httplib::Response res = http_.get("/videos/" + id, {{"X-Usuario-Id", std::to_string(usuarioId)}});
    if (res.status == 404) return std::nullopt;
    if (res.status != 200) throw http_.errorDeRespuesta(res, "obtener video");
    const nlohmann::json cuerpo = nlohmann::json::parse(res.body, nullptr, false);
    if (!cuerpo.is_object()) throw ErrorApi(TipoError::ServicioNoDisponible, "Respuesta ilegible del API de metadata");
    InfoVideo info;
    info.id = cuerpo.value("id", id);
    info.autorId = cuerpo.value("autor_id", 0u);
    info.estado = cuerpo.value("estado", "");
    info.tamanoOriginalBytes = cuerpo.value("tamano_original_bytes", int64_t{0});
    return info;
}

void ClienteMetadataHttp::actualizarProcesamiento(const std::string& id, const nlohmann::json& cambios) {
    exigirIdValido(id);
    const httplib::Response res = http_.patch("/videos/" + id, cambios);
    if (res.status != 200) throw http_.errorDeRespuesta(res, "actualizar procesamiento");
}

void ClienteMetadataHttp::eliminar(const std::string& id) {
    exigirIdValido(id);
    const httplib::Response res = http_.eliminar("/videos/" + id);
    if (res.status != 204 && res.status != 404) throw http_.errorDeRespuesta(res, "eliminar video");
}

}  // namespace tecflix::streaming
