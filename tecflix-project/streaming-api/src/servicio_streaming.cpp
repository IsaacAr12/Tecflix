#include "servicio_streaming.hpp"

#include <chrono>
#include <iostream>

#include "opentelemetry/trace/scope.h"

namespace tecflix::streaming {

namespace fs = std::filesystem;
namespace api_trace = opentelemetry::trace;

namespace {

constexpr size_t kLargoMaximoMensajeError = 900;  // metadata acepta hasta 1000

std::string recortar(const std::string& texto) {
    return texto.size() <= kLargoMaximoMensajeError ? texto : texto.substr(0, kLargoMaximoMensajeError);
}

}  // namespace

// ---------------- SubidaEnCurso ----------------

SubidaEnCurso::SubidaEnCurso(fs::path temporal, uint64_t maximoBytes)
    : temporal_(std::move(temporal)), maximoBytes_(maximoBytes) {}

SubidaEnCurso::~SubidaEnCurso() {
    if (entregada_) return;
    archivo_.close();
    std::error_code ignorado;
    fs::remove(temporal_, ignorado);
}

void SubidaEnCurso::iniciarArchivo(const std::string& nombreOriginal) {
    if (tieneArchivo()) throw ErrorApi(TipoError::DatosInvalidos, "Solo se permite un archivo por subida");
    const std::optional<std::string> extension = GestorAlmacenamiento::extensionPermitida(nombreOriginal);
    if (!extension) {
        throw ErrorApi(TipoError::DatosInvalidos, "Formato no soportado: use mp4, mov, mkv, webm o avi");
    }
    archivo_.open(temporal_, std::ios::binary | std::ios::trunc);
    if (!archivo_) throw ErrorApi(TipoError::ErrorInterno, "No se pudo crear el archivo temporal");
    extension_ = *extension;
}

bool SubidaEnCurso::escribir(const char* datos, size_t largo) {
    if (!archivo_.is_open()) {
        error_ = ErrorApi(TipoError::DatosInvalidos, "Se recibieron datos de archivo sin el campo 'archivo'");
        return false;
    }
    if (bytes_ + largo > maximoBytes_) {
        error_ = ErrorApi(TipoError::ArchivoDemasiadoGrande,
                          "El archivo supera el máximo de " + std::to_string(maximoBytes_) + " bytes");
        return false;
    }
    archivo_.write(datos, static_cast<std::streamsize>(largo));
    if (!archivo_) {
        error_ = ErrorApi(TipoError::ErrorInterno, "No se pudo escribir el archivo en disco");
        return false;
    }
    bytes_ += largo;
    return true;
}

// ---------------- ServicioStreaming ----------------

ServicioStreaming::ServicioStreaming(GestorAlmacenamiento& almacenamiento, IClienteUsuarios& usuarios,
                                     IClienteMetadata& metadata, ITranscodificador& transcodificador,
                                     PoolTranscodificacion& pool, uint64_t maximoBytesSubida)
    : almacenamiento_(almacenamiento),
      usuarios_(usuarios),
      metadata_(metadata),
      transcodificador_(transcodificador),
      pool_(pool),
      maximoBytesSubida_(maximoBytesSubida),
      duracionTranscodificacion_("transcodificacion_duracion_seg", "Duración de cada transcodificación", "s"),
      enCola_("transcodificaciones_en_cola", "Transcodificaciones en espera o en ejecución"),
      compensacionesFallidas_("compensaciones_fallidas_total",
                              "Pasos de compensación o de cierre que no se pudieron completar") {}

std::unique_ptr<SubidaEnCurso> ServicioStreaming::iniciarSubida(uint32_t usuarioId, uint64_t largoDeclarado) {
    if (largoDeclarado > maximoBytesSubida_ + kMargenMultipart) {
        throw ErrorApi(TipoError::ArchivoDemasiadoGrande,
                       "La subida supera el máximo de " + std::to_string(maximoBytesSubida_) + " bytes");
    }
    const InfoCuota cuota = usuarios_.obtenerCuota(usuarioId);
    if (largoDeclarado > cuota.disponibleBytes + kMargenMultipart) {
        throw ErrorApi(TipoError::CuotaExcedida, "El archivo no cabe en el espacio disponible (" +
                                                     std::to_string(cuota.disponibleBytes) + " bytes)");
    }
    return std::unique_ptr<SubidaEnCurso>(
        new SubidaEnCurso(almacenamiento_.nuevoArchivoTemporal(), maximoBytesSubida_));
}

std::string ServicioStreaming::completarSubida(uint32_t usuarioId, SubidaEnCurso& subida, const DatosSubida& datos) {
    if (!subida.tieneArchivo()) throw ErrorApi(TipoError::DatosInvalidos, "Falta el campo 'archivo'");
    if (subida.bytes() == 0) throw ErrorApi(TipoError::DatosInvalidos, "El archivo está vacío");
    subida.archivo_.close();
    if (!subida.archivo_) throw ErrorApi(TipoError::ErrorInterno, "No se pudo guardar el archivo");
    const auto bytes = static_cast<int64_t>(subida.bytes());

    // Paso 4: reserva atómica en el API de usuarios. Si no cabe lanza CUOTA_EXCEDIDA y
    // el destructor de la subida borra el archivo.
    usuarios_.registrarUso(usuarioId, bytes);

    // Paso 5: nombre del autor y registro en metadata, con compensación si falla.
    std::string id;
    try {
        nlohmann::json registro = {{"autor_id", usuarioId},
                                   {"autor_nombre", usuarios_.obtenerNombre(usuarioId)},
                                   {"tamano_original_bytes", bytes}};
        if (datos.titulo) registro["titulo"] = *datos.titulo;
        if (datos.descripcion) registro["descripcion"] = *datos.descripcion;
        if (datos.tema) registro["tema"] = *datos.tema;
        if (datos.etiquetas) registro["etiquetas"] = *datos.etiquetas;
        if (datos.visibilidad) registro["visibilidad"] = *datos.visibilidad;
        id = metadata_.crear(registro);
    } catch (...) {
        liberarCuota(usuarioId, bytes, "no se pudo crear la metadata");
        throw;
    }

    fs::path original;
    try {
        original = almacenamiento_.moverOriginal(subida.temporal_, id, subida.extension_);
        subida.entregada_ = true;
    } catch (const std::exception& e) {
        try {
            metadata_.eliminar(id);
        } catch (const std::exception& error) {
            registrarCompensacionFallida("no se pudo borrar la metadata de " + id + ": " + error.what());
        }
        liberarCuota(usuarioId, bytes, "no se pudo mover el archivo");
        throw ErrorApi(TipoError::ErrorInterno, std::string("No se pudo guardar el archivo: ") + e.what());
    }

    // Paso 6: la transcodificación sigue en otro hilo, como hija del span de esta subida.
    const api_trace::SpanContext padre = Telemetria::contextoActual();
    enCola_.sumar(1);
    pool_.encolar([this, id, usuarioId, bytes, original, padre] { transcodificar(id, usuarioId, bytes, original, padre); });
    return id;
}

void ServicioStreaming::transcodificar(const std::string& id, uint32_t autorId, int64_t tamanoOriginal,
                                       const fs::path& original, api_trace::SpanContext padre) {
    api_trace::StartSpanOptions opciones;
    opciones.parent = padre;
    auto span = Telemetria::tracer()->StartSpan("transcodificar", {{"video.id", id.c_str()}}, opciones);
    api_trace::Scope alcance(span);
    const auto inicio = std::chrono::steady_clock::now();

    try {
        const ResultadoTranscodificacion resultado =
            transcodificador_.transcodificar(original, almacenamiento_.carpetaVideo(id));
        std::error_code ignorado;
        fs::remove(original, ignorado);
        try {
            metadata_.actualizarProcesamiento(id, {{"estado", "listo"},
                                                   {"duracion_seg", resultado.duracionSeg},
                                                   {"ruta_manifest", "videos/" + id + "/manifest.mpd"},
                                                   {"tamano_bytes", almacenamiento_.tamanoCarpeta(id)}});
        } catch (const std::exception& e) {
            registrarCompensacionFallida("no se pudo marcar como listo el video " + id + ": " + e.what());
            span->SetStatus(api_trace::StatusCode::kError, e.what());
        }
    } catch (const std::exception& e) {
        // SPEC 6.6, paso 8: borrar los archivos, liberar la cuota y marcar el error.
        std::cerr << "[streaming-api] falló la transcodificación de " << id << ": " << e.what() << "\n";
        span->SetStatus(api_trace::StatusCode::kError, e.what());
        try {
            almacenamiento_.eliminarVideo(id);
        } catch (const std::exception& error) {
            registrarCompensacionFallida("no se pudieron borrar los archivos de " + id + ": " + error.what());
        }
        liberarCuota(autorId, tamanoOriginal, "falló la transcodificación");
        try {
            metadata_.actualizarProcesamiento(id, {{"estado", "error"}, {"mensaje_error", recortar(e.what())}});
        } catch (const std::exception& error) {
            registrarCompensacionFallida("no se pudo marcar con error el video " + id + ": " + error.what());
        }
    }

    duracionTranscodificacion_.registrar(
        std::chrono::duration<double>(std::chrono::steady_clock::now() - inicio).count());
    enCola_.sumar(-1);
    span->End();
}

fs::path ServicioStreaming::archivoParaReproducir(const std::string& id, const std::string& nombre,
                                                  uint32_t usuarioId) {
    const fs::path ruta = almacenamiento_.rutaArchivo(id, nombre);  // valida id y nombre
    const std::optional<InfoVideo> video = metadata_.obtener(id, usuarioId);
    if (!video) throw ErrorApi(TipoError::NoEncontrado, "El video no existe");
    if (video->estado != "listo") {
        throw ErrorApi(TipoError::Conflicto, "El video todavía no está listo para reproducirse");
    }
    if (!fs::is_regular_file(ruta)) throw ErrorApi(TipoError::NoEncontrado, "Archivo no encontrado");
    return ruta;
}

void ServicioStreaming::eliminar(const std::string& id, uint32_t usuarioId) {
    if (!GestorAlmacenamiento::idValido(id)) throw ErrorApi(TipoError::NoEncontrado, "El video no existe");
    const std::optional<InfoVideo> video = metadata_.obtener(id, usuarioId);
    if (!video) throw ErrorApi(TipoError::NoEncontrado, "El video no existe");
    if (video->autorId != usuarioId) throw ErrorApi(TipoError::SinPermiso, "Solo el autor puede eliminar el video");
    if (video->estado == "procesando") {
        throw ErrorApi(TipoError::Conflicto, "El video se está procesando; intente de nuevo cuando termine");
    }

    // Primero el registro: el video desaparece del listado aunque falle un paso posterior.
    metadata_.eliminar(id);
    try {
        almacenamiento_.eliminarVideo(id);
    } catch (const std::exception& e) {
        registrarCompensacionFallida("no se pudieron borrar los archivos de " + id + ": " + e.what());
    }
    // Si el video terminó en "error", su cuota ya se liberó cuando falló la transcodificación.
    if (video->estado == "listo") liberarCuota(usuarioId, video->tamanoOriginalBytes, "eliminación del video");
}

void ServicioStreaming::liberarCuota(uint32_t usuarioId, int64_t bytes, const std::string& motivo) {
    try {
        usuarios_.registrarUso(usuarioId, -bytes);
    } catch (const std::exception& e) {
        registrarCompensacionFallida("no se pudieron liberar " + std::to_string(bytes) + " bytes del usuario " +
                                     std::to_string(usuarioId) + " (" + motivo + "): " + e.what());
    }
}

void ServicioStreaming::registrarCompensacionFallida(const std::string& detalle) {
    std::cerr << "[streaming-api] " << detalle << "\n";
    compensacionesFallidas_.sumar();
}

}  // namespace tecflix::streaming
