#ifndef METADATA_REPOSITORIO_METADATA_HPP
#define METADATA_REPOSITORIO_METADATA_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "video_metadata.hpp"

namespace tecflix::metadata {

enum class CampoOrden { Titulo, Autor, Fecha };

struct ConsultaListado {
    uint32_t solicitanteId = 0;  // ve sus videos + los públicos listos (SPEC 7.3)
    int pagina = 1;              // empieza en 1
    int limite = 20;             // de 1 a 50
    CampoOrden orden = CampoOrden::Fecha;
    bool ascendente = false;
};

struct PaginaVideos {
    std::vector<VideoMetadata> videos;
    int64_t total = 0;  // cantidad total que cumple el filtro, para calcular total_paginas
};

// Acceso a los datos de los videos (SPEC 7.4). ServicioMetadata depende de esta
// interfaz y no de MongoDB: en producción recibe un RepositorioMongo y en las
// pruebas unitarias un mock de GoogleMock.
class IRepositorioMetadata {
public:
    virtual ~IRepositorioMetadata() = default;

    // Guarda un video nuevo y devuelve el id asignado.
    virtual std::string crear(const VideoMetadata& video) = 0;
    virtual std::optional<VideoMetadata> obtenerPorId(const std::string& id) = 0;
    // Filtro, orden y paginación se resuelven en la base de datos.
    virtual PaginaVideos listar(const ConsultaListado& consulta) = 0;
    // Solo titulo, descripcion, tema, etiquetas y visibilidad. false si no existe.
    virtual bool actualizarDescriptivos(const VideoMetadata& video) = 0;
    // Solo estado, duracion_seg, ruta_manifest, tamano_bytes y mensaje_error. false si no existe.
    virtual bool actualizarProcesamiento(const VideoMetadata& video) = 0;
    virtual bool eliminar(const std::string& id) = 0;
    // true si la base de datos responde (para GET /salud).
    virtual bool disponible() = 0;
};

}  // namespace tecflix::metadata

#endif  // METADATA_REPOSITORIO_METADATA_HPP
