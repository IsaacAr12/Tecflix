#include "servicio_metadata.hpp"

#include <algorithm>
#include <cctype>
#include <set>

#include "tecflix/errores.hpp"

namespace tecflix::metadata {

namespace {

const std::set<std::string> kCamposCreacion = {"titulo", "descripcion", "tema", "etiquetas", "visibilidad",
                                                "autor_id", "autor_nombre", "tamano_original_bytes"};
const std::set<std::string> kCamposDescriptivos = {"titulo", "descripcion", "tema", "etiquetas", "visibilidad"};
const std::set<std::string> kCamposProcesamiento = {"estado", "duracion_seg", "ruta_manifest", "tamano_bytes",
                                                    "mensaje_error"};

ErrorApi datosInvalidos(const std::string& mensaje) { return ErrorApi(TipoError::DatosInvalidos, mensaje); }

ErrorApi noEncontrado() { return ErrorApi(TipoError::NoEncontrado, "El video no existe"); }

void soloCamposPermitidos(const nlohmann::json& datos, const std::set<std::string>& permitidos) {
    if (!datos.is_object()) throw datosInvalidos("Se esperaba un objeto JSON");
    for (const auto& [campo, valor] : datos.items()) {
        if (permitidos.count(campo) == 0) throw datosInvalidos("El campo '" + campo + "' no está permitido aquí");
    }
}

void exigirCampo(const nlohmann::json& datos, const char* campo) {
    if (!datos.contains(campo)) throw datosInvalidos(std::string("Falta el campo obligatorio '") + campo + "'");
}

bool soloEspacios(const std::string& texto) {
    return std::all_of(texto.begin(), texto.end(), [](unsigned char c) { return std::isspace(c) != 0; });
}

void validarLargo(const std::string& texto, const char* campo, size_t maximo) {
    if (texto.size() > maximo) {
        throw datosInvalidos(std::string("El campo '") + campo + "' admite máximo " + std::to_string(maximo) +
                             " caracteres");
    }
}

bool transicionValida(EstadoVideo desde, EstadoVideo hacia) {
    return desde == EstadoVideo::Procesando && (hacia == EstadoVideo::Listo || hacia == EstadoVideo::Error);
}

}  // namespace

ServicioMetadata::ServicioMetadata(IRepositorioMetadata& repositorio) : repositorio_(repositorio) {}

void ServicioMetadata::validarDescriptivos(const VideoMetadata& video) {
    if (video.titulo.empty() || soloEspacios(video.titulo)) throw datosInvalidos("El título no puede estar vacío");
    validarLargo(video.titulo, "titulo", kLargoMaximoTitulo);
    validarLargo(video.descripcion, "descripcion", kLargoMaximoDescripcion);
    validarLargo(video.tema, "tema", kLargoMaximoTema);
    if (video.etiquetas.size() > kMaximoEtiquetas) {
        throw datosInvalidos("Máximo " + std::to_string(kMaximoEtiquetas) + " etiquetas");
    }
    for (const auto& etiqueta : video.etiquetas) {
        if (etiqueta.empty() || etiqueta.size() > kLargoMaximoEtiqueta) {
            throw datosInvalidos("Cada etiqueta debe tener entre 1 y " + std::to_string(kLargoMaximoEtiqueta) +
                                 " caracteres");
        }
        if (std::count(video.etiquetas.begin(), video.etiquetas.end(), etiqueta) > 1) {
            throw datosInvalidos("La etiqueta '" + etiqueta + "' está repetida");
        }
    }
}

VideoMetadata ServicioMetadata::crear(const nlohmann::json& datos) {
    soloCamposPermitidos(datos, kCamposCreacion);
    for (const char* campo : {"titulo", "autor_id", "autor_nombre", "tamano_original_bytes"}) exigirCampo(datos, campo);

    VideoMetadata video = VideoMetadata::desdeJson(datos);
    validarDescriptivos(video);
    if (video.autorNombre.empty() || soloEspacios(video.autorNombre)) {
        throw datosInvalidos("El nombre del autor no puede estar vacío");
    }
    validarLargo(video.autorNombre, "autor_nombre", kLargoMaximoNombreAutor);
    if (video.tamanoOriginalBytes <= 0) throw datosInvalidos("tamano_original_bytes debe ser mayor que cero");

    video.estado = EstadoVideo::Procesando;
    video.fechaCargaMs = ahoraEnMilisegundos();
    video.id = repositorio_.crear(video);
    return video;
}

VideoMetadata ServicioMetadata::obtener(const std::string& id, uint32_t solicitanteId) {
    std::optional<VideoMetadata> video = repositorio_.obtenerPorId(id);
    if (!video || !video->puedeVer(solicitanteId)) throw noEncontrado();
    return *video;
}

PaginaVideos ServicioMetadata::listar(const ConsultaListado& consulta) {
    if (consulta.pagina < 1) throw datosInvalidos("pagina debe ser mayor o igual a 1");
    if (consulta.limite < 1 || consulta.limite > kLimiteMaximo) {
        throw datosInvalidos("limite debe estar entre 1 y " + std::to_string(kLimiteMaximo));
    }
    return repositorio_.listar(consulta);
}

VideoMetadata ServicioMetadata::actualizarDescriptivos(const std::string& id, uint32_t solicitanteId,
                                                       const nlohmann::json& cambios) {
    soloCamposPermitidos(cambios, kCamposDescriptivos);
    const VideoMetadata nuevos = VideoMetadata::desdeJson(cambios);

    VideoMetadata video = obtener(id, solicitanteId);
    if (!video.esAutor(solicitanteId)) throw ErrorApi(TipoError::SinPermiso, "Solo el autor puede modificar el video");

    if (cambios.contains("titulo")) video.titulo = nuevos.titulo;
    if (cambios.contains("descripcion")) video.descripcion = nuevos.descripcion;
    if (cambios.contains("tema")) video.tema = nuevos.tema;
    if (cambios.contains("etiquetas")) video.etiquetas = nuevos.etiquetas;
    if (cambios.contains("visibilidad")) video.visibilidad = nuevos.visibilidad;
    validarDescriptivos(video);

    if (!repositorio_.actualizarDescriptivos(video)) throw noEncontrado();
    return video;
}

VideoMetadata ServicioMetadata::actualizarProcesamiento(const std::string& id, const nlohmann::json& cambios) {
    soloCamposPermitidos(cambios, kCamposProcesamiento);
    exigirCampo(cambios, "estado");
    const VideoMetadata nuevos = VideoMetadata::desdeJson(cambios);

    std::optional<VideoMetadata> actual = repositorio_.obtenerPorId(id);
    if (!actual) throw noEncontrado();
    VideoMetadata video = *actual;
    if (!transicionValida(video.estado, nuevos.estado)) {
        throw ErrorApi(TipoError::Conflicto,
                       "No se puede pasar de '" + aTexto(video.estado) + "' a '" + aTexto(nuevos.estado) + "'");
    }
    if (nuevos.duracionSeg < 0) throw datosInvalidos("duracion_seg no puede ser negativa");
    if (nuevos.tamanoBytes < 0) throw datosInvalidos("tamano_bytes no puede ser negativo");
    validarLargo(nuevos.rutaManifest, "ruta_manifest", 300);
    validarLargo(nuevos.mensajeError, "mensaje_error", 1000);

    video.estado = nuevos.estado;
    if (cambios.contains("duracion_seg")) video.duracionSeg = nuevos.duracionSeg;
    if (cambios.contains("ruta_manifest")) video.rutaManifest = nuevos.rutaManifest;
    if (cambios.contains("tamano_bytes")) video.tamanoBytes = nuevos.tamanoBytes;
    video.mensajeError = video.estado == EstadoVideo::Error ? nuevos.mensajeError : "";

    if (!repositorio_.actualizarProcesamiento(video)) throw noEncontrado();
    return video;
}

void ServicioMetadata::eliminar(const std::string& id) {
    if (!repositorio_.eliminar(id)) throw noEncontrado();
}

bool ServicioMetadata::baseDeDatosDisponible() { return repositorio_.disponible(); }

}  // namespace tecflix::metadata
