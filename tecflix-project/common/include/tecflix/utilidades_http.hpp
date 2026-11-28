#ifndef TECFLIX_UTILIDADES_HTTP_HPP
#define TECFLIX_UTILIDADES_HTTP_HPP

#include <httplib.h>

#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>

#include "tecflix/errores.hpp"

namespace tecflix {

// Funciones que usan los controladores de los tres APIs.
namespace http {

void responderJson(httplib::Response& res, int estado, const nlohmann::json& cuerpo);

// Cuerpo de error del SPEC 6.4: {"error": {"codigo": "...", "mensaje": "..."}}.
void responderError(httplib::Response& res, const ErrorApi& error);

// El cuerpo debe ser un objeto JSON; si no, lanza ErrorApi(DatosInvalidos).
nlohmann::json parsearObjetoJson(const std::string& cuerpo);

// X-Usuario-Id: entero entre 1 y 4294967295. Si falta o es inválido, lanza ErrorApi(NoAutenticado).
uint32_t usuarioSolicitante(const httplib::Request& req);

// Convierte texto en id de usuario; devuelve false si no es un entero entre 1 y 4294967295.
bool parsearIdUsuario(const std::string& texto, uint32_t& id);

// X-Token-Interno. tieneTokenInterno compara en tiempo constante; verificarTokenInterno
// lanza ErrorApi(NoAutenticado) si no coincide.
bool tieneTokenInterno(const httplib::Request& req, const std::string& tokenEsperado);
void verificarTokenInterno(const httplib::Request& req, const std::string& tokenEsperado);

// Ruta con los ids reemplazados (/videos/656f...a1 -> /videos/:id) para etiquetar
// métricas y spans sin crear una serie por video.
std::string normalizarRuta(const std::string& ruta);

}  // namespace http
}  // namespace tecflix

#endif  // TECFLIX_UTILIDADES_HTTP_HPP
