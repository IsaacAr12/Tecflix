#ifndef METADATA_TESTS_MOCK_REPOSITORIO_METADATA_HPP
#define METADATA_TESTS_MOCK_REPOSITORIO_METADATA_HPP

#include <gmock/gmock.h>

#include "repositorio_metadata.hpp"

namespace tecflix::metadata {

// Mock de GoogleMock: permite probar ServicioMetadata y ControladorMetadata sin MongoDB.
class MockRepositorioMetadata : public IRepositorioMetadata {
public:
    MOCK_METHOD(std::string, crear, (const VideoMetadata& video), (override));
    MOCK_METHOD(std::optional<VideoMetadata>, obtenerPorId, (const std::string& id), (override));
    MOCK_METHOD(PaginaVideos, listar, (const ConsultaListado& consulta), (override));
    MOCK_METHOD(bool, actualizarDescriptivos, (const VideoMetadata& video), (override));
    MOCK_METHOD(bool, actualizarProcesamiento, (const VideoMetadata& video), (override));
    MOCK_METHOD(bool, eliminar, (const std::string& id), (override));
    MOCK_METHOD(bool, disponible, (), (override));
};

// Video de ejemplo para las pruebas.
inline VideoMetadata videoDePrueba(uint32_t autor, Visibilidad visibilidad, EstadoVideo estado) {
    VideoMetadata video;
    video.id = "656f1a2b3c4d5e6f7a8b9c0d";
    video.titulo = "Clase de árboles B";
    video.autorId = autor;
    video.autorNombre = "autor" + std::to_string(autor);
    video.visibilidad = visibilidad;
    video.estado = estado;
    video.tamanoOriginalBytes = 1000;
    return video;
}

}  // namespace tecflix::metadata

#endif  // METADATA_TESTS_MOCK_REPOSITORIO_METADATA_HPP
