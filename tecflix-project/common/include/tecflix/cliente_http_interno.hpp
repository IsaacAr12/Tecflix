#ifndef TECFLIX_CLIENTE_HTTP_INTERNO_HPP
#define TECFLIX_CLIENTE_HTTP_INTERNO_HPP

#include <httplib.h>

#include <nlohmann/json.hpp>
#include <string>

#include "tecflix/errores.hpp"

namespace tecflix {

// Cliente para las llamadas entre APIs (SPEC 6.5 y 9). En cada petición agrega:
//   - X-Token-Interno
//   - traceparent del span actual, para que el trace siga en el otro API
//   - los encabezados extra que se pasen (por ejemplo X-Usuario-Id)
// Crea un httplib::Client por llamada, así es seguro usarlo desde varios hilos.
class ClienteHttpInterno {
public:
    ClienteHttpInterno(std::string urlBase, std::string tokenInterno, std::string nombreServicio,
                       int timeoutSegundos = 5);

    // Lanzan ErrorApi(ServicioNoDisponible) si el otro API no respondió.
    httplib::Response get(const std::string& ruta, httplib::Headers extra = {}) const;
    httplib::Response post(const std::string& ruta, const nlohmann::json& cuerpo, httplib::Headers extra = {}) const;
    httplib::Response patch(const std::string& ruta, const nlohmann::json& cuerpo, httplib::Headers extra = {}) const;
    httplib::Response eliminar(const std::string& ruta, httplib::Headers extra = {}) const;

    // Error equivalente a una respuesta no exitosa del otro API. Se conserva el codigo
    // del SPEC cuando viene en el cuerpo (por ejemplo DATOS_INVALIDOS o CUOTA_EXCEDIDA);
    // un 5xx se convierte en SERVICIO_NO_DISPONIBLE.
    ErrorApi errorDeRespuesta(const httplib::Response& respuesta, const std::string& operacion) const;

    const std::string& nombreServicio() const { return nombreServicio_; }

private:
    httplib::Client crearCliente() const;
    httplib::Headers encabezados(httplib::Headers extra) const;
    httplib::Response verificar(const httplib::Result& resultado, const std::string& operacion) const;

    std::string urlBase_;
    std::string tokenInterno_;
    std::string nombreServicio_;
    int timeoutSegundos_;
};

}  // namespace tecflix

#endif  // TECFLIX_CLIENTE_HTTP_INTERNO_HPP
