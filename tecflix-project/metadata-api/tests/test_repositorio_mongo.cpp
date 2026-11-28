// Pruebas de RepositorioMongo contra un MongoDB real (SPEC 7.4). Las que necesitan
// servidor se ejecutan solo si existe TECFLIX_URI_MONGO_PRUEBAS; en GitHub Actions
// se levanta MongoDB como servicio.
#include <gtest/gtest.h>

#include <chrono>
#include <mongocxx/client.hpp>
#include <mongocxx/instance.hpp>
#include <mongocxx/uri.hpp>

#include "repositorio_mongo.hpp"
#include "servicio_metadata.hpp"
#include "tecflix/configuracion.hpp"
#include "tecflix/errores.hpp"

using namespace tecflix;
using namespace tecflix::metadata;

namespace {

// mongocxx::instance debe existir una sola vez por proceso.
mongocxx::instance& instanciaMongo() {
    static mongocxx::instance instancia{};
    return instancia;
}

}  // namespace

TEST(RepositorioMongoSinServidor, ValidaIds) {
    EXPECT_TRUE(RepositorioMongo::idValido("656f1a2b3c4d5e6f7a8b9c0d"));
    EXPECT_FALSE(RepositorioMongo::idValido("656f1a2b3c4d5e6f7a8b9c0"));
    EXPECT_FALSE(RepositorioMongo::idValido("656f1a2b3c4d5e6f7a8b9c0z"));
}

TEST(RepositorioMongoSinServidor, UriInvalida) {
    instanciaMongo();
    EXPECT_THROW(RepositorioMongo("esto-no-es-una-uri", "x"), ErrorConfiguracion);
}

TEST(RepositorioMongoSinServidor, ServidorInalcanzableEsServicioNoDisponible) {
    instanciaMongo();
    RepositorioMongo repositorio("mongodb://127.0.0.1:1/?serverSelectionTimeoutMS=100", "x");
    EXPECT_FALSE(repositorio.disponible());
    try {
        repositorio.crear(VideoMetadata{});
        FAIL();
    } catch (const ErrorApi& e) {
        EXPECT_EQ(e.tipo(), TipoError::ServicioNoDisponible);
    }
    EXPECT_THROW(repositorio.asegurarIndices(), ErrorApi);
    EXPECT_THROW(repositorio.listar(ConsultaListado{}), ErrorApi);
    // Con un id inválido ni siquiera se consulta la base de datos.
    EXPECT_FALSE(repositorio.obtenerPorId("no-es-un-oid").has_value());
    EXPECT_FALSE(repositorio.eliminar("no-es-un-oid"));
    EXPECT_FALSE(repositorio.actualizarDescriptivos(VideoMetadata{}));
}

class RepositorioMongoTest : public ::testing::Test {
protected:
    std::string uri;
    std::string baseDatos;
    std::unique_ptr<RepositorioMongo> repositorio;

    void SetUp() override {
        instanciaMongo();
        uri = leerEntorno("TECFLIX_URI_MONGO_PRUEBAS", "");
        if (uri.empty()) GTEST_SKIP() << "Definir TECFLIX_URI_MONGO_PRUEBAS para probar contra MongoDB";
        baseDatos = "tecflix_prueba_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        repositorio = std::make_unique<RepositorioMongo>(uri, baseDatos);
        repositorio->asegurarIndices();
    }

    void TearDown() override {
        if (uri.empty()) return;
        mongocxx::client cliente{mongocxx::uri{uri}};
        cliente[baseDatos].drop();
    }

    VideoMetadata nuevo(uint32_t autor, const std::string& titulo, Visibilidad visibilidad, EstadoVideo estado,
                        int64_t fecha) {
        VideoMetadata video;
        video.titulo = titulo;
        video.descripcion = "d";
        video.tema = "t";
        video.etiquetas = {"x", "y"};
        video.visibilidad = visibilidad;
        video.estado = estado;
        video.autorId = autor;
        video.autorNombre = "autor " + std::to_string(autor);
        video.fechaCargaMs = fecha;
        video.tamanoOriginalBytes = 100;
        video.id = repositorio->crear(video);
        return video;
    }
};

