#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "controlador_metadata.hpp"
#include "mock_repositorio_metadata.hpp"
#include "servidor_en_segundo_plano.hpp"
#include "tecflix/errores.hpp"

using namespace tecflix;
using namespace tecflix::metadata;
using nlohmann::json;
using ::testing::_;
using ::testing::AllOf;
using ::testing::Field;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::Throw;

namespace {

const std::string kId = "656f1a2b3c4d5e6f7a8b9c0d";
const char* kToken = "token-de-prueba";

// Controlador real + servicio real + repositorio simulado, probados por HTTP.
class ControladorMetadataTest : public ::testing::Test {
protected:
    NiceMock<MockRepositorioMetadata> repositorio;
    ServicioMetadata servicio{repositorio};
    ServidorHttp servidor{"metadata-api", "1.0", "http://localhost:5173"};
    ControladorMetadata controlador{servicio, kToken};
    std::unique_ptr<pruebas::ServidorEnSegundoPlano> activo;
    std::unique_ptr<httplib::Client> cliente;

    void SetUp() override {
        servidor.agregarVerificacionSalud("mongodb", [this] { return servicio.baseDeDatosDisponible(); });
        controlador.registrarRutas(servidor.rutas());
        activo = std::make_unique<pruebas::ServidorEnSegundoPlano>(servidor);
        cliente = activo->cliente();
    }

    static httplib::Headers usuario(const std::string& id) { return {{"X-Usuario-Id", id}}; }
    static httplib::Headers interno() { return {{"X-Token-Interno", kToken}}; }
    static json cuerpo(const httplib::Result& res) { return json::parse(res->body); }
    static std::string codigo(const httplib::Result& res) { return cuerpo(res)["error"]["codigo"]; }
};

}  // namespace

TEST_F(ControladorMetadataTest, Salud) {
    EXPECT_CALL(repositorio, disponible()).WillOnce(Return(true));
    auto res = cliente->Get("/salud");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 200);
    EXPECT_EQ(cuerpo(res)["servicio"], "metadata-api");
    EXPECT_EQ(cuerpo(res)["dependencias"]["mongodb"], "ok");
}

TEST_F(ControladorMetadataTest, CrearEsInterno) {
    const json datos = {{"titulo", "t"}, {"autor_id", 7}, {"autor_nombre", "n"}, {"tamano_original_bytes", 10}};
    EXPECT_EQ(codigo(cliente->Post("/videos", datos.dump(), "application/json")), "NO_AUTENTICADO");
    EXPECT_EQ(cliente->Post("/videos", {{"X-Token-Interno", "malo"}}, datos.dump(), "application/json")->status, 401);

    EXPECT_CALL(repositorio, crear(_)).WillOnce(Return(kId));
    auto res = cliente->Post("/videos", interno(), datos.dump(), "application/json");
    ASSERT_EQ(res->status, 201);
    EXPECT_EQ(cuerpo(res)["id"], kId);
    EXPECT_EQ(cuerpo(res)["estado"], "procesando");

    res = cliente->Post("/videos", interno(), "no es json", "application/json");
    EXPECT_EQ(res->status, 400);
    EXPECT_EQ(codigo(res), "DATOS_INVALIDOS");
}

TEST_F(ControladorMetadataTest, ListadoConParametros) {
    PaginaVideos pagina;
    pagina.total = 45;
    pagina.videos.push_back(videoDePrueba(1, Visibilidad::Publico, EstadoVideo::Listo));
    EXPECT_CALL(repositorio, listar(AllOf(Field(&ConsultaListado::solicitanteId, 9u),
                                          Field(&ConsultaListado::orden, CampoOrden::Titulo),
                                          Field(&ConsultaListado::ascendente, true),
                                          Field(&ConsultaListado::pagina, 2), Field(&ConsultaListado::limite, 20))))
        .WillOnce(Return(pagina));
    auto res = cliente->Get("/videos?ordenar=titulo&pagina=2", usuario("9"));
    ASSERT_EQ(res->status, 200);
    EXPECT_EQ(cuerpo(res)["total"], 45);
    EXPECT_EQ(cuerpo(res)["total_paginas"], 3);
    EXPECT_EQ(cuerpo(res)["limite"], 20);
    EXPECT_EQ(cuerpo(res)["videos"].size(), 1u);

    EXPECT_CALL(repositorio, listar(AllOf(Field(&ConsultaListado::orden, CampoOrden::Autor),
                                          Field(&ConsultaListado::ascendente, false))))
        .WillOnce(Return(PaginaVideos{}));
    EXPECT_EQ(cliente->Get("/videos?ordenar=autor&orden=desc", usuario("9"))->status, 200);

    EXPECT_CALL(repositorio, listar(AllOf(Field(&ConsultaListado::orden, CampoOrden::Fecha),
                                          Field(&ConsultaListado::ascendente, false))))
        .WillOnce(Return(PaginaVideos{}));
    EXPECT_EQ(cuerpo(cliente->Get("/videos", usuario("9")))["total_paginas"], 0);
}

