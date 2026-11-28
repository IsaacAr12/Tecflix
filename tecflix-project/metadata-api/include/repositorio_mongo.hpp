#ifndef METADATA_REPOSITORIO_MONGO_HPP
#define METADATA_REPOSITORIO_MONGO_HPP

#include <bsoncxx/document/value.hpp>
#include <bsoncxx/document/view.hpp>
#include <memory>
#include <mongocxx/pool.hpp>
#include <string>

#include "repositorio_metadata.hpp"
#include "tecflix/telemetria.hpp"

namespace tecflix::metadata {

// Implementación de IRepositorioMetadata con el driver oficial mongocxx (SPEC 7.4).
// Es la única clase del proyecto que conoce MongoDB y BSON.
//
// httplib atiende peticiones en varios hilos y un mongocxx::client no se puede
// compartir entre hilos, por eso se usa un mongocxx::pool: cada operación toma un
// cliente y lo devuelve al terminar. mongocxx::instance debe existir antes (se crea en main).
class RepositorioMongo : public IRepositorioMetadata {
public:
    // Lanza ErrorConfiguracion si la URI es inválida.
    RepositorioMongo(const std::string& uri, std::string baseDatos);

    // Crea los índices de la sección 7.1. Es idempotente.
    void asegurarIndices();

    std::string crear(const VideoMetadata& video) override;
    std::optional<VideoMetadata> obtenerPorId(const std::string& id) override;
    PaginaVideos listar(const ConsultaListado& consulta) override;
    bool actualizarDescriptivos(const VideoMetadata& video) override;
    bool actualizarProcesamiento(const VideoMetadata& video) override;
    bool eliminar(const std::string& id) override;
    bool disponible() override;

    // Un ObjectId válido: 24 caracteres hexadecimales.
    static bool idValido(const std::string& id);

private:
    static bsoncxx::document::value aDocumento(const VideoMetadata& video);
    static bsoncxx::document::value camposDescriptivos(const VideoMetadata& video);
    static bsoncxx::document::value camposProcesamiento(const VideoMetadata& video);
    static VideoMetadata desdeDocumento(bsoncxx::document::view documento);

    // Ejecuta una operación midiendo mongo_consulta_duracion_ms y traduciendo los
    // errores del driver a ErrorApi(ServicioNoDisponible).
    template <typename Operacion>
    auto medir(const char* nombre, Operacion operacion);

    bool actualizarCampos(const std::string& id, const char* operacion, bsoncxx::document::value campos);

    std::unique_ptr<mongocxx::pool> pool_;
    std::string baseDatos_;
    Histograma duracionConsultas_;
};

}  // namespace tecflix::metadata

#endif  // METADATA_REPOSITORIO_MONGO_HPP
