#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "mocks_streaming.hpp"
#include "servicio_streaming.hpp"

using namespace tecflix;
using namespace tecflix::streaming;
using nlohmann::json;
using ::testing::_;
using ::testing::Invoke;
using ::testing::InSequence;
using ::testing::Return;
using ::testing::StrictMock;
using ::testing::Throw;
namespace fs = std::filesystem;

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

MATCHER_P2(TieneCampo, campo, valor, "") { return arg.contains(campo) && arg.at(campo) == valor; }

class ServicioStreamingTest : public ::testing::Test {
protected:
    CarpetaTemporal carpeta{"servicio"};
    GestorAlmacenamiento almacenamiento{carpeta.ruta()};
    StrictMock<MockClienteUsuarios> usuarios;
    StrictMock<MockClienteMetadata> metadata;
    StrictMock<MockTranscodificador> transcodificador;
    PoolTranscodificacion pool{1};
    ServicioStreaming servicio{almacenamiento, usuarios, metadata, transcodificador, pool, 1000};

    void TearDown() override { pool.esperarVacia(); }

    std::unique_ptr<SubidaEnCurso> subidaConArchivo(const std::string& contenido, const std::string& nombre = "clase.mp4") {
        EXPECT_CALL(usuarios, obtenerCuota(7)).WillOnce(Return(cuotaLibre(5000)));
        auto subida = servicio.iniciarSubida(7, contenido.size() + 200);
        subida->iniciarArchivo(nombre);
        EXPECT_TRUE(subida->escribir(contenido.data(), contenido.size()));
        return subida;
    }

    static DatosSubida datos() {
        DatosSubida d;
        d.titulo = "Clase";
        d.tema = "Datos";
        d.etiquetas = std::vector<std::string>{"btree"};
        d.visibilidad = "publico";
        return d;
    }

    size_t archivosTemporales() const {
        size_t cantidad = 0;
        for ([[maybe_unused]] const auto& entrada : fs::directory_iterator(carpeta.ruta() / ".subidas")) ++cantidad;
        return cantidad;
    }
};

}  // namespace

TEST_F(ServicioStreamingTest, SubidaCompletaSigueElFlujoDelSpec) {
    auto subida = subidaConArchivo("contenido-de-video");  // 18 bytes
    {
        InSequence orden;
        EXPECT_CALL(usuarios, registrarUso(7, 18));  // paso 4: reserva
        EXPECT_CALL(usuarios, obtenerNombre(7)).WillOnce(Return("isaac"));
        EXPECT_CALL(metadata, crear(::testing::AllOf(TieneCampo("autor_id", 7), TieneCampo("autor_nombre", "isaac"),
                                                     TieneCampo("tamano_original_bytes", 18),
                                                     TieneCampo("titulo", "Clase"), TieneCampo("visibilidad", "publico"))))
            .WillOnce(Return(kId));
        EXPECT_CALL(transcodificador, transcodificar(carpeta.ruta() / kId / "original.mp4", carpeta.ruta() / kId))
            .WillOnce(Invoke(escribirPaqueteFalso));
        EXPECT_CALL(metadata, actualizarProcesamiento(
                                  kId, ::testing::AllOf(TieneCampo("estado", "listo"), TieneCampo("duracion_seg", 12.0),
                                                        TieneCampo("ruta_manifest", "videos/" + kId + "/manifest.mpd"),
                                                        TieneCampo("tamano_bytes", 25))));
    }
    EXPECT_EQ(servicio.completarSubida(7, *subida, datos()), kId);
    pool.esperarVacia();
    EXPECT_FALSE(fs::exists(carpeta.ruta() / kId / "original.mp4"));  // se borra tras transcodificar
    EXPECT_TRUE(fs::exists(carpeta.ruta() / kId / "manifest.mpd"));
    subida.reset();
    EXPECT_TRUE(fs::exists(carpeta.ruta() / kId / "manifest.mpd"));  // el destructor no borra lo entregado
    EXPECT_EQ(archivosTemporales(), 0u);
}