TEST_F(ControladorMetadataTest, ParametrosInvalidos) {
    EXPECT_EQ(cliente->Get("/videos")->status, 401);
    EXPECT_EQ(cliente->Get("/videos", usuario("0"))->status, 401);
    for (const char* ruta : {"/videos?pagina=x", "/videos?pagina=1x", "/videos?limite=99999999999",
                             "/videos?ordenar=duracion", "/videos?orden=arriba", "/videos?limite=51"}) {
        auto res = cliente->Get(ruta, usuario("1"));
        EXPECT_EQ(res->status, 400) << ruta;
        EXPECT_EQ(codigo(res), "DATOS_INVALIDOS") << ruta;
    }
}

TEST_F(ControladorMetadataTest, DetalleYEdicion) {
    ON_CALL(repositorio, obtenerPorId(kId)).WillByDefault(Return(videoDePrueba(1, Visibilidad::Privado, EstadoVideo::Listo)));
    EXPECT_EQ(cliente->Get("/videos/" + kId, usuario("1"))->status, 200);
    EXPECT_EQ(codigo(cliente->Get("/videos/" + kId, usuario("2"))), "NO_ENCONTRADO");

    EXPECT_CALL(repositorio, actualizarDescriptivos(Field(&VideoMetadata::titulo, "Nuevo"))).WillOnce(Return(true));
    auto res = cliente->Put("/videos/" + kId, usuario("1"), R"({"titulo":"Nuevo"})", "application/json");
    ASSERT_EQ(res->status, 200);
    EXPECT_EQ(cuerpo(res)["titulo"], "Nuevo");

    ON_CALL(repositorio, obtenerPorId("publico")).WillByDefault(Return(videoDePrueba(1, Visibilidad::Publico, EstadoVideo::Listo)));
    res = cliente->Put("/videos/publico", usuario("2"), R"({"titulo":"x"})", "application/json");
    EXPECT_EQ(res->status, 403);
    EXPECT_EQ(codigo(res), "SIN_PERMISO");
}

TEST_F(ControladorMetadataTest, ProcesamientoYEliminacionSonInternos) {
    ON_CALL(repositorio, obtenerPorId(kId))
        .WillByDefault(Return(videoDePrueba(1, Visibilidad::Publico, EstadoVideo::Procesando)));
    const std::string listo = R"({"estado":"listo","duracion_seg":3.5,"tamano_bytes":10})";
    EXPECT_EQ(cliente->Patch("/videos/" + kId, usuario("1"), listo, "application/json")->status, 401);

    EXPECT_CALL(repositorio, actualizarProcesamiento(_)).WillOnce(Return(true));
    auto res = cliente->Patch("/videos/" + kId, interno(), listo, "application/json");
    ASSERT_EQ(res->status, 200);
    EXPECT_EQ(cuerpo(res)["estado"], "listo");

    EXPECT_EQ(cliente->Delete("/videos/" + kId, usuario("1"))->status, 401);
    EXPECT_CALL(repositorio, eliminar(kId)).WillOnce(Return(true));
    EXPECT_EQ(cliente->Delete("/videos/" + kId, interno())->status, 204);
}

TEST_F(ControladorMetadataTest, MongoCaidoEs502) {
    EXPECT_CALL(repositorio, listar(_))
        .WillOnce(Throw(ErrorApi(TipoError::ServicioNoDisponible, "MongoDB (listar): timeout")));
    auto res = cliente->Get("/videos", usuario("1"));
    EXPECT_EQ(res->status, 502);
    EXPECT_EQ(codigo(res), "SERVICIO_NO_DISPONIBLE");
}
