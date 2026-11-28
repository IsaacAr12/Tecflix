#include "repositorio_mongo.hpp"

#include <algorithm>
#include <bsoncxx/builder/basic/array.hpp>
#include <bsoncxx/builder/basic/document.hpp>
#include <bsoncxx/builder/basic/kvp.hpp>
#include <bsoncxx/oid.hpp>
#include <bsoncxx/types.hpp>
#include <cctype>
#include <chrono>
#include <mongocxx/client.hpp>
#include <mongocxx/collection.hpp>
#include <mongocxx/exception/exception.hpp>
#include <mongocxx/options/find.hpp>
#include <mongocxx/uri.hpp>

#include "tecflix/configuracion.hpp"
#include "tecflix/errores.hpp"

namespace tecflix::metadata {

using bsoncxx::builder::basic::kvp;
using bsoncxx::builder::basic::make_array;
using bsoncxx::builder::basic::make_document;

namespace {

constexpr const char* kColeccion = "videos";

// Orden en español sin distinguir mayúsculas ni tildes (SPEC 7.1). La misma collation
// se usa en los índices y en las consultas; si no coinciden, MongoDB no usa el índice.
bsoncxx::document::value collationEspanol() { return make_document(kvp("locale", "es"), kvp("strength", 1)); }

std::string leerTexto(bsoncxx::document::view documento, const char* campo) {
    auto elemento = documento[campo];
    if (elemento && elemento.type() == bsoncxx::type::k_string) return std::string(elemento.get_string().value);
    return "";
}

int64_t leerEntero(bsoncxx::document::view documento, const char* campo) {
    auto elemento = documento[campo];
    if (!elemento) return 0;
    switch (elemento.type()) {
        case bsoncxx::type::k_int32:  return elemento.get_int32().value;
        case bsoncxx::type::k_int64:  return elemento.get_int64().value;
        case bsoncxx::type::k_double: return static_cast<int64_t>(elemento.get_double().value);
        default:                      return 0;
    }
}

double leerDecimal(bsoncxx::document::view documento, const char* campo) {
    auto elemento = documento[campo];
    if (elemento && elemento.type() == bsoncxx::type::k_double) return elemento.get_double().value;
    return static_cast<double>(leerEntero(documento, campo));
}

const char* campoDeOrden(CampoOrden orden) {
    switch (orden) {
        case CampoOrden::Titulo: return "titulo";
        case CampoOrden::Autor:  return "autor_nombre";
        case CampoOrden::Fecha:  return "fecha_carga";
    }
    return "fecha_carga";
}

std::unique_ptr<mongocxx::pool> crearPool(const std::string& uri) {
    try {
        return std::make_unique<mongocxx::pool>(mongocxx::uri{uri});
    } catch (const mongocxx::exception& e) {
        throw ErrorConfiguracion(std::string("URI_MONGO inválida: ") + e.what());
    }
}

}  // namespace

RepositorioMongo::RepositorioMongo(const std::string& uri, std::string baseDatos)
    : pool_(crearPool(uri)),
      baseDatos_(std::move(baseDatos)),
      duracionConsultas_("mongo_consulta_duracion_ms", "Duración de las operaciones en MongoDB", "ms") {}

bool RepositorioMongo::idValido(const std::string& id) {
    return id.size() == 24 &&
           std::all_of(id.begin(), id.end(), [](unsigned char c) { return std::isxdigit(c) != 0; });
}

template <typename Operacion>
auto RepositorioMongo::medir(const char* nombre, Operacion operacion) {
    const auto inicio = std::chrono::steady_clock::now();
    auto registrar = [&] {
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - inicio).count();
        duracionConsultas_.registrar(ms, {{"operacion", nombre}});
    };
    try {
        auto resultado = operacion();
        registrar();
        return resultado;
    } catch (const mongocxx::exception& e) {
        registrar();
        throw ErrorApi(TipoError::ServicioNoDisponible, std::string("MongoDB (") + nombre + "): " + e.what());
    }
}

