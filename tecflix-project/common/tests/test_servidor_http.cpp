#include <gtest/gtest.h>

#include "servidor_en_segundo_plano.hpp"
#include "tecflix/errores.hpp"
#include "tecflix/utilidades_http.hpp"

using namespace tecflix;
using nlohmann::json;

namespace {

class ServidorHttpTest : public ::testing::Test {
protected:
    ServidorHttp servidor{"servicio-prueba", "1.0", "http://localhost:5173"};
    bool mongoResponde = true;

    void SetUp() override {
        servidor.agregarVerificacionSalud("mongodb", [this] { return mongoResponde; });
        auto& rutas = servidor.rutas();
        rutas.Get("/error-api", [](const httplib::Request&, httplib::Response&) {
            throw ErrorApi(TipoError::Conflicto, "estado incorrecto");
        });
        rutas.Get("/error-interno", [](const httplib::Request&, httplib::Response&) {
            throw std::runtime_error("detalle secreto");
        });
        rutas.Get("/error-desconocido", [](const httplib::Request&, httplib::Response&) { throw 42; });
        rutas.Get("/dependencia-caida", [](const httplib::Request&, httplib::Response&) {
            throw ErrorApi(TipoError::ServicioNoDisponible, "mongo no responde");
        });
        rutas.Post("/eco", [](const httplib::Request& req, httplib::Response& res) {
            http::responderJson(res, 200, http::parsearObjetoJson(req.body));
        });
        rutas.set_payload_max_length(100);
    }

    static json cuerpo(const httplib::Result& res) { return json::parse(res->body); }
};

}  // namespace

TEST_F(ServidorHttpTest, SaludReportaDependencias) {
    pruebas::ServidorEnSegundoPlano activo(servidor);
    auto cliente = activo.cliente();
    auto res = cliente->Get("/salud");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 200);
    EXPECT_EQ(cuerpo(res), json({{"estado", "ok"},
                                  {"servicio", "servicio-prueba"},
                                  {"version", "1.0"},
                                  {"dependencias", {{"mongodb", "ok"}}}}));

    mongoResponde = false;
    res = cliente->Get("/salud");
    EXPECT_EQ(res->status, 503);
    EXPECT_EQ(cuerpo(res)["estado"], "degradado");
    EXPECT_EQ(cuerpo(res)["dependencias"]["mongodb"], "sin conexion");
}

TEST_F(ServidorHttpTest, UnaVerificacionQueLanzaCuentaComoCaida) {
    servidor.agregarVerificacionSalud("otra", []() -> bool { throw std::runtime_error("x"); });
    pruebas::ServidorEnSegundoPlano activo(servidor);
    auto res = activo.cliente()->Get("/salud");
    EXPECT_EQ(res->status, 503);
    EXPECT_EQ(cuerpo(res)["dependencias"]["otra"], "sin conexion");
}

TEST_F(ServidorHttpTest, CorsConElOrigenDeLaWeb) {
    pruebas::ServidorEnSegundoPlano activo(servidor);
    auto res = activo.cliente()->Options("/videos");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 204);
    EXPECT_EQ(res->get_header_value("Access-Control-Allow-Origin"), "http://localhost:5173");
    EXPECT_NE(res->get_header_value("Access-Control-Allow-Headers").find("X-Usuario-Id"), std::string::npos);
    EXPECT_EQ(res->get_header_value("Access-Control-Allow-Headers").find("X-Token-Interno"), std::string::npos);
}

TEST_F(ServidorHttpTest, TraduceExcepcionesAlFormatoDelSpec) {
    pruebas::ServidorEnSegundoPlano activo(servidor);
    auto cliente = activo.cliente();

    auto res = cliente->Get("/error-api");
    EXPECT_EQ(res->status, 409);
    EXPECT_EQ(cuerpo(res)["error"]["codigo"], "CONFLICTO");
    EXPECT_EQ(cuerpo(res)["error"]["mensaje"], "estado incorrecto");

    res = cliente->Get("/error-interno");
    EXPECT_EQ(res->status, 500);
    EXPECT_EQ(cuerpo(res)["error"]["codigo"], "ERROR_INTERNO");
    EXPECT_EQ(res->body.find("detalle secreto"), std::string::npos);  // el detalle no sale

    EXPECT_EQ(cliente->Get("/error-desconocido")->status, 500);
    EXPECT_EQ(cliente->Get("/dependencia-caida")->status, 502);
}

TEST_F(ServidorHttpTest, ErroresPropiosDeHttplibTambienSonJson) {
    pruebas::ServidorEnSegundoPlano activo(servidor);
    auto cliente = activo.cliente();

    auto res = cliente->Get("/no-existe");
    EXPECT_EQ(res->status, 404);
    EXPECT_EQ(cuerpo(res)["error"]["codigo"], "NO_ENCONTRADO");

    res = cliente->Post("/eco", std::string(200, 'x'), "application/json");
    EXPECT_EQ(res->status, 413);
    EXPECT_EQ(cuerpo(res)["error"]["codigo"], "ARCHIVO_DEMASIADO_GRANDE");

    res = cliente->Post("/eco", "[1]", "application/json");
    EXPECT_EQ(res->status, 400);
    EXPECT_EQ(cuerpo(res)["error"]["codigo"], "DATOS_INVALIDOS");

    res = cliente->Post("/eco", R"({"a":1})", "application/json");
    EXPECT_EQ(res->status, 200);

    // 405 de httplib (método no soportado) se reporta como error interno genérico.
    httplib::Request req;
    req.method = "BREW";
    req.path = "/salud";
    auto raro = cliente->send(req);
    ASSERT_TRUE(raro);
    EXPECT_GE(raro->status, 400);
}
