#include "tecflix/utilidades_http.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace tecflix::http {

namespace {

bool esHexadecimal(const std::string& s) {
    return std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isxdigit(c) != 0; });
}

bool esNumero(const std::string& s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
}

}  // namespace

void responderJson(httplib::Response& res, int estado, const nlohmann::json& cuerpo) {
    res.status = estado;
    res.set_content(cuerpo.dump(), "application/json; charset=utf-8");
}

void responderError(httplib::Response& res, const ErrorApi& error) {
    responderJson(res, error.codigoHttp(), {{"error", {{"codigo", error.codigo()}, {"mensaje", error.what()}}}});
}

nlohmann::json parsearObjetoJson(const std::string& cuerpo) {
    nlohmann::json datos = nlohmann::json::parse(cuerpo, nullptr, false);
    if (datos.is_discarded() || !datos.is_object()) {
        throw ErrorApi(TipoError::DatosInvalidos, "El cuerpo debe ser un objeto JSON válido");
    }
    return datos;
}

bool parsearIdUsuario(const std::string& texto, uint32_t& id) {
    if (!esNumero(texto) || texto.size() > 10) return false;
    const unsigned long long valor = std::stoull(texto);
    if (valor == 0 || valor > 4294967295ULL) return false;
    id = static_cast<uint32_t>(valor);
    return true;
}

uint32_t usuarioSolicitante(const httplib::Request& req) {
    if (!req.has_header("X-Usuario-Id")) {
        throw ErrorApi(TipoError::NoAutenticado, "Falta el encabezado X-Usuario-Id");
    }
    uint32_t id = 0;
    if (!parsearIdUsuario(req.get_header_value("X-Usuario-Id"), id)) {
        throw ErrorApi(TipoError::NoAutenticado, "X-Usuario-Id debe ser un entero entre 1 y 4294967295");
    }
    return id;
}

bool tieneTokenInterno(const httplib::Request& req, const std::string& tokenEsperado) {
    const std::string recibido = req.get_header_value("X-Token-Interno");
    if (tokenEsperado.empty() || recibido.size() != tokenEsperado.size()) return false;
    // Tiempo constante: se recorren todos los caracteres aunque uno ya no coincida,
    // para no revelar cuántos caracteres del token adivinó un atacante.
    unsigned char diferencia = 0;
    for (size_t i = 0; i < recibido.size(); ++i) {
        diferencia |= static_cast<unsigned char>(recibido[i] ^ tokenEsperado[i]);
    }
    return diferencia == 0;
}

void verificarTokenInterno(const httplib::Request& req, const std::string& tokenEsperado) {
    if (!tieneTokenInterno(req, tokenEsperado)) {
        throw ErrorApi(TipoError::NoAutenticado, "X-Token-Interno inválido o ausente");
    }
}

std::string normalizarRuta(const std::string& ruta) {
    std::stringstream entrada(ruta);
    std::string segmento;
    std::string resultado;
    while (std::getline(entrada, segmento, '/')) {
        if (segmento.empty()) continue;
        if ((segmento.size() == 24 && esHexadecimal(segmento)) || esNumero(segmento)) {
            segmento = ":id";
        } else if (segmento.find('.') != std::string::npos) {
            segmento = ":archivo";
        }
        resultado += "/" + segmento;
    }
    return resultado.empty() ? "/" : resultado;
}

}  // namespace tecflix::http