bsoncxx::document::value RepositorioMongo::camposDescriptivos(const VideoMetadata& video) {
    bsoncxx::builder::basic::array etiquetas;
    for (const auto& etiqueta : video.etiquetas) etiquetas.append(etiqueta);
    return make_document(kvp("titulo", video.titulo), kvp("descripcion", video.descripcion), kvp("tema", video.tema),
                         kvp("etiquetas", etiquetas), kvp("visibilidad", aTexto(video.visibilidad)));
}

bsoncxx::document::value RepositorioMongo::camposProcesamiento(const VideoMetadata& video) {
    return make_document(kvp("estado", aTexto(video.estado)), kvp("duracion_seg", video.duracionSeg),
                         kvp("ruta_manifest", video.rutaManifest), kvp("tamano_bytes", video.tamanoBytes),
                         kvp("mensaje_error", video.mensajeError));
}

bsoncxx::document::value RepositorioMongo::aDocumento(const VideoMetadata& video) {
    bsoncxx::builder::basic::document documento;
    documento.append(bsoncxx::builder::concatenate(camposDescriptivos(video).view()));
    documento.append(bsoncxx::builder::concatenate(camposProcesamiento(video).view()));
    // autor_id es uint32_t en C++ y se guarda como int64: BSON no tiene enteros sin signo (SPEC 6.5).
    documento.append(kvp("autor_id", static_cast<int64_t>(video.autorId)),
                     kvp("autor_nombre", video.autorNombre),
                     kvp("fecha_carga", bsoncxx::types::b_date{std::chrono::milliseconds{video.fechaCargaMs}}),
                     kvp("tamano_original_bytes", video.tamanoOriginalBytes));
    return documento.extract();
}

VideoMetadata RepositorioMongo::desdeDocumento(bsoncxx::document::view documento) {
    VideoMetadata video;
    if (auto id = documento["_id"]; id && id.type() == bsoncxx::type::k_oid) video.id = id.get_oid().value.to_string();
    video.titulo = leerTexto(documento, "titulo");
    video.descripcion = leerTexto(documento, "descripcion");
    video.autorId = static_cast<uint32_t>(leerEntero(documento, "autor_id"));
    video.autorNombre = leerTexto(documento, "autor_nombre");
    video.duracionSeg = leerDecimal(documento, "duracion_seg");
    if (auto fecha = documento["fecha_carga"]; fecha && fecha.type() == bsoncxx::type::k_date) {
        video.fechaCargaMs = fecha.get_date().to_int64();
    }
    video.tema = leerTexto(documento, "tema");
    if (auto etiquetas = documento["etiquetas"]; etiquetas && etiquetas.type() == bsoncxx::type::k_array) {
        for (const auto& etiqueta : etiquetas.get_array().value) {
            if (etiqueta.type() == bsoncxx::type::k_string) {
                video.etiquetas.emplace_back(etiqueta.get_string().value);
            }
        }
    }
    video.visibilidad =
        leerTexto(documento, "visibilidad") == "publico" ? Visibilidad::Publico : Visibilidad::Privado;
    const std::string estado = leerTexto(documento, "estado");
    video.estado = estado.empty() ? EstadoVideo::Procesando : estadoDesdeTexto(estado);
    video.mensajeError = leerTexto(documento, "mensaje_error");
    video.rutaManifest = leerTexto(documento, "ruta_manifest");
    video.tamanoOriginalBytes = leerEntero(documento, "tamano_original_bytes");
    video.tamanoBytes = leerEntero(documento, "tamano_bytes");
    return video;
}

void RepositorioMongo::asegurarIndices() {
    medir("crear_indices", [&] {
        auto cliente = pool_->acquire();
        auto coleccion = (*cliente)[baseDatos_][kColeccion];
        const auto conCollation = make_document(kvp("collation", collationEspanol()));
        coleccion.create_index(make_document(kvp("autor_id", 1)));
        coleccion.create_index(make_document(kvp("visibilidad", 1), kvp("estado", 1)));
        coleccion.create_index(make_document(kvp("fecha_carga", -1)));
        coleccion.create_index(make_document(kvp("titulo", 1)), conCollation.view());
        coleccion.create_index(make_document(kvp("autor_nombre", 1)), conCollation.view());
        return true;
    });
}

