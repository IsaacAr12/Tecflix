#ifndef METADATA_SERVICIO_METADATA_HPP
#define METADATA_SERVICIO_METADATA_HPP

#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>

#include "repositorio_metadata.hpp"
#include "video_metadata.hpp"

namespace tecflix::metadata {

// Reglas de negocio del API de metadata (SPEC 7): validaciones, permisos del autor,
// visibilidad y transiciones de estado. No conoce HTTP ni MongoDB; el repositorio
// se inyecta por el constructor.
class ServicioMetadata {
public:
    static constexpr int kLimiteMaximo = 50;
    static constexpr size_t kLargoMaximoTitulo = 200;
    static constexpr size_t kLargoMaximoDescripcion = 5000;
    static constexpr size_t kLargoMaximoTema = 100;
    static constexpr size_t kMaximoEtiquetas = 20;
    static constexpr size_t kLargoMaximoEtiqueta = 50;
    static constexpr size_t kLargoMaximoNombreAutor = 100;

    explicit ServicioMetadata(IRepositorioMetadata& repositorio);

    // POST /videos (interno). Crea el video en estado "procesando".
    VideoMetadata crear(const nlohmann::json& datos);

    // GET /videos/{id}. Un video que el usuario no puede ver responde NO_ENCONTRADO.
    VideoMetadata obtener(const std::string& id, uint32_t solicitanteId);

    // GET /videos.
    PaginaVideos listar(const ConsultaListado& consulta);

    // PUT /videos/{id}. Solo el autor y solo los campos descriptivos.
    VideoMetadata actualizarDescriptivos(const std::string& id, uint32_t solicitanteId, const nlohmann::json& cambios);

    // PATCH /videos/{id} (interno). Lo usa el API de streaming al terminar de transcodificar.
    VideoMetadata actualizarProcesamiento(const std::string& id, const nlohmann::json& cambios);

    // DELETE /videos/{id} (interno).
    void eliminar(const std::string& id);

    bool baseDeDatosDisponible();

private:
    static void validarDescriptivos(const VideoMetadata& video);

    IRepositorioMetadata& repositorio_;
};

}  // namespace tecflix::metadata

#endif  // METADATA_SERVICIO_METADATA_HPP
