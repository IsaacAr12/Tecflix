#include "transcodificador.hpp"

#include <algorithm>
#include <nlohmann/json.hpp>
#include <sstream>

#include "ejecutor_procesos.hpp"
#include "tecflix/errores.hpp"

namespace tecflix::streaming {

namespace fs = std::filesystem;

namespace {

std::string ultimosCaracteres(const std::string& texto, size_t maximo = 500) {
    return texto.size() <= maximo ? texto : texto.substr(texto.size() - maximo);
}

}  // namespace

TranscodificadorFfmpeg::TranscodificadorFfmpeg(std::string rutaFfmpeg, std::string rutaFfprobe)
    : rutaFfmpeg_(std::move(rutaFfmpeg)), rutaFfprobe_(std::move(rutaFfprobe)) {}

InfoMedio TranscodificadorFfmpeg::parsearFfprobe(const std::string& json) {
    const nlohmann::json datos = nlohmann::json::parse(json, nullptr, false);
    if (datos.is_discarded() || !datos.is_object()) {
        throw ErrorApi(TipoError::DatosInvalidos, "ffprobe devolvió una respuesta ilegible");
    }
    InfoMedio info;
    for (const auto& flujo : datos.value("streams", nlohmann::json::array())) {
        const std::string tipo = flujo.value("codec_type", "");
        // Las carátulas de algunos archivos aparecen como "video" con attached_pic = 1.
        const bool esCaratula = flujo.contains("disposition") && flujo["disposition"].value("attached_pic", 0) == 1;
        if (tipo == "video" && !info.tieneVideo && !esCaratula) {
            info.tieneVideo = true;
            info.alto = flujo.value("height", 0);
        } else if (tipo == "audio") {
            info.tieneAudio = true;
        }
    }
    if (datos.contains("format") && datos["format"].contains("duration")) {
        const auto& duracion = datos["format"]["duration"];
        info.duracionSeg = duracion.is_string() ? std::stod(duracion.get<std::string>()) : duracion.get<double>();
    }
    return info;
}

std::vector<Rendicion> TranscodificadorFfmpeg::escalera(int altoOriginal) {
    static const std::vector<Rendicion> candidatas = {{1080, 5000}, {720, 2800}, {360, 800}};
    std::vector<Rendicion> elegidas;
    for (const Rendicion& rendicion : candidatas) {
        if (rendicion.alto <= altoOriginal) elegidas.push_back(rendicion);
    }
    // Videos de menos de 360 px: una sola calidad con su altura (par, como exige H.264).
    if (elegidas.empty()) elegidas.push_back({std::max(2, altoOriginal - (altoOriginal % 2)), 500});
    return elegidas;
}

std::vector<std::string> TranscodificadorFfmpeg::argumentosDash(const std::string& ffmpeg, const fs::path& original,
                                                                const fs::path& carpetaSalida, const InfoMedio& info,
                                                                const std::vector<Rendicion>& rendiciones) {
    // Se decodifica una sola vez y se divide en N escalados que conservan la proporción.
    std::ostringstream filtro;
    filtro << "[0:v]split=" << rendiciones.size();
    for (size_t i = 0; i < rendiciones.size(); ++i) filtro << "[s" << i << "]";
    for (size_t i = 0; i < rendiciones.size(); ++i) {
        filtro << ";[s" << i << "]scale=-2:" << rendiciones[i].alto << "[v" << i << "]";
    }

    std::vector<std::string> args = {ffmpeg, "-hide_banner", "-loglevel", "error", "-y",
                                     "-i", original.string(), "-filter_complex", filtro.str()};
    for (size_t i = 0; i < rendiciones.size(); ++i) {
        const std::string n = std::to_string(i);
        const int kbps = rendiciones[i].kbps;
        args.insert(args.end(), {"-map", "[v" + n + "]", "-c:v:" + n, "libx264",
                                 "-b:v:" + n, std::to_string(kbps) + "k",
                                 "-maxrate:v:" + n, std::to_string(kbps * 107 / 100) + "k",
                                 "-bufsize:v:" + n, std::to_string(kbps * 2) + "k"});
    }
    // Un keyframe exactamente cada segmento, sin importar los fps del original.
    const std::string segundos = std::to_string(kSegundosPorSegmento);
    args.insert(args.end(), {"-preset", "veryfast", "-profile:v", "main", "-pix_fmt", "yuv420p", "-sc_threshold", "0",
                             "-force_key_frames", "expr:gte(t,n_forced*" + segundos + ")"});
    if (info.tieneAudio) args.insert(args.end(), {"-map", "0:a:0", "-c:a", "aac", "-b:a", "128k", "-ac", "2"});
    args.insert(args.end(), {"-f", "dash", "-seg_duration", segundos, "-use_template", "1", "-use_timeline", "1",
                             "-init_seg_name", "init-$RepresentationID$.m4s",
                             "-media_seg_name", "seg-$RepresentationID$-$Number%05d$.m4s",
                             "-adaptation_sets", info.tieneAudio ? "id=0,streams=v id=1,streams=a" : "id=0,streams=v",
                             (carpetaSalida / kManifiesto).string()});
    return args;
}

InfoMedio TranscodificadorFfmpeg::analizar(const fs::path& original) const {
    const ResultadoProceso resultado = EjecutorProcesos::ejecutar(
        {rutaFfprobe_, "-v", "error", "-print_format", "json", "-show_format", "-show_streams", original.string()},
        false);
    if (resultado.codigoSalida != 0) throw ErrorApi(TipoError::DatosInvalidos, "El archivo no es un video válido");
    return parsearFfprobe(resultado.salida);
}

bool TranscodificadorFfmpeg::disponible() const {
    try {
        return EjecutorProcesos::ejecutar({rutaFfmpeg_, "-version"}, false).codigoSalida == 0;
    } catch (const std::exception&) {
        return false;
    }
}

ResultadoTranscodificacion TranscodificadorFfmpeg::transcodificar(const fs::path& original,
                                                                  const fs::path& carpetaSalida) {
    const InfoMedio info = analizar(original);
    if (!info.tieneVideo || info.alto <= 0) {
        throw ErrorApi(TipoError::DatosInvalidos, "El archivo no contiene una pista de video");
    }
    const std::vector<Rendicion> rendiciones = escalera(info.alto);
    const ResultadoProceso dash =
        EjecutorProcesos::ejecutar(argumentosDash(rutaFfmpeg_, original, carpetaSalida, info, rendiciones), true);
    if (dash.codigoSalida != 0 || !fs::exists(carpetaSalida / kManifiesto)) {
        throw ErrorApi(TipoError::ErrorInterno, "FFMPEG falló al generar DASH: " + ultimosCaracteres(dash.salida));
    }
    ResultadoTranscodificacion resultado;
    resultado.duracionSeg = info.duracionSeg;
    for (const Rendicion& rendicion : rendiciones) resultado.alturas.push_back(rendicion.alto);
    return resultado;
}

}  // namespace tecflix::streaming