TEST_F(ServicioStreamingTest, RechazosTempranosAntesDeRecibirElArchivo) {
    EXPECT_EQ(tipoDe([&] { servicio.iniciarSubida(7, 1000 + ServicioStreaming::kMargenMultipart + 1); }),
              TipoError::ArchivoDemasiadoGrande);
    EXPECT_CALL(usuarios, obtenerCuota(7)).WillOnce(Return(cuotaLibre(100)));
    EXPECT_EQ(tipoDe([&] { servicio.iniciarSubida(7, 100 + ServicioStreaming::kMargenMultipart + 1); }),
              TipoError::CuotaExcedida);
    EXPECT_CALL(usuarios, obtenerCuota(7)).WillOnce(Throw(ErrorApi(TipoError::ServicioNoDisponible, "caído")));
    EXPECT_EQ(tipoDe([&] { servicio.iniciarSubida(7, 10); }), TipoError::ServicioNoDisponible);
}

TEST_F(ServicioStreamingTest, ValidacionesDelArchivo) {
    EXPECT_CALL(usuarios, obtenerCuota(7)).WillRepeatedly(Return(cuotaLibre(5000)));
    auto subida = servicio.iniciarSubida(7, 0);
    EXPECT_FALSE(subida->escribir("x", 1));  // datos antes de iniciar el archivo
    EXPECT_EQ(subida->error()->tipo(), TipoError::DatosInvalidos);
    EXPECT_EQ(tipoDe([&] { servicio.completarSubida(7, *subida, datos()); }), TipoError::DatosInvalidos);

    subida = servicio.iniciarSubida(7, 0);
    EXPECT_EQ(tipoDe([&] { subida->iniciarArchivo("virus.exe"); }), TipoError::DatosInvalidos);
    subida->iniciarArchivo("clase.MOV");
    EXPECT_EQ(tipoDe([&] { subida->iniciarArchivo("otro.mp4"); }), TipoError::DatosInvalidos);
    EXPECT_EQ(tipoDe([&] { servicio.completarSubida(7, *subida, datos()); }), TipoError::DatosInvalidos);  // vacío

    const std::string grande(600, 'x');
    EXPECT_TRUE(subida->escribir(grande.data(), grande.size()));
    EXPECT_FALSE(subida->escribir(grande.data(), grande.size()));  // 1200 > 1000
    EXPECT_EQ(subida->error()->tipo(), TipoError::ArchivoDemasiadoGrande);
    EXPECT_EQ(subida->bytes(), 600u);
    subida.reset();
    EXPECT_EQ(archivosTemporales(), 0u);  // RAII: el parcial se borra
}

TEST_F(ServicioStreamingTest, SiLaReservaFallaNoSeCreaNada) {
    auto subida = subidaConArchivo("abc");
    EXPECT_CALL(usuarios, registrarUso(7, 3)).WillOnce(Throw(ErrorApi(TipoError::CuotaExcedida, "sin espacio")));
    EXPECT_EQ(tipoDe([&] { servicio.completarSubida(7, *subida, datos()); }), TipoError::CuotaExcedida);
    subida.reset();
    EXPECT_EQ(archivosTemporales(), 0u);
}

TEST_F(ServicioStreamingTest, SiMetadataRechazaSeLiberaLaCuota) {
    auto subida = subidaConArchivo("abc");
    {
        InSequence orden;
        EXPECT_CALL(usuarios, registrarUso(7, 3));
        EXPECT_CALL(usuarios, obtenerNombre(7)).WillOnce(Return("isaac"));
        EXPECT_CALL(metadata, crear(_)).WillOnce(Throw(ErrorApi(TipoError::DatosInvalidos, "título vacío")));
        EXPECT_CALL(usuarios, registrarUso(7, -3));  // compensación
    }
    EXPECT_EQ(tipoDe([&] { servicio.completarSubida(7, *subida, DatosSubida{}); }), TipoError::DatosInvalidos);
}

