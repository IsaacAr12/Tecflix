#include <gtest/gtest.h>

#include <fstream>

#include "gestor_almacenamiento.hpp"
#include "mocks_streaming.hpp"
#include "tecflix/errores.hpp"

using namespace tecflix;
using namespace tecflix::streaming;
namespace fs = std::filesystem;

namespace {
const std::string kId = "656f1a2b3c4d5e6f7a8b9c0d";
}

TEST(GestorAlmacenamiento, ValidaIds) {
    EXPECT_TRUE(GestorAlmacenamiento::idValido(kId));
    EXPECT_FALSE(GestorAlmacenamiento::idValido("656f1a2b3c4d5e6f7a8b9c0"));
    EXPECT_FALSE(GestorAlmacenamiento::idValido("656f1a2b3c4d5e6f7a8b9c0z"));
    EXPECT_FALSE(GestorAlmacenamiento::idValido("../../../../../etc/passwd"));
}

TEST(GestorAlmacenamiento, ValidaNombresDeSegmentoConLaExpresionDelSpec) {
    for (const char* valido : {"init-0.m4s", "init-12.m4s", "seg-0-00001.m4s", "seg-3-99999.m4s"}) {
        EXPECT_TRUE(GestorAlmacenamiento::nombreSegmentoValido(valido)) << valido;
    }
    for (const char* invalido : {"manifest.mpd", "../original.mp4", "seg-0-1.m4s", "seg-a-00001.m4s", "init-0.m4s.bak",
                                 "/etc/passwd", "", "init-.m4s"}) {
        EXPECT_FALSE(GestorAlmacenamiento::nombreSegmentoValido(invalido)) << invalido;
    }
}

TEST(GestorAlmacenamiento, ExtensionesPermitidas) {
    EXPECT_EQ(GestorAlmacenamiento::extensionPermitida("clase.MP4"), "mp4");
    EXPECT_EQ(GestorAlmacenamiento::extensionPermitida("a.b.webm"), "webm");
    for (const char* nombre : {"clase.mov", "c.mkv", "c.avi"}) {
        EXPECT_TRUE(GestorAlmacenamiento::extensionPermitida(nombre).has_value()) << nombre;
    }
    EXPECT_FALSE(GestorAlmacenamiento::extensionPermitida("script.sh").has_value());
    EXPECT_FALSE(GestorAlmacenamiento::extensionPermitida("sin_extension").has_value());
}

TEST(GestorAlmacenamiento, CicloDeVidaDeLosArchivos) {
    CarpetaTemporal carpeta("gestor");
    GestorAlmacenamiento gestor(carpeta.ruta() / "videos");
    EXPECT_TRUE(gestor.disponible());

    const fs::path temporal = gestor.nuevoArchivoTemporal();
    EXPECT_NE(temporal, gestor.nuevoArchivoTemporal());  // nombres únicos
    std::ofstream(temporal) << "12345";
    const fs::path original = gestor.moverOriginal(temporal, kId, "mp4");
    EXPECT_EQ(original, carpeta.ruta() / "videos" / kId / "original.mp4");
    EXPECT_FALSE(fs::exists(temporal));
    EXPECT_TRUE(fs::exists(original));

    std::ofstream(gestor.carpetaVideo(kId) / "seg-0-00001.m4s") << "123";
    EXPECT_EQ(gestor.tamanoCarpeta(kId), 8);
    EXPECT_EQ(gestor.rutaArchivo(kId, "manifest.mpd"), gestor.carpetaVideo(kId) / "manifest.mpd");
    EXPECT_THROW(gestor.rutaArchivo(kId, "original.mp4"), ErrorApi);
    EXPECT_THROW(gestor.carpetaVideo("malo"), ErrorApi);

    gestor.eliminarVideo(kId);
    EXPECT_FALSE(fs::exists(gestor.carpetaVideo(kId)));
    EXPECT_EQ(gestor.tamanoCarpeta(kId), 0);
}

TEST(GestorAlmacenamiento, NoDisponibleSiLaCarpetaDesaparece) {
    CarpetaTemporal carpeta("gestor_no");
    GestorAlmacenamiento gestor(carpeta.ruta() / "videos");
    fs::remove_all(carpeta.ruta() / "videos");
    EXPECT_FALSE(gestor.disponible());
}
