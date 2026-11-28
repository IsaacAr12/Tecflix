#include "tecflix/cliente_http_interno.hpp"

#include <map>

#include "tecflix/telemetria.hpp"

namespace tecflix {

namespace {

const std::map<std::string, TipoError>& tiposPorCodigo() {
    static const std::map<std::string, TipoError> tipos = {
        {"DATOS_INVALIDOS", TipoError::DatosInvalidos},
        {"SIN_PERMISO", TipoError::SinPermiso},
        {"NO_ENCONTRADO", TipoError::NoEncontrado},
        {"CONFLICTO", TipoError::Conflicto},
        {"CUOTA_EXCEDIDA", TipoError::CuotaExcedida},
        {"ARCHIVO_DEMASIADO_GRANDE", TipoError::ArchivoDemasiadoGrande},
    };
    return tipos;
}

TipoError tipoPorEstado(int estado) {
    switch (estado) {
        case 400: return TipoError::DatosInvalidos;
        case 403: return TipoError::SinPermiso;
        case 404: return TipoError::NoEncontrado;
        case 409: return TipoError::Conflicto;
        case 413: return TipoError::CuotaExcedida;
        default:  return TipoError::ServicioNoDisponible;
    }
}

}  // namespace

ClienteHttpInterno::ClienteHttpInterno(std::string urlBase, std::string tokenInterno, std::string nombreServicio,
                                       int timeoutSegundos)
    : urlBase_(std::move(urlBase)),
      tokenInterno_(std::move(tokenInterno)),
      nombreServicio_(std::move(nombreServicio)),
      timeoutSegundos_(timeoutSegundos) {}

httplib::Headers ClienteHttpInterno::encabezados(httplib::Headers extra) const {
    extra.emplace("X-Token-Interno", tokenInterno_);
    Telemetria::inyectarContexto(extra);
    return extra;
}

httplib::Response ClienteHttpInterno::verificar(const httplib::Result& resultado, const std::string& operacion) const {
    if (!resultado) {
        throw ErrorApi(TipoError::ServicioNoDisponible, nombreServicio_ + " no respondió (" + operacion + ": " +
                                                            httplib::to_string(resultado.error()) + ")");
    }
    return resultado.value();
}

httplib::Client ClienteHttpInterno::crearCliente() const {
    httplib::Client cliente(urlBase_);
    cliente.set_connection_timeout(timeoutSegundos_, 0);
    cliente.set_read_timeout(timeoutSegundos_, 0);
    return cliente;
}

httplib::Response ClienteHttpInterno::get(const std::string& ruta, httplib::Headers extra) const {
    httplib::Client cliente = crearCliente();
    return verificar(cliente.Get(ruta, encabezados(std::move(extra))), "GET " + ruta);
}

httplib::Response ClienteHttpInterno::post(const std::string& ruta, const nlohmann::json& cuerpo,
                                           httplib::Headers extra) const {
    httplib::Client cliente = crearCliente();
    return verificar(cliente.Post(ruta, encabezados(std::move(extra)), cuerpo.dump(), "application/json"),
                     "POST " + ruta);
}

httplib::Response ClienteHttpInterno::patch(const std::string& ruta, const nlohmann::json& cuerpo,
                                            httplib::Headers extra) const {
    httplib::Client cliente = crearCliente();
    return verificar(cliente.Patch(ruta, encabezados(std::move(extra)), cuerpo.dump(), "application/json"),
                     "PATCH " + ruta);
}

httplib::Response ClienteHttpInterno::eliminar(const std::string& ruta, httplib::Headers extra) const {
    httplib::Client cliente = crearCliente();
    return verificar(cliente.Delete(ruta, encabezados(std::move(extra))), "DELETE " + ruta);
}

ErrorApi ClienteHttpInterno::errorDeRespuesta(const httplib::Response& respuesta, const std::string& operacion) const {
    if (respuesta.status == 401) {
        // Solo pasa si TOKEN_INTERNO no coincide entre los APIs: es un error de configuración.
        return ErrorApi(TipoError::ErrorInterno, nombreServicio_ + " rechazó el token interno (" + operacion + ")");
    }
    std::string mensaje = nombreServicio_ + " respondió " + std::to_string(respuesta.status) + " (" + operacion + ")";
    TipoError tipo = tipoPorEstado(respuesta.status);
    const nlohmann::json cuerpo = nlohmann::json::parse(respuesta.body, nullptr, false);
    if (respuesta.status < 500 && cuerpo.is_object() && cuerpo.contains("error") && cuerpo["error"].is_object()) {
        const auto& error = cuerpo["error"];
        if (error.contains("mensaje") && error["mensaje"].is_string()) mensaje = error["mensaje"].get<std::string>();
        if (error.contains("codigo") && error["codigo"].is_string()) {
            auto it = tiposPorCodigo().find(error["codigo"].get<std::string>());
            if (it != tiposPorCodigo().end()) tipo = it->second;
        }
    }
    return ErrorApi(tipo, mensaje);
}

}  // namespace tecflix