TEST_F(ServicioStreamingTest, SiFallaLaCompensacionSoloSeRegistra) {
    auto subida = subidaConArchivo("abc");
    EXPECT_CALL(usuarios, registrarUso(7, 3));
    EXPECT_CALL(usuarios, obtenerNombre(7)).WillOnce(Throw(ErrorApi(TipoError::ServicioNoDisponible, "caído")));
    EXPECT_CALL(usuarios, registrarUso(7, -3)).WillOnce(Throw(ErrorApi(TipoError::ServicioNoDisponible, "caído")));
    // Se reporta el error original, no el de la compensación.
    EXPECT_EQ(tipoDe([&] { servicio.completarSubida(7, *subida, datos()); }), TipoError::ServicioNoDisponible);
}

TEST_F(ServicioStreamingTest, SiNoSePuedeMoverElArchivoSeDeshaceTodo) {
    auto subida = subidaConArchivo("abc");
    EXPECT_CALL(usuarios, registrarUso(7, 3));
    EXPECT_CALL(usuarios, obtenerNombre(7)).WillOnce(Return("isaac"));
    // Metadata devuelve un id inválido: no se puede crear la carpeta del video.
    EXPECT_CALL(metadata, crear(_)).WillOnce(Return("no-es-un-id"));
    EXPECT_CALL(metadata, eliminar("no-es-un-id")).WillOnce(Throw(ErrorApi(TipoError::ServicioNoDisponible, "x")));
    EXPECT_CALL(usuarios, registrarUso(7, -3));
    EXPECT_EQ(tipoDe([&] { servicio.completarSubida(7, *subida, datos()); }), TipoError::ErrorInterno);
}

TEST_F(ServicioStreamingTest, FallaDeTranscodificacionLimpiaLiberaYMarcaError) {
    auto subida = subidaConArchivo("abc");
    EXPECT_CALL(usuarios, registrarUso(7, 3));
    EXPECT_CALL(usuarios, obtenerNombre(7)).WillOnce(Return("isaac"));
    EXPECT_CALL(metadata, crear(_)).WillOnce(Return(kId));
    EXPECT_CALL(transcodificador, transcodificar(_, _))
        .WillOnce(Throw(ErrorApi(TipoError::DatosInvalidos, "El archivo no es un video válido")));
    EXPECT_CALL(usuarios, registrarUso(7, -3));
    EXPECT_CALL(metadata, actualizarProcesamiento(kId, ::testing::AllOf(TieneCampo("estado", "error"),
                                                                       TieneCampo("mensaje_error", "El archivo no es un video válido"))));
    servicio.completarSubida(7, *subida, datos());
    pool.esperarVacia();
    EXPECT_FALSE(fs::exists(carpeta.ruta() / kId));
}

TEST_F(ServicioStreamingTest, SiMetadataNoRespondeAlTerminarNoSeCae) {
    auto subida = subidaConArchivo("abc");
    EXPECT_CALL(usuarios, registrarUso(7, 3));
    EXPECT_CALL(usuarios, obtenerNombre(7)).WillOnce(Return("isaac"));
    EXPECT_CALL(metadata, crear(_)).WillOnce(Return(kId));
    EXPECT_CALL(transcodificador, transcodificar(_, _)).WillOnce(Invoke(escribirPaqueteFalso));
    EXPECT_CALL(metadata, actualizarProcesamiento(kId, _))
        .WillOnce(Throw(ErrorApi(TipoError::ServicioNoDisponible, "caído")));
    servicio.completarSubida(7, *subida, datos());
    pool.esperarVacia();
    EXPECT_TRUE(fs::exists(carpeta.ruta() / kId / "manifest.mpd"));  // los archivos quedan
}

TEST_F(ServicioStreamingTest, FallaTotalDeLaTranscodificacionYDeSusCompensaciones) {
    auto subida = subidaConArchivo("abc");
    EXPECT_CALL(usuarios, registrarUso(7, 3));
    EXPECT_CALL(usuarios, obtenerNombre(7)).WillOnce(Return("isaac"));
    EXPECT_CALL(metadata, crear(_)).WillOnce(Return(kId));
    EXPECT_CALL(transcodificador, transcodificar(_, _)).WillOnce(Throw(std::runtime_error(std::string(2000, 'e'))));
    EXPECT_CALL(usuarios, registrarUso(7, -3)).WillOnce(Throw(ErrorApi(TipoError::ServicioNoDisponible, "x")));
    EXPECT_CALL(metadata, actualizarProcesamiento(kId, _)).WillOnce(Invoke([](const std::string&, const json& cambios) {
        EXPECT_LE(cambios["mensaje_error"].get<std::string>().size(), 900u);  // se recorta para metadata
        throw ErrorApi(TipoError::ServicioNoDisponible, "x");
    }));
    servicio.completarSubida(7, *subida, datos());
    pool.esperarVacia();
}

