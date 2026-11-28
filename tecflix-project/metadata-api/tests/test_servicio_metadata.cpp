#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "mock_repositorio_metadata.hpp"
#include "servicio_metadata.hpp"
#include "tecflix/errores.hpp"

using namespace tecflix;
using namespace tecflix::metadata;
using nlohmann::json;
using ::testing::_;
using ::testing::DoAll;
using ::testing::Field;
using ::testing::Return;
using ::testing::SaveArg;
using ::testing::StrictMock;

namespace {

const std::string kId = "656f1a2b3c4d5e6f7a8b9c0d";

TipoError tipoDe(const std::function<void()>& accion) {
    try {
        accion();
    } catch (const ErrorApi& e) {
        return e.tipo();
    }
    ADD_FAILURE() << "se esperaba ErrorApi";
    return TipoError::ErrorInterno;
}

class ServicioMetadataTest : public ::testing::Test {
protected:
    StrictMock<MockRepositorioMetadata> repositorio;
    ServicioMetadata servicio{repositorio};

    static json datosCreacion() {
        return {{"titulo", "Clase de árboles B"},
                {"descripcion", "Grabación"},
                {"tema", "Algoritmos"},
                {"etiquetas", {"btree", "disco"}},
                {"visibilidad", "publico"},
                {"autor_id", 7},
                {"autor_nombre", "isaac"},
                {"tamano_original_bytes", 480742}};
    }
};

}  // namespace

TEST_F(ServicioMetadataTest, CrearGuardaEnEstadoProcesando) {
    VideoMetadata guardado;
    EXPECT_CALL(repositorio, crear(_)).WillOnce(DoAll(SaveArg<0>(&guardado), Return(kId)));
    const VideoMetadata video = servicio.crear(datosCreacion());
    EXPECT_EQ(video.id, kId);
    EXPECT_EQ(guardado.estado, EstadoVideo::Procesando);
    EXPECT_EQ(guardado.autorId, 7u);
    EXPECT_EQ(guardado.autorNombre, "isaac");
    EXPECT_EQ(guardado.tamanoOriginalBytes, 480742);
    EXPECT_EQ(guardado.visibilidad, Visibilidad::Publico);
    EXPECT_GT(guardado.fechaCargaMs, 0);
}

TEST_F(ServicioMetadataTest, CrearValidaLosDatos) {
    auto sin = [&](const char* campo) {
        json datos = datosCreacion();
        datos.erase(campo);
        return tipoDe([&] { servicio.crear(datos); });
    };
    for (const char* obligatorio : {"titulo", "autor_id", "autor_nombre", "tamano_original_bytes"}) {
        EXPECT_EQ(sin(obligatorio), TipoError::DatosInvalidos) << obligatorio;
    }
    auto con = [&](const char* campo, const json& valor) {
        json datos = datosCreacion();
        datos[campo] = valor;
        return tipoDe([&] { servicio.crear(datos); });
    };
    EXPECT_EQ(con("titulo", "   "), TipoError::DatosInvalidos);
    EXPECT_EQ(con("titulo", std::string(201, 'x')), TipoError::DatosInvalidos);
    EXPECT_EQ(con("descripcion", std::string(5001, 'x')), TipoError::DatosInvalidos);
    EXPECT_EQ(con("tema", std::string(101, 'x')), TipoError::DatosInvalidos);
    EXPECT_EQ(con("etiquetas", json(std::vector<std::string>(21, "x"))), TipoError::DatosInvalidos);
    EXPECT_EQ(con("etiquetas", {"a", "a"}), TipoError::DatosInvalidos);
    EXPECT_EQ(con("etiquetas", {""}), TipoError::DatosInvalidos);
    EXPECT_EQ(con("etiquetas", {std::string(51, 'x')}), TipoError::DatosInvalidos);
    EXPECT_EQ(con("autor_nombre", " "), TipoError::DatosInvalidos);
    EXPECT_EQ(con("autor_nombre", std::string(101, 'x')), TipoError::DatosInvalidos);
    EXPECT_EQ(con("tamano_original_bytes", 0), TipoError::DatosInvalidos);
    EXPECT_EQ(con("estado", "listo"), TipoError::DatosInvalidos);  // campo no permitido al crear
    EXPECT_EQ(tipoDe([&] { servicio.crear(json::array()); }), TipoError::DatosInvalidos);
}