std::string RepositorioMongo::crear(const VideoMetadata& video) {
    return medir("crear", [&] {
        auto cliente = pool_->acquire();
        const bsoncxx::oid id;
        bsoncxx::builder::basic::document documento;
        documento.append(kvp("_id", id), bsoncxx::builder::concatenate(aDocumento(video).view()));
        (*cliente)[baseDatos_][kColeccion].insert_one(documento.view());
        return id.to_string();
    });
}

std::optional<VideoMetadata> RepositorioMongo::obtenerPorId(const std::string& id) {
    if (!idValido(id)) return std::nullopt;
    return medir("obtener", [&]() -> std::optional<VideoMetadata> {
        auto cliente = pool_->acquire();
        auto resultado = (*cliente)[baseDatos_][kColeccion].find_one(make_document(kvp("_id", bsoncxx::oid{id})));
        if (!resultado) return std::nullopt;
        return desdeDocumento(resultado->view());
    });
}

PaginaVideos RepositorioMongo::listar(const ConsultaListado& consulta) {
    return medir("listar", [&] {
        auto cliente = pool_->acquire();
        auto coleccion = (*cliente)[baseDatos_][kColeccion];

        // SPEC 7.3: (público y listo) o (del solicitante, en cualquier estado).
        const auto filtro = make_document(kvp(
            "$or", make_array(make_document(kvp("visibilidad", "publico"), kvp("estado", "listo")),
                              make_document(kvp("autor_id", static_cast<int64_t>(consulta.solicitanteId))))));

        const int direccion = consulta.ascendente ? 1 : -1;
        mongocxx::options::find opciones;
        // _id desempata los videos con el mismo valor, para que la paginación sea estable.
        opciones.sort(make_document(kvp(std::string(campoDeOrden(consulta.orden)), direccion), kvp("_id", direccion)));
        opciones.skip(static_cast<int64_t>(consulta.pagina - 1) * consulta.limite);
        opciones.limit(consulta.limite);
        opciones.collation(collationEspanol());

        PaginaVideos pagina;
        for (const auto& documento : coleccion.find(filtro.view(), opciones)) {
            pagina.videos.push_back(desdeDocumento(documento));
        }
        pagina.total = coleccion.count_documents(filtro.view());
        return pagina;
    });
}

bool RepositorioMongo::actualizarCampos(const std::string& id, const char* operacion,
                                        bsoncxx::document::value campos) {
    if (!idValido(id)) return false;
    return medir(operacion, [&] {
        auto cliente = pool_->acquire();
        auto resultado = (*cliente)[baseDatos_][kColeccion].update_one(make_document(kvp("_id", bsoncxx::oid{id})),
                                                                       make_document(kvp("$set", campos.view())));
        return resultado && resultado->matched_count() > 0;
    });
}

bool RepositorioMongo::actualizarDescriptivos(const VideoMetadata& video) {
    return actualizarCampos(video.id, "actualizar_descriptivos", camposDescriptivos(video));
}

bool RepositorioMongo::actualizarProcesamiento(const VideoMetadata& video) {
    return actualizarCampos(video.id, "actualizar_procesamiento", camposProcesamiento(video));
}

bool RepositorioMongo::eliminar(const std::string& id) {
    if (!idValido(id)) return false;
    return medir("eliminar", [&] {
        auto cliente = pool_->acquire();
        auto resultado = (*cliente)[baseDatos_][kColeccion].delete_one(make_document(kvp("_id", bsoncxx::oid{id})));
        return resultado && resultado->deleted_count() > 0;
    });
}

bool RepositorioMongo::disponible() {
    try {
        auto cliente = pool_->acquire();
        (*cliente)["admin"].run_command(make_document(kvp("ping", 1)));
        return true;
    } catch (const mongocxx::exception&) {
        return false;
    }
}

}  // namespace tecflix::metadata
