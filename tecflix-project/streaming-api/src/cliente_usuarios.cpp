#include "cliente_usuarios.hpp"

#include <nlohmann/json.hpp>

namespace tecflix::streaming {

namespace {

nlohmann::json cuerpoObjeto(const httplib::Response& res) {
    nlohmann::json cuerpo = nlohmann::json::parse(res.body, nullptr, false);
    if (!cuerpo.is_object()) throw ErrorApi(TipoError::ServicioNoDisponible, "Respuesta ilegible del API de usuarios");
    return cuerpo;
}

uint64_t leerBytes(const nlohmann::json& cuerpo, const char* campo) {
    if (!cuerpo.contains(campo) || !cuerpo[campo].is_number_integer()) {
        throw ErrorApi(TipoError::ServicioNoDisponible, std::string("El API de usuarios no devolvió '") + campo + "'");
    }
    return cuerpo[campo].get<uint64_t>();
}

httplib::Headers comoUsuario(uint32_t usuarioId) { return {{"X-Usuario-Id", std::to_string(usuarioId)}}; }

}  // namespace

ClienteUsuariosHttp::ClienteUsuariosHttp(std::string url, std::string tokenInterno)
    : http_(std::move(url), std::move(tokenInterno), "API de usuarios") {}

InfoCuota ClienteUsuariosHttp::obtenerCuota(uint32_t usuarioId) {
    const std::string ruta = "/usuarios/" + std::to_string(usuarioId) + "/cuota";
    const httplib::Response res = http_.get(ruta, comoUsuario(usuarioId));
    if (res.status != 200) throw http_.errorDeRespuesta(res, "consultar cuota");
    const nlohmann::json cuerpo = cuerpoObjeto(res);
    return {leerBytes(cuerpo, "cuota_bytes"), leerBytes(cuerpo, "usado_bytes"), leerBytes(cuerpo, "disponible_bytes")};
}

std::string ClienteUsuariosHttp::obtenerNombre(uint32_t usuarioId) {
    const httplib::Response res = http_.get("/usuarios/" + std::to_string(usuarioId), comoUsuario(usuarioId));
    if (res.status != 200) throw http_.errorDeRespuesta(res, "consultar usuario");
    const nlohmann::json cuerpo = cuerpoObjeto(res);
    if (!cuerpo.contains("nombre_usuario") || !cuerpo["nombre_usuario"].is_string()) {
        throw ErrorApi(TipoError::ServicioNoDisponible, "El API de usuarios no devolvió 'nombre_usuario'");
    }
    return cuerpo["nombre_usuario"].get<std::string>();
}

void ClienteUsuariosHttp::registrarUso(uint32_t usuarioId, int64_t deltaBytes) {
    const std::string ruta = "/usuarios/" + std::to_string(usuarioId) + "/uso";
    const httplib::Response res = http_.post(ruta, {{"delta_bytes", deltaBytes}});
    if (res.status != 200 && res.status != 204) throw http_.errorDeRespuesta(res, "registrar uso");
}

}  // namespace tecflix::streaming