TEST_F(ServicioMetadataTest, ObtenerRespetaLaVisibilidad) {
    EXPECT_CALL(repositorio, obtenerPorId(kId))
        .WillRepeatedly(Return(videoDePrueba(1, Visibilidad::Privado, EstadoVideo::Listo)));
    EXPECT_NO_THROW(servicio.obtener(kId, 1));
    EXPECT_EQ(tipoDe([&] { servicio.obtener(kId, 2); }), TipoError::NoEncontrado);  // no se revela

    EXPECT_CALL(repositorio, obtenerPorId("otro")).WillOnce(Return(std::nullopt));
    EXPECT_EQ(tipoDe([&] { servicio.obtener("otro", 1); }), TipoError::NoEncontrado);
}

TEST_F(ServicioMetadataTest, ListarValidaYDelegaEnElRepositorio) {
    ConsultaListado consulta;
    consulta.solicitanteId = 3;
    consulta.limite = 50;
    PaginaVideos pagina;
    pagina.total = 1;
    pagina.videos.push_back(videoDePrueba(1, Visibilidad::Publico, EstadoVideo::Listo));
    EXPECT_CALL(repositorio, listar(Field(&ConsultaListado::limite, 50))).WillOnce(Return(pagina));
    EXPECT_EQ(servicio.listar(consulta).total, 1);

    consulta.limite = 51;
    EXPECT_EQ(tipoDe([&] { servicio.listar(consulta); }), TipoError::DatosInvalidos);
    consulta.limite = 0;
    EXPECT_EQ(tipoDe([&] { servicio.listar(consulta); }), TipoError::DatosInvalidos);
    consulta.limite = 10;
    consulta.pagina = 0;
    EXPECT_EQ(tipoDe([&] { servicio.listar(consulta); }), TipoError::DatosInvalidos);
}

TEST_F(ServicioMetadataTest, ActualizarDescriptivosSoloElAutor) {
    EXPECT_CALL(repositorio, obtenerPorId(kId))
        .WillRepeatedly(Return(videoDePrueba(1, Visibilidad::Publico, EstadoVideo::Listo)));
    VideoMetadata guardado;
    EXPECT_CALL(repositorio, actualizarDescriptivos(_)).WillOnce(DoAll(SaveArg<0>(&guardado), Return(true)));

    const VideoMetadata video = servicio.actualizarDescriptivos(
        kId, 1, {{"titulo", "Nuevo"}, {"descripcion", "d"}, {"tema", "t"}, {"etiquetas", {"x"}}, {"visibilidad", "privado"}});
    EXPECT_EQ(video.titulo, "Nuevo");
    EXPECT_EQ(guardado.visibilidad, Visibilidad::Privado);
    EXPECT_EQ(guardado.etiquetas, std::vector<std::string>{"x"});

    EXPECT_EQ(tipoDe([&] { servicio.actualizarDescriptivos(kId, 2, {{"titulo", "x"}}); }), TipoError::SinPermiso);
    EXPECT_EQ(tipoDe([&] { servicio.actualizarDescriptivos(kId, 1, {{"estado", "listo"}}); }),
              TipoError::DatosInvalidos);
    EXPECT_EQ(tipoDe([&] { servicio.actualizarDescriptivos(kId, 1, {{"titulo", ""}}); }), TipoError::DatosInvalidos);
}

TEST_F(ServicioMetadataTest, ActualizarDescriptivosDeUnVideoQueDesaparece) {
    EXPECT_CALL(repositorio, obtenerPorId(kId))
        .WillOnce(Return(videoDePrueba(1, Visibilidad::Publico, EstadoVideo::Listo)));
    EXPECT_CALL(repositorio, actualizarDescriptivos(_)).WillOnce(Return(false));
    EXPECT_EQ(tipoDe([&] { servicio.actualizarDescriptivos(kId, 1, {{"titulo", "x"}}); }), TipoError::NoEncontrado);
}

