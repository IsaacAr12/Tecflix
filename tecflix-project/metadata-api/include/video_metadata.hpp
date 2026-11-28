#ifndef METADATA_VIDEO_METADATA_HPP
#define METADATA_VIDEO_METADATA_HPP

#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace tecflix::metadata {

enum class Visibilidad { Publico, Privado };

// SPEC 7.1: procesando -> listo | error.
enum class EstadoVideo { Procesando, Listo, Error };

std::string aTexto(Visibilidad visibilidad);
std::string aTexto(EstadoVideo estado);
// Lanzan ErrorApi(DatosInvalidos) si el texto no es un valor válido.
Visibilidad visibilidadDesdeTexto(const std::string& texto);
EstadoVideo estadoDesdeTexto(const std::string& texto);

// Milisegundos desde 1970 (UTC) -> "2026-11-28T10:15:00Z".
std::string fechaIso8601(int64_t milisegundos);
int64_t ahoraEnMilisegundos();

// Entidad del video (SPEC 7.1). No conoce BSON: la conversión a documentos de
// MongoDB es responsabilidad exclusiva de RepositorioMongo.
class VideoMetadata {
public:
    std::string id;
    std::string titulo;
    std::string descripcion;
    uint32_t autorId = 0;
    std::string autorNombre;
    double duracionSeg = 0.0;
    int64_t fechaCargaMs = 0;
    std::string tema;
    std::vector<std::string> etiquetas;
    Visibilidad visibilidad = Visibilidad::Privado;
    EstadoVideo estado = EstadoVideo::Procesando;
    std::string mensajeError;
    std::string rutaManifest;
    int64_t tamanoOriginalBytes = 0;
    int64_t tamanoBytes = 0;

    bool esAutor(uint32_t usuarioId) const { return autorId == usuarioId; }

    // Lo ve su autor, o cualquiera si es público y ya está listo.
    bool puedeVer(uint32_t usuarioId) const {
        return esAutor(usuarioId) || (visibilidad == Visibilidad::Publico && estado == EstadoVideo::Listo);
    }

    nlohmann::json aJson() const;

    // Lee los campos presentes en el JSON (nombres del SPEC, en snake_case) y valida
    // sus tipos. Las reglas de negocio (largos, obligatorios...) las valida el servicio.
    static VideoMetadata desdeJson(const nlohmann::json& datos);
};

}  // namespace tecflix::metadata

#endif  // METADATA_VIDEO_METADATA_HPP
