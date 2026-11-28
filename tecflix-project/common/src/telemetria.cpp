#include "tecflix/telemetria.hpp"

#include <iostream>
#include <mutex>

#include "opentelemetry/context/propagation/text_map_propagator.h"
#include "opentelemetry/context/runtime_context.h"
#include "opentelemetry/exporters/otlp/otlp_http_exporter_factory.h"
#include "opentelemetry/exporters/otlp/otlp_http_exporter_options.h"
#include "opentelemetry/exporters/otlp/otlp_http_metric_exporter_factory.h"
#include "opentelemetry/exporters/otlp/otlp_http_metric_exporter_options.h"
#include "opentelemetry/metrics/noop.h"
#include "opentelemetry/metrics/provider.h"
#include "opentelemetry/sdk/metrics/export/periodic_exporting_metric_reader_factory.h"
#include "opentelemetry/sdk/metrics/export/periodic_exporting_metric_reader_options.h"
#include "opentelemetry/sdk/metrics/meter_provider.h"
#include "opentelemetry/sdk/metrics/meter_provider_factory.h"
#include "opentelemetry/sdk/metrics/view/view_registry.h"
#include "opentelemetry/sdk/resource/resource.h"
#include "opentelemetry/sdk/trace/batch_span_processor_factory.h"
#include "opentelemetry/sdk/trace/batch_span_processor_options.h"
#include "opentelemetry/sdk/trace/simple_processor_factory.h"
#include "opentelemetry/sdk/trace/tracer_provider.h"
#include "opentelemetry/sdk/trace/tracer_provider_factory.h"
#include "opentelemetry/trace/context.h"
#include "opentelemetry/trace/noop.h"
#include "opentelemetry/trace/propagation/http_trace_context.h"
#include "opentelemetry/trace/provider.h"

namespace tecflix {

namespace nostd = opentelemetry::nostd;
namespace api_trace = opentelemetry::trace;
namespace api_metrics = opentelemetry::metrics;
namespace sdk_trace = opentelemetry::sdk::trace;
namespace sdk_metrics = opentelemetry::sdk::metrics;
namespace propagacion = opentelemetry::context::propagation;
namespace otlp = opentelemetry::exporter::otlp;

namespace {

constexpr const char* kNombreInstrumentacion = "tecflix";
constexpr std::chrono::milliseconds kIntervaloMetricas{10000};

std::mutex mutexProveedores;
std::shared_ptr<sdk_trace::TracerProvider> proveedorTraces;
std::shared_ptr<sdk_metrics::MeterProvider> proveedorMetricas;

// Adaptador entre los encabezados de httplib y la API de propagación de OpenTelemetry.
class PortadorEncabezados : public propagacion::TextMapCarrier {
public:
    explicit PortadorEncabezados(httplib::Headers& encabezados) : encabezados_(encabezados) {}

    nostd::string_view Get(nostd::string_view clave) const noexcept override {
        auto it = encabezados_.find(std::string(clave));  // httplib compara sin distinguir mayúsculas
        if (it == encabezados_.end()) return "";
        return it->second;
    }

