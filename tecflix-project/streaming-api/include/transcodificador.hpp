#ifndef STREAMING_TRANSCODIFICADOR_HPP
#define STREAMING_TRANSCODIFICADOR_HPP

#include <filesystem>
#include <string>
#include <vector>

namespace tecflix::streaming {

struct ResultadoTranscodificacion {
    double duracionSeg = 0.0;
    std::vector<int> alturas;  // calidades generadas, de mayor a menor
};

// Convierte el video original en un paquete MPEG-DASH (manifest.mpd + segmentos)
// dentro de carpetaSalida (SPEC 8.1 y 8.3). Lanza ErrorApi si falla.
class ITranscodificador {
public:
    virtual ~ITranscodificador() = default;
    virtual ResultadoTranscodificacion transcodificar(const std::filesystem::path& original,
                                                      const std::filesystem::path& carpetaSalida) = 0;
};

struct InfoMedio {
    double duracionSeg = 0.0;
    int alto = 0;
    bool tieneVideo = false;
    bool tieneAudio = false;
};

struct Rendicion {
    int alto = 0;
    int kbps = 0;
};

// Implementación con FFMPEG y ffprobe (SPEC 8.3). Escalera de calidades:
// 1080p 5000k, 720p 2800k y 360p 800k, sin superar la altura original.
class TranscodificadorFfmpeg : public ITranscodificador {
public:
    static constexpr int kSegundosPorSegmento = 4;
    static constexpr const char* kManifiesto = "manifest.mpd";

    explicit TranscodificadorFfmpeg(std::string rutaFfmpeg = "ffmpeg", std::string rutaFfprobe = "ffprobe");

    ResultadoTranscodificacion transcodificar(const std::filesystem::path& original,
                                              const std::filesystem::path& carpetaSalida) override;

    InfoMedio analizar(const std::filesystem::path& original) const;

    // true si el ejecutable de FFMPEG responde (para GET /salud).
    bool disponible() const;

    // Funciones puras, públicas para probarlas por separado.
    static InfoMedio parsearFfprobe(const std::string& json);
    static std::vector<Rendicion> escalera(int altoOriginal);
    static std::vector<std::string> argumentosDash(const std::string& ffmpeg, const std::filesystem::path& original,
                                                   const std::filesystem::path& carpetaSalida, const InfoMedio& info,
                                                   const std::vector<Rendicion>& rendiciones);

private:
    std::string rutaFfmpeg_;
    std::string rutaFfprobe_;
};

}  // namespace tecflix::streaming

#endif  // STREAMING_TRANSCODIFICADOR_HPP
