#include "controlador_streaming.hpp"

#include <filesystem>
#include <map>
#include <set>

#include "tecflix/errores.hpp"
#include "tecflix/utilidades_http.hpp"

namespace tecflix::streaming {

namespace {

const std::set<std::string> kCamposTexto = {"titulo", "descripcion", "tema", "etiquetas", "visibilidad"};

// Lee las partes del formulario multipart a medida que llegan: los campos de texto
// se guardan en memoria (son pequeños) y el archivo se pasa a la SubidaEnCurso.
class FormularioSubida {
public:
    explicit FormularioSubida(SubidaEnCurso& subida) : subida_(subida) {}

    bool empezarParte(const httplib::MultipartFormData& parte) {
        parteActual_ = parte.name;
        if (parte.name == "archivo") {
            // El SPEC pide los campos de texto antes del archivo: así un formulario inválido
            // se rechaza antes de recibir todo el video.
            if (textos_.count("titulo") == 0) {
                return fallar("Envíe los campos de texto antes del archivo (falta 'titulo')");
            }
            try {
                subida_.iniciarArchivo(parte.filename);
            } catch (const ErrorApi& e) {
                error_ = e;
                return false;
            }
            return true;
        }
        if (kCamposTexto.count(parte.name) == 0) return fallar("Campo desconocido en el formulario: '" + parte.name + "'");
        if (textos_.count(parte.name) > 0) return fallar("El campo '" + parte.name + "' está repetido");
        textos_[parte.name] = "";
        return true;
    }

    bool recibir(const char* datos, size_t largo) {
        if (parteActual_ == "archivo") return subida_.escribir(datos, largo);
        std::string& texto = textos_[parteActual_];
        if (texto.size() + largo > ControladorStreaming::kLargoMaximoCampoTexto) {
            return fallar("El campo '" + parteActual_ + "' es demasiado largo");
        }
        texto.append(datos, largo);
        return true;
    }

    DatosSubida datos() const {
        DatosSubida datos;
        auto copiar = [this](const char* campo, std::optional<std::string>& destino) {
            if (auto it = textos_.find(campo); it != textos_.end()) destino = it->second;
        };
        copiar("titulo", datos.titulo);
        copiar("descripcion", datos.descripcion);
        copiar("tema", datos.tema);
        copiar("visibilidad", datos.visibilidad);
        if (auto it = textos_.find("etiquetas"); it != textos_.end()) {
            datos.etiquetas = ControladorStreaming::parsearEtiquetas(it->second);
        }
        return datos;
    }

    const std::optional<ErrorApi>& error() const { return error_; }

private:
    bool fallar(const std::string& mensaje) {
        error_ = ErrorApi(TipoError::DatosInvalidos, mensaje);
        return false;
    }

    SubidaEnCurso& subida_;
    std::string parteActual_;
    std::map<std::string, std::string> textos_;
    std::optional<ErrorApi> error_;
};

const char* tipoContenido(const std::string& nombre) {
    return nombre == "manifest.mpd" ? "application/dash+xml" : "video/iso.segment";
}

}  // namespace

ControladorStreaming::ControladorStreaming(ServicioStreaming& servicio, uint64_t maximoBytesSubida)
    : servicio_(servicio),
      maximoBytesSubida_(maximoBytesSubida),
      segmentosServidos_("segmentos_servidos_total", "Segmentos DASH entregados a los reproductores"),
      bytesServidos_("bytes_servidos_total", "Bytes de video entregados a los reproductores", "By") {}

std::vector<std::string> ControladorStreaming::parsearEtiquetas(const std::string& texto) {
    const nlohmann::json json = nlohmann::json::parse(texto, nullptr, false);
    if (!json.is_array()) {
        throw ErrorApi(TipoError::DatosInvalidos, R"(etiquetas debe ser una lista JSON, por ejemplo ["btree","disco"])");
    }
    std::vector<std::string> etiquetas;
    for (const auto& etiqueta : json) {
        if (!etiqueta.is_string()) throw ErrorApi(TipoError::DatosInvalidos, "Cada etiqueta debe ser texto");
        etiquetas.push_back(etiqueta.get<std::string>());
    }
    return etiquetas;
}

uint64_t ControladorStreaming::bytesEnviados(const httplib::Request& req, uint64_t tamanoArchivo) {
    if (req.ranges.empty()) return tamanoArchivo;
    uint64_t total = 0;
    for (const auto& [inicio, fin] : req.ranges) {
        if (inicio < 0) {
            total += std::min<uint64_t>(static_cast<uint64_t>(fin), tamanoArchivo);  // bytes=-N (los últimos N)
        } else if (static_cast<uint64_t>(inicio) < tamanoArchivo) {
            const uint64_t ultimo = fin < 0 ? tamanoArchivo - 1 : std::min<uint64_t>(fin, tamanoArchivo - 1);
            total += ultimo - static_cast<uint64_t>(inicio) + 1;
        }
    }
    return total;
}

void ControladorStreaming::registrarRutas(httplib::Server& servidor) {
    // Margen para la cabecera multipart; el límite exacto del archivo lo aplica SubidaEnCurso.
    servidor.set_payload_max_length(maximoBytesSubida_ + ServicioStreaming::kMargenMultipart);

    servidor.Post("/videos", [this](const httplib::Request& req, httplib::Response& res,
                                    const httplib::ContentReader& lector) { subir(req, res, lector); });

    servidor.Get(R"(/videos/([^/]+)/([^/]+))",
                 [this](const httplib::Request& req, httplib::Response& res) { servirArchivo(req, res); });

    servidor.Delete(R"(/videos/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
        servicio_.eliminar(req.matches[1], http::usuarioSolicitante(req));
        res.status = 204;
    });
}

void ControladorStreaming::subir(const httplib::Request& req, httplib::Response& res,
                                 const httplib::ContentReader& lector) {
    const uint32_t usuario = http::usuarioSolicitante(req);
    if (!req.is_multipart_form_data()) throw ErrorApi(TipoError::DatosInvalidos, "Se esperaba multipart/form-data");

    std::unique_ptr<SubidaEnCurso> subida =
        servicio_.iniciarSubida(usuario, req.get_header_value_u64("Content-Length"));
    FormularioSubida formulario(*subida);
    const bool completo =
        lector([&](const httplib::MultipartFormData& parte) { return formulario.empezarParte(parte); },
               [&](const char* datos, size_t largo) { return formulario.recibir(datos, largo); });
    if (formulario.error()) throw *formulario.error();
    if (subida->error()) throw *subida->error();
    if (!completo) throw ErrorApi(TipoError::DatosInvalidos, "La subida se interrumpió antes de terminar");

    const std::string id = servicio_.completarSubida(usuario, *subida, formulario.datos());
    http::responderJson(res, 202, {{"id_video", id}, {"estado", "procesando"}});
}

void ControladorStreaming::servirArchivo(const httplib::Request& req, httplib::Response& res) {
    const std::string nombre = req.matches[2];
    const std::filesystem::path ruta =
        servicio_.archivoParaReproducir(req.matches[1], nombre, http::usuarioSolicitante(req));
    res.set_file_content(ruta.string(), tipoContenido(nombre));
    // Un paquete DASH publicado no cambia; "private" porque el acceso depende del usuario.
    res.set_header("Cache-Control", "private, max-age=3600");
    if (nombre != "manifest.mpd") segmentosServidos_.sumar();
    bytesServidos_.sumar(bytesEnviados(req, std::filesystem::file_size(ruta)));
}

}  // namespace tecflix::streaming