    void Set(nostd::string_view clave, nostd::string_view valor) noexcept override {
        encabezados_.erase(std::string(clave));
        encabezados_.emplace(std::string(clave), std::string(valor));
    }

private:
    httplib::Headers& encabezados_;
};

// El propagador W3C se usa siempre, aunque no se exporte nada, para que traceparent
// viaje entre APIs desde el primer momento.
nostd::shared_ptr<propagacion::TextMapPropagator> propagador() {
    static const nostd::shared_ptr<propagacion::TextMapPropagator> instancia(
        new api_trace::propagation::HttpTraceContext());
    return instancia;
}

}  // namespace

void Telemetria::inicializar(const std::string& servicio, const std::string& version, const std::string& endpoint) {
    otlp::OtlpHttpExporterOptions opcionesTraces;
    opcionesTraces.url = endpoint + "/v1/traces";
    otlp::OtlpHttpMetricExporterOptions opcionesMetricas;
    opcionesMetricas.url = endpoint + "/v1/metrics";
    inicializarConExportadores(servicio, version, otlp::OtlpHttpExporterFactory::Create(opcionesTraces),
                               otlp::OtlpHttpMetricExporterFactory::Create(opcionesMetricas), true);
}

void Telemetria::inicializarConExportadores(const std::string& servicio, const std::string& version,
                                            std::unique_ptr<sdk_trace::SpanExporter> traces,
                                            std::unique_ptr<sdk_metrics::PushMetricExporter> metricas,
                                            bool procesarEnLotes) {
    finalizar();
    std::lock_guard<std::mutex> bloqueo(mutexProveedores);
    const auto recurso =
        opentelemetry::sdk::resource::Resource::Create({{"service.name", servicio}, {"service.version", version}});

    std::unique_ptr<sdk_trace::SpanProcessor> procesador =
        procesarEnLotes
            ? sdk_trace::BatchSpanProcessorFactory::Create(std::move(traces), sdk_trace::BatchSpanProcessorOptions{})
            : sdk_trace::SimpleSpanProcessorFactory::Create(std::move(traces));
    proveedorTraces = sdk_trace::TracerProviderFactory::Create(std::move(procesador), recurso);
    api_trace::Provider::SetTracerProvider(nostd::shared_ptr<api_trace::TracerProvider>(proveedorTraces));

    sdk_metrics::PeriodicExportingMetricReaderOptions opcionesLector;
    opcionesLector.export_interval_millis = kIntervaloMetricas;
    opcionesLector.export_timeout_millis = kIntervaloMetricas / 2;
    proveedorMetricas =
        sdk_metrics::MeterProviderFactory::Create(std::make_unique<sdk_metrics::ViewRegistry>(), recurso);
    proveedorMetricas->AddMetricReader(
        sdk_metrics::PeriodicExportingMetricReaderFactory::Create(std::move(metricas), opcionesLector));
    api_metrics::Provider::SetMeterProvider(nostd::shared_ptr<api_metrics::MeterProvider>(proveedorMetricas));
}

void Telemetria::finalizar() {
    std::lock_guard<std::mutex> bloqueo(mutexProveedores);
    // ForceFlush envía lo pendiente; el destructor de cada proveedor hace el Shutdown.
    if (proveedorTraces) {
        proveedorTraces->ForceFlush();
        proveedorTraces.reset();
    }
    if (proveedorMetricas) {
        proveedorMetricas->ForceFlush();
        proveedorMetricas.reset();
    }
    api_trace::Provider::SetTracerProvider(
        nostd::shared_ptr<api_trace::TracerProvider>(new api_trace::NoopTracerProvider()));
    api_metrics::Provider::SetMeterProvider(
        nostd::shared_ptr<api_metrics::MeterProvider>(new api_metrics::NoopMeterProvider()));
}

nostd::shared_ptr<api_trace::Tracer> Telemetria::tracer() {
    return api_trace::Provider::GetTracerProvider()->GetTracer(kNombreInstrumentacion);
}

nostd::shared_ptr<api_metrics::Meter> Telemetria::medidor() {
    return api_metrics::Provider::GetMeterProvider()->GetMeter(kNombreInstrumentacion);
}

void Telemetria::inyectarContexto(httplib::Headers& encabezados) {
    PortadorEncabezados portador(encabezados);
    propagador()->Inject(portador, opentelemetry::context::RuntimeContext::GetCurrent());
}

opentelemetry::context::Context Telemetria::extraerContexto(const httplib::Headers& encabezados) {
    httplib::Headers copia = encabezados;
    PortadorEncabezados portador(copia);
    auto actual = opentelemetry::context::RuntimeContext::GetCurrent();
    return propagador()->Extract(portador, actual);
}

api_trace::SpanContext Telemetria::contextoActual() {
    return api_trace::GetSpan(opentelemetry::context::RuntimeContext::GetCurrent())->GetContext();
}

// ---------------- Instrumentos ----------------

Contador::Contador(const std::string& nombre, const std::string& descripcion, const std::string& unidad)
    : instrumento_(Telemetria::medidor()->CreateUInt64Counter(nombre, descripcion, unidad)) {}

void Contador::sumar(uint64_t valor, const Atributos& atributos) { instrumento_->Add(valor, atributos); }

ContadorBidireccional::ContadorBidireccional(const std::string& nombre, const std::string& descripcion,
                                             const std::string& unidad)
    : instrumento_(Telemetria::medidor()->CreateInt64UpDownCounter(nombre, descripcion, unidad)) {}

void ContadorBidireccional::sumar(int64_t valor, const Atributos& atributos) { instrumento_->Add(valor, atributos); }

Histograma::Histograma(const std::string& nombre, const std::string& descripcion, const std::string& unidad)
    : instrumento_(Telemetria::medidor()->CreateDoubleHistogram(nombre, descripcion, unidad)) {}

void Histograma::registrar(double valor, const Atributos& atributos) {
    instrumento_->Record(valor, atributos, opentelemetry::context::RuntimeContext::GetCurrent());
}

}  // namespace tecflix
