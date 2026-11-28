// API de Streaming de Tecflix (SPEC 8).
// Variables de entorno (SPEC 6.2):
//   PUERTO                       8083 por defecto
//   URL_API_USUARIOS             obligatoria, p. ej. http://localhost:8081
//   URL_API_METADATA             obligatoria, p. ej. http://localhost:8082
//   TOKEN_INTERNO                obligatoria
//   ORIGEN_APP_WEB               http://localhost:5173 por defecto
//   DIR_VIDEOS                   ./datos/videos por defecto
//   HILOS_TRANSCODIFICACION      1 por defecto
//   MAX_BYTES_SUBIDA             4294967296 (4 GiB) por defecto
//   FFMPEG, FFPROBE              ffmpeg y ffprobe por defecto
//   OTEL_EXPORTER_OTLP_ENDPOINT  p. ej. http://otel-collector:4318 (sin ella no se exporta telemetría)
#include <csignal>
#include <iostream>

#include "cliente_metadata.hpp"
#include "cliente_usuarios.hpp"
#include "controlador_streaming.hpp"
#include "gestor_almacenamiento.hpp"
#include "pool_transcodificacion.hpp"
#include "servicio_streaming.hpp"
#include "tecflix/configuracion.hpp"
#include "tecflix/servidor_http.hpp"
#include "tecflix/telemetria.hpp"
#include "transcodificador.hpp"

namespace {

constexpr const char* kServicio = "streaming-api";
constexpr const char* kVersion = "1.0";
tecflix::ServidorHttp* servidorActivo = nullptr;

void alRecibirSenal(int) {
    if (servidorActivo != nullptr) servidorActivo->detener();
}

}  // namespace

int main() {
    using namespace tecflix;
    using namespace tecflix::streaming;
    try {
        const int puerto = static_cast<int>(leerEntornoEntero("PUERTO", 8083));
        const std::string urlUsuarios = leerEntornoObligatorio("URL_API_USUARIOS");
        const std::string urlMetadata = leerEntornoObligatorio("URL_API_METADATA");
        const std::string tokenInterno = leerEntornoObligatorio("TOKEN_INTERNO");
        const std::string origenWeb = leerEntorno("ORIGEN_APP_WEB", "http://localhost:5173");
        const std::string carpetaVideos = leerEntorno("DIR_VIDEOS", "./datos/videos");
        const int64_t hilos = leerEntornoEntero("HILOS_TRANSCODIFICACION", 1);
        const int64_t maximoSubida = leerEntornoEntero("MAX_BYTES_SUBIDA", 4LL * 1024 * 1024 * 1024);
        const std::string collector = leerEntorno("OTEL_EXPORTER_OTLP_ENDPOINT", "");
        if (hilos < 1) throw ErrorConfiguracion("HILOS_TRANSCODIFICACION debe ser al menos 1");
        if (maximoSubida < 1) throw ErrorConfiguracion("MAX_BYTES_SUBIDA debe ser mayor que cero");

        // La telemetría se inicializa antes de crear los objetos que registran métricas.
        if (collector.empty()) {
            std::cerr << "Aviso: OTEL_EXPORTER_OTLP_ENDPOINT no está definida; no se exportará telemetría\n";
        } else {
            Telemetria::inicializar(kServicio, kVersion, collector);
        }

        GestorAlmacenamiento almacenamiento(carpetaVideos);
        ClienteUsuariosHttp usuarios(urlUsuarios, tokenInterno);
        ClienteMetadataHttp metadata(urlMetadata, tokenInterno);
        TranscodificadorFfmpeg transcodificador(leerEntorno("FFMPEG", "ffmpeg"), leerEntorno("FFPROBE", "ffprobe"));
        const bool ffmpegDisponible = transcodificador.disponible();  // se revisa una vez al arrancar
        if (!ffmpegDisponible) std::cerr << "Aviso: no se encontró FFMPEG; las subidas terminarán en error\n";

        PoolTranscodificacion pool(static_cast<size_t>(hilos));
        ServicioStreaming servicio(almacenamiento, usuarios, metadata, transcodificador, pool,
                                   static_cast<uint64_t>(maximoSubida));
        ServidorHttp servidor(kServicio, kVersion, origenWeb);
        servidor.agregarVerificacionSalud("almacenamiento", [&almacenamiento] { return almacenamiento.disponible(); });
        servidor.agregarVerificacionSalud("ffmpeg", [ffmpegDisponible] { return ffmpegDisponible; });
        ControladorStreaming controlador(servicio, static_cast<uint64_t>(maximoSubida));
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
