#include <gtest/gtest.h>

#include <chrono>
#include <thread>

#include "opentelemetry/exporters/memory/in_memory_metric_data.h"
#include "opentelemetry/exporters/memory/in_memory_metric_exporter_factory.h"
#include "opentelemetry/exporters/memory/in_memory_span_data.h"
#include "opentelemetry/exporters/memory/in_memory_span_exporter_factory.h"
#include "opentelemetry/trace/scope.h"
#include "servidor_en_segundo_plano.hpp"
#include "tecflix/cliente_http_interno.hpp"
#include "tecflix/telemetria.hpp"
#include "tecflix/utilidades_http.hpp"

using namespace tecflix;
namespace memoria = opentelemetry::exporter::memory;

namespace {

// Instala exportadores en memoria para inspeccionar los spans y las métricas.
class TelemetriaTest : public ::testing::Test {
protected:
    std::shared_ptr<memoria::InMemorySpanData> spans;
    std::shared_ptr<memoria::SimpleAggregateInMemoryMetricData> metricas =
        std::make_shared<memoria::SimpleAggregateInMemoryMetricData>();

    void SetUp() override {
        auto exportadorSpans = memoria::InMemorySpanExporterFactory::Create(spans);
        Telemetria::inicializarConExportadores("prueba", "1.0", std::move(exportadorSpans),
                                               memoria::InMemoryMetricExporterFactory::Create(metricas), false);
    }
    void TearDown() override { Telemetria::finalizar(); }

    // El span del servidor termina después de enviar la respuesta: se espera un poco.
    std::vector<std::unique_ptr<opentelemetry::sdk::trace::SpanData>> esperarSpans(size_t cantidad) {
        std::vector<std::unique_ptr<opentelemetry::sdk::trace::SpanData>> todos;
        for (int intento = 0; intento < 200 && todos.size() < cantidad; ++intento) {
            for (auto& span : spans->GetSpans()) todos.push_back(std::move(span));
            if (todos.size() < cantidad) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return todos;
    }
};

}  // namespace

TEST_F(TelemetriaTest, UnSpanPorPeticionQueContinuaElTraceRecibido) {
    ServidorHttp servidor("prueba", "1.0", "*");
    pruebas::ServidorEnSegundoPlano activo(servidor);
    const std::string traceparent = "00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01";
    auto res = activo.cliente()->Get("/videos/656f1a2b3c4d5e6f7a8b9c0d", {{"traceparent", traceparent}});
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 404);

    auto recibidos = esperarSpans(1);
    ASSERT_EQ(recibidos.size(), 1u);
    const auto& span = *recibidos[0];
    EXPECT_EQ(std::string(span.GetName()), "GET /videos/:id");
    char traceId[32];
    span.GetTraceId().ToLowerBase16(traceId);
    EXPECT_EQ(std::string(traceId, 32), "4bf92f3577b34da6a3ce929d0e0e4736");
    const auto& atributos = span.GetAttributes();
    EXPECT_EQ(opentelemetry::nostd::get<int32_t>(atributos.at("http.response.status_code")), 404);
}

TEST_F(TelemetriaTest, ElClienteInternoPropagaTraceparentYElToken) {
    ServidorHttp destino("destino", "1.0", "*");
    std::string traceparentRecibido;
    std::string tokenRecibido;
    destino.rutas().Get("/eco", [&](const httplib::Request& req, httplib::Response& res) {
        traceparentRecibido = req.get_header_value("traceparent");
        tokenRecibido = req.get_header_value("X-Token-Interno");
        res.status = 204;
    });
    pruebas::ServidorEnSegundoPlano activo(destino);

    auto span = Telemetria::tracer()->StartSpan("operacion");
    {
        opentelemetry::trace::Scope alcance(span);
        ClienteHttpInterno cliente(activo.url(), "tok", "destino");
        EXPECT_EQ(cliente.get("/eco").status, 204);
        char traceId[32];
        Telemetria::contextoActual().trace_id().ToLowerBase16(traceId);
        EXPECT_NE(traceparentRecibido.find(std::string(traceId, 32)), std::string::npos);
    }
    span->End();
    EXPECT_EQ(tokenRecibido, "tok");
}

TEST_F(TelemetriaTest, RegistraLasMetricasHttp) {
    {
        ServidorHttp servidor("prueba", "1.0", "*");
        servidor.rutas().Get("/falla", [](const httplib::Request&, httplib::Response&) {
            throw std::runtime_error("x");
        });
        pruebas::ServidorEnSegundoPlano activo(servidor);
        activo.cliente()->Get("/salud");
        activo.cliente()->Get("/falla");
        esperarSpans(2);
        Contador contador("prueba_total", "contador de prueba");
        contador.sumar(3, {{"tipo", "a"}});
        ContadorBidireccional cola("prueba_cola", "cola de prueba");
        cola.sumar(2);
        cola.sumar(-1);
    }
    Telemetria::finalizar();  // vacía las métricas pendientes hacia el exportador en memoria
    EXPECT_FALSE(metricas->Get("tecflix", "http_peticiones_total").empty());
    EXPECT_FALSE(metricas->Get("tecflix", "http_duracion_ms").empty());
    EXPECT_FALSE(metricas->Get("tecflix", "http_errores_total").empty());
    EXPECT_FALSE(metricas->Get("tecflix", "prueba_total").empty());
    EXPECT_FALSE(metricas->Get("tecflix", "prueba_cola").empty());
}

TEST(TelemetriaOtlp, SeInicializaYFinalizaAunqueNoHayaCollector) {
    Telemetria::inicializar("prueba", "1.0", "http://127.0.0.1:1");
    auto span = Telemetria::tracer()->StartSpan("sin-collector");
    span->End();
    Histograma histograma("prueba_ms", "histograma de prueba", "ms");
    histograma.registrar(1.5);
    Telemetria::finalizar();
    SUCCEED();
}
