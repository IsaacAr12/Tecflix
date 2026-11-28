// Pruebas de ClienteMetadataHttp y ClienteUsuariosHttp contra servidores HTTP de
// prueba que imitan los contratos del SPEC (7.2 y 6.9).
#include <gtest/gtest.h>

#include "cliente_metadata.hpp"
#include "cliente_usuarios.hpp"
#include "servidor_en_segundo_plano.hpp"
#include "tecflix/errores.hpp"
#include "tecflix/utilidades_http.hpp"

using namespace tecflix;
using namespace tecflix::streaming;
using nlohmann::json;

namespace {

const std::string kId = "656f1a2b3c4d5e6f7a8b9c0d";
const std::string kRoto = "ffffffffffffffffffffffff";

TipoError tipoDe(const std::function<void()>& accion) {
    try {
        accion();
    } catch (const ErrorApi& e) {
        return e.tipo();
    }
    ADD_FAILURE() << "se esperaba ErrorApi";
    return TipoError::ErrorInterno;
}

void errorSpec(httplib::Response& res, int estado, const char* codigo) {
    http::responderJson(res, estado, {{"error", {{"codigo", codigo}, {"mensaje", "detalle"}}}});
}

}  // namespace

TEST(ClienteMetadataHttp, ContratoConElApiDeMetadata) {
    ServidorHttp falso("metadata-falso", "1.0", "*");
    json ultimoCuerpo;
    std::string ultimoUsuario;
    auto& rutas = falso.rutas();
    rutas.Post("/videos", [&](const httplib::Request& req, httplib::Response& res) {
        http::verificarTokenInterno(req, "tok");
        ultimoCuerpo = json::parse(req.body);
        if (ultimoCuerpo.contains("titulo") && ultimoCuerpo["titulo"] == "") return errorSpec(res, 400, "DATOS_INVALIDOS");
        if (ultimoCuerpo.contains("titulo") && ultimoCuerpo["titulo"] == "sin-id") return http::responderJson(res, 201, {{"x", 1}});
        http::responderJson(res, 201, {{"id", kId}});
    });
    rutas.Get(R"(/videos/(\w+))", [&](const httplib::Request& req, httplib::Response& res) {
        ultimoUsuario = req.get_header_value("X-Usuario-Id");
        if (req.matches[1] == kRoto) return errorSpec(res, 500, "ERROR_INTERNO");
        if (req.matches[1] != kId) return errorSpec(res, 404, "NO_ENCONTRADO");
        http::responderJson(res, 200, {{"id", kId}, {"autor_id", 7}, {"estado", "listo"}, {"tamano_original_bytes", 99}});
    });
    rutas.Patch(R"(/videos/(\w+))", [&](const httplib::Request& req, httplib::Response& res) {
        http::verificarTokenInterno(req, "tok");
        if (req.matches[1] != kId) return errorSpec(res, 409, "CONFLICTO");
        http::responderJson(res, 200, json::parse(req.body));
    });
    rutas.Delete(R"(/videos/(\w+))", [&](const httplib::Request& req, httplib::Response& res) {
        http::verificarTokenInterno(req, "tok");
        if (req.matches[1] == kRoto) return errorSpec(res, 500, "ERROR_INTERNO");
        if (req.matches[1] != kId) return errorSpec(res, 404, "NO_ENCONTRADO");
        res.status = 204;
    });
    pruebas::ServidorEnSegundoPlano activo(falso);
    ClienteMetadataHttp cliente(activo.url(), "tok");

    EXPECT_EQ(cliente.crear({{"titulo", "t"}}), kId);
    EXPECT_EQ(ultimoCuerpo["titulo"], "t");
    EXPECT_EQ(tipoDe([&] { cliente.crear({{"titulo", ""}}); }), TipoError::DatosInvalidos);
    EXPECT_EQ(tipoDe([&] { cliente.crear({{"titulo", "sin-id"}}); }), TipoError::ServicioNoDisponible);

    auto info = cliente.obtener(kId, 7);
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(ultimoUsuario, "7");
    EXPECT_EQ(info->autorId, 7u);
    EXPECT_EQ(info->estado, "listo");
    EXPECT_EQ(info->tamanoOriginalBytes, 99);
    EXPECT_FALSE(cliente.obtener("aaaaaaaaaaaaaaaaaaaaaaaa", 7).has_value());
    EXPECT_EQ(tipoDe([&] { cliente.obtener(kRoto, 7); }), TipoError::ServicioNoDisponible);
    EXPECT_EQ(tipoDe([&] { cliente.obtener("../../admin", 7); }), TipoError::NoEncontrado);

    EXPECT_NO_THROW(cliente.actualizarProcesamiento(kId, {{"estado", "listo"}}));
    EXPECT_EQ(tipoDe([&] { cliente.actualizarProcesamiento("aaaaaaaaaaaaaaaaaaaaaaaa", {{"estado", "listo"}}); }),
              TipoError::Conflicto);

    EXPECT_NO_THROW(cliente.eliminar(kId));
    EXPECT_NO_THROW(cliente.eliminar("aaaaaaaaaaaaaaaaaaaaaaaa"));  // ya no existía: no es error
    EXPECT_EQ(tipoDe([&] { cliente.eliminar(kRoto); }), TipoError::ServicioNoDisponible);
}

