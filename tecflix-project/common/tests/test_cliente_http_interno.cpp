#include <gtest/gtest.h>

#include "servidor_en_segundo_plano.hpp"
#include "tecflix/cliente_http_interno.hpp"
#include "tecflix/utilidades_http.hpp"

using namespace tecflix;
using nlohmann::json;

namespace {

class ClienteHttpInternoTest : public ::testing::Test {
protected:
    ServidorHttp destino{"destino", "1.0", "*"};

    void SetUp() override {
        auto& rutas = destino.rutas();
        rutas.Get("/usuario", [](const httplib::Request& req, httplib::Response& res) {
            http::responderJson(res, 200, {{"usuario", req.get_header_value("X-Usuario-Id")}});
        });
        rutas.Post("/eco", [](const httplib::Request& req, httplib::Response& res) {
            http::responderJson(res, 201, http::parsearObjetoJson(req.body));
        });
        rutas.Patch("/eco", [](const httplib::Request& req, httplib::Response& res) {
            http::responderJson(res, 200, http::parsearObjetoJson(req.body));
        });
        rutas.Delete("/eco", [](const httplib::Request&, httplib::Response& res) { res.status = 204; });
    }
};

}  // namespace

TEST_F(ClienteHttpInternoTest, MetodosHttp) {
    pruebas::ServidorEnSegundoPlano activo(destino);
    ClienteHttpInterno cliente(activo.url(), "tok", "API de prueba", 2);
    EXPECT_EQ(cliente.nombreServicio(), "API de prueba");
    EXPECT_EQ(json::parse(cliente.get("/usuario", {{"X-Usuario-Id", "7"}}).body)["usuario"], "7");
    EXPECT_EQ(cliente.post("/eco", {{"a", 1}}).status, 201);
    EXPECT_EQ(json::parse(cliente.patch("/eco", {{"b", 2}}).body)["b"], 2);
    EXPECT_EQ(cliente.eliminar("/eco").status, 204);
}

TEST(ClienteHttpInterno, SinRespuestaEsServicioNoDisponible) {
    ClienteHttpInterno cliente("http://127.0.0.1:1", "tok", "API caída", 1);
    try {
        cliente.get("/x");
        FAIL();
    } catch (const ErrorApi& e) {
        EXPECT_EQ(e.tipo(), TipoError::ServicioNoDisponible);
        EXPECT_NE(std::string(e.what()).find("API caída no respondió"), std::string::npos);
    }
    EXPECT_THROW(cliente.post("/x", json::object()), ErrorApi);
    EXPECT_THROW(cliente.patch("/x", json::object()), ErrorApi);
    EXPECT_THROW(cliente.eliminar("/x"), ErrorApi);
}

TEST(ClienteHttpInterno, TraduceLasRespuestasDeError) {
    ClienteHttpInterno cliente("http://x", "tok", "API de usuarios");
    auto respuesta = [](int estado, const std::string& cuerpo) {
        httplib::Response r;
        r.status = estado;
        r.body = cuerpo;
        return r;
    };
    // Se conserva el codigo y el mensaje del SPEC que envía el otro API.
    ErrorApi cuota = cliente.errorDeRespuesta(
        respuesta(413, R"({"error":{"codigo":"CUOTA_EXCEDIDA","mensaje":"Sin espacio"}})"), "reservar");
    EXPECT_EQ(cuota.tipo(), TipoError::CuotaExcedida);
    EXPECT_STREQ(cuota.what(), "Sin espacio");

    ErrorApi grande = cliente.errorDeRespuesta(
        respuesta(413, R"({"error":{"codigo":"ARCHIVO_DEMASIADO_GRANDE","mensaje":"x"}})"), "op");
    EXPECT_EQ(grande.tipo(), TipoError::ArchivoDemasiadoGrande);

    // Sin cuerpo con formato del SPEC, el tipo sale del código HTTP.
    EXPECT_EQ(cliente.errorDeRespuesta(respuesta(400, "texto"), "op").tipo(), TipoError::DatosInvalidos);
    EXPECT_EQ(cliente.errorDeRespuesta(respuesta(403, ""), "op").tipo(), TipoError::SinPermiso);
    EXPECT_EQ(cliente.errorDeRespuesta(respuesta(404, ""), "op").tipo(), TipoError::NoEncontrado);
    EXPECT_EQ(cliente.errorDeRespuesta(respuesta(409, ""), "op").tipo(), TipoError::Conflicto);
    EXPECT_EQ(cliente.errorDeRespuesta(respuesta(413, ""), "op").tipo(), TipoError::CuotaExcedida);
    EXPECT_EQ(cliente.errorDeRespuesta(respuesta(418, R"({"error":{"codigo":"RARO"}})"), "op").tipo(),
              TipoError::ServicioNoDisponible);

    // Un 5xx del otro API es "servicio no disponible", aunque traiga un codigo.
    ErrorApi caido = cliente.errorDeRespuesta(
        respuesta(500, R"({"error":{"codigo":"DATOS_INVALIDOS","mensaje":"x"}})"), "op");
    EXPECT_EQ(caido.tipo(), TipoError::ServicioNoDisponible);
    EXPECT_NE(std::string(caido.what()).find("respondió 500"), std::string::npos);

    // 401 entre APIs = TOKEN_INTERNO mal configurado.
    EXPECT_EQ(cliente.errorDeRespuesta(respuesta(401, ""), "op").tipo(), TipoError::ErrorInterno);
}
