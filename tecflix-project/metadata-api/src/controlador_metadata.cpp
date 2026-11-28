#include "controlador_metadata.hpp"

#include "tecflix/errores.hpp"
#include "tecflix/utilidades_http.hpp"

namespace tecflix::metadata {

namespace {

int enteroParametro(const httplib::Request& req, const char* nombre, int porDefecto) {
    if (!req.has_param(nombre)) return porDefecto;
    const std::string texto = req.get_param_value(nombre);
    try {
        size_t usados = 0;
        const int valor = std::stoi(texto, &usados);
        if (usados == texto.size()) return valor;
    } catch (const std::exception&) {
        // se reporta abajo como dato inválido
    }
    throw ErrorApi(TipoError::DatosInvalidos, std::string("El parámetro '") + nombre + "' debe ser un entero");
}

}  // namespace

ControladorMetadata::ControladorMetadata(ServicioMetadata& servicio, std::string tokenInterno)
    : servicio_(servicio), tokenInterno_(std::move(tokenInterno)) {}

ConsultaListado ControladorMetadata::consultaDesdeParametros(const httplib::Request& req, uint32_t solicitanteId) {
    ConsultaListado consulta;
    consulta.solicitanteId = solicitanteId;
    consulta.pagina = enteroParametro(req, "pagina", 1);
    consulta.limite = enteroParametro(req, "limite", 20);

    const std::string ordenar = req.has_param("ordenar") ? req.get_param_value("ordenar") : "fecha";
    if (ordenar == "titulo") {
        consulta.orden = CampoOrden::Titulo;
    } else if (ordenar == "autor") {
        consulta.orden = CampoOrden::Autor;
    } else if (ordenar == "fecha") {
        consulta.orden = CampoOrden::Fecha;
    } else {
        throw ErrorApi(TipoError::DatosInvalidos, "ordenar debe ser 'titulo', 'autor' o 'fecha'");
    }

    // Por defecto: la fecha de la más reciente a la más vieja; título y autor de la A a la Z.
    const std::string porDefecto = consulta.orden == CampoOrden::Fecha ? "desc" : "asc";
    const std::string orden = req.has_param("orden") ? req.get_param_value("orden") : porDefecto;
    if (orden != "asc" && orden != "desc") throw ErrorApi(TipoError::DatosInvalidos, "orden debe ser 'asc' o 'desc'");
    consulta.ascendente = orden == "asc";
    return consulta;
}

void ControladorMetadata::registrarRutas(httplib::Server& servidor) {
    servidor.Get("/videos", [this](const httplib::Request& req, httplib::Response& res) {
        const ConsultaListado consulta = consultaDesdeParametros(req, http::usuarioSolicitante(req));
        const PaginaVideos pagina = servicio_.listar(consulta);
        nlohmann::json videos = nlohmann::json::array();
        for (const auto& video : pagina.videos) videos.push_back(video.aJson());
        http::responderJson(res, 200,
                            {{"videos", videos},
                             {"pagina", consulta.pagina},
                             {"limite", consulta.limite},
                             {"total", pagina.total},
                             {"total_paginas", (pagina.total + consulta.limite - 1) / consulta.limite}});
    });

    servidor.Get(R"(/videos/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
        http::responderJson(res, 200, servicio_.obtener(req.matches[1], http::usuarioSolicitante(req)).aJson());
    });

    servidor.Post("/videos", [this](const httplib::Request& req, httplib::Response& res) {
        http::verificarTokenInterno(req, tokenInterno_);
        http::responderJson(res, 201, servicio_.crear(http::parsearObjetoJson(req.body)).aJson());
    });

    servidor.Put(R"(/videos/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
        const uint32_t usuario = http::usuarioSolicitante(req);
        const VideoMetadata video =
            servicio_.actualizarDescriptivos(req.matches[1], usuario, http::parsearObjetoJson(req.body));
        http::responderJson(res, 200, video.aJson());
    });

    servidor.Patch(R"(/videos/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
        http::verificarTokenInterno(req, tokenInterno_);
        const VideoMetadata video = servicio_.actualizarProcesamiento(req.matches[1], http::parsearObjetoJson(req.body));
        http::responderJson(res, 200, video.aJson());
    });

    servidor.Delete(R"(/videos/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
        http::verificarTokenInterno(req, tokenInterno_);
        servicio_.eliminar(req.matches[1]);
        res.status = 204;
    });
}

}  // namespace tecflix::metadata