TEST_F(ServicioStreamingTest, ReproduccionVerificaAccesoYEstado) {
    fs::create_directories(carpeta.ruta() / kId);
    escribirPaqueteFalso({}, carpeta.ruta() / kId);

    EXPECT_CALL(metadata, obtener(kId, 9)).WillOnce(Return(std::nullopt));
    EXPECT_EQ(tipoDe([&] { servicio.archivoParaReproducir(kId, "manifest.mpd", 9); }), TipoError::NoEncontrado);

    EXPECT_CALL(metadata, obtener(kId, 7)).WillOnce(Return(infoVideo(kId, 7, "procesando")));
    EXPECT_EQ(tipoDe([&] { servicio.archivoParaReproducir(kId, "manifest.mpd", 7); }), TipoError::Conflicto);

    EXPECT_CALL(metadata, obtener(kId, 9)).WillRepeatedly(Return(infoVideo(kId, 7, "listo")));
    EXPECT_EQ(servicio.archivoParaReproducir(kId, "seg-0-00001.m4s", 9), carpeta.ruta() / kId / "seg-0-00001.m4s");
    EXPECT_EQ(tipoDe([&] { servicio.archivoParaReproducir(kId, "seg-1-00001.m4s", 9); }), TipoError::NoEncontrado);

    // Nombres o ids inválidos se rechazan sin consultar a metadata.
    EXPECT_EQ(tipoDe([&] { servicio.archivoParaReproducir(kId, "../original.mp4", 9); }), TipoError::NoEncontrado);
    EXPECT_EQ(tipoDe([&] { servicio.archivoParaReproducir("malo", "manifest.mpd", 9); }), TipoError::NoEncontrado);
}

TEST_F(ServicioStreamingTest, EliminarSigueElFlujoDelSpec) {
    fs::create_directories(carpeta.ruta() / kId);
    escribirPaqueteFalso({}, carpeta.ruta() / kId);

    EXPECT_EQ(tipoDe([&] { servicio.eliminar("malo", 7); }), TipoError::NoEncontrado);
    EXPECT_CALL(metadata, obtener(kId, 8)).WillOnce(Return(std::nullopt));
    EXPECT_EQ(tipoDe([&] { servicio.eliminar(kId, 8); }), TipoError::NoEncontrado);
    EXPECT_CALL(metadata, obtener(kId, 9)).WillOnce(Return(infoVideo(kId, 7, "listo")));
    EXPECT_EQ(tipoDe([&] { servicio.eliminar(kId, 9); }), TipoError::SinPermiso);
    EXPECT_CALL(metadata, obtener(kId, 7)).WillOnce(Return(infoVideo(kId, 7, "procesando")));
    EXPECT_EQ(tipoDe([&] { servicio.eliminar(kId, 7); }), TipoError::Conflicto);

    {
        InSequence orden;
        EXPECT_CALL(metadata, obtener(kId, 7)).WillOnce(Return(infoVideo(kId, 7, "listo", 500)));
        EXPECT_CALL(metadata, eliminar(kId));
        EXPECT_CALL(usuarios, registrarUso(7, -500));
    }
    servicio.eliminar(kId, 7);
    EXPECT_FALSE(fs::exists(carpeta.ruta() / kId));
}

TEST_F(ServicioStreamingTest, EliminarUnVideoConErrorNoLiberaDosVeces) {
    EXPECT_CALL(metadata, obtener(kId, 7)).WillOnce(Return(infoVideo(kId, 7, "error", 500)));
    EXPECT_CALL(metadata, eliminar(kId));
    // Sin registrarUso: la cuota se liberó cuando falló la transcodificación (StrictMock lo verifica).
    servicio.eliminar(kId, 7);
    EXPECT_EQ(servicio.transcodificacionesPendientes(), 0u);
}
