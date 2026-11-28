#include <gtest/gtest.h>

#include "tecflix/utilidades_http.hpp"

using namespace tecflix;

TEST(UtilidadesHttp, ParseaIdsDeUsuario) {
    uint32_t id = 0;
    EXPECT_TRUE(http::parsearIdUsuario("1", id));
    EXPECT_EQ(id, 1u);
    EXPECT_TRUE(http::parsearIdUsuario("4294967295", id));
    EXPECT_EQ(id, 4294967295u);
    EXPECT_FALSE(http::parsearIdUsuario("0", id));
    EXPECT_FALSE(http::parsearIdUsuario("4294967296", id));
    EXPECT_FALSE(http::parsearIdUsuario("99999999999", id));
    EXPECT_FALSE(http::parsearIdUsuario("-1", id));
    EXPECT_FALSE(http::parsearIdUsuario("12a", id));
    EXPECT_FALSE(http::parsearIdUsuario("", id));
}

TEST(UtilidadesHttp, UsuarioSolicitante) {
    httplib::Request req;
    try {
        http::usuarioSolicitante(req);
        FAIL();
    } catch (const ErrorApi& e) {
        EXPECT_EQ(e.tipo(), TipoError::NoAutenticado);
    }
    req.headers.emplace("X-Usuario-Id", "abc");
    EXPECT_THROW(http::usuarioSolicitante(req), ErrorApi);
    req.headers.clear();
    req.headers.emplace("x-usuario-id", "15");  // los encabezados no distinguen mayúsculas
    EXPECT_EQ(http::usuarioSolicitante(req), 15u);
}

TEST(UtilidadesHttp, TokenInterno) {
    httplib::Request req;
    EXPECT_FALSE(http::tieneTokenInterno(req, "secreto"));
    req.headers.emplace("X-Token-Interno", "secretx");
    EXPECT_FALSE(http::tieneTokenInterno(req, "secreto"));
    EXPECT_FALSE(http::tieneTokenInterno(req, ""));  // un token vacío nunca es válido
    EXPECT_THROW(http::verificarTokenInterno(req, "secreto"), ErrorApi);
    req.headers.clear();
    req.headers.emplace("X-Token-Interno", "secreto");
    EXPECT_TRUE(http::tieneTokenInterno(req, "secreto"));
    EXPECT_NO_THROW(http::verificarTokenInterno(req, "secreto"));
}

TEST(UtilidadesHttp, ParseaObjetosJson) {
    EXPECT_EQ(http::parsearObjetoJson(R"({"a":1})")["a"], 1);
    EXPECT_THROW(http::parsearObjetoJson(""), ErrorApi);
    EXPECT_THROW(http::parsearObjetoJson("[1,2]"), ErrorApi);
    EXPECT_THROW(http::parsearObjetoJson("{malo"), ErrorApi);
}

TEST(UtilidadesHttp, RespondeErroresConElFormatoDelSpec) {
    httplib::Response res;
    http::responderError(res, ErrorApi(TipoError::CuotaExcedida, "Sin espacio"));
    EXPECT_EQ(res.status, 413);
    const auto cuerpo = nlohmann::json::parse(res.body);
    EXPECT_EQ(cuerpo["error"]["codigo"], "CUOTA_EXCEDIDA");
    EXPECT_EQ(cuerpo["error"]["mensaje"], "Sin espacio");
}

TEST(UtilidadesHttp, NormalizaRutas) {
    EXPECT_EQ(http::normalizarRuta("/"), "/");
    EXPECT_EQ(http::normalizarRuta("/videos/656f1a2b3c4d5e6f7a8b9c0d"), "/videos/:id");
    EXPECT_EQ(http::normalizarRuta("/usuarios/15/cuota"), "/usuarios/:id/cuota");
    EXPECT_EQ(http::normalizarRuta("/videos/656f1a2b3c4d5e6f7a8b9c0d/seg-0-00001.m4s"), "/videos/:id/:archivo");
    EXPECT_EQ(http::normalizarRuta("//salud/"), "/salud");
}
