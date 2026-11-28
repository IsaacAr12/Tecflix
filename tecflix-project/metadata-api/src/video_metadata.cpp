#include "video_metadata.hpp"

#include <chrono>
#include <ctime>

#include "tecflix/errores.hpp"

namespace tecflix::metadata {

namespace {

ErrorApi tipoIncorrecto(const char* campo, const char* tipo) {
    return ErrorApi(TipoError::DatosInvalidos, std::string("El campo '") + campo + "' debe ser " + tipo);
}

std::string leerTexto(const nlohmann::json& datos, const char* campo) {
    if (!datos.at(campo).is_string()) throw tipoIncorrecto(campo, "texto");
    return datos.at(campo).get<std::string>();
}

int64_t leerEntero(const nlohmann::json& datos, const char* campo) {
    if (!datos.at(campo).is_number_integer()) throw tipoIncorrecto(campo, "un número entero");
    return datos.at(campo).get<int64_t>();
}

}  // namespace

std::string aTexto(Visibilidad visibilidad) { return visibilidad == Visibilidad::Publico ? "publico" : "privado"; }

std::string aTexto(EstadoVideo estado) {
    switch (estado) {
        case EstadoVideo::Procesando: return "procesando";
        case EstadoVideo::Listo:      return "listo";
        case EstadoVideo::Error:      return "error";
    }
    return "error";
}

Visibilidad visibilidadDesdeTexto(const std::string& texto) {
    if (texto == "publico") return Visibilidad::Publico;
    if (texto == "privado") return Visibilidad::Privado;
    throw ErrorApi(TipoError::DatosInvalidos, "visibilidad debe ser 'publico' o 'privado'");
}

EstadoVideo estadoDesdeTexto(const std::string& texto) {
    if (texto == "procesando") return EstadoVideo::Procesando;
    if (texto == "listo") return EstadoVideo::Listo;
    if (texto == "error") return EstadoVideo::Error;
    throw ErrorApi(TipoError::DatosInvalidos, "estado debe ser 'procesando', 'listo' o 'error'");
}

std::string fechaIso8601(int64_t milisegundos) {
    const auto segundos = static_cast<std::time_t>(milisegundos / 1000);
    std::tm utc{};
    gmtime_r(&segundos, &utc);
    char texto[32];
    std::strftime(texto, sizeof(texto), "%Y-%m-%dT%H:%M:%SZ", &utc);
    return texto;
}

int64_t ahoraEnMilisegundos() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

nlohmann::json VideoMetadata::aJson() const {
    nlohmann::json json = {
        {"id", id},
        {"titulo", titulo},
        {"descripcion", descripcion},
        {"autor_id", autorId},
        {"autor_nombre", autorNombre},
        {"duracion_seg", duracionSeg},
        {"fecha_carga", fechaIso8601(fechaCargaMs)},
        {"tema", tema},
        {"etiquetas", etiquetas},
        {"visibilidad", aTexto(visibilidad)},
        {"estado", aTexto(estado)},
        {"ruta_manifest", rutaManifest},
        {"tamano_original_bytes", tamanoOriginalBytes},
        {"tamano_bytes", tamanoBytes},
    };
    if (estado == EstadoVideo::Error) json["mensaje_error"] = mensajeError;
    return json;
}

VideoMetadata VideoMetadata::desdeJson(const nlohmann::json& datos) {
    if (!datos.is_object()) throw ErrorApi(TipoError::DatosInvalidos, "Se esperaba un objeto JSON");
    VideoMetadata video;
    if (datos.contains("titulo")) video.titulo = leerTexto(datos, "titulo");
    if (datos.contains("descripcion")) video.descripcion = leerTexto(datos, "descripcion");
    if (datos.contains("tema")) video.tema = leerTexto(datos, "tema");
    if (datos.contains("autor_nombre")) video.autorNombre = leerTexto(datos, "autor_nombre");
    if (datos.contains("ruta_manifest")) video.rutaManifest = leerTexto(datos, "ruta_manifest");
    if (datos.contains("mensaje_error")) video.mensajeError = leerTexto(datos, "mensaje_error");
    if (datos.contains("visibilidad")) video.visibilidad = visibilidadDesdeTexto(leerTexto(datos, "visibilidad"));
    if (datos.contains("estado")) video.estado = estadoDesdeTexto(leerTexto(datos, "estado"));
    if (datos.contains("autor_id")) {
        const int64_t autor = leerEntero(datos, "autor_id");
        if (autor < 1 || autor > 4294967295LL) throw tipoIncorrecto("autor_id", "un entero entre 1 y 4294967295");
        video.autorId = static_cast<uint32_t>(autor);
    }
    if (datos.contains("tamano_original_bytes")) {
        video.tamanoOriginalBytes = leerEntero(datos, "tamano_original_bytes");
    }
    if (datos.contains("tamano_bytes")) video.tamanoBytes = leerEntero(datos, "tamano_bytes");
    if (datos.contains("duracion_seg")) {
        if (!datos.at("duracion_seg").is_number()) throw tipoIncorrecto("duracion_seg", "un número");
        video.duracionSeg = datos.at("duracion_seg").get<double>();
    }
    if (datos.contains("etiquetas")) {
        if (!datos.at("etiquetas").is_array()) throw tipoIncorrecto("etiquetas", "una lista de textos");
        for (const auto& etiqueta : datos.at("etiquetas")) {
            if (!etiqueta.is_string()) throw tipoIncorrecto("etiquetas", "una lista de textos");
            video.etiquetas.push_back(etiqueta.get<std::string>());
        }
    }
    return video;
}

}  // namespace tecflix::metadata
