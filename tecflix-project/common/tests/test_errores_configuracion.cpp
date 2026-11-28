#include <gtest/gtest.h>

#include <cstdlib>

#include "tecflix/configuracion.hpp"
#include "tecflix/errores.hpp"

using namespace tecflix;

TEST(Errores, CodigosDelSpec) {
    const struct {
        TipoError tipo;
        int http;
        const char* codigo;
    } casos[] = {
        {TipoError::DatosInvalidos, 400, "DATOS_INVALIDOS"},
        {TipoError::NoAutenticado, 401, "NO_AUTENTICADO"},
        {TipoError::SinPermiso, 403, "SIN_PERMISO"},
        {TipoError::NoEncontrado, 404, "NO_ENCONTRADO"},
        {TipoError::Conflicto, 409, "CONFLICTO"},
        {TipoError::CuotaExcedida, 413, "CUOTA_EXCEDIDA"},
        {TipoError::ArchivoDemasiadoGrande, 413, "ARCHIVO_DEMASIADO_GRANDE"},
        {TipoError::ErrorInterno, 500, "ERROR_INTERNO"},
        {TipoError::ServicioNoDisponible, 502, "SERVICIO_NO_DISPONIBLE"},
    };
    for (const auto& caso : casos) {
        ErrorApi error(caso.tipo, "mensaje");
        EXPECT_EQ(error.codigoHttp(), caso.http);
        EXPECT_STREQ(error.codigo(), caso.codigo);
        EXPECT_EQ(error.tipo(), caso.tipo);
        EXPECT_STREQ(error.what(), "mensaje");
    }
}

TEST(Configuracion, LeeVariablesDeEntorno) {
    setenv("TECFLIX_PRUEBA_TEXTO", "hola", 1);
    setenv("TECFLIX_PRUEBA_NUMERO", "42", 1);
    unsetenv("TECFLIX_PRUEBA_AUSENTE");
    EXPECT_EQ(leerEntorno("TECFLIX_PRUEBA_TEXTO", "x"), "hola");
    EXPECT_EQ(leerEntorno("TECFLIX_PRUEBA_AUSENTE", "x"), "x");
    EXPECT_EQ(leerEntornoObligatorio("TECFLIX_PRUEBA_TEXTO"), "hola");
    EXPECT_THROW(leerEntornoObligatorio("TECFLIX_PRUEBA_AUSENTE"), ErrorConfiguracion);
    EXPECT_EQ(leerEntornoEntero("TECFLIX_PRUEBA_NUMERO", 1), 42);
    EXPECT_EQ(leerEntornoEntero("TECFLIX_PRUEBA_AUSENTE", 7), 7);
    setenv("TECFLIX_PRUEBA_NUMERO", "4x", 1);
    EXPECT_THROW(leerEntornoEntero("TECFLIX_PRUEBA_NUMERO", 1), ErrorConfiguracion);
    setenv("TECFLIX_PRUEBA_NUMERO", "abc", 1);
    EXPECT_THROW(leerEntornoEntero("TECFLIX_PRUEBA_NUMERO", 1), ErrorConfiguracion);
}