TEST_F(RepositorioMongoTest, CrudCompleto) {
    EXPECT_TRUE(repositorio->disponible());
    VideoMetadata video = nuevo(4294967295u, "Título", Visibilidad::Publico, EstadoVideo::Procesando, 1000);
    ASSERT_TRUE(RepositorioMongo::idValido(video.id));

    auto leido = repositorio->obtenerPorId(video.id);
    ASSERT_TRUE(leido.has_value());
    EXPECT_EQ(leido->titulo, "Título");
    EXPECT_EQ(leido->autorId, 4294967295u);  // uint32_t completo, guardado como int64
    EXPECT_EQ(leido->etiquetas, (std::vector<std::string>{"x", "y"}));
    EXPECT_EQ(leido->visibilidad, Visibilidad::Publico);
    EXPECT_EQ(leido->fechaCargaMs, 1000);
    EXPECT_EQ(leido->tamanoOriginalBytes, 100);
    EXPECT_EQ(leido->estado, EstadoVideo::Procesando);

    leido->titulo = "Nuevo";
    leido->visibilidad = Visibilidad::Privado;
    leido->estado = EstadoVideo::Listo;  // no debe guardarse con actualizarDescriptivos
    EXPECT_TRUE(repositorio->actualizarDescriptivos(*leido));
    EXPECT_EQ(repositorio->obtenerPorId(video.id)->estado, EstadoVideo::Procesando);

    leido->duracionSeg = 9.5;
    leido->rutaManifest = "videos/" + video.id + "/manifest.mpd";
    leido->tamanoBytes = 900;
    EXPECT_TRUE(repositorio->actualizarProcesamiento(*leido));

    auto final = repositorio->obtenerPorId(video.id);
    EXPECT_EQ(final->titulo, "Nuevo");
    EXPECT_EQ(final->visibilidad, Visibilidad::Privado);
    EXPECT_EQ(final->estado, EstadoVideo::Listo);
    EXPECT_DOUBLE_EQ(final->duracionSeg, 9.5);
    EXPECT_EQ(final->rutaManifest, "videos/" + video.id + "/manifest.mpd");
    EXPECT_EQ(final->tamanoBytes, 900);

    EXPECT_TRUE(repositorio->eliminar(video.id));
    EXPECT_FALSE(repositorio->eliminar(video.id));
    EXPECT_FALSE(repositorio->obtenerPorId(video.id).has_value());
    EXPECT_FALSE(repositorio->actualizarProcesamiento(*final));
}

TEST_F(RepositorioMongoTest, ListarFiltraOrdenaYPagina) {
    nuevo(1, "Árbol B", Visibilidad::Publico, EstadoVideo::Listo, 3000);
    nuevo(1, "arbol AVL", Visibilidad::Publico, EstadoVideo::Listo, 1000);
    nuevo(2, "Zeta privado", Visibilidad::Privado, EstadoVideo::Listo, 2000);
    nuevo(3, "Bosque", Visibilidad::Publico, EstadoVideo::Procesando, 4000);

    ConsultaListado consulta;
    consulta.solicitanteId = 2;
    consulta.orden = CampoOrden::Titulo;
    consulta.ascendente = true;
    PaginaVideos pagina = repositorio->listar(consulta);
    ASSERT_EQ(pagina.total, 3);
    EXPECT_EQ(pagina.videos[0].titulo, "arbol AVL");  // ignora mayúsculas y tildes
    EXPECT_EQ(pagina.videos[1].titulo, "Árbol B");
    EXPECT_EQ(pagina.videos[2].titulo, "Zeta privado");

    consulta.orden = CampoOrden::Fecha;
    consulta.ascendente = false;
    consulta.limite = 2;
    consulta.pagina = 2;
    pagina = repositorio->listar(consulta);
    ASSERT_EQ(pagina.videos.size(), 1u);
    EXPECT_EQ(pagina.videos[0].titulo, "arbol AVL");

    consulta.solicitanteId = 3;  // ve su propio video en proceso
    consulta.orden = CampoOrden::Autor;
    consulta.pagina = 1;
    consulta.limite = 10;
    EXPECT_EQ(repositorio->listar(consulta).total, 3);
}

TEST_F(RepositorioMongoTest, ServicioSobreMongo) {
    ServicioMetadata servicio(*repositorio);
    const VideoMetadata video = servicio.crear(
        {{"titulo", "t"}, {"autor_id", 9}, {"autor_nombre", "n"}, {"tamano_original_bytes", 200}});
    EXPECT_EQ(servicio.obtener(video.id, 9).titulo, "t");
    EXPECT_THROW(servicio.obtener(video.id, 8), ErrorApi);  // privado y en proceso
}
