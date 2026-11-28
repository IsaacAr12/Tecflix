// API de Metadata de Tecflix (SPEC 7).
// Variables de entorno (SPEC 6.2):
//   PUERTO                       8082 por defecto
//   URI_MONGO                    obligatoria, p. ej. mongodb://localhost:27017/?serverSelectionTimeoutMS=5000
//   BD_MONGO                     tecflix por defecto
//   TOKEN_INTERNO                obligatoria
//   ORIGEN_APP_WEB               http://localhost:5173 por defecto
//   OTEL_EXPORTER_OTLP_ENDPOINT  p. ej. http://otel-collector:4318 (sin ella no se exporta telemetría)
#include <csignal>
#include <iostream>
#include <mongocxx/instance.hpp>

#include "controlador_metadata.hpp"
#include "repositorio_mongo.hpp"
#include "servicio_metadata.hpp"
#include "tecflix/configuracion.hpp"
#include "tecflix/servidor_http.hpp"
#include "tecflix/telemetria.hpp"

namespace {

constexpr const char* kServicio = "metadata-api";
constexpr const char* kVersion = "1.0";
tecflix::ServidorHttp* servidorActivo = nullptr;

void alRecibirSenal(int) {
    if (servidorActivo != nullptr) servidorActivo->detener();
}

}  // namespace

int main() {
    using namespace tecflix;
    using namespace tecflix::metadata;
    try {
        const int puerto = static_cast<int>(leerEntornoEntero("PUERTO", 8082));
        const std::string uriMongo = leerEntornoObligatorio("URI_MONGO");
        const std::string baseDatos = leerEntorno("BD_MONGO", "tecflix");
        const std::string tokenInterno = leerEntornoObligatorio("TOKEN_INTERNO");
        const std::string origenWeb = leerEntorno("ORIGEN_APP_WEB", "http://localhost:5173");
        const std::string collector = leerEntorno("OTEL_EXPORTER_OTLP_ENDPOINT", "");

        // La telemetría se inicializa antes de crear los objetos que registran métricas.
        if (collector.empty()) {
            std::cerr << "Aviso: OTEL_EXPORTER_OTLP_ENDPOINT no está definida; no se exportará telemetría\n";
        } else {
            Telemetria::inicializar(kServicio, kVersion, collector);
        }

        const mongocxx::instance instanciaMongo{};  // una sola vez por proceso
        RepositorioMongo repositorio(uriMongo, baseDatos);
        try {
            repositorio.asegurarIndices();
        } catch (const std::exception& e) {
            // Se puede arrancar sin MongoDB; GET /salud lo reporta como "degradado".
            std::cerr << "Aviso: no se pudieron crear los índices: " << e.what() << "\n";
        }

        ServicioMetadata servicio(repositorio);
        ServidorHttp servidor(kServicio, kVersion, origenWeb);
        servidor.agregarVerificacionSalud("mongodb", [&servicio] { return servicio.baseDeDatosDisponible(); });
        ControladorMetadata controlador(servicio, tokenInterno);
        controlador.registrarRutas(servidor.rutas());

        servidorActivo = &servidor;
        std::signal(SIGINT, alRecibirSenal);
        std::signal(SIGTERM, alRecibirSenal);

        std::cout << kServicio << " escuchando en el puerto " << puerto << std::endl;
        const bool ok = servidor.escuchar("0.0.0.0", puerto);
        Telemetria::finalizar();
        if (!ok) {
            std::cerr << "No se pudo abrir el puerto " << puerto << "\n";
            return 1;
        }
    } catch (const ErrorConfiguracion& e) {
        std::cerr << "Error de configuración: " << e.what() << "\n";
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "Error fatal: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