TEST(ClienteMetadataHttp, RespuestaIlegible) {
    ServidorHttp falso("metadata-falso", "1.0", "*");
    falso.rutas().Get(R"(/videos/(\w+))", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("[1,2]", "application/json");
    });
    pruebas::ServidorEnSegundoPlano activo(falso);
    ClienteMetadataHttp cliente(activo.url(), "tok");
    EXPECT_EQ(tipoDe([&] { cliente.obtener(kId, 1); }), TipoError::ServicioNoDisponible);
}

TEST(ClienteUsuariosHttp, ContratoConElApiDeUsuarios) {
    ServidorHttp falso("usuarios-falso", "1.0", "*");
    int64_t usado = 100;
    auto& rutas = falso.rutas();
    rutas.Get(R"(/usuarios/(\d+)/cuota)", [&](const httplib::Request& req, httplib::Response& res) {
        http::verificarTokenInterno(req, "tok");
        if (req.matches[1] == "9") return http::responderJson(res, 200, {{"cuota_bytes", 1000}});
        http::responderJson(res, 200, {{"cuota_bytes", 1000}, {"usado_bytes", usado}, {"disponible_bytes", 1000 - usado}});
    });
    rutas.Get(R"(/usuarios/(\d+))", [&](const httplib::Request& req, httplib::Response& res) {
        if (req.get_header_value("X-Usuario-Id") != req.matches[1]) return errorSpec(res, 403, "SIN_PERMISO");
        if (req.matches[1] == "9") return http::responderJson(res, 200, {{"username", "viejo"}});
        if (req.matches[1] == "8") return errorSpec(res, 404, "NO_ENCONTRADO");
        http::responderJson(res, 200, {{"id", 7}, {"nombre_usuario", "isaac"}});
    });
    rutas.Post(R"(/usuarios/(\d+)/uso)", [&](const httplib::Request& req, httplib::Response& res) {
        http::verificarTokenInterno(req, "tok");
        const int64_t delta = json::parse(req.body)["delta_bytes"];
        if (usado + delta > 1000) return errorSpec(res, 413, "CUOTA_EXCEDIDA");
        usado += delta;
        http::responderJson(res, 200, {{"usado_bytes", usado}});
    });
    pruebas::ServidorEnSegundoPlano activo(falso);
    ClienteUsuariosHttp cliente(activo.url(), "tok");

    const InfoCuota cuota = cliente.obtenerCuota(7);
    EXPECT_EQ(cuota.cuotaBytes, 1000u);
    EXPECT_EQ(cuota.disponibleBytes, 900u);
    EXPECT_EQ(tipoDe([&] { cliente.obtenerCuota(9); }), TipoError::ServicioNoDisponible);  // falta un campo

    EXPECT_EQ(cliente.obtenerNombre(7), "isaac");
    EXPECT_EQ(tipoDe([&] { cliente.obtenerNombre(9); }), TipoError::ServicioNoDisponible);
    EXPECT_EQ(tipoDe([&] { cliente.obtenerNombre(8); }), TipoError::NoEncontrado);

    cliente.registrarUso(7, 500);
    EXPECT_EQ(usado, 600);
    EXPECT_EQ(tipoDe([&] { cliente.registrarUso(7, 500); }), TipoError::CuotaExcedida);
    cliente.registrarUso(7, -600);
    EXPECT_EQ(usado, 0);
}

TEST(ClienteUsuariosHttp, ApiCaido) {
    ClienteUsuariosHttp cliente("http://127.0.0.1:1", "tok");
    EXPECT_EQ(tipoDe([&] { cliente.obtenerCuota(1); }), TipoError::ServicioNoDisponible);
    ServidorHttp falso("usuarios-falso", "1.0", "*");
    falso.rutas().Get(R"(/usuarios/(\d+)/cuota)", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("no json", "text/plain");
    });
    falso.rutas().Get(R"(/usuarios/(\d+))", [](const httplib::Request&, httplib::Response& res) { res.status = 500; });
    falso.rutas().Post(R"(/usuarios/(\d+)/uso)", [](const httplib::Request&, httplib::Response& res) { res.status = 503; });
    pruebas::ServidorEnSegundoPlano activo(falso);
    ClienteUsuariosHttp conRespuestasMalas(activo.url(), "tok");
    EXPECT_EQ(tipoDe([&] { conRespuestasMalas.obtenerCuota(1); }), TipoError::ServicioNoDisponible);
    EXPECT_EQ(tipoDe([&] { conRespuestasMalas.obtenerNombre(1); }), TipoError::ServicioNoDisponible);
    EXPECT_EQ(tipoDe([&] { conRespuestasMalas.registrarUso(1, 5); }), TipoError::ServicioNoDisponible);
}