TEST_F(ServicioMetadataTest, ProcesamientoSigueLasTransiciones) {
    EXPECT_CALL(repositorio, obtenerPorId(kId))
        .WillRepeatedly(Return(videoDePrueba(1, Visibilidad::Publico, EstadoVideo::Procesando)));
    VideoMetadata guardado;
    EXPECT_CALL(repositorio, actualizarProcesamiento(_)).WillOnce(DoAll(SaveArg<0>(&guardado), Return(true)));
    servicio.actualizarProcesamiento(kId, {{"estado", "listo"},
                                           {"duracion_seg", 10.0},
                                           {"ruta_manifest", "videos/x/manifest.mpd"},
                                           {"tamano_bytes", 900},
                                           {"mensaje_error", "se ignora si no es error"}});
    EXPECT_EQ(guardado.estado, EstadoVideo::Listo);
    EXPECT_DOUBLE_EQ(guardado.duracionSeg, 10.0);
    EXPECT_EQ(guardado.rutaManifest, "videos/x/manifest.mpd");
    EXPECT_EQ(guardado.tamanoBytes, 900);
    EXPECT_TRUE(guardado.mensajeError.empty());

    EXPECT_CALL(repositorio, actualizarProcesamiento(_)).WillOnce(DoAll(SaveArg<0>(&guardado), Return(true)));
    servicio.actualizarProcesamiento(kId, {{"estado", "error"}, {"mensaje_error", "códec no soportado"}});
    EXPECT_EQ(guardado.mensajeError, "códec no soportado");
}

TEST_F(ServicioMetadataTest, ProcesamientoRechazaCambiosInvalidos) {
    EXPECT_CALL(repositorio, obtenerPorId(kId))
        .WillRepeatedly(Return(videoDePrueba(1, Visibilidad::Publico, EstadoVideo::Listo)));
    EXPECT_EQ(tipoDe([&] { servicio.actualizarProcesamiento(kId, {{"estado", "error"}}); }), TipoError::Conflicto);
    EXPECT_EQ(tipoDe([&] { servicio.actualizarProcesamiento(kId, json::object()); }), TipoError::DatosInvalidos);
    EXPECT_EQ(tipoDe([&] { servicio.actualizarProcesamiento(kId, {{"estado", "listo"}, {"titulo", "x"}}); }),
              TipoError::DatosInvalidos);

    EXPECT_CALL(repositorio, obtenerPorId("procesando"))
        .WillRepeatedly(Return(videoDePrueba(1, Visibilidad::Publico, EstadoVideo::Procesando)));
    auto con = [&](const json& cambios) { return tipoDe([&] { servicio.actualizarProcesamiento("procesando", cambios); }); };
    EXPECT_EQ(con({{"estado", "listo"}, {"duracion_seg", -1}}), TipoError::DatosInvalidos);
    EXPECT_EQ(con({{"estado", "listo"}, {"tamano_bytes", -1}}), TipoError::DatosInvalidos);
    EXPECT_EQ(con({{"estado", "listo"}, {"ruta_manifest", std::string(301, 'x')}}), TipoError::DatosInvalidos);
    EXPECT_EQ(con({{"estado", "procesando"}}), TipoError::Conflicto);

    EXPECT_CALL(repositorio, obtenerPorId("nada")).WillOnce(Return(std::nullopt));
    EXPECT_EQ(tipoDe([&] { servicio.actualizarProcesamiento("nada", {{"estado", "listo"}}); }), TipoError::NoEncontrado);

    EXPECT_CALL(repositorio, actualizarProcesamiento(_)).WillOnce(Return(false));
    EXPECT_EQ(con({{"estado", "listo"}}), TipoError::NoEncontrado);
}

TEST_F(ServicioMetadataTest, EliminarYSalud) {
    EXPECT_CALL(repositorio, eliminar(kId)).WillOnce(Return(true)).WillOnce(Return(false));
    EXPECT_NO_THROW(servicio.eliminar(kId));
    EXPECT_EQ(tipoDe([&] { servicio.eliminar(kId); }), TipoError::NoEncontrado);
    EXPECT_CALL(repositorio, disponible()).WillOnce(Return(false));
    EXPECT_FALSE(servicio.baseDeDatosDisponible());
}
