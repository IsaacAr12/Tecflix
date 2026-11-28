#ifndef TECFLIX_TELEMETRIA_HPP
#define TECFLIX_TELEMETRIA_HPP

#include <httplib.h>

#include <cstdint>
#include <map>
#include <memory>
#include <string>

#include "opentelemetry/context/context.h"
#include "opentelemetry/metrics/meter.h"
#include "opentelemetry/metrics/sync_instruments.h"
#include "opentelemetry/nostd/shared_ptr.h"
#include "opentelemetry/nostd/unique_ptr.h"
#include "opentelemetry/sdk/metrics/push_metric_exporter.h"
#include "opentelemetry/sdk/trace/exporter.h"
#include "opentelemetry/trace/span_context.h"
#include "opentelemetry/trace/tracer.h"

namespace tecflix {

using Atributos = std::map<std::string, std::string>;

// Configuración de OpenTelemetry (SPEC 9). Los APIs envían traces y métricas por
// OTLP/HTTP al collector. Mientras no se llame inicializar(), los proveedores globales
// son "no-op": el código instrumentado funciona igual, pero no exporta nada (así
// corren las pruebas unitarias).
//
// Importante: inicializar() debe llamarse en main() ANTES de crear servidores,
// servicios y repositorios, porque los instrumentos de métricas se crean en sus
// constructores y quedan ligados al proveedor que exista en ese momento.
class Telemetria {
public:
    // endpoint: por ejemplo "http://otel-collector:4318" (OTEL_EXPORTER_OTLP_ENDPOINT).
    static void inicializar(const std::string& servicio, const std::string& version, const std::string& endpoint);

    // Igual que inicializar(), pero con exportadores dados (las pruebas usan los de memoria).
    // procesarEnLotes = false exporta cada span al terminar, sin esperar.
    static void inicializarConExportadores(const std::string& servicio, const std::string& version,
                                           std::unique_ptr<opentelemetry::sdk::trace::SpanExporter> traces,
                                           std::unique_ptr<opentelemetry::sdk::metrics::PushMetricExporter> metricas,
                                           bool procesarEnLotes);

    // Envía lo pendiente y vuelve a los proveedores no-op.
    static void finalizar();

    static opentelemetry::nostd::shared_ptr<opentelemetry::trace::Tracer> tracer();
    static opentelemetry::nostd::shared_ptr<opentelemetry::metrics::Meter> medidor();

    // Propagación W3C (encabezado traceparent) entre APIs.
    static void inyectarContexto(httplib::Headers& encabezados);
    static opentelemetry::context::Context extraerContexto(const httplib::Headers& encabezados);

    // Contexto del span activo en este hilo (para continuar el trace en otro hilo).
    static opentelemetry::trace::SpanContext contextoActual();
};

// Envoltorios de los instrumentos de métricas con atributos de texto.
class Contador {
public:
    Contador(const std::string& nombre, const std::string& descripcion, const std::string& unidad = "");
    void sumar(uint64_t valor = 1, const Atributos& atributos = {});

private:
    opentelemetry::nostd::unique_ptr<opentelemetry::metrics::Counter<uint64_t>> instrumento_;
};

class ContadorBidireccional {
public:
    ContadorBidireccional(const std::string& nombre, const std::string& descripcion, const std::string& unidad = "");
    void sumar(int64_t valor, const Atributos& atributos = {});

private:
    opentelemetry::nostd::unique_ptr<opentelemetry::metrics::UpDownCounter<int64_t>> instrumento_;
};

class Histograma {
public:
    Histograma(const std::string& nombre, const std::string& descripcion, const std::string& unidad);
    void registrar(double valor, const Atributos& atributos = {});

private:
    opentelemetry::nostd::unique_ptr<opentelemetry::metrics::Histogram<double>> instrumento_;
};

}  // namespace tecflix

#endif  // TECFLIX_TELEMETRIA_HPP
