#include "tecflix/servidor_http.hpp"

#include <chrono>
#include <iostream>
#include <memory>

#include "opentelemetry/trace/context.h"
#include "opentelemetry/trace/scope.h"
#include "opentelemetry/trace/span.h"
#include "tecflix/errores.hpp"
#include "tecflix/utilidades_http.hpp"

namespace tecflix {

namespace api_trace = opentelemetry::trace;
namespace nostd = opentelemetry::nostd;

namespace {

// httplib atiende cada petición de principio a fin en un mismo hilo del pool, así que
// el span y la hora de inicio se guardan por hilo entre el pre-routing y el logger.
struct EstadoPeticion {
    std::chrono::steady_clock::time_point inicio;
    nostd::shared_ptr<api_trace::Span> span;
    std::unique_ptr<api_trace::Scope> alcance;
};
thread_local EstadoPeticion peticionActual;

void cerrarSpanActual() {
    peticionActual.alcance.reset();
    if (peticionActual.span) {
        peticionActual.span->End();
        peticionActual.span = nullptr;
    }
}

TipoError tipoPorEstado(int estado) {
    switch (estado) {
        case 400: return TipoError::DatosInvalidos;
        case 404: return TipoError::NoEncontrado;
        case 413: return TipoError::ArchivoDemasiadoGrande;
        default:  return TipoError::ErrorInterno;
    }
}

}  // namespace

ServidorHttp::ServidorHttp(std::string servicio, std::string version, std::string origenPermitido)
    : servicio_(std::move(servicio)),
      version_(std::move(version)),
      origenPermitido_(std::move(origenPermitido)),
      peticiones_("http_peticiones_total", "Peticiones HTTP atendidas"),
      errores_("http_errores_total", "Peticiones que terminaron con un error 5xx"),
      duracion_("http_duracion_ms", "Duración de las peticiones HTTP", "ms") {
    configurar();
}

void ServidorHttp::configurar() {
    servidor_.set_default_headers({
        {"Access-Control-Allow-Origin", origenPermitido_},
        {"Vary", "Origin"},
        {"Access-Control-Allow-Methods", "GET, POST, PUT, PATCH, DELETE, OPTIONS"},
        {"Access-Control-Allow-Headers", "Content-Type, X-Usuario-Id, Range, traceparent"},
        {"Access-Control-Expose-Headers", "Content-Length, Content-Range"},
    });

    servidor_.set_pre_routing_handler([this](const httplib::Request& req, httplib::Response&) {
        iniciarPeticion(req);
        return httplib::Server::HandlerResponse::Unhandled;
    });
    servidor_.set_logger([this](const httplib::Request& req, const httplib::Response& res) { terminarPeticion(req, res); });

    servidor_.set_exception_handler([this](const httplib::Request& req, httplib::Response& res, std::exception_ptr ep) {
        try {
            std::rethrow_exception(ep);
        } catch (const ErrorApi& error) {
            http::responderError(res, error);
            if (error.codigoHttp() >= 500) {
                std::cerr << "[" << servicio_ << "] " << req.method << " " << req.path << ": " << error.what() << "\n";
            }
        } catch (const std::exception& e) {
            // El detalle va al log, nunca a la respuesta (SPEC 6.4).
            std::cerr << "[" << servicio_ << "] error no controlado en " << req.method << " " << req.path << ": "
                      << e.what() << "\n";
            http::responderError(res, ErrorApi(TipoError::ErrorInterno, "Error interno del servidor"));
        } catch (...) {
            http::responderError(res, ErrorApi(TipoError::ErrorInterno, "Error interno del servidor"));
        }
    });

    // Errores que genera httplib por su cuenta (ruta inexistente, cuerpo demasiado grande...).
    servidor_.set_error_handler([](const httplib::Request&, httplib::Response& res) {
        if (!res.body.empty()) return httplib::Server::HandlerResponse::Unhandled;
        const int estado = res.status;
        const TipoError tipo = tipoPorEstado(estado);
        http::responderError(res, ErrorApi(tipo, estado == 404 ? "Ruta no encontrada" : httplib::status_message(estado)));
        res.status = estado;
        return httplib::Server::HandlerResponse::Handled;
    });

    // Preflight de CORS: el navegador lo envía antes de POST/PUT/PATCH/DELETE.
    servidor_.Options(R"(.*)", [](const httplib::Request&, httplib::Response& res) { res.status = 204; });

    servidor_.Get("/salud", [this](const httplib::Request&, httplib::Response& res) { responderSalud(res); });
}

void ServidorHttp::agregarVerificacionSalud(std::string dependencia, std::function<bool()> verificacion) {
    verificaciones_.emplace_back(std::move(dependencia), std::move(verificacion));
}

void ServidorHttp::responderSalud(httplib::Response& res) const {
    nlohmann::json dependencias = nlohmann::json::object();
    bool todoBien = true;
    for (const auto& [nombre, verificacion] : verificaciones_) {
        bool ok = false;
        try {
            ok = verificacion();
        } catch (const std::exception&) {
            ok = false;
        }
        dependencias[nombre] = ok ? "ok" : "sin conexion";
        todoBien = todoBien && ok;
    }
    http::responderJson(res, todoBien ? 200 : 503,
                        {{"estado", todoBien ? "ok" : "degradado"},
                         {"servicio", servicio_},
                         {"version", version_},
                         {"dependencias", dependencias}});
}

void ServidorHttp::iniciarPeticion(const httplib::Request& req) {
    cerrarSpanActual();  // por si una petición anterior de este hilo no llegó al logger
    peticionActual.inicio = std::chrono::steady_clock::now();

    // Continúa el trace del API que hizo la llamada (encabezado traceparent), si lo hay.
    const auto contextoPadre = Telemetria::extraerContexto(req.headers);
    api_trace::StartSpanOptions opciones;
    opciones.kind = api_trace::SpanKind::kServer;
    opciones.parent = api_trace::GetSpan(contextoPadre)->GetContext();
    const std::string nombre = req.method + " " + http::normalizarRuta(req.path);
    peticionActual.span = Telemetria::tracer()->StartSpan(
        nombre, {{"http.request.method", req.method.c_str()}, {"url.path", req.path.c_str()}}, opciones);
    peticionActual.alcance = std::make_unique<api_trace::Scope>(peticionActual.span);
}

void ServidorHttp::terminarPeticion(const httplib::Request& req, const httplib::Response& res) {
    const double milisegundos =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - peticionActual.inicio).count();
    const std::string ruta = http::normalizarRuta(req.path);
    peticiones_.sumar(1, {{"ruta", ruta}, {"metodo", req.method}, {"codigo", std::to_string(res.status)}});
    duracion_.registrar(milisegundos, {{"ruta", ruta}, {"metodo", req.method}});
    if (res.status >= 500) errores_.sumar(1, {{"ruta", ruta}, {"metodo", req.method}});

    if (peticionActual.span) {
        peticionActual.span->SetAttribute("http.route", ruta);
        peticionActual.span->SetAttribute("http.response.status_code", res.status);
        if (res.status >= 500) peticionActual.span->SetStatus(api_trace::StatusCode::kError);
    }
    cerrarSpanActual();
}

bool ServidorHttp::escuchar(const std::string& host, int puerto) { return servidor_.listen(host, puerto); }

int ServidorHttp::tomarPuertoLibre(const std::string& host) { return servidor_.bind_to_any_port(host); }

bool ServidorHttp::escucharEnPuertoTomado() { return servidor_.listen_after_bind(); }

void ServidorHttp::detener() { servidor_.stop(); }

void ServidorHttp::esperarListo() const { servidor_.wait_until_ready(); }

}  // namespace tecflix
