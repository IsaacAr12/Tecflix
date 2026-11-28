#ifndef STREAMING_SERVICIO_STREAMING_HPP
#define STREAMING_SERVICIO_STREAMING_HPP

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "cliente_metadata.hpp"
#include "cliente_usuarios.hpp"
#include "gestor_almacenamiento.hpp"
#include "pool_transcodificacion.hpp"
#include "tecflix/errores.hpp"
#include "tecflix/telemetria.hpp"
#include "transcodificador.hpp"

namespace tecflix::streaming {

// Campos de texto del formulario de subida (SPEC 6.6). Los ausentes se omiten y el
// API de metadata decide (por ejemplo, la visibilidad por defecto es "privado").
struct DatosSubida {
    std::optional<std::string> titulo;
    std::optional<std::string> descripcion;
    std::optional<std::string> tema;
    std::optional<std::vector<std::string>> etiquetas;
    std::optional<std::string> visibilidad;
};

// Un archivo que se está recibiendo. Se escribe en disco por bloques (nunca entero
// en memoria) y, si no se completa, el destructor lo borra (RAII, SPEC 8.4).
class SubidaEnCurso {
public:
    ~SubidaEnCurso();
    SubidaEnCurso(const SubidaEnCurso&) = delete;
    SubidaEnCurso& operator=(const SubidaEnCurso&) = delete;

    // Valida la extensión del nombre original contra la lista blanca y abre el archivo.
    // Lanza ErrorApi(DatosInvalidos) si no es válida o si ya se había iniciado.
    void iniciarArchivo(const std::string& nombreOriginal);

    // false si se pasa de MAX_BYTES_SUBIDA o falla el disco; el motivo queda en error().
    bool escribir(const char* datos, size_t largo);

    const std::optional<ErrorApi>& error() const { return error_; }
    bool tieneArchivo() const { return !extension_.empty(); }
    uint64_t bytes() const { return bytes_; }

private:
    friend class ServicioStreaming;
    SubidaEnCurso(std::filesystem::path temporal, uint64_t maximoBytes);

    std::filesystem::path temporal_;
    uint64_t maximoBytes_;
    std::ofstream archivo_;
    std::string extension_;
    uint64_t bytes_ = 0;
    std::optional<ErrorApi> error_;
    bool entregada_ = false;  // el archivo ya se movió a la carpeta del video
};

// Orquesta los flujos de subida, reproducción y eliminación (SPEC 6.6, 6.7 y 6.8),
// incluidas las compensaciones cuando un paso falla a medias.
class ServicioStreaming {
public:
    // Margen para la cabecera multipart al comparar Content-Length con la cuota disponible.
    static constexpr uint64_t kMargenMultipart = 64 * 1024;

    ServicioStreaming(GestorAlmacenamiento& almacenamiento, IClienteUsuarios& usuarios, IClienteMetadata& metadata,
                      ITranscodificador& transcodificador, PoolTranscodificacion& pool, uint64_t maximoBytesSubida);

    // Paso 2: antes de leer el cuerpo, rechaza con 413 lo que no cabe.
    std::unique_ptr<SubidaEnCurso> iniciarSubida(uint32_t usuarioId, uint64_t largoDeclarado);

    // Pasos 4 a 6: reserva la cuota, crea la metadata, mueve el archivo y encola la
    // transcodificación. Devuelve el id del video. Si algo falla, compensa lo hecho.
    std::string completarSubida(uint32_t usuarioId, SubidaEnCurso& subida, const DatosSubida& datos);

    // SPEC 6.7: ruta del manifiesto o de un segmento, si el usuario puede ver el video y está listo.
    std::filesystem::path archivoParaReproducir(const std::string& id, const std::string& nombre, uint32_t usuarioId);

    // SPEC 6.8.
    void eliminar(const std::string& id, uint32_t usuarioId);

    size_t transcodificacionesPendientes() const { return pool_.pendientes(); }

private:
    void transcodificar(const std::string& id, uint32_t autorId, int64_t tamanoOriginal,
                        const std::filesystem::path& original, opentelemetry::trace::SpanContext padre);
    // Libera cuota como compensación: si falla, lo registra en vez de lanzar.
    void liberarCuota(uint32_t usuarioId, int64_t bytes, const std::string& motivo);
    void registrarCompensacionFallida(const std::string& detalle);

    GestorAlmacenamiento& almacenamiento_;
    IClienteUsuarios& usuarios_;
    IClienteMetadata& metadata_;
    ITranscodificador& transcodificador_;
    PoolTranscodificacion& pool_;
    uint64_t maximoBytesSubida_;

    Histograma duracionTranscodificacion_;
    ContadorBidireccional enCola_;
    Contador compensacionesFallidas_;
};

}  // namespace tecflix::streaming

#endif  // STREAMING_SERVICIO_STREAMING_HPP
