#include <gtest/gtest.h>

#include "tecflix/errores.hpp"
#include "video_metadata.hpp"

using namespace tecflix;
using namespace tecflix::metadata;
using nlohmann::json;

TEST(VideoMetadata, EnumsATextoYDeVuelta) {
    for (auto estado : {EstadoVideo::Procesando, EstadoVideo::Listo, EstadoVideo::Error}) {
        EXPECT_EQ(estadoDesdeTexto(aTexto(estado)), estado);
    }
    EXPECT_EQ(visibilidadDesdeTexto("publico"), Visibilidad::Publico);
    EXPECT_EQ(visibilidadDesdeTexto("privado"), Visibilidad::Privado);
    EXPECT_THROW(visibilidadDesdeTexto("oculto"), ErrorApi);
    EXPECT_THROW(estadoDesdeTexto("subiendo"), ErrorApi);
}

TEST(VideoMetadata, FechasEnUtc) {
    EXPECT_EQ(fechaIso8601(0), "1970-01-01T00:00:00Z");
    EXPECT_EQ(fechaIso8601(1764324900000LL), "2025-11-28T10:15:00Z");
    EXPECT_GT(ahoraEnMilisegundos(), 1764324900000LL);
}

TEST(VideoMetadata, ReglasDeVisibilidad) {
    VideoMetadata video;
    video.autorId = 1;
    video.visibilidad = Visibilidad::Publico;
    video.estado = EstadoVideo::Procesando;
    EXPECT_TRUE(video.esAutor(1));
    EXPECT_TRUE(video.puedeVer(1));
    EXPECT_FALSE(video.puedeVer(2));  // público, pero todavía no está listo
    video.estado = EstadoVideo::Listo;
    EXPECT_TRUE(video.puedeVer(2));
    video.visibilidad = Visibilidad::Privado;
    EXPECT_FALSE(video.puedeVer(2));
}

TEST(VideoMetadata, AJsonUsaLosNombresDelSpec) {
    VideoMetadata video;
    video.id = "abc";
    video.titulo = "t";
    video.autorId = 7;
    video.etiquetas = {"a"};
    video.mensajeError = "no se muestra si no hay error";
    const json resultado = video.aJson();
    for (const char* campo : {"id", "titulo", "descripcion", "autor_id", "autor_nombre", "duracion_seg",
                              "fecha_carga", "tema", "etiquetas", "visibilidad", "estado", "ruta_manifest",
                              "tamano_original_bytes", "tamano_bytes"}) {
        EXPECT_TRUE(resultado.contains(campo)) << campo;
    }
    EXPECT_EQ(resultado["autor_id"], 7);
    EXPECT_EQ(resultado["estado"], "procesando");
    EXPECT_FALSE(resultado.contains("mensaje_error"));
    video.estado = EstadoVideo::Error;
    EXPECT_EQ(video.aJson()["mensaje_error"], "no se muestra si no hay error");
}

TEST(VideoMetadata, DesdeJsonLeeYValidaTipos) {
    const VideoMetadata video = VideoMetadata::desdeJson({{"titulo", "t"},
                                                          {"descripcion", "d"},
                                                          {"tema", "x"},
                                                          {"autor_id", 4294967295LL},
                                                          {"autor_nombre", "n"},
                                                          {"etiquetas", {"a", "b"}},
                                                          {"visibilidad", "publico"},
                                                          {"estado", "listo"},
                                                          {"duracion_seg", 12},
                                                          {"ruta_manifest", "videos/x/manifest.mpd"},
                                                          {"tamano_original_bytes", 10},
                                                          {"tamano_bytes", 20},
                                                          {"mensaje_error", "m"},
                                                          {"desconocido", true}});
    EXPECT_EQ(video.autorId, 4294967295u);
    EXPECT_EQ(video.etiquetas.size(), 2u);
    EXPECT_EQ(video.visibilidad, Visibilidad::Publico);
    EXPECT_EQ(video.estado, EstadoVideo::Listo);
    EXPECT_DOUBLE_EQ(video.duracionSeg, 12.0);
    EXPECT_EQ(video.tamanoBytes, 20);

    const json malos[] = {json::array(),
                          {{"titulo", 5}},
                          {{"autor_id", "7"}},
                          {{"autor_id", 0}},
                          {{"autor_id", 4294967296LL}},
                          {{"etiquetas", "a"}},
                          {{"etiquetas", {1}}},
                          {{"duracion_seg", "1"}},
                          {{"tamano_bytes", 1.5}},
                          {{"visibilidad", "oculto"}}};
    for (const auto& malo : malos) EXPECT_THROW(VideoMetadata::desdeJson(malo), ErrorApi) << malo.dump();
}
