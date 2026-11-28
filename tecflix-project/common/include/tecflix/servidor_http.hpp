#ifndef TECFLIX_SERVIDOR_HTTP_HPP
#define TECFLIX_SERVIDOR_HTTP_HPP

#include <httplib.h>

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "tecflix/telemetria.hpp"

namespace tecflix {

// Servidor HTTP común de los APIs. Resuelve lo que el SPEC exige a todos:
//   - CORS con el origen de la aplicación web y respuesta 204 a OPTIONS (6.3)
//   - errores en formato {"error": {"codigo", "mensaje"}} (6.4)
//   - GET /salud con el estado de las dependencias (6.4)
//   - un span y las métricas http_* por cada petición (9)
// Los controladores registran sus rutas en rutas().
class ServidorHttp {
public:
    ServidorHttp(std::string servicio, std::string version, std::string origenPermitido);

    ServidorHttp(const ServidorHttp&) = delete;
    ServidorHttp& operator=(const ServidorHttp&) = delete;

    httplib::Server& rutas() { return servidor_; }

    // Agrega una dependencia a GET /salud (por ejemplo "mongodb").
    void agregarVerificacionSalud(std::string dependencia, std::function<bool()> verificacion);

    bool escuchar(const std::string& host, int puerto);
    // Para pruebas: toma un puerto libre y luego se llama escucharEnPuertoTomado() en otro hilo.
    int tomarPuertoLibre(const std::string& host);
    bool escucharEnPuertoTomado();
    void detener();
    void esperarListo() const;

private:
    void configurar();
    void iniciarPeticion(const httplib::Request& req);
    void terminarPeticion(const httplib::Request& req, const httplib::Response& res);
    void responderSalud(httplib::Response& res) const;

    std::string servicio_;
    std::string version_;
    std::string origenPermitido_;
    httplib::Server servidor_;
    std::vector<std::pair<std::string, std::function<bool()>>> verificaciones_;

    Contador peticiones_;
    Contador errores_;
    Histograma duracion_;
};

}  // namespace tecflix

#endif  // TECFLIX_SERVIDOR_HTTP_HPP
